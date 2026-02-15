#include "PrusaLinkAPIStrategy.h"
#include "BgcodeParser.h"
#include "ConfigurationManager.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
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

    // If API didn't provide filament data, try parsing bgcode file header
    if (totalFilamentG <= 0.0f) {
        if (jobId == bgcodeFilamentJobId && bgcodeFilamentG > 0.0f) {
            totalFilamentG = bgcodeFilamentG;
        } else if (jobId != bgcodeFilamentJobId) {
            bgcodeFilamentJobId = jobId;
            bgcodeFilamentG = 0.0f;
            JsonVariant downloadRef = jobDoc["file"]["refs"]["download"];
            if (!downloadRef.isNull()) {
                bgcodeFilamentG = fetchFilamentFromBgcode(downloadRef.as<String>());
                totalFilamentG = bgcodeFilamentG;
            }
        }
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

    Serial.printf("PrusaLinkAPIStrategy: Fetching bgcode header from %s:%d%s\n",
        host.c_str(), port, downloadRef.c_str());

    int code = http.GET();
    if (code != 200 && code != 206) {
        Serial.printf("PrusaLinkAPIStrategy: bgcode header fetch failed: %d (path: %s)\n", code, downloadRef.c_str());
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
