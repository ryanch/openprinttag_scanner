#include "StubPrinterLinkStrategy.h"
#include <Arduino.h>
#include <cstring>

void StubPrinterLinkStrategy::update() {
    // Initialize start time on first call
    if (!started) {
        startTimeMs = millis();
        started = true;
        Serial.println("StubPrinterLinkStrategy: Started simulation");
    }

    unsigned long elapsedMs = millis() - startTimeMs;

    // Timeline:
    // 0-5 sec: No job (waiting)
    // 5-65 sec: PRINTING state, progress 0% -> 100%
    // 65+ sec: FINISHED state

    if (elapsedMs < JOB_START_DELAY_MS) {
        // No job yet
        hasJob = false;
        jobId = -1;
        progress = 0.0f;
        totalFilamentG = 0.0f;
        jobState[0] = '\0';
    } else {
        unsigned long jobElapsedMs = elapsedMs - JOB_START_DELAY_MS;

        hasJob = true;
        jobId = STUB_JOB_ID;
        totalFilamentG = STUB_TOTAL_FILAMENT_G;

        if (jobElapsedMs < PRINT_DURATION_MS) {
            // Printing: progress from 0% to 100% over 60 seconds
            strncpy(jobState, "PRINTING", sizeof(jobState) - 1);
            jobState[sizeof(jobState) - 1] = '\0';
            progress = (float)jobElapsedMs / (float)PRINT_DURATION_MS * 100.0f;
        } else {
            // Finished
            strncpy(jobState, "FINISHED", sizeof(jobState) - 1);
            jobState[sizeof(jobState) - 1] = '\0';
            progress = 100.0f;
        }
    }
}
