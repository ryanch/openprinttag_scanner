#include "ApplicationManager.h"
#include "NFCManager.h"
#include "LCDManager.h"
#include <Arduino.h>
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
        }
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

        char line1[17];
        char line2[17];
        snprintf(line1, sizeof(line1), "Type: %s", msg.payload.spoolDetected.material_name);
        snprintf(line2, sizeof(line2), "Remain: %.0fg", msg.payload.spoolDetected.kg_remaining * 1000.0f);
        lcdManager->updateScreen(line1, line2);
    }
}

void ApplicationManager::handleSpoolUpdated(const AppMessage& msg) {
    Serial.printf("EVENT: SpoolUpdated - spool_id=%s, update_type=%u, success=%s\n",
        msg.payload.spoolUpdated.spool_id,
        msg.payload.spoolUpdated.update_type,
        msg.payload.spoolUpdated.success ? "true" : "false");

    if (lcdManager) {
        if (msg.payload.spoolUpdated.success) {
            char line2[17];
            snprintf(line2, sizeof(line2), "Remain: %.0fg", msg.payload.spoolUpdated.kg_remaining * 1000.0f);
            lcdManager->updateScreen("Spool Updated!", line2);
        } else {
            lcdManager->updateScreen("Spool Update", "Failed!");
        }
    }
}

void ApplicationManager::finishPrint(float gramsUsed, bool canceled) {
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
