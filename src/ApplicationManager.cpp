#include "ApplicationManager.h"
#include <Arduino.h>

ApplicationManager& ApplicationManager::getInstance() {
    static ApplicationManager instance;
    return instance;
}

bool ApplicationManager::begin() {
    if (messageQueue != nullptr) {
        return true;  // Already initialized
    }

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
                Serial.printf("EVENT: PrintStarted - job_id=%d\n",
                    msg.payload.printStarted.job_id);
                break;

            case AppMessageType::PRINT_CANCELED:
                Serial.printf("EVENT: PrintCanceled - job_id=%d, est_filament=%.2fg\n",
                    msg.payload.printCanceled.job_id,
                    msg.payload.printCanceled.est_filament_used_grams);
                break;

            case AppMessageType::PRINT_FINISHED:
                Serial.printf("EVENT: PrintFinished - job_id=%d, filament=%.2fg\n",
                    msg.payload.printFinished.job_id,
                    msg.payload.printFinished.filament_used_grams);
                break;

            case AppMessageType::SPOOL_DETECTED:
                Serial.printf("EVENT: SpoolDetected - spool_id=%s, material_type=%u, kg_remaining=%.3f\n",
                    msg.payload.spoolDetected.spool_id,
                    msg.payload.spoolDetected.material_type,
                    msg.payload.spoolDetected.kg_remaining);
                break;

            case AppMessageType::SPOOL_UPDATED:
                Serial.printf("EVENT: SpoolUpdated - spool_id=%s, update_type=%u, success=%s\n",
                    msg.payload.spoolUpdated.spool_id,
                    msg.payload.spoolUpdated.update_type,
                    msg.payload.spoolUpdated.success ? "true" : "false");
                break;
        }
    }
}
