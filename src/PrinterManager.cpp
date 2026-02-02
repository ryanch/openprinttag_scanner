#include "PrinterManager.h"
#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

PrinterManager& PrinterManager::getInstance() {
    static PrinterManager instance;
    return instance;
}

void PrinterManager::begin() {
    state = PrinterState::IDLE;
    currentJobId = -1;
    currentJobTotalFilamentG = 0.0f;
    lastProgressPercent = 0.0f;
    Serial.println("PrinterManager: Initialized");
}

void PrinterManager::startPollingTask() {
    if (pollingTaskHandle != nullptr) {
        Serial.println("PrinterManager: Polling task already running");
        return;
    }

    xTaskCreate(
        pollingTaskFunc,
        "PrinterPoll",
        POLLING_TASK_STACK_SIZE,
        this,
        POLLING_TASK_PRIORITY,
        &pollingTaskHandle
    );

    Serial.println("PrinterManager: Polling task started");
}

void PrinterManager::pollingTaskFunc(void* param) {
    PrinterManager* self = static_cast<PrinterManager*>(param);
    auto& config = ConfigurationManager::getInstance();

    while (true) {
        self->poll();
        vTaskDelay(pdMS_TO_TICKS(config.getPollIntervalMs()));
    }
}

void PrinterManager::poll() {
    auto& config = ConfigurationManager::getInstance();
    HTTPClient http;

    // First, get quick status
    String statusUrl = String(config.getPrusaLinkURL()) + "/api/v1/status";
    http.begin(statusUrl);
    http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());

    int statusCode = http.GET();
    if (statusCode != 200) {
        Serial.printf("PrinterManager: Status request failed: %d\n", statusCode);
        http.end();

        // If we were tracking and lost connection, treat as job disappeared
        if (state == PrinterState::TRACKING) {
            handleJobDisappeared();
        }
        return;
    }

    String statusPayload = http.getString();
    http.end();

    JsonDocument statusDoc;
    DeserializationError statusErr = deserializeJson(statusDoc, statusPayload);
    if (statusErr) {
        Serial.printf("PrinterManager: Status JSON parse error: %s\n", statusErr.c_str());
        return;
    }

    // Check if there's a job
    JsonVariant jobVariant = statusDoc["job"];
    bool hasJob = !jobVariant.isNull() && jobVariant["id"].is<int>();
    int jobId = hasJob ? jobVariant["id"].as<int>() : -1;
    float progress = hasJob ? jobVariant["progress"].as<float>() : 0.0f;

    if (!hasJob) {
        // No job present
        if (state == PrinterState::TRACKING) {
            // Job disappeared without proper state change
            handleJobDisappeared();
        }
        return;
    }

    // We have a job - get detailed job info
    String jobUrl = String(config.getPrusaLinkURL()) + "/api/v1/job";
    http.begin(jobUrl);
    http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());

    int jobStatusCode = http.GET();
    if (jobStatusCode != 200) {
        Serial.printf("PrinterManager: Job request failed: %d\n", jobStatusCode);
        http.end();
        return;
    }

    String jobPayload = http.getString();
    http.end();

    JsonDocument jobDoc;
    DeserializationError jobErr = deserializeJson(jobDoc, jobPayload);
    if (jobErr) {
        Serial.printf("PrinterManager: Job JSON parse error: %s\n", jobErr.c_str());
        return;
    }

    String jobState = jobDoc["state"].as<String>();
    float totalFilamentG = 0.0f;

    // Extract filament usage from file metadata
    JsonVariant fileMeta = jobDoc["file"]["meta"];
    if (!fileMeta.isNull()) {
        // PrusaLink uses "filament used [g]" key
        JsonVariant filamentUsed = fileMeta["filament used [g]"];
        if (!filamentUsed.isNull()) {
            totalFilamentG = filamentUsed.as<float>();
        }
    }

    // State machine logic
    if (state == PrinterState::IDLE) {
        if (jobState == "PRINTING" || jobState == "PAUSED") {
            handleJobDetected(jobId, totalFilamentG);
        }
    } else if (state == PrinterState::TRACKING) {
        // Check if job ID changed (new job while we were tracking)
        if (jobId != currentJobId) {
            // Old job was replaced - treat as canceled
            handleJobCanceled(currentJobId, lastProgressPercent);
            // Start tracking new job
            handleJobDetected(jobId, totalFilamentG);
            return;
        }

        // Update progress for potential cancel estimation
        lastProgressPercent = progress;

        if (jobState == "FINISHED") {
            handleJobFinished(jobId, totalFilamentG);
        } else if (jobState == "STOPPED" || jobState == "ERROR") {
            handleJobCanceled(jobId, progress);
        }
        // PRINTING or PAUSED - continue tracking
    }
}

void PrinterManager::handleJobDetected(int jobId, float totalFilamentG) {
    state = PrinterState::TRACKING;
    currentJobId = jobId;
    currentJobTotalFilamentG = totalFilamentG;
    lastProgressPercent = 0.0f;

    AppMessage msg;
    msg.type = AppMessageType::PRINT_STARTED;
    msg.payload.printStarted.job_id = jobId;
    ApplicationManager::getInstance().sendMessage(msg);

    Serial.printf("PrinterManager: Now tracking job %d (total filament: %.2fg)\n",
        jobId, totalFilamentG);
}

void PrinterManager::handleJobFinished(int jobId, float filamentUsedG) {
    AppMessage msg;
    msg.type = AppMessageType::PRINT_FINISHED;
    msg.payload.printFinished.job_id = jobId;
    msg.payload.printFinished.filament_used_grams = filamentUsedG;
    ApplicationManager::getInstance().sendMessage(msg);

    Serial.printf("PrinterManager: Job %d finished (filament: %.2fg)\n",
        jobId, filamentUsedG);

    state = PrinterState::IDLE;
    currentJobId = -1;
    currentJobTotalFilamentG = 0.0f;
    lastProgressPercent = 0.0f;
}

void PrinterManager::handleJobCanceled(int jobId, float progressPercent) {
    float estimatedFilament = (progressPercent / 100.0f) * currentJobTotalFilamentG;

    AppMessage msg;
    msg.type = AppMessageType::PRINT_CANCELED;
    msg.payload.printCanceled.job_id = jobId;
    msg.payload.printCanceled.est_filament_used_grams = estimatedFilament;
    ApplicationManager::getInstance().sendMessage(msg);

    Serial.printf("PrinterManager: Job %d canceled at %.1f%% (est filament: %.2fg)\n",
        jobId, progressPercent, estimatedFilament);

    state = PrinterState::IDLE;
    currentJobId = -1;
    currentJobTotalFilamentG = 0.0f;
    lastProgressPercent = 0.0f;
}

void PrinterManager::handleJobDisappeared() {
    float estimatedFilament = (lastProgressPercent / 100.0f) * currentJobTotalFilamentG;

    AppMessage msg;
    msg.type = AppMessageType::PRINT_CANCELED;
    msg.payload.printCanceled.job_id = currentJobId;
    msg.payload.printCanceled.est_filament_used_grams = estimatedFilament;
    ApplicationManager::getInstance().sendMessage(msg);

    Serial.printf("PrinterManager: Job %d disappeared (est filament: %.2fg)\n",
        currentJobId, estimatedFilament);

    state = PrinterState::IDLE;
    currentJobId = -1;
    currentJobTotalFilamentG = 0.0f;
    lastProgressPercent = 0.0f;
}
