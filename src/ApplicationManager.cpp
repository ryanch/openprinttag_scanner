#include "ApplicationManager.h"
#ifndef NATIVE_TEST
  #include "NFCTypes.h"
  #include "NFCManager.h"
  #include "LCDManager.h"
  #include "SpoolmanManager.h"
  #include "ConfigurationManager.h"
  #include <Arduino.h>
#else
  #include "platform/NativePlatform.h"
  #include "FakeLCDManager.h"
  #include "TestNFCManager.h"
#endif
#include <cstring>

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
}

void ApplicationManager::handleMessage(const AppMessage& msg) {
    switch (msg.type) {
        case AppMessageType::PRINT_STARTED:
            handlePrintStarted(msg);
            break;

        case AppMessageType::PRINT_CANCELED:
            handlePrintCanceled(msg);
            break;

        case AppMessageType::PRINT_FINISHED:
            handlePrintFinished(msg);
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
}

void ApplicationManager::handlePrintCanceled(const AppMessage& msg) {
    Serial.printf("EVENT: PrintCanceled - job_id=%d, est_filament=%.2fg\n",
        msg.payload.printCanceled.job_id,
        msg.payload.printCanceled.est_filament_used_grams);

    if (currentState == AppState::MONITORING_PRINT) {
        finishPrint(msg.payload.printCanceled.est_filament_used_grams, true);
    }
    currentState = AppState::IDLE;
}

void ApplicationManager::handlePrintFinished(const AppMessage& msg) {
    Serial.printf("EVENT: PrintFinished - job_id=%d, filament=%.2fg\n",
        msg.payload.printFinished.job_id,
        msg.payload.printFinished.filament_used_grams);

    if (currentState == AppState::MONITORING_PRINT) {
        finishPrint(msg.payload.printFinished.filament_used_grams, false);
    }
    currentState = AppState::IDLE;
}

void ApplicationManager::handleSpoolDetected(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolDetected - spool_id=%s, material_type=%u, kg_remaining=%.3f\n",
        msg.payload.spoolDetected.spool_id,
        msg.payload.spoolDetected.material_type,
        msg.payload.spoolDetected.kg_remaining);

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

    // Update LCD with spool info (dedupe by spool_id)
    if (lcdManager && strcmp(lastDisplayedSpoolId, msg.payload.spoolDetected.spool_id) != 0) {
        strncpy(lastDisplayedSpoolId, msg.payload.spoolDetected.spool_id, sizeof(lastDisplayedSpoolId) - 1);
        lastDisplayedSpoolId[sizeof(lastDisplayedSpoolId) - 1] = '\0';
        lastDisplayedBlankId[0] = '\0';  // Clear so blank tag re-displays if swapped

        char line1[17];
        char line2[17];
        snprintf(line1, sizeof(line1), "Type: %.10s", msg.payload.spoolDetected.material_name);
        snprintf(line2, sizeof(line2), "Remain: %.0fg", msg.payload.spoolDetected.kg_remaining * 1000.0f);
        lcdManager->updateScreen(line1, line2);
    } else if (lcdManager) {
        Serial.printf("ApplicationManager: Skipping LCD update for already displayed spool %s\n", msg.payload.spoolDetected.spool_id);
    }

#ifndef NATIVE_TEST
    // Trigger Spoolman sync if configured
    if (SpoolmanManager::getInstance().isConfigured()) {
        enqueueSpoolmanSync(msg.payload.spoolDetected);
    }
#endif
}

void ApplicationManager::handleSpoolUpdated(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolUpdated - spool_id=%s, update_type=%u, success=%s\n",
        msg.payload.spoolUpdated.spool_id,
        msg.payload.spoolUpdated.update_type,
        msg.payload.spoolUpdated.success ? "true" : "false");

#ifndef NATIVE_TEST
    bool spoolmanConfigured = SpoolmanManager::getInstance().isConfigured();
#else
    bool spoolmanConfigured = false;
#endif

    if (lcdManager) {
        if (msg.payload.spoolUpdated.success) {
            char line1[17];
            snprintf(line1, sizeof(line1), "Updated: %.0fg",
                     msg.payload.spoolUpdated.kg_remaining * 1000.0f);
            if (spoolmanConfigured) {
                lcdManager->updateScreen(line1, "Syncing Spoolman");
            } else {
                char line2[17];
                snprintf(line2, sizeof(line2), "Remain: %.0fg",
                         msg.payload.spoolUpdated.kg_remaining * 1000.0f);
                lcdManager->updateScreen("Spool Updated!", line2);
            }
        } else {
            lcdManager->updateScreen("Spool Update", "Failed!");
        }
    }

#ifndef NATIVE_TEST
    // Trigger Spoolman sync after spool weight update
    if (spoolmanConfigured && msg.payload.spoolUpdated.success) {
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
        }
    }
#endif
}

void ApplicationManager::handleBlankTagDetected(const AppMessage& msg) {
    Serial.printf("EVENT: BlankTagDetected - spool_id=%s\n",
        msg.payload.blankTag.spool_id);

    if (lcdManager && strcmp(lastDisplayedBlankId, msg.payload.blankTag.spool_id) != 0) {
        strncpy(lastDisplayedBlankId, msg.payload.blankTag.spool_id, sizeof(lastDisplayedBlankId) - 1);
        lastDisplayedBlankId[sizeof(lastDisplayedBlankId) - 1] = '\0';
        lastDisplayedSpoolId[0] = '\0';  // Clear so valid spool re-displays if swapped

        lcdManager->updateScreen("Unknown Tag", "Use app to setup");
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

    if (lcdManager) {
        if (msg.payload.spoolmanSynced.success) {
            char line1[17];
            snprintf(line1, sizeof(line1), "Updated: %.0fg",
                     msg.payload.spoolmanSynced.kg_remaining * 1000.0f);
            lcdManager->updateScreen(line1, "Spoolman OK!");
        } else {
            char line1[17];
            snprintf(line1, sizeof(line1), "Updated: %.0fg",
                     msg.payload.spoolmanSynced.kg_remaining * 1000.0f);
            lcdManager->updateScreen(line1, "Spoolman Error");
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
