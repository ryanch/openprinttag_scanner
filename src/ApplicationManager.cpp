#include "ApplicationManager.h"
#ifndef NATIVE_TEST
  #include "NFCTypes.h"
  #include "NFCManager.h"
  #include "LCDManager.h"
  #include "BluetoothManager.h"
  #include "PrinterManager.h"
  #include "SpoolmanManager.h"
  #include "ConfigurationManager.h"
  #include "HomeAssistantManager.h"
  #include <Arduino.h>
  #if USE_STATUS_LED
  #include "LEDManager.h"
  extern LEDManager ledManager;
  #endif
  #include <WiFi.h>
#else
  #include "platform/NativePlatform.h"
  #include "FakeLCDManager.h"
  #include "TestNFCManager.h"
#endif
#include <cstring>

#ifdef NATIVE_TEST
static constexpr uint32_t TAG_REMOVED_STATUS_DELAY_MS = 25;
static constexpr uint32_t TYPE_REMAIN_DISPLAY_DELAY_MS = 25;
#else
static constexpr uint32_t TAG_REMOVED_STATUS_DELAY_MS = 5000;
static constexpr uint32_t TYPE_REMAIN_DISPLAY_DELAY_MS = 5000;
#endif

ApplicationManager& ApplicationManager::getInstance() {
    static ApplicationManager instance;
    return instance;
}

bool ApplicationManager::begin(LCDManager* lcd) {
    if (messageQueue != nullptr) {
        return true;  // Already initialized
    }

    lcdManager = lcd;

    messageQueue = xQueueCreate(QUEUE_SIZE, sizeof(AppMessage));
    if (messageQueue == nullptr) {
        Serial.println("ApplicationManager: Failed to create message queue");
        return false;
    }

    Serial.println("ApplicationManager: Message queue created");
    return true;
}

bool ApplicationManager::sendMessage(const AppMessage& msg, uint32_t waitMs) {
    if (messageQueue == nullptr) {
        Serial.println("ApplicationManager: Queue not initialized");
        return false;
    }

    TickType_t ticksToWait = (waitMs == 0) ? 0 : pdMS_TO_TICKS(waitMs);
    BaseType_t result = xQueueSend(messageQueue, &msg, ticksToWait);
    return result == pdTRUE;
}

void ApplicationManager::processMessages() {
    if (messageQueue == nullptr) {
        return;
    }

    AppMessage msg;
    while (xQueueReceive(messageQueue, &msg, 0) == pdTRUE) {
        handleMessage(msg);
    }

    if (pendingStatusAfterTagRemoved) {
        uint32_t elapsedMs = static_cast<uint32_t>(millis() - tagRemovedAtMs);
        if (elapsedMs >= TAG_REMOVED_STATUS_DELAY_MS) {
            showStatusOnLCD();
            pendingStatusAfterTagRemoved = false;
        }
    }

    // Check for delayed Type/Remain display
    if (pendingTypeRemainDisplay && lcdManager) {
        uint32_t elapsedMs = static_cast<uint32_t>(millis() - typeRemainScheduledAtMs);
        if (elapsedMs >= TYPE_REMAIN_DISPLAY_DELAY_MS) {
            char line1[17];
            char line2[17];
            snprintf(line1, sizeof(line1), "Type: %.10s", delayedDisplayMaterialName);
            snprintf(line2, sizeof(line2), "Remain: %.0fg", delayedDisplayKgRemaining * 1000.0f);
            lcdManager->updateScreen(line1, line2);

            pendingTypeRemainDisplay = false;

            Serial.printf("ApplicationManager: Displayed delayed Type/Remain - Type: %s, Remain: %.0fg\n",
                         delayedDisplayMaterialName, delayedDisplayKgRemaining * 1000.0f);
        }
    }
}

void ApplicationManager::showStatusOnLCD() {
    if (lcdManager == nullptr) {
        return;
    }

#ifndef NATIVE_TEST
    auto& config = ConfigurationManager::getInstance();

    char bleInd = BluetoothManager::getInstance().isAdvertising() ? '+' : '!';

    char wifiInd;
    if (strlen(config.getWiFiSSID()) == 0) {
        wifiInd = '?';
    } else {
        wifiInd = (WiFi.status() == WL_CONNECTED) ? '+' : '!';
    }

    char prusaInd;
    if (strlen(config.getPrusaLinkURL()) == 0) {
        prusaInd = '?';
    } else {
        prusaInd = PrinterManager::getInstance().isConnected() ? '+' : '!';
    }

    char smInd;
    if (strlen(config.getSpoolmanURL()) == 0) {
        smInd = '?';
    } else {
        smInd = '+';
    }

    char haInd;
    if (!config.getHAEnabled()) {
        haInd = '?';
    } else if (strlen(config.getHAMqttHost()) == 0) {
        haInd = '!';
    } else {
        haInd = HomeAssistantManager::getInstance().isConnected() ? '+' : '!';
    }
#else
    // Native tests do not initialize network/BLE integrations.
    char bleInd = '?';
    char wifiInd = '?';
    char prusaInd = '?';
    char smInd = '?';
    char haInd = '?';
#endif

    char line1[17];
    char line2[17];
    snprintf(line1, sizeof(line1), "NFC+ BLE%c Wifi%c", bleInd, wifiInd);
    snprintf(line2, sizeof(line2), "PL%c SM%c HA%c", prusaInd, smInd, haInd);
    lcdManager->updateScreen(line1, line2);
}

void ApplicationManager::scheduleTypeRemainDisplay(const char* material_name, float kg_remaining) {
    strncpy(delayedDisplayMaterialName, material_name, sizeof(delayedDisplayMaterialName) - 1);
    delayedDisplayMaterialName[sizeof(delayedDisplayMaterialName) - 1] = '\0';
    delayedDisplayKgRemaining = kg_remaining;
    pendingTypeRemainDisplay = true;
    typeRemainScheduledAtMs = millis();
}

void ApplicationManager::handleMessage(const AppMessage& msg) {
    switch (msg.type) {
        case AppMessageType::PRINT_STARTED:
            handlePrintStarted(msg);
            break;

        case AppMessageType::PRINT_ENDED:
            handlePrintEnded(msg);
            break;

        case AppMessageType::SPOOL_DETECTED:
            handleSpoolDetected(msg);
            break;

        case AppMessageType::SPOOL_UPDATED:
            handleSpoolUpdated(msg);
            break;

        case AppMessageType::BLANK_TAG_DETECTED:
            handleBlankTagDetected(msg);
            break;

        case AppMessageType::SPOOLMAN_SYNCED:
            handleSpoolmanSynced(msg);
            break;

        case AppMessageType::TAG_REMOVED:
            handleTagRemoved(msg);
            break;

        case AppMessageType::HA_WRITE_TAG:
            handleHAWriteTag(msg);
            break;

        case AppMessageType::HA_UPDATE_REMAINING:
            handleHAUpdateRemaining(msg);
            break;
    }
}

void ApplicationManager::handlePrintStarted(const AppMessage& msg) {
    Serial.printf("EVENT: PrintStarted - job_id=%d\n",
        msg.payload.printStarted.job_id);

    // Request fresh spool detection
    NFCManager::getInstance().requestCurrentSpool();

    // Transition to monitoring state
    currentState = AppState::MONITORING_PRINT;
    currentJobId = msg.payload.printStarted.job_id;
    startingSpoolId[0] = '\0';
    spoolChangedDuringPrint = false;

    if (lcdManager) {
        char line2[17];
        snprintf(line2, sizeof(line2), "Job: %d", currentJobId);
        lcdManager->updateScreen("Print Started", line2);
    }

    // Publish printer state to HA
    {
        char json[96];
        snprintf(json, sizeof(json), "{\"state\":\"printing\",\"job_id\":%d}",
                 msg.payload.printStarted.job_id);
        publishToHA("printer/state", json, true);
    }
}

void ApplicationManager::handlePrintEnded(const AppMessage& msg) {
    Serial.printf("EVENT: PrintEnded - job_id=%d, filament=%.2fg, canceled=%s\n",
        msg.payload.printEnded.job_id,
        msg.payload.printEnded.filament_used_grams,
        msg.payload.printEnded.canceled ? "true" : "false");

    if (currentState == AppState::MONITORING_PRINT) {
        finishPrint(msg.payload.printEnded.filament_used_grams, msg.payload.printEnded.canceled);
    }
    currentState = AppState::IDLE;

    // Publish printer state to HA
    {
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"state\":\"idle\",\"last_job_id\":%d,\"filament_used_g\":%.1f}",
                 msg.payload.printEnded.job_id,
                 msg.payload.printEnded.filament_used_grams);
        publishToHA("printer/state", json, true);
    }
}

void ApplicationManager::handleSpoolDetected(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolDetected - spool_id=%s, material_type=%u, kg_remaining=%.3f\n",
        msg.payload.spoolDetected.spool_id,
        msg.payload.spoolDetected.material_type,
        msg.payload.spoolDetected.kg_remaining);
    
    #if USE_STATUS_LED
        ledManager.flashTagDetected();
        ledManager.showFilamentColor(
            msg.payload.spoolDetected.primary_color[0],
            msg.payload.spoolDetected.primary_color[1],
            msg.payload.spoolDetected.primary_color[2]
        );
    #endif

    if (currentState == AppState::MONITORING_PRINT) {
        if (startingSpoolId[0] == '\0') {
            // First spool detected during this print - capture it
            strncpy(startingSpoolId, msg.payload.spoolDetected.spool_id, sizeof(startingSpoolId) - 1);
            startingSpoolId[sizeof(startingSpoolId) - 1] = '\0';
            Serial.printf("ApplicationManager: Captured starting spool: %s\n", startingSpoolId);
        } else if (strcmp(startingSpoolId, msg.payload.spoolDetected.spool_id) != 0) {
            // Different spool detected during print
            spoolChangedDuringPrint = true;
            Serial.printf("ApplicationManager: WARNING - Spool changed during print! Was %s, now %s\n",
                startingSpoolId, msg.payload.spoolDetected.spool_id);
        }
    }

    // Clear any pending delayed Type/Remain display (new spool detected)
    pendingTypeRemainDisplay = false;
    pendingStatusAfterTagRemoved = false;

    // Update LCD with spool info (dedupe by spool_id)
    if (lcdManager && strcmp(lastDisplayedSpoolId, msg.payload.spoolDetected.spool_id) != 0) {
        strncpy(lastDisplayedSpoolId, msg.payload.spoolDetected.spool_id, sizeof(lastDisplayedSpoolId) - 1);
        lastDisplayedSpoolId[sizeof(lastDisplayedSpoolId) - 1] = '\0';
        lastDisplayedBlankId[0] = '\0';  // Clear so blank tag re-displays if swapped

        char line1[17];
        char line2[17];
        snprintf(line1, sizeof(line1), "Type: %.10s", msg.payload.spoolDetected.material_name);
        snprintf(line2, sizeof(line2), "Remain: %.0fg", msg.payload.spoolDetected.kg_remaining * 1000.0f);
        lcdManager->updateScreen("**** Spool ****", "*** Scanned ***", line1, line2);
    } else if (lcdManager) {
        Serial.printf("ApplicationManager: Skipping LCD update for already displayed spool %s\n", msg.payload.spoolDetected.spool_id);
    }

    // Publish tag state to HA (always, regardless of mode)
    {
        const auto& s = msg.payload.spoolDetected;
        char colorHex[8];
        snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X",
                 s.primary_color[0], s.primary_color[1], s.primary_color[2]);
        char json[384];
        snprintf(json, sizeof(json),
                 "{\"uid\":\"%s\",\"present\":true,\"material_type\":\"%s\","
                 "\"material_name\":\"%s\",\"color\":\"%s\",\"manufacturer\":\"%s\","
                 "\"remaining_g\":%.1f,\"initial_weight_g\":%.1f,\"spoolman_id\":%d,"
                 "\"blank\":false}",
                 s.spool_id, s.material_name, s.material_name, colorHex,
                 s.manufacturer, s.kg_remaining * 1000.0f, s.initial_weight_g,
                 s.spoolman_id);
        publishToHA("tag/state", json, true);
    }

#ifndef NATIVE_TEST
    // Trigger Spoolman sync if configured (only in SELF_DIRECTED mode)
    // Skip if suppress_spoolman_sync is set (e.g., after write_spoolman_spool batch)
    if (automationMode == AutomationMode::SELF_DIRECTED &&
        SpoolmanManager::getInstance().isConfigured() &&
        !msg.payload.spoolDetected.suppress_spoolman_sync) {
        enqueueSpoolmanSync(msg.payload.spoolDetected);
    }
#endif
}

void ApplicationManager::handleSpoolUpdated(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolUpdated - spool_id=%s, update_type=%u, success=%s\n",
        msg.payload.spoolUpdated.spool_id,
        msg.payload.spoolUpdated.update_type,
        msg.payload.spoolUpdated.success ? "true" : "false");

    // Get current spool state for material name (needed for delayed display)
    char materialName[32] = {0};
    float kgRemaining = msg.payload.spoolUpdated.kg_remaining;

#ifndef NATIVE_TEST
    CurrentSpoolState state;
    if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
        opt_get_material_name(&state.tag_data, materialName, sizeof(materialName));
    }
    bool spoolmanConfigured = SpoolmanManager::getInstance().isConfigured();
#else
    // In test mode, also try to get material name for delayed display testing
    CurrentSpoolState state;
    if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
        opt_get_material_name(&state.tag_data, materialName, sizeof(materialName));
    }
    bool spoolmanConfigured = false;
#endif

#if USE_STATUS_LED
    if (msg.payload.spoolUpdated.success) {
        ledManager.flashWriteSuccess();
    } else {
        ledManager.flashWriteFailure();
    }

    if (state.tag_data_valid) {
        uint8_t color[4] = {0};
        if (opt_get_primary_color(&state.tag_data, color) == OPT_OK) {
            ledManager.showFilamentColor(color[0], color[1], color[2]);
        } else {
            ledManager.showOff();
        }
    } else {
        ledManager.showOff();
    }
#endif

    if (lcdManager) {
        if (msg.payload.spoolUpdated.success) {
            char line1[17];
            snprintf(line1, sizeof(line1), "Updated: %.0fg",
                     kgRemaining * 1000.0f);
            if (spoolmanConfigured) {
                lcdManager->updateScreen(line1, "Syncing Spoolman");
                // Type/Remain will be scheduled after SPOOLMAN_SYNCED
            } else {
                char line2[17];
                snprintf(line2, sizeof(line2), "Remain: %.0fg",
                         kgRemaining * 1000.0f);
                lcdManager->updateScreen("Spool Updated!", line2);

                // Schedule Type/Remain display after 5 seconds (no Spoolman path)
                if (materialName[0] != '\0') {
                    scheduleTypeRemainDisplay(materialName, kgRemaining);
                }
            }
        } else {
            lcdManager->updateScreen("Spool Update", "Failed!");
        }
    }

#ifndef NATIVE_TEST
    // Avoid sync loops:
    // - FORMAT_NEW is followed by SPOOL_DETECTED, which already triggers sync
    // - WRITE_SPOOLMAN_ID would cause sync->writeback->sync feedback
    // - suppress_sync flag (used for batched writes like Mode B) prevents syncs
    const uint8_t updateType = msg.payload.spoolUpdated.update_type;
    const bool shouldSyncAfterUpdate =
        !msg.payload.spoolUpdated.suppress_sync &&
        updateType != static_cast<uint8_t>(NFCWriteType::FORMAT_NEW) &&
        updateType != static_cast<uint8_t>(NFCWriteType::WRITE_SPOOLMAN_ID);

    // Invalidate cache after WRITE_SPOOLMAN_ID so the next sync reads the fresh tag value
    if (msg.payload.spoolUpdated.success &&
        updateType == static_cast<uint8_t>(NFCWriteType::WRITE_SPOOLMAN_ID)) {
        SpoolmanManager::getInstance().invalidateCachedSpoolmanId(msg.payload.spoolUpdated.spool_id);
    }

    // Trigger Spoolman sync after relevant successful tag updates (only in SELF_DIRECTED mode)
    if (automationMode == AutomationMode::SELF_DIRECTED &&
        spoolmanConfigured && msg.payload.spoolUpdated.success && shouldSyncAfterUpdate) {
        // Read current spool state from NFCManager to get full tag data
        CurrentSpoolState state;
        if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
            SpoolDetectedPayload fakeSpool;
            memset(&fakeSpool, 0, sizeof(fakeSpool));
            strncpy(fakeSpool.spool_id, msg.payload.spoolUpdated.spool_id, sizeof(fakeSpool.spool_id) - 1);
            opt_get_material_type(&state.tag_data, &fakeSpool.material_type);
            fakeSpool.kg_remaining = msg.payload.spoolUpdated.kg_remaining;
            opt_get_primary_color(&state.tag_data, fakeSpool.primary_color);
            if (opt_get_density(&state.tag_data, &fakeSpool.density) != OPT_OK) {
                fakeSpool.density = 0.0f;
            }
            if (opt_get_filament_diameter(&state.tag_data, &fakeSpool.diameter) != OPT_OK) {
                fakeSpool.diameter = 0.0f;
            }
            float full_weight = 0.0f;
            opt_get_actual_full_weight(&state.tag_data, &full_weight);
            fakeSpool.initial_weight_g = full_weight;
            if (opt_get_brand_name(&state.tag_data, fakeSpool.manufacturer, sizeof(fakeSpool.manufacturer)) != OPT_OK) {
                fakeSpool.manufacturer[0] = '\0';
            }
            int32_t tagSpoolmanId = -1;
            opt_get_gp_spoolman_id(&state.tag_data, &tagSpoolmanId);
            fakeSpool.spoolman_id = tagSpoolmanId;
            enqueueSpoolmanSync(fakeSpool);
        } else {
            // NFC state may be temporarily unavailable right after a write.
            // Force a re-read so SPOOL_DETECTED re-emits with full tag data and triggers sync.
            Serial.println("ApplicationManager: Spool update sync deferred; requesting fresh spool read");
            NFCManager::getInstance().requestCurrentSpool();
        }
    }
#endif
}

void ApplicationManager::handleBlankTagDetected(const AppMessage& msg) {
    Serial.printf("EVENT: BlankTagDetected - spool_id=%s\n",
        msg.payload.blankTag.spool_id);
#if USE_STATUS_LED
    ledManager.flashParseFailed();
#endif

    pendingStatusAfterTagRemoved = false;

    if (lcdManager && strcmp(lastDisplayedBlankId, msg.payload.blankTag.spool_id) != 0) {
        strncpy(lastDisplayedBlankId, msg.payload.blankTag.spool_id, sizeof(lastDisplayedBlankId) - 1);
        lastDisplayedBlankId[sizeof(lastDisplayedBlankId) - 1] = '\0';
        lastDisplayedSpoolId[0] = '\0';  // Clear so valid spool re-displays if swapped

        lcdManager->updateScreen("**** Spool ****", "*** Scanned ***", "Unknown Tag", "Use app to setup");
    }

    // Publish blank tag state to HA
    {
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"uid\":\"%s\",\"present\":true,\"material_type\":\"\","
                 "\"material_name\":\"\",\"color\":\"\",\"manufacturer\":\"\","
                 "\"remaining_g\":0.0,\"initial_weight_g\":0.0,\"spoolman_id\":-1,"
                 "\"blank\":true}",
                 msg.payload.blankTag.spool_id);
        publishToHA("tag/state", json, true);
    }
}

void ApplicationManager::finishPrint(float gramsUsed, bool /*canceled*/) {
    if (spoolChangedDuringPrint) {
        Serial.println("ApplicationManager: Spool changed during print - not updating weight");
        if (lcdManager) {
            lcdManager->updateScreen("Spool changed!", "No update");
        }
        return;
    }

    if (startingSpoolId[0] == '\0') {
        Serial.println("ApplicationManager: No spool detected during print - not updating weight");
        if (lcdManager) {
            lcdManager->updateScreen("No spool found", "No update");
        }
        return;
    }

    if (gramsUsed > 0) {
        Serial.printf("ApplicationManager: Updating spool %s - removing %.2fg\n",
            startingSpoolId, gramsUsed);

        // Only auto-update NFC tag in SELF_DIRECTED mode
        if (automationMode == AutomationMode::SELF_DIRECTED) {
            if (lcdManager) {
                lcdManager->updateScreen("Updating spool..", "");
            }

            // Enqueue write request with expected spool ID
            NFCWriteRequest request;
            request.request_id = millis();  // Simple unique ID
            request.type = NFCWriteType::REMOVE_WEIGHT;
            strncpy(request.expected_spool_id, startingSpoolId, sizeof(request.expected_spool_id) - 1);
            request.expected_spool_id[sizeof(request.expected_spool_id) - 1] = '\0';
            request.data.grams_to_remove = gramsUsed;

            NFCManager::getInstance().enqueueWrite(request);
        } else {
            if (lcdManager) {
                lcdManager->updateScreen("Print done", "HA controlled");
            }
        }
    } else {
        Serial.println("ApplicationManager: No filament used - not updating spool");
        if (lcdManager) {
            lcdManager->updateScreen("Print done", "No filament used");
        }
    }
}

void ApplicationManager::handleSpoolmanSynced(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolmanSynced - spool_id=%s, success=%s, spoolman_id=%d\n",
        msg.payload.spoolmanSynced.spool_id,
        msg.payload.spoolmanSynced.success ? "true" : "false",
        msg.payload.spoolmanSynced.spoolman_id);

    // Get current spool state for material name
    char materialName[32] = {0};
    float kgRemaining = msg.payload.spoolmanSynced.kg_remaining;

    CurrentSpoolState state;
    if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
        opt_get_material_name(&state.tag_data, materialName, sizeof(materialName));
    }

    if (lcdManager) {
        if (msg.payload.spoolmanSynced.success) {
            char line1[17];
            char line2[17];
            snprintf(line1, sizeof(line1), "Type: %.10s", materialName);
            snprintf(line2, sizeof(line2), "Remain: %.0fg",
                     kgRemaining * 1000.0f);
            lcdManager->updateScreen(line1, line2);
        } else {
            char line1[17];
            snprintf(line1, sizeof(line1), "Updated: %.0fg",
                     kgRemaining * 1000.0f);
            lcdManager->updateScreen(line1, "Spoolman Error");

            // Schedule Type/Remain display after 5 seconds even on error
            if (materialName[0] != '\0') {
                scheduleTypeRemainDisplay(materialName, kgRemaining);
            }
        }
    }

#ifndef NATIVE_TEST
    // Update recent spool sync status
    if (msg.payload.spoolmanSynced.success) {
        NFCManager::getInstance().updateRecentSpoolSyncStatus(
            msg.payload.spoolmanSynced.spool_id, true);
    }

    // Write Spoolman ID back to tag if it's new or changed
    if (msg.payload.spoolmanSynced.success && msg.payload.spoolmanSynced.spoolman_id > 0) {
        CurrentSpoolState state;
        if (NFCManager::getInstance().getCurrentSpoolState(state) && state.tag_data_valid) {
            int32_t existingId = -1;
            opt_get_gp_spoolman_id(&state.tag_data, &existingId);
            if (existingId != msg.payload.spoolmanSynced.spoolman_id) {
                NFCWriteRequest request;
                request.request_id = millis();
                request.type = NFCWriteType::WRITE_SPOOLMAN_ID;
                strncpy(request.expected_spool_id, msg.payload.spoolmanSynced.spool_id,
                        sizeof(request.expected_spool_id) - 1);
                request.expected_spool_id[sizeof(request.expected_spool_id) - 1] = '\0';
                request.data.spoolman_id = msg.payload.spoolmanSynced.spoolman_id;
                NFCManager::getInstance().enqueueWrite(request);
                Serial.printf("ApplicationManager: Enqueued WRITE_SPOOLMAN_ID %d for spool %s\n",
                    msg.payload.spoolmanSynced.spoolman_id, msg.payload.spoolmanSynced.spool_id);
            }
        }
    }
#endif
}

void ApplicationManager::handleTagRemoved(const AppMessage& msg) {
    Serial.printf("EVENT: TagRemoved - spool_id=%s\n",
        msg.payload.tagRemoved.spool_id);
#if USE_STATUS_LED
    ledManager.showOff();
#endif

    // Clear displayed spool so next scan re-displays
    lastDisplayedSpoolId[0] = '\0';
    lastDisplayedBlankId[0] = '\0';
    tagRemovedAtMs = millis();
    pendingStatusAfterTagRemoved = true;

    // Publish tag removed to HA
    publishToHA("tag/state",
                "{\"uid\":\"\",\"present\":false,\"material_type\":\"\","
                "\"material_name\":\"\",\"color\":\"\",\"manufacturer\":\"\","
                "\"remaining_g\":0.0,\"initial_weight_g\":0.0,\"spoolman_id\":-1,"
                "\"blank\":false}",
                true);
}

void ApplicationManager::handleHAWriteTag(const AppMessage& msg) {
    Serial.printf("EVENT: HAWriteTag - expected_uid=%s\n",
        msg.payload.haWriteTag.expected_uid);

    // Enqueue individual write requests for each field
    const auto& p = msg.payload.haWriteTag;

    // Material type
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = millis();
    req.type = NFCWriteType::CHANGE_FILAMENT_TYPE;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    req.data.new_material_type = p.material_type;
    NFCManager::getInstance().enqueueWrite(req);

    // Color
    memset(&req, 0, sizeof(req));
    req.request_id = millis() + 1;
    req.type = NFCWriteType::CHANGE_COLOR;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    memcpy(req.data.new_color, p.color, 4);
    NFCManager::getInstance().enqueueWrite(req);

    // Brand name
    memset(&req, 0, sizeof(req));
    req.request_id = millis() + 2;
    req.type = NFCWriteType::SET_BRAND_NAME;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    strncpy(req.data.brand_name, p.manufacturer, sizeof(req.data.brand_name) - 1);
    NFCManager::getInstance().enqueueWrite(req);

    // Remaining weight (set consumed = initial - remaining)
    if (p.initial_weight_g > 0) {
        float consumed = p.initial_weight_g - p.remaining_g;
        if (consumed < 0) consumed = 0;
        memset(&req, 0, sizeof(req));
        req.request_id = millis() + 3;
        req.type = NFCWriteType::SET_CONSUMED_WEIGHT;
        strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
        req.data.consumed_weight = consumed;
        NFCManager::getInstance().enqueueWrite(req);
    }

    // Spoolman ID
    if (p.spoolman_id > 0) {
        memset(&req, 0, sizeof(req));
        req.request_id = millis() + 4;
        req.type = NFCWriteType::WRITE_SPOOLMAN_ID;
        strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
        req.data.spoolman_id = p.spoolman_id;
        NFCManager::getInstance().enqueueWrite(req);
    }
}

void ApplicationManager::handleHAUpdateRemaining(const AppMessage& msg) {
    Serial.printf("EVENT: HAUpdateRemaining - expected_uid=%s, remaining_g=%.2f\n",
        msg.payload.haUpdateRemaining.expected_uid,
        msg.payload.haUpdateRemaining.remaining_g);

    const auto& p = msg.payload.haUpdateRemaining;

    // Set consumed weight: we need the initial weight from the current tag
    // The NFC write will compute consumed = initial - remaining internally
    // We use SET_CONSUMED_WEIGHT with consumed = initial - remaining
    // But we don't know initial here. Use SET_CONSUMED_WEIGHT with a sentinel approach.
    // Actually, the HA manager will compute consumed before sending this message.
    // For now, treat remaining_g as a consumed weight to set.
    // The HA task should compute consumed = initial - remaining before sending.
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = millis();
    req.type = NFCWriteType::SET_CONSUMED_WEIGHT;
    strncpy(req.expected_spool_id, p.expected_uid, sizeof(req.expected_spool_id) - 1);
    // remaining_g here is actually the consumed weight (computed by HA task)
    req.data.consumed_weight = p.remaining_g;
    NFCManager::getInstance().enqueueWrite(req);
}

#ifndef NATIVE_TEST
void ApplicationManager::enqueueSpoolmanSync(const SpoolDetectedPayload& spool) {
    SpoolmanSyncRequest req;
    memset(&req, 0, sizeof(req));
    strncpy(req.spool_id, spool.spool_id, sizeof(req.spool_id) - 1);
    req.material_type = spool.material_type;
    strncpy(req.manufacturer, spool.manufacturer, sizeof(req.manufacturer) - 1);
    memcpy(req.color, spool.primary_color, 4);
    req.remaining_weight_g = spool.kg_remaining * 1000.0f;
    req.initial_weight_g = spool.initial_weight_g;

    // Use tag density if available, otherwise use default
    if (spool.density > 0.0f) {
        req.density = spool.density;
    } else {
        // Default densities by material
        switch (spool.material_type) {
            case OPT_MATERIAL_TYPE_PLA:  req.density = 1.24f; break;
            case OPT_MATERIAL_TYPE_PETG: req.density = 1.27f; break;
            case OPT_MATERIAL_TYPE_ABS:  req.density = 1.04f; break;
            default: req.density = 1.20f; break;
        }
    }

    // Use tag diameter if available, otherwise default 1.75mm
    req.diameter = (spool.diameter > 0.0f) ? spool.diameter : 1.75f;

    req.spoolman_id = spool.spoolman_id;

    SpoolmanManager::getInstance().enqueueSync(req);
}
#endif

void ApplicationManager::publishToHA(const char* topicSuffix, const char* payload, bool retained) {
#ifndef NATIVE_TEST
    auto& ha = HomeAssistantManager::getInstance();
    if (!ha.isConfigured()) return;

    HAPublishRequest req;
    memset(&req, 0, sizeof(req));

    char deviceId[7];
    HomeAssistantManager::getDeviceId(deviceId, sizeof(deviceId));
    snprintf(req.topic, sizeof(req.topic), "openprinttag/%s/%s", deviceId, topicSuffix);
    strncpy(req.payload, payload, sizeof(req.payload) - 1);
    req.retained = retained;

    ha.enqueuePublish(req);
#else
    (void)topicSuffix;
    (void)payload;
    (void)retained;
#endif
}
