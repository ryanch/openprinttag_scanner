#include "PrusaLinkAPIStrategy.h"
#include "BgcodeParser.h"
#include "ConfigurationManager.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "esp_heap_caps.h"
#include <cstring>
#include <cstdlib>

static constexpr size_t URL_BUFFER_SIZE = 192;
static constexpr size_t STATUS_JSON_CAPACITY = 1024;
static constexpr size_t JOB_JSON_CAPACITY = 4096;

#ifndef PRUSALINK_HEAP_TRACE
#define PRUSALINK_HEAP_TRACE 0
#endif

static bool buildUrl(char* out, size_t outSize, const char* base, const char* path) {
    if (out == nullptr || outSize == 0 || base == nullptr || path == nullptr) {
        return false;
    }
    int written = snprintf(out, outSize, "%s%s", base, path);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

static bool buildJobUrl(char* out, size_t outSize, const char* base, int jobId) {
    if (out == nullptr || outSize == 0 || base == nullptr) {
        return false;
    }
    int written = snprintf(out, outSize, "%s/api/v1/job/%d", base, jobId);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

static void logHeapSnapshot(const char* stage) {
#if PRUSALINK_HEAP_TRACE
    Serial.printf("PrusaLinkAPIStrategy: Heap %s free=%u largest=%u\n",
             stage,
             static_cast<unsigned>(ESP.getFreeHeap()),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
#else
    (void)stage;
#endif
}

void PrusaLinkAPIStrategy::update() {
    auto& config = ConfigurationManager::getInstance();

    // Reset state
    connected = false;
    hasJob = false;
    jobId = -1;
    progress = 0.0f;
    totalFilamentG = 0.0f;
    jobState[0] = '\0';

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
        http.useHTTP10(true);
        http.setReuse(false);

        // Get quick status
        char statusUrl[URL_BUFFER_SIZE];
        if (!buildUrl(statusUrl, sizeof(statusUrl), config.getPrusaLinkURL(), "/api/v1/status")) {
            Serial.println("PrusaLinkAPIStrategy: status URL too long");
            break;
        }
        http.begin(statusUrl);
        http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());

        logHeapSnapshot("before_status_get");
        int statusCode = http.GET();
        if (statusCode != 200) {
            Serial.printf("PrusaLinkAPIStrategy: Status request failed: %d\n", statusCode);
            logHeapSnapshot("status_get_failed");
            http.end();
            break;
        }
        logHeapSnapshot("after_status_get");

        connected = true;
        JsonDocument statusDoc;
        logHeapSnapshot("before_status_deserialize");
        DeserializationError statusErr = deserializeJson(statusDoc, http.getStream());
        logHeapSnapshot("after_status_deserialize");
        http.end();
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
        char jobUrl[URL_BUFFER_SIZE];
        if (!buildUrl(jobUrl, sizeof(jobUrl), config.getPrusaLinkURL(), "/api/v1/job")) {
            Serial.println("PrusaLinkAPIStrategy: job URL too long");
            break;
        }
        http.begin(jobUrl);
        http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());

        logHeapSnapshot("before_job_get");
        int jobStatusCode = http.GET();
        if (jobStatusCode != 200) {
            Serial.printf("PrusaLinkAPIStrategy: Job request failed: %d\n", jobStatusCode);
            logHeapSnapshot("job_get_failed");
            http.end();
            break;
        }
        logHeapSnapshot("after_job_get");

        JsonDocument jobDoc;
        logHeapSnapshot("before_job_deserialize");
        DeserializationError jobErr = deserializeJson(jobDoc, http.getStream());
        logHeapSnapshot("after_job_deserialize");
        http.end();
        if (jobErr) {
            Serial.printf("PrusaLinkAPIStrategy: Job JSON parse error: %s\n", jobErr.c_str());
            break;
        }

        const char* state = jobDoc["state"] | "";
        strncpy(jobState, state, sizeof(jobState) - 1);
        jobState[sizeof(jobState) - 1] = '\0';

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
            const char* downloadRef = downloadRefVar.as<const char*>();
            if (downloadRef != nullptr) {
                strncpy(savedDownloadRef, downloadRef, sizeof(savedDownloadRef) - 1);
                savedDownloadRef[sizeof(savedDownloadRef) - 1] = '\0';
                savedDownloadRefJobId = jobId;
            }
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

float PrusaLinkAPIStrategy::fetchFilamentFromBgcode(const char* downloadRef) {
    auto& config = ConfigurationManager::getInstance();
    if (downloadRef == nullptr || downloadRef[0] == '\0') return 0.0f;

    // Parse host/port from PrusaLink URL to avoid HTTPClient URL-encoding issues
    const char* baseUrl = config.getPrusaLinkURL();
    const char* scheme = strstr(baseUrl, "://");
    if (scheme == nullptr) return 0.0f;
    const char* hostPort = scheme + 3;
    char host[96] = {0};
    int port = 80;
    const char* colon = strchr(hostPort, ':');
    if (colon != nullptr) {
        size_t hostLen = static_cast<size_t>(colon - hostPort);
        if (hostLen >= sizeof(host)) hostLen = sizeof(host) - 1;
        memcpy(host, hostPort, hostLen);
        host[hostLen] = '\0';
        port = atoi(colon + 1);
    } else {
        strncpy(host, hostPort, sizeof(host) - 1);
    }

    WiFiClient client;
    HTTPClient http;
    http.setReuse(false);
    http.begin(client, host, port, downloadRef);
    http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());
    http.addHeader("Range", "bytes=0-8191");

    const int maxAttempts = 6;
    const int baseDelayMs = 1000;
    const size_t BUF_SIZE = BGCODE_BUF_SIZE;
    uint8_t* buf = bgcodeBuf_;

    float result = 0.0f;

    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        if (attempt > 1) {
            int delayMs = baseDelayMs * (attempt - 1);
            Serial.printf("PrusaLinkAPIStrategy: bgcode fetch attempt %d/%d after %dms delay\n", attempt, maxAttempts, delayMs);
            vTaskDelay(pdMS_TO_TICKS(delayMs));
            http.end();
            client.stop();
            client = WiFiClient();
            http.begin(client, host, port, downloadRef);
            http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());
            http.addHeader("Range", "bytes=0-8191");
        }
        logHeapSnapshot("before_bgcode_get");
        int code = http.GET();
        if (code != 200 && code != 206) {
            Serial.printf("PrusaLinkAPIStrategy: bgcode fetch attempt %d/%d failed: %d\n", attempt, maxAttempts, code);
            logHeapSnapshot("bgcode_get_failed");
            continue;
        }

        if (code == 200) {
            Serial.println("PrusaLinkAPIStrategy: Server ignored Range header, reading first 8KB only");
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

        // Log download details for diagnostics
        Serial.printf("PrusaLinkAPIStrategy: bgcode attempt %d/%d: HTTP %d, %zu bytes read, magic=0x%02X%02X%02X%02X\n",
                 attempt, maxAttempts, code, bytesRead,
                 bytesRead > 0 ? buf[0] : 0, bytesRead > 1 ? buf[1] : 0,
                 bytesRead > 2 ? buf[2] : 0, bytesRead > 3 ? buf[3] : 0);

        result = parseBgcodeFilament(buf, bytesRead);
        if (result > 0.0f) {
            Serial.printf("PrusaLinkAPIStrategy: Parsed filament from bgcode header: %.2fg\n", result);
            break;
        }

        Serial.printf("PrusaLinkAPIStrategy: bgcode attempt %d/%d: download OK but parse failed (%zu bytes)\n",
                 attempt, maxAttempts, bytesRead);
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
    http.useHTTP10(true);
    http.setReuse(false);
    char jobUrl[URL_BUFFER_SIZE];
    if (!buildJobUrl(jobUrl, sizeof(jobUrl), config.getPrusaLinkURL(), savedDownloadRefJobId)) {
        if (mutexHeld) xSemaphoreGive(httpMutex_);
        Serial.println("PrusaLinkAPIStrategy: deferred job URL too long");
        return 0.0f;
    }
    const int maxAttempts = 6;
    int code = -1;
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        if (attempt > 1) {
            int delayMs = (attempt - 1) * 1000;
            Serial.printf("PrusaLinkAPIStrategy: Deferred job API retry %d/%d after %dms delay\n", attempt, maxAttempts, delayMs);
            delay(delayMs);
        }
        http.begin(jobUrl);
        http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());
        logHeapSnapshot("before_deferred_job_get");
        code = http.GET();
        if (code == 200) {
            JsonDocument doc;
            logHeapSnapshot("before_deferred_job_deserialize");
            if (!deserializeJson(doc, http.getStream())) {
                logHeapSnapshot("after_deferred_job_deserialize");
                JsonVariant filamentUsed = doc["file"]["meta"]["filament used [g]"];
                if (!filamentUsed.isNull()) {
                    result = filamentUsed.as<float>();
                    Serial.printf("PrusaLinkAPIStrategy: Got deferred filament from job API: %.2fg\n", result);
                }
            } else {
                logHeapSnapshot("deferred_job_deserialize_failed");
            }
            http.end();
            break;
        } else {
            Serial.printf("PrusaLinkAPIStrategy: Deferred job API attempt %d/%d failed: %d\n", attempt, maxAttempts, code);
            logHeapSnapshot("deferred_job_get_failed");
            http.end();
            if (code == 405) {
                Serial.println("PrusaLinkAPIStrategy: 405 Method Not Allowed - skipping further job API retries");
                break;
            }
        }
    }

    // Fall back to bgcode header parsing
    if (result <= 0.0f && savedDownloadRef[0] != '\0') {
        result = fetchFilamentFromBgcode(savedDownloadRef);
    }

    bgcodeFilamentJobId = savedDownloadRefJobId;
    bgcodeFilamentG = result;

    if (mutexHeld) xSemaphoreGive(httpMutex_);
    return bgcodeFilamentG;
}
