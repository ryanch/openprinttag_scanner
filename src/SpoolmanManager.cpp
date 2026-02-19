#include "SpoolmanManager.h"
#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include "openprinttag_lib.h"

// --- File-local HTTP helpers ---

static int httpGet(const char* path, String& response) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    WiFiClient client;
    HTTPClient http;

    String url = String(baseUrl) + path;
    http.begin(client, url);
    int code = http.GET();
    if (code > 0) {
        response = http.getString();
    }
    http.end();
    return code;
}

static int httpPost(const char* path, const char* body, String& response) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    WiFiClient client;
    HTTPClient http;

    String url = String(baseUrl) + path;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    if (code > 0) {
        response = http.getString();
    }
    http.end();
    return code;
}

static int httpPatch(const char* path, const char* body, String& response) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    WiFiClient client;
    HTTPClient http;

    String url = String(baseUrl) + path;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.PATCH(body);
    if (code > 0) {
        response = http.getString();
    }
    http.end();
    return code;
}

// --- File-local Spoolman API helpers ---

static const char* materialTypeToSpoolmanStr(uint8_t type) {
    switch (type) {
        case OPT_MATERIAL_TYPE_PLA:  return "PLA";
        case OPT_MATERIAL_TYPE_PETG: return "PETG";
        case OPT_MATERIAL_TYPE_TPU:  return "TPU";
        case OPT_MATERIAL_TYPE_ABS:  return "ABS";
        case OPT_MATERIAL_TYPE_ASA:  return "ASA";
        case OPT_MATERIAL_TYPE_PC:   return "PC";
        case OPT_MATERIAL_TYPE_PCTG: return "PCTG";
        case OPT_MATERIAL_TYPE_PP:   return "PP";
        case OPT_MATERIAL_TYPE_PA6:  return "PA6";
        case OPT_MATERIAL_TYPE_PA11: return "PA11";
        case OPT_MATERIAL_TYPE_PA12: return "PA12";
        case OPT_MATERIAL_TYPE_PA66: return "PA66";
        case OPT_MATERIAL_TYPE_CPE:  return "CPE";
        case OPT_MATERIAL_TYPE_TPE:  return "TPE";
        case OPT_MATERIAL_TYPE_HIPS: return "HIPS";
        case OPT_MATERIAL_TYPE_PHA:  return "PHA";
        case OPT_MATERIAL_TYPE_PET:  return "PET";
        case OPT_MATERIAL_TYPE_PEI:  return "PEI";
        case OPT_MATERIAL_TYPE_PBT:  return "PBT";
        case OPT_MATERIAL_TYPE_PVB:  return "PVB";
        case OPT_MATERIAL_TYPE_PVA:  return "PVA";
        case OPT_MATERIAL_TYPE_PEKK: return "PEKK";
        case OPT_MATERIAL_TYPE_PEEK: return "PEEK";
        case OPT_MATERIAL_TYPE_BVOH: return "BVOH";
        case OPT_MATERIAL_TYPE_TPC:  return "TPC";
        case OPT_MATERIAL_TYPE_PPS:  return "PPS";
        default: return "PLA";
    }
}

static int findOrCreateVendor(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        name = "Unknown";
    }

    // Search for existing vendor
    String path = String("/api/v1/vendor?name=") + name;
    String response;
    int code = httpGet(path.c_str(), response);

    Serial.printf("SpoolmanManager: get vendor '%s' code=%d\n", name, code);

    if (code == 200 || code == 201) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        if (error == DeserializationError::Ok) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject vendor : arr) {
                const char* vendorName = vendor["name"] | "";
                if (strcasecmp(vendorName, name) == 0) {
                    int id = vendor["id"] | -1;
                    Serial.printf("SpoolmanManager: Found vendor '%s' id=%d\n", name, id);
                    return id;
                }
            }
        } else {
            Serial.printf("SpoolmanManager: Failed to parse vendor JSON: %s\n", error.c_str());
            //Serial.print( response );
        }
    }

    // Create new vendor
    JsonDocument createDoc;
    createDoc["name"] = name;
    String body;
    serializeJson(createDoc, body);

    code = httpPost("/api/v1/vendor", body.c_str(), response);
    if (code == 200) {
        JsonDocument respDoc;
        if (deserializeJson(respDoc, response) == DeserializationError::Ok) {
            int id = respDoc["id"] | -1;
            Serial.printf("SpoolmanManager: Created vendor '%s' id=%d\n", name, id);
            return id;
        }
    }

    Serial.printf("SpoolmanManager: Failed to create vendor '%s', code=%d\n", name, code);
    return -1;
}

static int findOrCreateFilament(int vendorId, const SpoolmanSyncRequest& req) {
    const char* material = materialTypeToSpoolmanStr(req.material_type);

    // Search for existing filament
    char path[256];
    snprintf(path, sizeof(path), "/api/v1/filament?vendor_id=%d&material=%s", vendorId, material);
    String response;
    int code = httpGet(path, response);
    if (code == 200) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        if (error == DeserializationError::Ok) {
            JsonArray arr = doc.as<JsonArray>();
            if (arr.size() > 0) {
                int id = arr[0]["id"] | -1;
                Serial.printf("SpoolmanManager: Found filament material=%s id=%d\n", material, id);
                return id;
            }
        } else {
            Serial.printf("SpoolmanManager: Failed to parse filament JSON: %s\n", error.c_str());
        }
    }

    // Create new filament
    char colorHex[7];
    snprintf(colorHex, sizeof(colorHex), "%02X%02X%02X", req.color[0], req.color[1], req.color[2]);

    JsonDocument createDoc;
    createDoc["name"] = material;
    createDoc["vendor_id"] = vendorId;
    createDoc["material"] = material;
    createDoc["density"] = req.density;
    createDoc["diameter"] = req.diameter;
    createDoc["weight"] = req.initial_weight_g;
    createDoc["color_hex"] = colorHex;

    String body;
    serializeJson(createDoc, body);

    code = httpPost("/api/v1/filament", body.c_str(), response);
    if (code == 200 || code == 201) {
        JsonDocument respDoc;
        if (deserializeJson(respDoc, response) == DeserializationError::Ok) {
            int id = respDoc["id"] | -1;
            Serial.printf("SpoolmanManager: Created filament material=%s id=%d\n", material, id);
            return id;
        }
    }

    Serial.printf("SpoolmanManager: Failed to create filament, code=%d\n", code);
    return -1;
}

static int findSpoolByUuid(int filamentId, const char* uuid) {
    char path[128];
    snprintf(path, sizeof(path), "/api/v1/spool?filament.id=%d", filamentId);
    String response;
    int code = httpGet(path, response);
    if (code != 200) {
        return -1;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error != DeserializationError::Ok) {
        Serial.printf("SpoolmanManager: Failed to parse spool JSON: %s\n", error.c_str());
        return -1;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject spool : arr) {
        JsonObject extra = spool["extra"];
        if (!extra.isNull()) {
            const char* tagUuid = extra["openprinttag_uuid"] | "";
            if (strcmp(tagUuid, uuid) == 0) {
                int id = spool["id"] | -1;
                Serial.printf("SpoolmanManager: Found spool uuid=%s id=%d\n", uuid, id);
                return id;
            }
        }
    }

    return -1;
}

static int createSpool(int filamentId, const SpoolmanSyncRequest& req) {
    char colorHex[7];
    snprintf(colorHex, sizeof(colorHex), "%02X%02X%02X", req.color[0], req.color[1], req.color[2]);

    JsonDocument doc;
    doc["filament_id"] = filamentId;
    doc["remaining_weight"] = req.remaining_weight_g;
    doc["initial_weight"] = req.initial_weight_g;

    // add extra quotes arround value, since spoolman expects extra values to be valid json.
    char uuidEscaped[32];
    snprintf(uuidEscaped, sizeof(uuidEscaped), "\"%s\"", req.spool_id);
    doc["extra"]["openprinttag_uuid"] = uuidEscaped;

    String body;
    serializeJson(doc, body);

    String response;
    int code = httpPost("/api/v1/spool", body.c_str(), response);

    if (code == 200 || code == 201) {
        JsonDocument respDoc;
        if (deserializeJson(respDoc, response) == DeserializationError::Ok) {
            int id = respDoc["id"] | -1;
            Serial.printf("SpoolmanManager: Created spool for %s, id=%d\n", req.spool_id, id);
            return id;
        }
        Serial.printf("SpoolmanManager: Created spool but failed to parse response\n");
        return -1;
    }

    Serial.printf("SpoolmanManager: Failed to create spool, code=%d\n", code);
    Serial.print(body);
    return -1;
}

static bool lookupSpoolById(int spoolId, const char* uuid) {
    char path[64];
    snprintf(path, sizeof(path), "/api/v1/spool/%d", spoolId);
    String response;
    int code = httpGet(path, response);
    if (code != 200) {
        Serial.printf("SpoolmanManager: lookupSpoolById(%d) returned %d\n", spoolId, code);
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error != DeserializationError::Ok) {
        Serial.printf("SpoolmanManager: Failed to parse spool JSON: %s\n", error.c_str());
        return false;
    }

    JsonObject extra = doc["extra"];
    if (extra.isNull()) {
        Serial.printf("SpoolmanManager: Spool %d has no extra field\n", spoolId);
        return false;
    }

    const char* tagUuid = extra["openprinttag_uuid"] | "";
    // Spoolman stores extra values as JSON strings (with quotes), so check both
    if (strcmp(tagUuid, uuid) == 0) {
        return true;
    }
    // Check quoted form: "\"UUID\""
    char quoted[80];
    snprintf(quoted, sizeof(quoted), "\"%s\"", uuid);
    if (strcmp(tagUuid, quoted) == 0) {
        return true;
    }

    Serial.printf("SpoolmanManager: Spool %d UUID mismatch: '%s' != '%s'\n", spoolId, tagUuid, uuid);
    return false;
}

static bool updateSpool(int spoolId, int filamentId, float remainingWeight) {
    JsonDocument doc;
    doc["remaining_weight"] = remainingWeight;
    if (filamentId >= 0) {
        doc["filament_id"] = filamentId;
    }

    String body;
    serializeJson(doc, body);

    char path[64];
    snprintf(path, sizeof(path), "/api/v1/spool/%d", spoolId);

    String response;
    int code = httpPatch(path, body.c_str(), response);
    if (code == 200) {
        Serial.printf("SpoolmanManager: Updated spool id=%d, remaining=%.1fg\n", spoolId, remainingWeight);
        return true;
    }

    Serial.printf("SpoolmanManager: Failed to update spool, code=%d\n", code);
    return false;
}

// --- SpoolmanManager class implementation ---

SpoolmanManager& SpoolmanManager::getInstance() {
    static SpoolmanManager instance;
    return instance;
}

bool SpoolmanManager::begin(SemaphoreHandle_t httpMutex) {
    httpMutex_ = httpMutex;

    syncQueue = xQueueCreate(QUEUE_SIZE, sizeof(SpoolmanSyncRequest));
    if (syncQueue == nullptr) {
        Serial.println("SpoolmanManager: Failed to create queue");
        return false;
    }

    Serial.println("SpoolmanManager: Initialized");
    return true;
}

void SpoolmanManager::startTask() {
    if (taskHandle != nullptr) {
        return;
    }

    xTaskCreatePinnedToCore(
        taskFunc,
        "SpoolmanSync",
        TASK_STACK_SIZE,
        this,
        TASK_PRIORITY,
        &taskHandle,
        1  // Core 1
    );
    Serial.println("SpoolmanManager: Task started");
}

bool SpoolmanManager::enqueueSync(const SpoolmanSyncRequest& req) {
    if (syncQueue == nullptr) {
        return false;
    }
    return xQueueSend(syncQueue, &req, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool SpoolmanManager::isConfigured() const {
    return strlen(ConfigurationManager::getInstance().getSpoolmanURL()) > 0;
}

void SpoolmanManager::taskFunc(void* param) {
    SpoolmanManager* self = static_cast<SpoolmanManager*>(param);
    self->taskLoop();
}

void SpoolmanManager::taskLoop() {
    SpoolmanSyncRequest req;
    while (true) {
        if (xQueueReceive(syncQueue, &req, portMAX_DELAY) == pdTRUE) {
            if (!isConfigured()) {
                continue;
            }

            Serial.printf("SpoolmanManager: Syncing spool %s\n", req.spool_id);
            int resolvedSpoolmanId = -1;
            bool success = syncSpool(req, resolvedSpoolmanId);

            // Send result to ApplicationManager
            AppMessage msg;
            msg.type = AppMessageType::SPOOLMAN_SYNCED;
            strncpy(msg.payload.spoolmanSynced.spool_id, req.spool_id,
                    sizeof(msg.payload.spoolmanSynced.spool_id) - 1);
            msg.payload.spoolmanSynced.spool_id[sizeof(msg.payload.spoolmanSynced.spool_id) - 1] = '\0';
            msg.payload.spoolmanSynced.success = success;
            msg.payload.spoolmanSynced.kg_remaining = req.remaining_weight_g / 1000.0f;
            msg.payload.spoolmanSynced.spoolman_id = resolvedSpoolmanId;
            ApplicationManager::getInstance().sendMessage(msg);
        }
    }
}

bool SpoolmanManager::syncSpool(const SpoolmanSyncRequest& req, int& resolvedSpoolmanId) {
    if (xSemaphoreTake(httpMutex_, HTTP_MUTEX_TIMEOUT) != pdTRUE) {
        Serial.println("SpoolmanManager: Could not acquire HTTP mutex");
        return false;
    }

    resolvedSpoolmanId = -1;
    bool success = false;

    // Fast path: if we have a spoolman_id from the tag, try direct lookup
    if (req.spoolman_id > 0) {
        Serial.printf("SpoolmanManager: Fast path - looking up spool %d\n", req.spoolman_id);
        if (lookupSpoolById(req.spoolman_id, req.spool_id)) {
            // UUID matches - just update remaining weight
            // We still need a filament_id for updateSpool; get it from the spool response
            // But updateSpool only needs it for the PATCH body, and the spool already has it.
            // Simplify: use findOrCreateVendor/Filament to get filament_id for the update.
            int vendorId = findOrCreateVendor(req.manufacturer);
            int filamentId = (vendorId >= 0) ? findOrCreateFilament(vendorId, req) : -1;
            if (filamentId >= 0) {
                success = updateSpool(req.spoolman_id, filamentId, req.remaining_weight_g);
            } else {
                // Filament lookup failed but spool exists - update without changing filament_id
                // Use a minimal PATCH with just remaining_weight
                success = updateSpool(req.spoolman_id, -1, req.remaining_weight_g);
            }
            resolvedSpoolmanId = req.spoolman_id;
            xSemaphoreGive(httpMutex_);
            return success;
        }
        Serial.println("SpoolmanManager: Fast path failed, falling back to slow path");
    }

    // Slow path: full vendor → filament → spool lookup/creation
    int vendorId = findOrCreateVendor(req.manufacturer);
    if (vendorId < 0) {
        Serial.println("SpoolmanManager: Failed to find/create vendor");
        xSemaphoreGive(httpMutex_);
        return false;
    }

    int filamentId = findOrCreateFilament(vendorId, req);
    if (filamentId < 0) {
        Serial.println("SpoolmanManager: Failed to find/create filament");
        xSemaphoreGive(httpMutex_);
        return false;
    }

    int spoolId = findSpoolByUuid(filamentId, req.spool_id);

    if (spoolId < 0) {
        spoolId = createSpool(filamentId, req);
        success = (spoolId >= 0);
    } else {
        success = updateSpool(spoolId, filamentId, req.remaining_weight_g);
    }

    if (success && spoolId > 0) {
        resolvedSpoolmanId = spoolId;
    }

    xSemaphoreGive(httpMutex_);
    return success;
}
