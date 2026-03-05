#include "NFCManager.h"
#ifndef NATIVE_TEST
  #include "ApplicationManager.h"
  #include "HardwareNFCConnection.h"
  #include <Arduino.h>
#else
  #include "platform/NativePlatform.h"
  #include "FakeLCDManager.h"
  #include "StubApplicationManager.h"
#endif
#include <cstring>
#include <time.h>


//#define DUMP_TAGS_TO_CONSOLE

#ifdef DUMP_TAGS_TO_CONSOLE
    #include <base64.hpp>
#endif

NFCManager& NFCManager::getInstance() {
    static NFCManager instance;
    return instance;
}

bool NFCManager::begin() {
    // Create hardware connection if none was injected
    if (connection_ == nullptr) {
#ifndef NATIVE_TEST
        connection_ = new HardwareNFCConnection();
        ownsConnection_ = true;
#else
        // In native tests, connection must be injected via setConnection()
        Serial.println("NFCManager: No connection injected for native test");
        return false;
#endif
    }

    if (!connection_->begin()) {
        Serial.println("NFCManager: Failed to initialize connection");
        return false;
    }

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

bool NFCManager::getCurrentSpoolState(CurrentSpoolState& out) {
    if (tagMutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    out = currentSpool;
    xSemaphoreGive(tagMutex);
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
    uint32_t scanCount = 0;
    uint32_t detectCount = 0;
    uint32_t failCount = 0;

    Serial.println("NFCManager: scanLoop() started, polling every 50ms");

#ifndef NATIVE_TEST
    // Register this task with the ESP32 hardware watchdog.
    // If scanLoop() hangs for >NFC_WDT_TIMEOUT_S (e.g., stuck in driver I/O),
    // the watchdog triggers a system reset automatically.
    esp_task_wdt_init(NFC_WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);
#endif

    // Initial reset + RF setup
    connection_->reset();
    connection_->setupRF();

    // One-time startup diagnostic
#ifndef NATIVE_TEST
    static_cast<HardwareNFCConnection*>(connection_)->logDiagnostics();
#endif

    while (true) {
#ifndef NATIVE_TEST
        esp_task_wdt_reset();
#endif
        uint8_t uid[8];
        uint8_t uidLength = 0;
        scanCount++;

        // Log heartbeat every 200 scans (~10 seconds)
        //if (scanCount % 200 == 0) {
        //    Serial.printf("NFCManager: heartbeat scan=%lu detected=%lu failed=%lu\n",
        //                  scanCount, detectCount, failCount);
        //}

        // Watchdog: check if we've exceeded failure thresholds
        if (consecutiveFailures_ >= RESTART_THRESHOLD) {
            Serial.println("NFCManager: CRITICAL - too many consecutive failures, restarting ESP");
#ifndef NATIVE_TEST
            delay(100);  // let serial flush
            ESP.restart();
#endif
        }
        if (consecutiveFailures_ > 0 && (consecutiveFailures_ % RECOVERY_THRESHOLD) == 0) {
            attemptRecovery();
        }

        // Re-arm RF field for each scan (reset + setupRF).
        // The reset is needed to clear transceiver state between scans.
        connection_->reset();
        if (!connection_->setupRF()) {
            consecutiveFailures_++;
            if (failCount++ % 200 == 0) {
                Serial.println("NFCManager: setupRF() failed");
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Give tag time to power up from RF field before sending inventory.
        // ISO15693 tags need ~5-10ms to charge from the RF field.
        vTaskDelay(pdMS_TO_TICKS(10));

        // setupRF succeeded, chip is responsive
        consecutiveFailures_ = 0;

        // Detect tag
        if (connection_->detectTag(uid, &uidLength)) {
            detectCount++;
            // Log UID on first detection
            if (!lastSeenValid || memcmp(uid, lastSeenUid, uidLength) != 0) {
                Serial.printf("NFCManager: Tag detected! UID=");
                for (uint8_t i = 0; i < uidLength; i++) {
                    Serial.printf("%02X", uid[i]);
                }
                Serial.println("");
            }

            // Store UID for addressed read/write commands
            connection_->setCurrentUid(uid, uidLength);

            // Check if this is the same spool we already processed
            if (!isDuplicateSpool(uid, uidLength)) {
                Serial.println("NFCManager: New spool, reading tag...");
                // readAndParseTag manages its own mutex internally
                if (!readAndParseTag(uid, uidLength)) {
                    Serial.println("NFCManager: readAndParseTag() failed - treating as blank tag");
                    // Take mutex briefly for blank tag state update
                    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        for (uint8_t i = 0; i < uidLength && i < 8; i++) {
                            sprintf(currentSpool.spool_id + (i * 2), "%02X", uid[i]);
                        }
                        currentSpool.spool_id[uidLength * 2] = '\0';
                        memcpy(currentSpool.uid, uid, uidLength);
                        currentSpool.uid_length = uidLength;
                        currentSpool.present = true;
                        currentSpool.tag_data_valid = false;
                        currentSpool.blank_tag_present = true;

                        sendBlankTagMessage();

                        memcpy(lastSeenUid, uid, uidLength);
                        lastSeenUidLength = uidLength;
                        lastSeenValid = true;
                        xSemaphoreGive(tagMutex);
                    } else {
                        Serial.println("NFCManager: Could not acquire tagMutex");
                    }
                }
            }

            // Process any pending write requests while tag is present
            processWriteQueue();
        } else {
            // No tag detected - clear state
            if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (lastSeenValid) {
                    Serial.println("NFCManager: Tag removed");

                    // Send TAG_REMOVED message before clearing state
                    sendTagRemovedMessage();
                }
                currentSpool.present = false;
                currentSpool.blank_tag_present = false;
                lastSeenValid = false;
                xSemaphoreGive(tagMutex);
            }
        }

        // Small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void NFCManager::attemptRecovery() {
    Serial.println("NFCManager: WATCHDOG - attempting hardware recovery");
    if (connection_->hardwareReset()) {
        Serial.println("NFCManager: Hardware reset succeeded");
    } else {
        Serial.println("NFCManager: Hardware reset failed");
    }
}

bool NFCManager::readAndParseTag(uint8_t* uid, uint8_t uid_length) {
    // NFC I/O into local buffer — no mutex needed for hardware access
    opt_tag_t localTag;
    opt_init(&localTag);

    // OpenPrintTag uses ISO15693 (NFC-V) tags, designed for ICODE SLIX2 320B
    Serial.println("NFCManager: Reading tag data...");
    opt_nfc_hal_t* hal = connection_->getHal();
    opt_error_t err = opt_read_from_nfc(&localTag, hal, 0, 78);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to read tag data: %s\n", opt_error_str(err));
        return false;
    }

    Serial.println("NFCManager: Read successful, parsing NDEF...");
    err = opt_parse_ndef(&localTag);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to parse NDEF: %s\n", opt_error_str(err));
        return false;
    }

    // NFC I/O complete — take mutex only for the fast state copy
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: Could not acquire tagMutex for state update");
        return false;
    }

    currentSpool.tag_data = localTag;

    // Store UID as hex string
    for (uint8_t i = 0; i < uid_length && i < 8; i++) {
        sprintf(currentSpool.spool_id + (i * 2), "%02X", uid[i]);
    }
    currentSpool.spool_id[uid_length * 2] = '\0';

    memcpy(currentSpool.uid, uid, uid_length);
    currentSpool.uid_length = uid_length;

    currentSpool.present = true;
    currentSpool.tag_data_valid = true;

    addToRecentSpools();

    Serial.printf("NFCManager: Parsed spool %s\n", currentSpool.spool_id);

    sendSpoolDetectedMessage();

    // Update dedup state
    memcpy(lastSeenUid, uid, uid_length);
    lastSeenUidLength = uid_length;
    lastSeenValid = true;

    xSemaphoreGive(tagMutex);

    #ifdef DUMP_TAGS_TO_CONSOLE
        size_t encoded_max_len = ((sizeof(localTag.data) + 2) / 3) * 4 + 1;
        char* base64_output_buffer = new char[encoded_max_len];
        unsigned int encoded_len = encode_base64(localTag.data, sizeof(localTag.data), (unsigned char*)base64_output_buffer);
        base64_output_buffer[encoded_len] = '\0';
        Serial.println("NFCManager: Spool data:");
        Serial.println(base64_output_buffer);
    #endif

    return true;
}

bool NFCManager::formatNewSpool() {
    Serial.println("NFCManager: formatNewSpool() called");

    // All NFC I/O uses a local buffer — no mutex needed
    opt_tag_t localTag;
    opt_init(&localTag);

    // Format as empty tag with aux region for usage tracking
    // ISO15693 ICODE SLIX2 has 320 bytes, use 312 bytes with 32-byte aux region
    opt_error_t err = opt_format_empty_tag(&localTag, 312, 32);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to format empty tag: %s\n", opt_error_str(err));
        return false;
    }
    Serial.println("NFCManager: Tag formatted, setting defaults...");

    // Set default values for new spool
    err = opt_set_material_type(&localTag, OPT_MATERIAL_TYPE_PLA);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set material type: %s\n", opt_error_str(err));
        return false;
    }

    err = opt_set_actual_full_weight(&localTag, 1000.0f);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set weight: %s\n", opt_error_str(err));
        return false;
    }

    uint8_t white[4] = {255, 255, 255, 255};
    err = opt_set_primary_color(&localTag, white);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set color: %s\n", opt_error_str(err));
        return false;
    }

    err = opt_set_consumed_weight(&localTag, 0.0f);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set consumed weight: %s\n", opt_error_str(err));
        return false;
    }

    err = opt_set_brand_name(&localTag, "Unknown");
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to set brand name: %s\n", opt_error_str(err));
        return false;
    }

    Serial.println("NFCManager: Writing to NFC tag...");
    opt_nfc_hal_t* hal = connection_->getHal();
    err = opt_write_to_nfc(&localTag, hal);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to write formatted tag: %s\n", opt_error_str(err));
        return false;
    }

    // Re-read and verify with retries
    Serial.println("NFCManager: Verifying write...");
    const int maxRetries = 3;
    for (int retry = 0; retry < maxRetries; retry++) {
        vTaskDelay(pdMS_TO_TICKS(50 * (retry + 1)));  // 50ms, 100ms, 150ms

        err = opt_read_from_nfc(&localTag, hal, 0, 78);
        if (err == OPT_OK) {
            err = opt_parse_ndef(&localTag);
            if (err == OPT_OK) {
                // Verified — copy result into shared state under mutex
                if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                    Serial.println("NFCManager: Could not acquire tagMutex after format verify");
                    return false;
                }
                currentSpool.tag_data = localTag;
                currentSpool.tag_data_valid = true;
                currentSpool.blank_tag_present = false;
                addToRecentSpools();
                sendSpoolDetectedMessage();
                xSemaphoreGive(tagMutex);

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
    err = opt_parse_ndef(&localTag);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to parse in-memory data: %s\n", opt_error_str(err));
        return false;
    }

    // Copy unverified result into shared state under mutex
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: Could not acquire tagMutex after format fallback");
        return false;
    }
    currentSpool.tag_data = localTag;
    currentSpool.tag_data_valid = true;
    currentSpool.blank_tag_present = false;
    addToRecentSpools();
    sendSpoolDetectedMessage();
    xSemaphoreGive(tagMutex);

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

    // Get density (use 0 to signal "not available" to caller)
    if (opt_get_density(&currentSpool.tag_data, &msg.payload.spoolDetected.density) != OPT_OK) {
        msg.payload.spoolDetected.density = 0.0f;
    }

    // Get filament diameter
    if (opt_get_filament_diameter(&currentSpool.tag_data, &msg.payload.spoolDetected.diameter) != OPT_OK) {
        msg.payload.spoolDetected.diameter = 0.0f;
    }

    // Get initial (full) weight
    msg.payload.spoolDetected.initial_weight_g = full_weight;

    // Get manufacturer/brand name
    if (opt_get_brand_name(&currentSpool.tag_data, msg.payload.spoolDetected.manufacturer,
                           sizeof(msg.payload.spoolDetected.manufacturer)) != OPT_OK) {
        msg.payload.spoolDetected.manufacturer[0] = '\0';
    }

    // Get Spoolman ID from tag (if present)
    int32_t spoolman_id = -1;
    opt_get_gp_spoolman_id(&currentSpool.tag_data, &spoolman_id);
    msg.payload.spoolDetected.spoolman_id = spoolman_id;

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

void NFCManager::sendBlankTagMessage() {
    AppMessage msg;
    msg.type = AppMessageType::BLANK_TAG_DETECTED;
    strncpy(msg.payload.blankTag.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.blankTag.spool_id) - 1);
    msg.payload.blankTag.spool_id[sizeof(msg.payload.blankTag.spool_id) - 1] = '\0';

    ApplicationManager::getInstance().sendMessage(msg);
}

void NFCManager::sendTagRemovedMessage() {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::TAG_REMOVED;
    strncpy(msg.payload.tagRemoved.spool_id, currentSpool.spool_id,
            sizeof(msg.payload.tagRemoved.spool_id) - 1);
    msg.payload.tagRemoved.spool_id[sizeof(msg.payload.tagRemoved.spool_id) - 1] = '\0';
    msg.payload.tagRemoved.last_remaining_kg = 0.0f;
    msg.payload.tagRemoved.spoolman_id = -1;

    if (currentSpool.tag_data_valid) {
        float full_weight = 0.0f, consumed = 0.0f;
        opt_get_actual_full_weight(&currentSpool.tag_data, &full_weight);
        opt_get_consumed_weight(&currentSpool.tag_data, &consumed);
        msg.payload.tagRemoved.last_remaining_kg = (full_weight - consumed) / 1000.0f;

        int32_t smId = -1;
        opt_get_gp_spoolman_id(&currentSpool.tag_data, &smId);
        msg.payload.tagRemoved.spoolman_id = smId;
    }

    ApplicationManager::getInstance().sendMessage(msg);
}

bool NFCManager::enqueueRawWrite(const NFCWriteRequest& req, const uint8_t* data, size_t dataSize) {
    if (dataSize == 0 || dataSize > RAW_WRITE_BUFFER_SIZE) {
        return false;
    }
    if (rawWritePending_) {
        Serial.println("NFCManager: Raw write already pending");
        return false;
    }
    memcpy(rawWriteBuffer_, data, dataSize);
    rawWriteBufferSize_ = dataSize;
    rawWritePending_ = true;
    return enqueueWrite(req);
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

    // Process at most one write per call — remaining writes get handled in
    // subsequent scan cycles (50ms apart), keeping tag detection responsive.
    while (xQueuePeek(writeQueue, &request, 0) == pdTRUE) {
        // Check if already completed — skip without counting as our one write
        if (isRequestCompleted(request.request_id)) {
            xQueueReceive(writeQueue, &request, 0);
            continue;
        }

        // Dequeue the request
        xQueueReceive(writeQueue, &request, 0);

        // executeWrite() manages its own mutex internally
        bool success = executeWrite(request);

        markRequestCompleted(request.request_id);

        // Snapshot spool info under mutex for the notification message
        char snapshotSpoolId[64];
        float snapshotRemainingGrams = 0;
        bool snapshotValid = false;
        if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            strncpy(snapshotSpoolId, currentSpool.spool_id, sizeof(snapshotSpoolId));
            snapshotSpoolId[sizeof(snapshotSpoolId) - 1] = '\0';
            if (currentSpool.tag_data_valid) {
                opt_get_remaining_weight(&currentSpool.tag_data, &snapshotRemainingGrams);
                snapshotValid = true;
            }
            xSemaphoreGive(tagMutex);
        }

        // Build and send the update message with snapshotted data
        AppMessage msg;
        msg.type = AppMessageType::SPOOL_UPDATED;
        strncpy(msg.payload.spoolUpdated.spool_id, snapshotSpoolId,
                sizeof(msg.payload.spoolUpdated.spool_id) - 1);
        msg.payload.spoolUpdated.spool_id[sizeof(msg.payload.spoolUpdated.spool_id) - 1] = '\0';
        msg.payload.spoolUpdated.update_type = static_cast<uint8_t>(request.type);
        msg.payload.spoolUpdated.success = success;
        msg.payload.spoolUpdated.kg_remaining = snapshotValid ? snapshotRemainingGrams / 1000.0f : 0;
        ApplicationManager::getInstance().sendMessage(msg);

        // One write per cycle — break to let scan loop run between writes
        break;
    }
}

bool NFCManager::writeRawTag() {
    Serial.println("NFCManager: writeRawTag() called");

    // Copy raw data into a local opt_tag_t
    opt_tag_t localTag;
    opt_init(&localTag);
    memcpy(localTag.data, rawWriteBuffer_, rawWriteBufferSize_);
    localTag.data_size = rawWriteBufferSize_;
    localTag.initialized = true;

    // Write all pages to NFC tag
    Serial.println("NFCManager: Writing raw data to NFC tag...");
    opt_nfc_hal_t* hal = connection_->getHal();
    opt_error_t err = opt_write_to_nfc(&localTag, hal);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to write raw tag: %s\n", opt_error_str(err));
        rawWritePending_ = false;
        return false;
    }

    // Re-read and verify with retries
    Serial.println("NFCManager: Verifying raw write...");
    const int maxRetries = 3;
    for (int retry = 0; retry < maxRetries; retry++) {
        vTaskDelay(pdMS_TO_TICKS(50 * (retry + 1)));

        err = opt_read_from_nfc(&localTag, hal, 0, 78);
        if (err == OPT_OK) {
            err = opt_parse_ndef(&localTag);
            if (err == OPT_OK) {
                if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                    Serial.println("NFCManager: Could not acquire tagMutex after raw write verify");
                    rawWritePending_ = false;
                    return false;
                }
                currentSpool.tag_data = localTag;
                currentSpool.tag_data_valid = true;
                currentSpool.blank_tag_present = false;
                lastSeenValid = false;  // Force re-detection on next scan
                addToRecentSpools();
                sendSpoolDetectedMessage();
                xSemaphoreGive(tagMutex);

                rawWritePending_ = false;
                Serial.println("NFCManager: writeRawTag() complete - verified");
                return true;
            }
            Serial.printf("NFCManager: Parse failed on retry %d: %s\n", retry + 1, opt_error_str(err));
        } else {
            Serial.printf("NFCManager: Re-read failed on retry %d: %s\n", retry + 1, opt_error_str(err));
        }
    }

    // All retries failed - fall back to trusting in-memory data
    Serial.println("NFCManager: Verification retries exhausted, trusting in-memory data");
    err = opt_parse_ndef(&localTag);
    if (err != OPT_OK) {
        Serial.printf("NFCManager: Failed to parse in-memory raw data: %s\n", opt_error_str(err));
        rawWritePending_ = false;
        return false;
    }

    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: Could not acquire tagMutex after raw write fallback");
        rawWritePending_ = false;
        return false;
    }
    currentSpool.tag_data = localTag;
    currentSpool.tag_data_valid = true;
    currentSpool.blank_tag_present = false;
    lastSeenValid = false;
    addToRecentSpools();
    sendSpoolDetectedMessage();
    xSemaphoreGive(tagMutex);

    rawWritePending_ = false;
    Serial.println("NFCManager: writeRawTag() complete - unverified");
    return true;
}

static bool isAuxOnlyWrite(const NFCWriteType type) {
    return type == NFCWriteType::REMOVE_WEIGHT ||
           type == NFCWriteType::SET_CONSUMED_WEIGHT ||
           type == NFCWriteType::WRITE_SPOOLMAN_ID;
}

static opt_error_t applyWriteUpdate(opt_tag_t& tag, const NFCWriteRequest& request) {
    switch (request.type) {
        case NFCWriteType::REMOVE_WEIGHT:
            return opt_add_consumed_weight(&tag, request.data.grams_to_remove);
        case NFCWriteType::CHANGE_COLOR:
            return opt_set_primary_color(&tag, request.data.new_color);
        case NFCWriteType::CHANGE_FILAMENT_TYPE:
            return opt_set_material_type(&tag, request.data.new_material_type);
        case NFCWriteType::SET_CONSUMED_WEIGHT:
            return opt_set_consumed_weight(&tag, request.data.consumed_weight);
        case NFCWriteType::SET_BRAND_NAME:
            return opt_set_brand_name(&tag, request.data.brand_name);
        case NFCWriteType::WRITE_SPOOLMAN_ID:
            return opt_set_gp_spoolman_id(&tag, request.data.spoolman_id);
        default:
            return OPT_ERR_INVALID_PARAM;
    }
}

bool NFCManager::executeWrite(const NFCWriteRequest& request) {
    // Handle FORMAT_NEW — formatNewSpool() manages its own mutex
    if (request.type == NFCWriteType::FORMAT_NEW) {
        // Validate under mutex
        if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("NFCManager: FORMAT_NEW - could not acquire tagMutex");
            return false;
        }
        if (request.expected_spool_id[0] != '\0' &&
            strcmp(currentSpool.spool_id, request.expected_spool_id) != 0) {
            xSemaphoreGive(tagMutex);
            Serial.println("NFCManager: FORMAT_NEW rejected - UID mismatch");
            return false;
        }
        xSemaphoreGive(tagMutex);

        // formatNewSpool() does NFC I/O without mutex, takes mutex at end for state copy
        return formatNewSpool();
    }

    // Handle WRITE_RAW_TAG — writeRawTag() manages its own mutex
    if (request.type == NFCWriteType::WRITE_RAW_TAG) {
        if (!rawWritePending_) {
            Serial.println("NFCManager: WRITE_RAW_TAG - no raw data pending");
            return false;
        }
        // Validate under mutex
        if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("NFCManager: WRITE_RAW_TAG - could not acquire tagMutex");
            rawWritePending_ = false;
            return false;
        }
        if (request.expected_spool_id[0] != '\0' &&
            strcmp(currentSpool.spool_id, request.expected_spool_id) != 0) {
            xSemaphoreGive(tagMutex);
            Serial.println("NFCManager: WRITE_RAW_TAG rejected - UID mismatch");
            rawWritePending_ = false;
            return false;
        }
        xSemaphoreGive(tagMutex);

        return writeRawTag();
    }

    // For non-FORMAT writes: take mutex to validate + modify in-memory data, then release for NFC I/O
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: executeWrite - could not acquire tagMutex");
        return false;
    }

    if (!currentSpool.tag_data_valid) {
        xSemaphoreGive(tagMutex);
        return false;
    }

    if (request.expected_spool_id[0] != '\0') {
        if (strcmp(currentSpool.spool_id, request.expected_spool_id) != 0) {
            Serial.printf("NFCManager: Write rejected: expected spool %s but found %s\n",
                request.expected_spool_id, currentSpool.spool_id);
            xSemaphoreGive(tagMutex);
            return false;
        }
    }

    // Snapshot current tag into reusable scratch storage under mutex; do not mutate shared state until write succeeds.
    writeScratchTag_ = currentSpool.tag_data;
    xSemaphoreGive(tagMutex);

    // Preflight update on a copy to detect overflow before touching shared state.
    opt_tag_t& updatedTag = writeScratchTag_;
    opt_error_t err = applyWriteUpdate(updatedTag, request);
    if (err != OPT_OK) {
        if (err == OPT_ERR_REGION_OVERFLOW) {
            Serial.println("NFCManager: Region overflow during preflight update; keeping original tag data");
        } else {
            Serial.printf("NFCManager: Failed to update in-memory tag data: %s\n", opt_error_str(err));
        }
        return false;
    }

    // NFC I/O without mutex — only scan task touches the hardware
    opt_nfc_hal_t* hal = connection_->getHal();
    const bool auxOnlyWrite = isAuxOnlyWrite(request.type);
    if (auxOnlyWrite) {
        err = opt_write_aux_region(&updatedTag, hal);
    } else {
        err = opt_write_dirty_pages(&updatedTag, hal);
    }

    if (err != OPT_OK) {
        Serial.printf("NFCManager: Write failed (%s), attempting raw fallback\n", opt_error_str(err));

        // Fallback: write full binary image from updatedTag directly.
        // This avoids nesting another large opt_tag_t on NFCScanTask stack.
        err = opt_write_to_nfc(&updatedTag, hal);
        if (err == OPT_OK) {
            const int maxRetries = 3;
            for (int retry = 0; retry < maxRetries; retry++) {
                vTaskDelay(pdMS_TO_TICKS(50 * (retry + 1)));
                err = opt_read_from_nfc(&updatedTag, hal, 0, 78);
                if (err == OPT_OK) {
                    err = opt_parse_ndef(&updatedTag);
                    if (err == OPT_OK) {
                        break;
                    }
                }
            }
            if (err == OPT_OK) {
                if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                    Serial.println("NFCManager: executeWrite fallback succeeded but commit lock failed");
                    return false;
                }
                currentSpool.tag_data = updatedTag;
                currentSpool.tag_data_valid = true;
                currentSpool.blank_tag_present = false;
                lastSeenValid = false;
                addToRecentSpools();
                sendSpoolDetectedMessage();
                xSemaphoreGive(tagMutex);
                return true;
            }
        }

        Serial.printf("NFCManager: Raw fallback failed (%s)\n", opt_error_str(err));
        return false;
    }

    // Commit the successful update to shared state.
    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("NFCManager: executeWrite - write succeeded but commit lock failed");
        return false;
    }
    currentSpool.tag_data = updatedTag;
    currentSpool.tag_data_valid = true;
    currentSpool.blank_tag_present = false;
    xSemaphoreGive(tagMutex);

    switch (request.type) {
        case NFCWriteType::REMOVE_WEIGHT:
            Serial.printf("NFCManager: Removed %.2f grams from spool\n", request.data.grams_to_remove);
            break;
        case NFCWriteType::CHANGE_COLOR:
            Serial.printf("NFCManager: Changed color to RGBA(%u,%u,%u,%u)\n",
                request.data.new_color[0], request.data.new_color[1],
                request.data.new_color[2], request.data.new_color[3]);
            break;
        case NFCWriteType::CHANGE_FILAMENT_TYPE:
            Serial.printf("NFCManager: Changed material type to %u\n", request.data.new_material_type);
            break;
        case NFCWriteType::SET_CONSUMED_WEIGHT:
            Serial.printf("NFCManager: Set consumed weight to %.2f grams\n", request.data.consumed_weight);
            break;
        case NFCWriteType::SET_BRAND_NAME:
            Serial.printf("NFCManager: Set brand name to %s\n", request.data.brand_name);
            break;
        case NFCWriteType::WRITE_SPOOLMAN_ID:
            Serial.printf("NFCManager: Wrote spoolman ID %d to tag\n", request.data.spoolman_id);
            break;
        default:
            break;
    }

    return true;
}

void NFCManager::sendSpoolUpdatedMessage(uint32_t request_id, NFCWriteType type, bool success) {
    (void)request_id;

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
    // Benign race: reads lastSeenValid/lastSeenUid without mutex. These are only
    // written by the scan task (same task calling this), so no torn reads. The BLE
    // thread's requestCurrentSpool() can clear lastSeenValid, but worst case we do
    // one extra NFC read — no correctness issue.
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

    newEntry.last_seen = time(nullptr);
    newEntry.valid = true;
    newEntry.synced_to_spoolman = false;

    int32_t smId = -1;
    opt_get_gp_spoolman_id(&currentSpool.tag_data, &smId);
    newEntry.spoolman_id = smId;

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

bool NFCManager::scanOnce() {
    uint8_t uid[8];
    uint8_t uidLength = 0;
    bool result = false;

    connection_->reset();
    connection_->setupRF();

    if (connection_->detectTag(uid, &uidLength)) {
        connection_->setCurrentUid(uid, uidLength);

        if (!isDuplicateSpool(uid, uidLength)) {
            // readAndParseTag manages its own mutex internally
            if (readAndParseTag(uid, uidLength)) {
                result = true;
            } else {
                // Blank tag detected — take mutex briefly for state update
                if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    for (uint8_t i = 0; i < uidLength && i < 8; i++) {
                        sprintf(currentSpool.spool_id + (i * 2), "%02X", uid[i]);
                    }
                    currentSpool.spool_id[uidLength * 2] = '\0';
                    memcpy(currentSpool.uid, uid, uidLength);
                    currentSpool.uid_length = uidLength;
                    currentSpool.present = true;
                    currentSpool.tag_data_valid = false;
                    currentSpool.blank_tag_present = true;
                    memcpy(lastSeenUid, uid, uidLength);
                    lastSeenUidLength = uidLength;
                    lastSeenValid = true;
                    xSemaphoreGive(tagMutex);
                }
            }
        }
        processWriteQueue();
    } else {
        if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (lastSeenValid) {
                sendTagRemovedMessage();
            }
            currentSpool.present = false;
            currentSpool.blank_tag_present = false;
            lastSeenValid = false;
            xSemaphoreGive(tagMutex);
        }
    }
    return result;
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

void NFCManager::updateRecentSpoolSyncStatus(const char* spool_id, bool synced) {
    if (tagMutex == nullptr || spool_id == nullptr) {
        return;
    }

    if (xSemaphoreTake(tagMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    for (size_t i = 0; i < recentSpoolsCount; i++) {
        if (recentSpools[i].valid && strcmp(recentSpools[i].spool_id, spool_id) == 0) {
            recentSpools[i].synced_to_spoolman = synced;
            break;
        }
    }

    xSemaphoreGive(tagMutex);
}
