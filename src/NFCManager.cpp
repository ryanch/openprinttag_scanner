#include "NFCManager.h"
#include "ApplicationManager.h"
#include <Arduino.h>
#include <cstring>

// Static HAL callback functions
static opt_error_t halReadPage(void* ctx, uint8_t page, uint8_t* buffer) {
    Adafruit_PN532* nfc = static_cast<Adafruit_PN532*>(ctx);
    if (nfc->ntag2xx_ReadPage(page, buffer)) {
        return OPT_OK;
    }
    return OPT_ERR_NFC_READ;
}

static opt_error_t halWritePage(void* ctx, uint8_t page, const uint8_t* data) {
    Adafruit_PN532* nfc = static_cast<Adafruit_PN532*>(ctx);
    // Cast away const - Adafruit library doesn't modify the data but doesn't use const
    if (nfc->ntag2xx_WritePage(page, const_cast<uint8_t*>(data))) {
        return OPT_OK;
    }
    return OPT_ERR_NFC_WRITE;
}

NFCManager& NFCManager::getInstance() {
    static NFCManager instance;
    return instance;
}

bool NFCManager::begin() {
    // Create Adafruit_PN532 instance with software SPI
    nfc = new Adafruit_PN532(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

    Serial.println("NFCManager: Starting PN532...");
    nfc->begin();

    uint32_t versiondata = nfc->getFirmwareVersion();
    if (!versiondata) {
        Serial.println("NFCManager: PN532 not found");
        return false;
    }

    Serial.printf("NFCManager: PN532 firmware version: %08X\n", versiondata);

    // Configure PN532 to read RFID tags
    nfc->SAMConfig();

    // Set up HAL for openprinttag
    nfcHal.read_page = halReadPage;
    nfcHal.write_page = halWritePage;
    nfcHal.is_present = nullptr;
    nfcHal.user_ctx = nfc;

    // Create FreeRTOS primitives
    writeQueue = xQueueCreate(8, sizeof(NFCWriteRequest));
    if (writeQueue == nullptr) {
        Serial.println("NFCManager: Failed to create write queue");
        return false;
    }

    tagMutex = xSemaphoreCreateMutex();
    if (tagMutex == nullptr) {
        Serial.println("NFCManager: Failed to create tag mutex");
        return false;
    }

    completedMutex = xSemaphoreCreateMutex();
    if (completedMutex == nullptr) {
        Serial.println("NFCManager: Failed to create completed mutex");
        return false;
    }

    // Initialize state
    memset(&currentSpool, 0, sizeof(currentSpool));
    memset(lastSeenUid, 0, sizeof(lastSeenUid));
    lastSeenUidLength = 0;
    lastSeenValid = false;
    memset(completedRequests, 0, sizeof(completedRequests));
    completedRequestsIndex = 0;
    memset(recentSpools, 0, sizeof(recentSpools));
    recentSpoolsCount = 0;

    Serial.println("NFCManager: Initialized successfully");
    return true;
}

void NFCManager::startScanTask() {
    xTaskCreatePinnedToCore(
        scanTaskFunc,
        "NFCScanTask",
        4096,
        this,
        1,
        &scanTaskHandle,
        1  // Run on core 1
    );
    Serial.println("NFCManager: Scan task started");
}

void NFCManager::scanTaskFunc(void* param) {
    NFCManager* self = static_cast<NFCManager*>(param);
    self->scanLoop();
}

void NFCManager::scanLoop() {
    while (true) {
        uint8_t uid[7];
        uint8_t uidLength;

        // Wait for an NFC tag with 100ms timeout
        if (nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
            // Check if this is the same spool we already processed
            if (!isDuplicateSpool(uid, uidLength)) {
                // Take mutex for tag operations
                if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (readAndParseTag(uid, uidLength)) {
                        sendSpoolDetectedMessage();

                        // Update last seen UID
                        memcpy(lastSeenUid, uid, uidLength);
                        lastSeenUidLength = uidLength;
                        lastSeenValid = true;
                    }
                    xSemaphoreGive(tagMutex);
                }
            }

            // Process any pending write requests while tag is present
            processWriteQueue();
        } else {
            // No tag detected - clear state
            if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                currentSpool.present = false;
                lastSeenValid = false;
                xSemaphoreGive(tagMutex);
            }
        }

        // Small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool NFCManager::readAndParseTag(uint8_t* uid, uint8_t uid_length) {
    // Initialize openprinttag context
    opt_init(&currentSpool.tag_data);

    // Try to read tag data from NFC
    // Start with fewer pages since different NTAG variants have different sizes
    // NTAG213: 45 pages, NTAG215: 135 pages, NTAG216: 231 pages
    Serial.println("NFCManager: Reading tag data...");
    opt_error_t err = opt_read_from_nfc(&currentSpool.tag_data, &nfcHal, 4, 41);  // NTAG213 user pages
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to read tag data: %s\n", opt_error_str(err));
        Serial.println("NFCManager: Attempting to format as new spool...");
        // Try to format as new spool
        if (formatNewSpool()) {
            Serial.println("NFCManager: Formatted blank tag as new spool");
        } else {
            Serial.println("NFCManager: Format failed, giving up");
            return false;
        }
    } else {
        Serial.println("NFCManager: Read successful, parsing NDEF...");
        // Parse NDEF structure
        err = opt_parse_ndef(&currentSpool.tag_data);
        if (err != OPT_OK) {
            Serial.printf("NFCManager: Failed to parse NDEF: %s\n", opt_error_str(err));
            Serial.println("NFCManager: Attempting to format as new spool...");
            // Try to format as new spool
            if (formatNewSpool()) {
                Serial.println("NFCManager: Formatted tag as new spool after parse failure");
            } else {
                Serial.println("NFCManager: Format failed, giving up");
                return false;
            }
        }
    }

    // Store UID as hex string
    for (uint8_t i = 0; i < uid_length && i < 31; i++) {
        sprintf(currentSpool.spool_id + (i * 2), "%02X", uid[i]);
    }
    currentSpool.spool_id[uid_length * 2] = '\0';

    // Copy UID
    memcpy(currentSpool.uid, uid, uid_length);
    currentSpool.uid_length = uid_length;

    currentSpool.present = true;
    currentSpool.tag_data_valid = true;

    // Add to recent spools history
    addToRecentSpools();

    Serial.printf("NFCManager: Parsed spool %s\n", currentSpool.spool_id);
    return true;
}

bool NFCManager::formatNewSpool() {
    Serial.println("NFCManager: formatNewSpool() called");

    // Format as empty tag with aux region for usage tracking
    // NTAG213 has 144 bytes of user data (pages 4-39), use smaller size for compatibility
    opt_error_t err = opt_format_empty_tag(&currentSpool.tag_data, 144, 32);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to format empty tag: %s\n", opt_error_str(err));
        return false;
    }
    Serial.println("NFCManager: Tag formatted, setting defaults...");

    // Set default values for new spool
    err = opt_set_material_type(&currentSpool.tag_data, OPT_MATERIAL_TYPE_PLA);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set material type: %s\n", opt_error_str(err));
        return false;
    }

    // Set weight to 1kg (1000g) - reasonable default
    err = opt_set_actual_full_weight(&currentSpool.tag_data, 1000.0f);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set weight: %s\n", opt_error_str(err));
        return false;
    }

    // Set primary color to black (RGBA)
    uint8_t black[4] = {0, 0, 0, 255};
    err = opt_set_primary_color(&currentSpool.tag_data, black);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set color: %s\n", opt_error_str(err));
        return false;
    }

    // Set consumed weight to 0
    err = opt_set_consumed_weight(&currentSpool.tag_data, 0.0f);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set consumed weight: %s\n", opt_error_str(err));
        return false;
    }

    Serial.println("NFCManager: Writing to NFC tag...");
    // Write to NFC tag
    err = opt_write_to_nfc(&currentSpool.tag_data, &nfcHal);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to write formatted tag: %s\n", opt_error_str(err));
        return false;
    }

    // Re-read and verify with retries
    Serial.println("NFCManager: Verifying write...");
    const int maxRetries = 3;
    for (int retry = 0; retry < maxRetries; retry++) {
        // Give the tag time to settle after write
        delay(50 * (retry + 1));  // Increasing delay: 50ms, 100ms, 150ms

        err = opt_read_from_nfc(&currentSpool.tag_data, &nfcHal, 4, 41);
        if (err == OPT_OK) {
            err = opt_parse_ndef(&currentSpool.tag_data);
            if (err == OPT_OK) {
                currentSpool.tag_data_valid = true;
                Serial.println("NFCManager: formatNewSpool() complete - verified");
                return true;
            }
            Serial.printf("NFCManager: Parse failed on retry %d: %s\n", retry + 1, opt_error_str(err));
        } else {
            Serial.printf("NFCManager: Re-read failed on retry %d: %s\n", retry + 1, opt_error_str(err));
        }
    }

    // All retries failed - fall back to trusting the in-memory data
    Serial.println("NFCManager: Verification retries exhausted, trusting in-memory data");
    err = opt_parse_ndef(&currentSpool.tag_data);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to parse in-memory data: %s\n", opt_error_str(err));
        return false;
    }

    currentSpool.tag_data_valid = true;
    Serial.println("NFCManager: formatNewSpool() complete - unverified");
    return true;
}

void NFCManager::sendSpoolDetectedMessage() {
    if (!currentSpool.tag_data_valid) {
        return;
    }

    AppMessage msg;
    msg.type = AppMessageType::SPOOL_DETECTED;

    // Copy spool ID
    strncpy(msg.payload.spoolDetected.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.spoolDetected.spool_id) - 1);
    msg.payload.spoolDetected.spool_id[sizeof(msg.payload.spoolDetected.spool_id) - 1] = '\0';

    // Get material type
    uint8_t material_type = 0;
    if (opt_get_material_type(&currentSpool.tag_data, &material_type) == OPT_OK) {
        msg.payload.spoolDetected.material_type = material_type;
    } else {
        msg.payload.spoolDetected.material_type = 0;
    }

    // Calculate remaining weight in kg
    float full_weight = 0.0f;
    float consumed = 0.0f;
    opt_get_actual_full_weight(&currentSpool.tag_data, &full_weight);
    opt_get_consumed_weight(&currentSpool.tag_data, &consumed);
    msg.payload.spoolDetected.kg_remaining = (full_weight - consumed) / 1000.0f;

    // Get primary color
    if (opt_get_primary_color(&currentSpool.tag_data, msg.payload.spoolDetected.primary_color) != OPT_OK) {
        memset(msg.payload.spoolDetected.primary_color, 0, 4);
    }

    // Get material name
    if (opt_get_material_name(&currentSpool.tag_data, msg.payload.spoolDetected.material_name,
                               sizeof(msg.payload.spoolDetected.material_name)) != OPT_OK) {
        // Use material type abbreviation as fallback
        const char* abbrev = opt_material_type_str((opt_material_type_t)material_type);
        if (abbrev) {
            strncpy(msg.payload.spoolDetected.material_name, abbrev,
                    sizeof(msg.payload.spoolDetected.material_name) - 1);
            msg.payload.spoolDetected.material_name[sizeof(msg.payload.spoolDetected.material_name) - 1] = '\0';
        } else {
            msg.payload.spoolDetected.material_name[0] = '\0';
        }
    }

    ApplicationManager::getInstance().sendMessage(msg);
}

bool NFCManager::enqueueWrite(const NFCWriteRequest& req) {
    if (writeQueue == nullptr) {
        return false;
    }

    // Check if already completed (duplicate)
    if (isRequestCompleted(req.request_id)) {
        return true;  // Already done, return success
    }

    return xQueueSend(writeQueue, &req, pdMS_TO_TICKS(100)) == pdTRUE;
}

void NFCManager::processWriteQueue() {
    if (writeQueue == nullptr || !currentSpool.present) {
        return;
    }

    NFCWriteRequest request;

    // Peek at queue to check for pending requests
    while (xQueuePeek(writeQueue, &request, 0) == pdTRUE) {
        // Check if already completed
        if (isRequestCompleted(request.request_id)) {
            // Remove from queue and skip
            xQueueReceive(writeQueue, &request, 0);
            continue;
        }

        // Take mutex for tag operations
        if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return;  // Couldn't get mutex, try again later
        }

        // Remove from queue
        xQueueReceive(writeQueue, &request, 0);

        // Execute the write
        bool success = executeWrite(request);

        // Mark as completed
        markRequestCompleted(request.request_id);

        // Send update message
        sendSpoolUpdatedMessage(request.request_id, request.type, success);

        xSemaphoreGive(tagMutex);
    }
}

bool NFCManager::executeWrite(const NFCWriteRequest& request) {
    if (!currentSpool.tag_data_valid) {
        return false;
    }

    // Verify spool matches if expected_spool_id is set
    if (request.expected_spool_id[0] != '\0') {
        if (strcmp(currentSpool.spool_id, request.expected_spool_id) != 0) {
            Serial.printf("NFCManager: Write rejected: expected spool %s but found %s\n",
                request.expected_spool_id, currentSpool.spool_id);
            return false;
        }
    }

    opt_error_t err;

    switch (request.type) {
        case NFCWriteType::REMOVE_WEIGHT: {
            // Add to consumed weight
            err = opt_add_consumed_weight(&currentSpool.tag_data, request.data.grams_to_remove);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to add consumed weight: %s\n", opt_error_str(err));
                return false;
            }
            // Write aux region (usage tracking)
            err = opt_write_aux_region(&currentSpool.tag_data, &nfcHal);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to write aux region: %s\n", opt_error_str(err));
                return false;
            }
            Serial.printf("NFCManager: Removed %.2f grams from spool\n", request.data.grams_to_remove);
            break;
        }

        case NFCWriteType::CHANGE_COLOR: {
            err = opt_set_primary_color(&currentSpool.tag_data, request.data.new_color);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to set color: %s\n", opt_error_str(err));
                return false;
            }
            err = opt_write_dirty_pages(&currentSpool.tag_data, &nfcHal);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to write color: %s\n", opt_error_str(err));
                return false;
            }
            Serial.printf("NFCManager: Changed color to RGBA(%u,%u,%u,%u)\n",
                request.data.new_color[0], request.data.new_color[1],
                request.data.new_color[2], request.data.new_color[3]);
            break;
        }

        case NFCWriteType::CHANGE_FILAMENT_TYPE: {
            err = opt_set_material_type(&currentSpool.tag_data, request.data.new_material_type);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to set material type: %s\n", opt_error_str(err));
                return false;
            }
            err = opt_write_dirty_pages(&currentSpool.tag_data, &nfcHal);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to write material type: %s\n", opt_error_str(err));
                return false;
            }
            Serial.printf("NFCManager: Changed material type to %u\n", request.data.new_material_type);
            break;
        }

        case NFCWriteType::SET_CONSUMED_WEIGHT: {
            err = opt_set_consumed_weight(&currentSpool.tag_data, request.data.consumed_weight);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to set consumed weight: %s\n", opt_error_str(err));
                return false;
            }
            err = opt_write_aux_region(&currentSpool.tag_data, &nfcHal);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to write aux region: %s\n", opt_error_str(err));
                return false;
            }
            Serial.printf("NFCManager: Set consumed weight to %.2f grams\n", request.data.consumed_weight);
            break;
        }

        case NFCWriteType::SET_BRAND_NAME: {
            err = opt_set_brand_name(&currentSpool.tag_data, request.data.brand_name);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to set brand name: %s\n", opt_error_str(err));
                return false;
            }
            err = opt_write_dirty_pages(&currentSpool.tag_data, &nfcHal);
            if (err != OPT_OK) {
                Serial.printf("NFCManager: Failed to write brand name: %s\n", opt_error_str(err));
                return false;
            }
            Serial.printf("NFCManager: Set brand name to %s\n", request.data.brand_name);
            break;
        }

        default:
            Serial.printf("NFCManager: Unknown write type: %u\n", static_cast<uint8_t>(request.type));
            return false;
    }

    return true;
}

void NFCManager::sendSpoolUpdatedMessage(uint32_t request_id, NFCWriteType type, bool success) {
    AppMessage msg;
    msg.type = AppMessageType::SPOOL_UPDATED;

    strncpy(msg.payload.spoolUpdated.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.spoolUpdated.spool_id) - 1);
    msg.payload.spoolUpdated.spool_id[sizeof(msg.payload.spoolUpdated.spool_id) - 1] = '\0';

    msg.payload.spoolUpdated.update_type = static_cast<uint8_t>(type);
    msg.payload.spoolUpdated.success = success;

    // Get remaining weight from tag data
    float remaining_grams = 0;
    if (currentSpool.tag_data_valid) {
        opt_get_remaining_weight(&currentSpool.tag_data, &remaining_grams);
    }
    msg.payload.spoolUpdated.kg_remaining = remaining_grams / 1000.0f;

    ApplicationManager::getInstance().sendMessage(msg);
}

bool NFCManager::isRequestCompleted(uint32_t request_id) {
    if (completedMutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(completedMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool found = false;
    for (size_t i = 0; i < COMPLETED_REQUESTS_SIZE; i++) {
        if (completedRequests[i] == request_id) {
            found = true;
            break;
        }
    }

    xSemaphoreGive(completedMutex);
    return found;
}

void NFCManager::markRequestCompleted(uint32_t request_id) {
    if (completedMutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(completedMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    completedRequests[completedRequestsIndex] = request_id;
    completedRequestsIndex = (completedRequestsIndex + 1) % COMPLETED_REQUESTS_SIZE;

    xSemaphoreGive(completedMutex);
}

bool NFCManager::isDuplicateSpool(const uint8_t* uid, uint8_t uid_length) {
    if (!lastSeenValid) {
        return false;
    }

    if (uid_length != lastSeenUidLength) {
        return false;
    }

    return memcmp(uid, lastSeenUid, uid_length) == 0;
}

void NFCManager::requestCurrentSpool() {
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lastSeenValid = false;
        memset(lastSeenUid, 0, sizeof(lastSeenUid));
        lastSeenUidLength = 0;
        xSemaphoreGive(tagMutex);
    }
}

void NFCManager::addToRecentSpools() {
    if (!currentSpool.tag_data_valid) {
        return;
    }

    // Check if this spool already exists in recent list
    int existingIndex = -1;
    for (size_t i = 0; i < recentSpoolsCount; i++) {
        if (strcmp(recentSpools[i].spool_id, currentSpool.spool_id) == 0) {
            existingIndex = i;
            break;
        }
    }

    // Create new entry from current spool
    RecentSpoolEntry newEntry;
    memset(&newEntry, 0, sizeof(newEntry));
    strncpy(newEntry.spool_id, currentSpool.spool_id, sizeof(newEntry.spool_id) - 1);

    opt_get_material_type(&currentSpool.tag_data, &newEntry.material_type);
    opt_get_primary_color(&currentSpool.tag_data, newEntry.color);

    if (opt_get_brand_name(&currentSpool.tag_data, newEntry.manufacturer, sizeof(newEntry.manufacturer)) != OPT_OK) {
        newEntry.manufacturer[0] = '\0';
    }

    float full_weight = 0.0f, consumed = 0.0f;
    opt_get_actual_full_weight(&currentSpool.tag_data, &full_weight);
    opt_get_consumed_weight(&currentSpool.tag_data, &consumed);
    newEntry.grams_remaining = (int)(full_weight - consumed);

    newEntry.last_seen_ms = millis();
    newEntry.valid = true;

    if (existingIndex >= 0) {
        // Spool exists - shift entries to remove it from current position
        for (size_t i = existingIndex; i > 0; i--) {
            recentSpools[i] = recentSpools[i - 1];
        }
    } else {
        // New spool - shift all entries down to make room
        size_t shiftCount = (recentSpoolsCount < MAX_RECENT_SPOOLS) ? recentSpoolsCount : (MAX_RECENT_SPOOLS - 1);
        for (size_t i = shiftCount; i > 0; i--) {
            recentSpools[i] = recentSpools[i - 1];
        }
        if (recentSpoolsCount < MAX_RECENT_SPOOLS) {
            recentSpoolsCount++;
        }
    }

    // Place new entry at front
    recentSpools[0] = newEntry;
}

size_t NFCManager::getRecentSpools(RecentSpoolEntry* entries, size_t maxEntries) {
    if (tagMutex == nullptr) {
        return 0;
    }

    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < recentSpoolsCount && count < maxEntries; i++) {
        if (recentSpools[i].valid) {
            // Skip the current spool if it matches
            if (currentSpool.present && strcmp(recentSpools[i].spool_id, currentSpool.spool_id) == 0) {
                continue;
            }
            entries[count++] = recentSpools[i];
        }
    }

    xSemaphoreGive(tagMutex);
    return count;
}
