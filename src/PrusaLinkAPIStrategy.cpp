#include "PrusaLinkAPIStrategy.h"
#include "BgcodeParser.h"
#include "ConfigurationManager.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <json.hpp>

#include "esp_heap_caps.h"
#include <cstring>
#include <cstdlib>
#include <cctype>

static constexpr size_t URL_BUFFER_SIZE = 192;

//#define PRUSALINK_HEAP_TRACE 1
#ifndef PRUSALINK_HEAP_TRACE
#define PRUSALINK_HEAP_TRACE 0
#endif

//#define PRUSALINK_API_TRACE 1
#ifndef PRUSALINK_API_TRACE
#define PRUSALINK_API_TRACE 0
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

static float parseFilamentFromRawJobJson(const String& response) {
    const char* key = "\"filament used [g]\"";
    int keyPos = response.indexOf(key);
    if (keyPos < 0) {
        return 0.0f;
    }

    const char* raw = response.c_str();
    const char* p = raw + keyPos + strlen(key);

    while (*p != '\0' && *p != ':') {
        p++;
    }
    if (*p != ':') {
        return 0.0f;
    }
    p++;

    while (*p != '\0' && isspace(static_cast<unsigned char>(*p))) {
        p++;
    }

    char* end = nullptr;
    float val = strtof(p, &end);
    if (end == p || val <= 0.0f) {
        return 0.0f;
    }
    return val;
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

using namespace io;
using namespace json;

#if PRUSALINK_API_TRACE
static const char* nodeTypeName(json_node_type node) {
    switch (node) {
        case json_node_type::field: return "field";
        case json_node_type::object: return "object";
        case json_node_type::end_object: return "end_object";
        case json_node_type::array: return "array";
        case json_node_type::end_array: return "end_array";
        case json_node_type::value: return "value";
        case json_node_type::value_part: return "value_part";
        case json_node_type::end_value_part: return "end_value_part";
        default: return "unknown";
    }
}

static const char* valueTypeName(json_value_type type) {
    switch (type) {
        case json_value_type::none: return "none";
        case json_value_type::null: return "null";
        case json_value_type::integer: return "integer";
        case json_value_type::real: return "real";
        case json_value_type::boolean: return "boolean";
        default: return "unknown";
    }
}

static void logReaderNode(const char* tag, json_reader& reader) {
    const char* text = reader.value();
    if (text == nullptr) {
        text = "";
    }
    Serial.printf("PrusaLinkAPIStrategy[%s]: depth=%u node=%s value_type=%s value='%s'\n",
                  tag,
                  reader.depth(),
                  nodeTypeName(reader.node_type()),
                  valueTypeName(reader.value_type()),
                  text);
}
#endif

static bool readStringValue(json_reader& reader, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return false;
    }
    out[0] = '\0';
    json_node_type node = reader.node_type();
    if (node != json_node_type::value &&
        node != json_node_type::value_part &&
        node != json_node_type::end_value_part) {
        return false;
    }

    size_t written = 0;
    auto appendValue = [&]() {
        const char* part = reader.value();
        if (part == nullptr) {
            return;
        }
        while (*part != '\0' && written + 1 < outSize) {
            out[written++] = *part++;
        }
        out[written] = '\0';
    };

    appendValue();
    if (node == json_node_type::value_part) {
        while (reader.read()) {
            json_node_type next = reader.node_type();
            if (next != json_node_type::value_part &&
                next != json_node_type::end_value_part) {
                return written > 0;
            }
            appendValue();
            if (next == json_node_type::end_value_part) {
                break;
            }
        }
    }
    return written > 0;
}

static void parseStatusPayload(stream& stm, bool& outHasJob, int& outJobId, float& outProgress) {
    outHasJob = false;
    outJobId = -1;
    outProgress = 0.0f;

    json_reader reader(stm);
    bool inJobObject = false;
    unsigned jobDepth = 0;

    while (reader.read()) {
        json_node_type node = reader.node_type();
#if PRUSALINK_API_TRACE
        logReaderNode("status", reader);
#endif
        if (node == json_node_type::field) {
            const char* field = reader.value();
            if (!inJobObject && strcmp(field, "job") == 0) {
                if (reader.read() && reader.node_type() == json_node_type::object) {
                    inJobObject = true;
                    jobDepth = reader.depth();
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[status]: entered job object depth=%u\n", jobDepth);
#endif
                }
                continue;
            }
            if (inJobObject && strcmp(field, "id") == 0) {
                if (reader.read() &&
                    reader.node_type() == json_node_type::value &&
                    (reader.value_type() == json_value_type::integer ||
                     reader.value_type() == json_value_type::real)) {
                    outJobId = static_cast<int>(reader.value_int());
                    outHasJob = true;
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[status]: parsed job.id=%d\n", outJobId);
#endif
                }
                continue;
            }
            if (inJobObject && strcmp(field, "progress") == 0) {
                if (reader.read() &&
                    reader.node_type() == json_node_type::value &&
                    (reader.value_type() == json_value_type::real ||
                     reader.value_type() == json_value_type::integer)) {
                    outProgress = static_cast<float>(reader.value_real());
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[status]: parsed job.progress=%.3f\n", outProgress);
#endif
                }
                continue;
            }
        } else if (node == json_node_type::end_object && inJobObject && reader.depth() == jobDepth) {
            inJobObject = false;
#if PRUSALINK_API_TRACE
            Serial.println("PrusaLinkAPIStrategy[status]: leaving job object");
#endif
        }
    }
}

static void parseJobPayload(stream& stm, char* outState, size_t outStateSize, float& outFilamentG,
                            char* outDownloadRef, size_t outDownloadRefSize) {
    if (outState != nullptr && outStateSize > 0) {
        outState[0] = '\0';
    }
    if (outDownloadRef != nullptr && outDownloadRefSize > 0) {
        outDownloadRef[0] = '\0';
    }
    outFilamentG = 0.0f;

    json_reader reader(stm);
    int fileDepth = -1;
    int metaDepth = -1;
    int refsDepth = -1;

    while (reader.read()) {
        json_node_type node = reader.node_type();
#if PRUSALINK_API_TRACE
        logReaderNode("job", reader);
#endif
        if (node == json_node_type::field) {
            const char* field = reader.value();

            const bool atTopLevel = (fileDepth < 0 && metaDepth < 0 && refsDepth < 0);
            if (atTopLevel && strcmp(field, "state") == 0) {
                if (outState != nullptr && outStateSize > 0 && reader.read()) {
                    readStringValue(reader, outState, outStateSize);
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[job]: parsed state='%s'\n", outState);
#endif
                }
                continue;
            }

            if (atTopLevel && strcmp(field, "file") == 0) {
                if (reader.read() && reader.node_type() == json_node_type::object) {
                    fileDepth = static_cast<int>(reader.depth());
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[job]: entered file object depth=%d\n", fileDepth);
#endif
                }
                continue;
            }

            if (fileDepth >= 0 && strcmp(field, "meta") == 0) {
#if PRUSALINK_API_TRACE
                Serial.printf("PrusaLinkAPIStrategy[job]: Found 'meta' field at fileDepth=%d\n", fileDepth);
#endif
                if (reader.read() && reader.node_type() == json_node_type::object) {
                    metaDepth = static_cast<int>(reader.depth());
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[job]: entered meta object depth=%d\n", metaDepth);
#endif
                } else {
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[job]: ERROR - 'meta' is not an object! node_type=%s\n",
                                  nodeTypeName(reader.node_type()));
#endif
                }
                continue;
            }

            if (fileDepth >= 0 && strcmp(field, "refs") == 0) {
                if (reader.read() && reader.node_type() == json_node_type::object) {
                    refsDepth = static_cast<int>(reader.depth());
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[job]: entered refs object depth=%d\n", refsDepth);
#endif
                }
                continue;
            }

            if (metaDepth >= 0 && strcmp(field, "filament used [g]") == 0) {
#if PRUSALINK_API_TRACE
                Serial.printf("PrusaLinkAPIStrategy[job]: Found 'filament used [g]' field at metaDepth=%d\n", metaDepth);
#endif
                if (reader.read() &&
                    reader.node_type() == json_node_type::value &&
                    (reader.value_type() == json_value_type::real ||
                     reader.value_type() == json_value_type::integer)) {
                    outFilamentG = static_cast<float>(reader.value_real());
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[job]: parsed filament used [g]=%.3f\n", outFilamentG);
#endif
                } else {
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[job]: ERROR - failed to read filament value! node_type=%s value_type=%s\n",
                                  nodeTypeName(reader.node_type()), valueTypeName(reader.value_type()));
#endif
                }
                continue;
            }

            if (refsDepth >= 0 && strcmp(field, "download") == 0) {
                if (outDownloadRef != nullptr && outDownloadRefSize > 0 && reader.read()) {
                    readStringValue(reader, outDownloadRef, outDownloadRefSize);
#if PRUSALINK_API_TRACE
                    Serial.printf("PrusaLinkAPIStrategy[job]: parsed refs.download='%s'\n", outDownloadRef);
#endif
                }
                continue;
            }
        } else if (node == json_node_type::end_object) {
            const int depth = static_cast<int>(reader.depth());
            if (refsDepth >= 0 && depth == refsDepth) {
                refsDepth = -1;
#if PRUSALINK_API_TRACE
                Serial.println("PrusaLinkAPIStrategy[job]: leaving refs object");
#endif
            } else if (metaDepth >= 0 && depth == metaDepth) {
                metaDepth = -1;
#if PRUSALINK_API_TRACE
                Serial.println("PrusaLinkAPIStrategy[job]: leaving meta object");
#endif
            } else if (fileDepth >= 0 && depth == fileDepth) {
                fileDepth = -1;
#if PRUSALINK_API_TRACE
                Serial.println("PrusaLinkAPIStrategy[job]: leaving file object");
#endif
            }
        }
    }
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
#if PRUSALINK_API_TRACE
        Serial.printf("PrusaLinkAPIStrategy: GET %s\n", statusUrl);
#endif
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
#if PRUSALINK_API_TRACE
        Serial.printf("PrusaLinkAPIStrategy: Status request OK: %d\n", statusCode);
#endif
        logHeapSnapshot("after_status_get");

        connected = true;
        logHeapSnapshot("before_status_deserialize");
        arduino_stream statusStream(&http.getStream());
        parseStatusPayload(statusStream, hasJob, jobId, progress);
#if PRUSALINK_API_TRACE
        Serial.printf("PrusaLinkAPIStrategy: status parse result hasJob=%d jobId=%d progress=%.3f\n",
                      hasJob ? 1 : 0, jobId, progress);
#endif
        logHeapSnapshot("after_status_deserialize");
        http.end();

        if (!hasJob) {
            break;
        }

        // Get detailed job info
        char jobUrl[URL_BUFFER_SIZE];
        if (!buildUrl(jobUrl, sizeof(jobUrl), config.getPrusaLinkURL(), "/api/v1/job")) {
            Serial.println("PrusaLinkAPIStrategy: job URL too long");
            break;
        }
#if PRUSALINK_API_TRACE
        Serial.printf("PrusaLinkAPIStrategy: GET %s\n", jobUrl);
#endif
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
#if PRUSALINK_API_TRACE
        Serial.printf("PrusaLinkAPIStrategy: Job request OK: %d\n", jobStatusCode);
#endif
        logHeapSnapshot("after_job_get");

        logHeapSnapshot("before_job_deserialize");

        // Read the entire response into a String for debugging
        String response = http.getString();
#if PRUSALINK_API_TRACE
        Serial.printf("PrusaLinkAPIStrategy: Raw job response: %s\n", response.c_str());
#endif

        // Create mutable copy for buffer_stream (requires non-const pointer)
        size_t respLen = response.length();
        uint8_t* respBuf = (uint8_t*)malloc(respLen);
        if (respBuf == nullptr) {
            Serial.println("PrusaLinkAPIStrategy: Failed to allocate buffer for job response");
            http.end();
            break;
        }
        memcpy(respBuf, response.c_str(), respLen);
        buffer_stream bufStream(respBuf, respLen);

        char parsedState[sizeof(jobState)] = {0};
        char parsedDownloadRef[sizeof(savedDownloadRef)] = {0};
        float parsedFilamentG = 0.0f;
        parseJobPayload(bufStream, parsedState, sizeof(parsedState), parsedFilamentG,
                        parsedDownloadRef, sizeof(parsedDownloadRef));
#if PRUSALINK_API_TRACE
        Serial.printf("PrusaLinkAPIStrategy: job parse result state='%s' filament=%.3f download='%s'\n",
                      parsedState, parsedFilamentG, parsedDownloadRef);
#endif

        free(respBuf);
        logHeapSnapshot("after_job_deserialize");
        http.end();

        if (parsedFilamentG <= 0.0f) {
            float rawParsed = parseFilamentFromRawJobJson(response);
            if (rawParsed > 0.0f) {
                parsedFilamentG = rawParsed;
                Serial.printf("PrusaLinkAPIStrategy: Parsed filament from raw job JSON fallback: %.2fg\n", parsedFilamentG);
            }
        }

        strncpy(jobState, parsedState, sizeof(jobState) - 1);
        jobState[sizeof(jobState) - 1] = '\0';
        totalFilamentG = parsedFilamentG;

        if (parsedDownloadRef[0] != '\0') {
            strncpy(savedDownloadRef, parsedDownloadRef, sizeof(savedDownloadRef) - 1);
            savedDownloadRef[sizeof(savedDownloadRef) - 1] = '\0';
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

float PrusaLinkAPIStrategy::fetchDeferredFilament(int expectedJobId) {
    if (expectedJobId < 0) return 0.0f;
    if (savedDownloadRefJobId != expectedJobId) {
        Serial.printf("PrusaLinkAPIStrategy: Deferred filament skipped, expected job %d but saved ref is job %d\n",
            expectedJobId, savedDownloadRefJobId);
        return 0.0f;
    }

    if (bgcodeFilamentJobId == expectedJobId && bgcodeFilamentG > 0.0f)
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
    if (!buildJobUrl(jobUrl, sizeof(jobUrl), config.getPrusaLinkURL(), expectedJobId)) {
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
            logHeapSnapshot("before_deferred_job_deserialize");

            // Read the entire response for debugging
            String deferredResponse = http.getString();
#if PRUSALINK_API_TRACE
            Serial.printf("PrusaLinkAPIStrategy: Deferred job response: %s\n", deferredResponse.c_str());
#endif

            // Create mutable copy for buffer_stream
            size_t deferredLen = deferredResponse.length();
            uint8_t* deferredBuf = (uint8_t*)malloc(deferredLen);
            if (deferredBuf != nullptr) {
                memcpy(deferredBuf, deferredResponse.c_str(), deferredLen);
                buffer_stream bufStream(deferredBuf, deferredLen);

                char stateBuf[2] = {0};
                char downloadRefBuf[2] = {0};
                float parsedFilament = 0.0f;
                parseJobPayload(bufStream, stateBuf, sizeof(stateBuf), parsedFilament,
                                downloadRefBuf, sizeof(downloadRefBuf));

                free(deferredBuf);

                if (parsedFilament <= 0.0f) {
                    float rawParsed = parseFilamentFromRawJobJson(deferredResponse);
                    if (rawParsed > 0.0f) {
                        parsedFilament = rawParsed;
                        Serial.printf("PrusaLinkAPIStrategy: Parsed deferred filament from raw JSON fallback: %.2fg\n",
                            parsedFilament);
                    }
                }

                if (parsedFilament > 0.0f) {
                    result = parsedFilament;
                    Serial.printf("PrusaLinkAPIStrategy: Got deferred filament from job API: %.2fg\n", result);
                } else {
                    Serial.printf("PrusaLinkAPIStrategy: Deferred job API attempt %d/%d had no filament metadata yet\n",
                        attempt, maxAttempts);
#if PRUSALINK_API_TRACE
                    Serial.println("PrusaLinkAPIStrategy: WARNING - deferred job parse returned 0g");
#endif
                }
            } else {
                Serial.println("PrusaLinkAPIStrategy: Failed to allocate buffer for deferred response");
            }

            logHeapSnapshot("after_deferred_job_deserialize");
            http.end();
            if (result > 0.0f) {
                break;
            }
            continue;
        } else {
            Serial.printf("PrusaLinkAPIStrategy: Deferred job API attempt %d/%d failed: %d\n", attempt, maxAttempts, code);
            logHeapSnapshot("deferred_job_get_failed");
            http.end();
            if (code == 405 || code == 204) {
                Serial.println("PrusaLinkAPIStrategy: 405 Method Not Allowed - skipping further job API retries");
                break;
            }
        }
    }

    // Fall back to bgcode header parsing
    if (result <= 0.0f && savedDownloadRef[0] != '\0') {
        result = fetchFilamentFromBgcode(savedDownloadRef);
    }

    bgcodeFilamentJobId = expectedJobId;
    bgcodeFilamentG = result;

    if (mutexHeld) xSemaphoreGive(httpMutex_);
    return bgcodeFilamentG;
}
