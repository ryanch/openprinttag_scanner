#include "DebugLogBuffer.h"
#include "PrusaLinkAPIStrategy.h"
#include "BgcodeParser.h"
#include "ConfigurationManager.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "esp_heap_caps.h"

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
    DBG_LOGF("PrusaLinkAPIStrategy: Heap %s free=%u largest=%u\n",
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
    jobState = "";

    // Acquire HTTP mutex if available
    bool mutexHeld = false;
    if (httpMutex_ != nullptr) {
        if (xSemaphoreTake(httpMutex_, pdMS_TO_TICKS(10000)) != pdTRUE) {
            DBG_LOGLN("PrusaLinkAPIStrategy: Could not acquire HTTP mutex");
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
            DBG_LOGLN("PrusaLinkAPIStrategy: status URL too long");
            break;
        }
        http.begin(statusUrl);
        http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());

        logHeapSnapshot("before_status_get");
        int statusCode = http.GET();
        if (statusCode != 200) {
            DBG_LOGF("PrusaLinkAPIStrategy: Status request failed: %d\n", statusCode);
            logHeapSnapshot("status_get_failed");
            http.end();
            break;
        }
        logHeapSnapshot("after_status_get");

        connected = true;
        StaticJsonDocument<STATUS_JSON_CAPACITY> statusDoc;
        logHeapSnapshot("before_status_deserialize");
        DeserializationError statusErr = deserializeJson(statusDoc, http.getStream());
        logHeapSnapshot("after_status_deserialize");
        http.end();
        if (statusErr) {
            DBG_LOGF("PrusaLinkAPIStrategy: Status JSON parse error: %s\n", statusErr.c_str());
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
            DBG_LOGLN("PrusaLinkAPIStrategy: job URL too long");
            break;
        }
        http.begin(jobUrl);
        http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());

        logHeapSnapshot("before_job_get");
        int jobStatusCode = http.GET();
        if (jobStatusCode != 200) {
            DBG_LOGF("PrusaLinkAPIStrategy: Job request failed: %d\n", jobStatusCode);
            logHeapSnapshot("job_get_failed");
            http.end();
            break;
        }
        logHeapSnapshot("after_job_get");

        StaticJsonDocument<JOB_JSON_CAPACITY> jobDoc;
        logHeapSnapshot("before_job_deserialize");
        DeserializationError jobErr = deserializeJson(jobDoc, http.getStream());
        logHeapSnapshot("after_job_deserialize");
        http.end();
        if (jobErr) {
            DBG_LOGF("PrusaLinkAPIStrategy: Job JSON parse error: %s\n", jobErr.c_str());
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
            DBG_LOGF("PrusaLinkAPIStrategy: bgcode fetch attempt %d/%d after %dms delay\n", attempt, maxAttempts, delayMs);
            vTaskDelay(pdMS_TO_TICKS(delayMs));
            http.end();
            http.begin(client, host, port, downloadRef);
            http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());
            http.addHeader("Range", "bytes=0-8191");
        }
        logHeapSnapshot("before_bgcode_get");
        code = http.GET();
        if (code == 200 || code == 206) break;
        DBG_LOGF("PrusaLinkAPIStrategy: bgcode fetch attempt %d/%d failed: %d\n", attempt, maxAttempts, code);
        logHeapSnapshot("bgcode_get_failed");
    }

    if (code != 200 && code != 206) {
        DBG_LOGF("PrusaLinkAPIStrategy: bgcode header fetch failed after %d attempts (path: %s)\n", maxAttempts, downloadRef.c_str());
        http.end();
        return 0.0f;
    }

    if (code == 200) {
        DBG_LOGLN("PrusaLinkAPIStrategy: Server ignored Range header, reading first 8KB only");
    }

    const size_t BUF_SIZE = 8192;
    uint8_t* buf = (uint8_t*)malloc(BUF_SIZE);
    if (!buf) {
        logHeapSnapshot("bgcode_buf_alloc_failed");
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
        DBG_LOGF("PrusaLinkAPIStrategy: Parsed filament from bgcode header: %.2fg\n", result);
    } else {
        DBG_LOGF("PrusaLinkAPIStrategy: Could not parse filament from bgcode header (%zu bytes read)\n", bytesRead);
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
        DBG_LOGLN("PrusaLinkAPIStrategy: deferred job URL too long");
        return 0.0f;
    }
    http.begin(jobUrl);
    http.addHeader("X-Api-Key", config.getPrusaLinkAPIKey());
    logHeapSnapshot("before_deferred_job_get");
    int code = http.GET();
    if (code == 200) {
        StaticJsonDocument<JOB_JSON_CAPACITY> doc;
        logHeapSnapshot("before_deferred_job_deserialize");
        if (!deserializeJson(doc, http.getStream())) {
            logHeapSnapshot("after_deferred_job_deserialize");
            JsonVariant filamentUsed = doc["file"]["meta"]["filament used [g]"];
            if (!filamentUsed.isNull()) {
                result = filamentUsed.as<float>();
                DBG_LOGF("PrusaLinkAPIStrategy: Got deferred filament from job API: %.2fg\n", result);
            }
        } else {
            logHeapSnapshot("deferred_job_deserialize_failed");
        }
        http.end();
    } else {
        DBG_LOGF("PrusaLinkAPIStrategy: Deferred job API request failed: %d\n", code);
        logHeapSnapshot("deferred_job_get_failed");
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
