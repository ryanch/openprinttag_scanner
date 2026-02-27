#include "PrinterManager.h"
#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include <Arduino.h>
#include <cstring>

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

bool PrinterManager::isConnected() const {
    return strategy != nullptr && strategy->isConnected();
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
    const char* jobState = strategy->getJobState();

    // State machine logic
    if (state == PrinterState::IDLE) {
        if (strcmp(jobState, "PRINTING") == 0 || strcmp(jobState, "PAUSED") == 0) {
            handleJobDetected(jobId, totalFilamentG);
        }
    } else if (state == PrinterState::TRACKING) {
        // Check if job ID changed (new job while we were tracking)
        if (jobId != currentJobId) {
            // Old job was replaced - treat as canceled
            resolveAndSendJobEnd(currentJobId, lastProgressPercent);
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

        if (strcmp(jobState, "FINISHED") == 0) {
            resolveAndSendJobEnd(jobId, 100.0f);
        } else if (strcmp(jobState, "STOPPED") == 0 || strcmp(jobState, "ERROR") == 0) {
            resolveAndSendJobEnd(jobId, progress);
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

    if (totalFilamentG <= 0) {
        Serial.printf("PrinterManager: WARNING - No filament data from API for job %d\n", jobId);
    }

    Serial.printf("PrinterManager: Now tracking job %d (total filament: %.2fg)\n",
        jobId, totalFilamentG);
}

void PrinterManager::resolveAndSendJobEnd(int jobId, float progressPercent) {
    bool canceled = (progressPercent < 100.0f);

    // Try deferred filament if we don't have total yet
    if (currentJobTotalFilamentG <= 0.0f && strategy) {
        float deferred = strategy->fetchDeferredFilament();
        if (deferred > 0.0f) {
            currentJobTotalFilamentG = deferred;
            Serial.printf("PrinterManager: Got deferred filament: %.2fg\n", deferred);
        }
    }

    float filamentUsed = (progressPercent / 100.0f) * currentJobTotalFilamentG;

    AppMessage msg;
    msg.type = AppMessageType::PRINT_ENDED;
    msg.payload.printEnded.job_id = jobId;
    msg.payload.printEnded.filament_used_grams = filamentUsed;
    msg.payload.printEnded.canceled = canceled;
    ApplicationManager::getInstance().sendMessage(msg);

    Serial.printf("PrinterManager: Job %d %s at %.1f%% (filament: %.2fg)\n",
        jobId, canceled ? "canceled" : "finished", progressPercent, filamentUsed);

    state = PrinterState::IDLE;
    currentJobId = -1;
    currentJobTotalFilamentG = 0.0f;
    lastProgressPercent = 0.0f;
}

void PrinterManager::handleJobDisappeared() {
    float progress = (lastProgressPercent >= 95.0f) ? 100.0f : lastProgressPercent;

    Serial.printf("PrinterManager: Job %d disappeared at %.1f%% - treating as %s\n",
        currentJobId, lastProgressPercent, progress >= 100.0f ? "finished" : "canceled");

    resolveAndSendJobEnd(currentJobId, progress);
}
