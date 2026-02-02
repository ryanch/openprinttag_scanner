#include "PrusaLinkAPIStrategy.h"
#include "ConfigurationManager.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

void PrusaLinkAPIStrategy::update() {
    auto& config = ConfigurationManager::getInstance();
    HTTPClient http;

    // Reset state
    connected = false;
    hasJob = false;
    jobId = -1;
    progress = 0.0f;
    totalFilamentG = 0.0f;
    jobState = "";

    // Get quick status
    String statusUrl = String(config.getPrusaLinkURL()) + "/api/v1/status";
    http.begin(statusUrl);
    http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());

    int statusCode = http.GET();
    if (statusCode != 200) {
        Serial.printf("PrusaLinkAPIStrategy: Status request failed: %d\n", statusCode);
        http.end();
        return;
    }

    connected = true;
    String statusPayload = http.getString();
    http.end();

    JsonDocument statusDoc;
    DeserializationError statusErr = deserializeJson(statusDoc, statusPayload);
    if (statusErr) {
        Serial.printf("PrusaLinkAPIStrategy: Status JSON parse error: %s\n", statusErr.c_str());
        return;
    }

    // Check if there's a job
    JsonVariant jobVariant = statusDoc["job"];
    hasJob = !jobVariant.isNull() && jobVariant["id"].is<int>();

    if (!hasJob) {
        return;
    }

    jobId = jobVariant["id"].as<int>();
    progress = jobVariant["progress"].as<float>();

    // Get detailed job info
    String jobUrl = String(config.getPrusaLinkURL()) + "/api/v1/job";
    http.begin(jobUrl);
    http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());

    int jobStatusCode = http.GET();
    if (jobStatusCode != 200) {
        Serial.printf("PrusaLinkAPIStrategy: Job request failed: %d\n", jobStatusCode);
        http.end();
        return;
    }

    String jobPayload = http.getString();
    http.end();

    JsonDocument jobDoc;
    DeserializationError jobErr = deserializeJson(jobDoc, jobPayload);
    if (jobErr) {
        Serial.printf("PrusaLinkAPIStrategy: Job JSON parse error: %s\n", jobErr.c_str());
        return;
    }

    jobState = jobDoc["state"].as<String>();

    // Extract filament usage from file metadata
    JsonVariant fileMeta = jobDoc["file"]["meta"];
    if (!fileMeta.isNull()) {
        JsonVariant filamentUsed = fileMeta["filament used [g]"];
        if (!filamentUsed.isNull()) {
            totalFilamentG = filamentUsed.as<float>();
        }
    }
}
