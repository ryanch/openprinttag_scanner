#include "PrinterManager.h"
#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include <Arduino.h>

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

void PrinterManager::setStrategy(IPrinterLinkStrategy* strat) {
    strategy = strat;
}

void PrinterManager::poll() {
    if (!strategy) {
        return;
    }

    strategy->update();

    // Check connection status
    if (!strategy->isConnected()) {
        if (state == PrinterState::TRACKING) {
            handleJobDisappeared();
        }
        return;
    }

    // Check if there's a job
    if (!strategy->hasActiveJob()) {
        if (state == PrinterState::TRACKING) {
            handleJobDisappeared();
        }
        return;
    }

    // Get job info from strategy
    int jobId = strategy->getJobId();
    float progress = strategy->getProgress();
    float totalFilamentG = strategy->getTotalFilamentGrams();
    String jobState = strategy->getJobState();

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

        // Update stored filament if API still reports it (may become 0 once finished)
        if (totalFilamentG > 0) {
            currentJobTotalFilamentG = totalFilamentG;
        }

        if (jobState == "FINISHED") {
            handleJobFinished(jobId, currentJobTotalFilamentG);
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
