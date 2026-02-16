#include "PrusaLinkAPIStrategy.h"
#include "BgcodeParser.h"
#include "ConfigurationManager.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

void PrusaLinkAPIStrategy::update() {
    auto& config = ConfigurationManager::getInstance();

    // Reset state
    connected = false;
    hasJob = false;
    jobId = -1;
    progress = 0.0f;
    totalFilamentG = 0.0f;
    jobState = "";

    // Acquire HTTP mutex if available
    bool mutexHeld = false;
    if (httpMutex_ != nullptr) {
        if (xSemaphoreTake(httpMutex_, pdMS_TO_TICKS(10000)) != pdTRUE) {
            Serial.println("PrusaLinkAPIStrategy: Could not acquire HTTP mutex");
            return;
        }
        mutexHeld = true;
    }

    // RAII-style cleanup: all paths must go through 'done' label or fall through
    do {
        HTTPClient http;

        // Get quick status
        String statusUrl = String(config.getPrusaLinkURL()) + "/api/v1/status";
        http.begin(statusUrl);
        http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());

        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("PrusaLinkAPIStrategy: Status request failed: %d\n", statusCode);
            http.end();
            break;
        }

        connected = true;
        String statusPayload = http.getString();
        http.end();

        JsonDocument statusDoc;
        DeserializationError statusErr = deserializeJson(statusDoc, statusPayload);
        if (statusErr) {
            Serial.printf("PrusaLinkAPIStrategy: Status JSON parse error: %s\n", statusErr.c_str());
            break;
        }

        // Check if there's a job
        JsonVariant jobVariant = statusDoc["job"];
        hasJob = !jobVariant.isNull() && jobVariant["id"].is<int>();

        if (!hasJob) {
            break;
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
            break;
        }

        String jobPayload = http.getString();
        http.end();

        JsonDocument jobDoc;
        DeserializationError jobErr = deserializeJson(jobDoc, jobPayload);
        if (jobErr) {
            Serial.printf("PrusaLinkAPIStrategy: Job JSON parse error: %s\n", jobErr.c_str());
            break;
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

        // Save download ref for deferred fetch after print completes
        JsonVariant downloadRefVar = jobDoc["file"]["refs"]["download"];
        if (!downloadRefVar.isNull()) {
            savedDownloadRef = downloadRefVar.as<String>();
            savedDownloadRefJobId = jobId;
        }

        // Use cached bgcode data if available (from a previous deferred fetch)
        if (totalFilamentG <= 0.0f) {
            if (jobId == bgcodeFilamentJobId && bgcodeFilamentG > 0.0f) {
                totalFilamentG = bgcodeFilamentG;
            }
        }
    } while (false);

    if (mutexHeld) {
        xSemaphoreGive(httpMutex_);
    }
}

float PrusaLinkAPIStrategy::fetchFilamentFromBgcode(const String& downloadRef) {
    auto& config = ConfigurationManager::getInstance();

    // Parse host/port from PrusaLink URL to avoid HTTPClient URL-encoding issues
    String baseUrl = String(config.getPrusaLinkURL());
    int hostStart = baseUrl.indexOf("://");
    if (hostStart < 0) return 0.0f;
    hostStart += 3;
    String hostPort = baseUrl.substring(hostStart);
    String host = hostPort;
    int port = 80;
    int colonIdx = hostPort.indexOf(':');
    if (colonIdx > 0) {
        host = hostPort.substring(0, colonIdx);
        port = hostPort.substring(colonIdx + 1).toInt();
    }

    WiFiClient client;
    HTTPClient http;
    http.setReuse(false);
    http.begin(client, host, port, downloadRef);
    http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());
    http.addHeader("Range", "bytes=0-8191");

    const int maxAttempts = 3;
    const int baseDelayMs = 1000;
    int code = 0;

    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        if (attempt > 1) {
            int delayMs = baseDelayMs * (attempt - 1);
            Serial.printf("PrusaLinkAPIStrategy: bgcode fetch attempt %d/%d after %dms delay\n", attempt, maxAttempts, delayMs);
            vTaskDelay(pdMS_TO_TICKS(delayMs));
            http.end();
            http.begin(client, host, port, downloadRef);
            http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());
            http.addHeader("Range", "bytes=0-8191");
        }
        code = http.GET();
        if (code == 200 || code == 206) break;
        Serial.printf("PrusaLinkAPIStrategy: bgcode fetch attempt %d/%d failed: %d\n", attempt, maxAttempts, code);
    }

    if (code != 200 && code != 206) {
        Serial.printf("PrusaLinkAPIStrategy: bgcode header fetch failed after %d attempts (path: %s)\n", maxAttempts, downloadRef.c_str());
        http.end();
        return 0.0f;
    }

    if (code == 200) {
        Serial.println("PrusaLinkAPIStrategy: Server ignored Range header, reading first 8KB only");
    }

    const size_t BUF_SIZE = 8192;
    uint8_t* buf = (uint8_t*)malloc(BUF_SIZE);
    if (!buf) {
        http.end();
        return 0.0f;
    }

    // Read at most BUF_SIZE bytes, then force-close the TCP connection.
    // If Range was ignored the full file may be streaming — stop() drops it
    // without trying to consume remaining data (unlike http.end() which may).
    size_t bytesRead = 0;
    unsigned long timeout = millis() + 5000;

    while (bytesRead < BUF_SIZE && millis() < timeout) {
        if (client.available()) {
            int toRead = client.available();
            if (toRead > (int)(BUF_SIZE - bytesRead)) toRead = BUF_SIZE - bytesRead;
            int n = client.readBytes(buf + bytesRead, toRead);
            if (n > 0) bytesRead += n;
        } else if (!client.connected()) {
            break;
        } else {
            delay(10);
        }
    }

    client.stop();
    http.end();

    float result = parseBgcodeFilament(buf, bytesRead);
    free(buf);

    if (result > 0.0f) {
        Serial.printf("PrusaLinkAPIStrategy: Parsed filament from bgcode header: %.2fg\n", result);
    } else {
        Serial.printf("PrusaLinkAPIStrategy: Could not parse filament from bgcode header (%zu bytes read)\n", bytesRead);
    }

    return result;
}

float PrusaLinkAPIStrategy::fetchDeferredFilament() {
    if (savedDownloadRefJobId < 0) return 0.0f;
    if (bgcodeFilamentJobId == savedDownloadRefJobId && bgcodeFilamentG > 0.0f)
        return bgcodeFilamentG;

    bool mutexHeld = false;
    if (httpMutex_ != nullptr) {
        if (xSemaphoreTake(httpMutex_, pdMS_TO_TICKS(10000)) != pdTRUE) return 0.0f;
        mutexHeld = true;
    }

    auto& config = ConfigurationManager::getInstance();
    float result = 0.0f;

    // Try job API first — may have metadata now that print is done
    HTTPClient http;
    String jobUrl = String(config.getPrusaLinkURL()) + "/api/v1/job/" + String(savedDownloadRefJobId);
    http.begin(jobUrl);
    http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());
    int code = http.GET();
    if (code == 200) {
        String payload = http.getString();
        http.end();
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            JsonVariant filamentUsed = doc["file"]["meta"]["filament used [g]"];
            if (!filamentUsed.isNull()) {
                result = filamentUsed.as<float>();
                Serial.printf("PrusaLinkAPIStrategy: Got deferred filament from job API: %.2fg\n", result);
            }
        }
    } else {
        Serial.printf("PrusaLinkAPIStrategy: Deferred job API request failed: %d\n", code);
        http.end();
    }

    // Fall back to bgcode header parsing
    if (result <= 0.0f && !savedDownloadRef.isEmpty()) {
        result = fetchFilamentFromBgcode(savedDownloadRef);
    }

    bgcodeFilamentJobId = savedDownloadRefJobId;
    bgcodeFilamentG = result;

    if (mutexHeld) xSemaphoreGive(httpMutex_);
    return bgcodeFilamentG;
}
