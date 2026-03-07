#include "SpoolmanManager.h"
#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <json.hpp>

#include <Arduino.h>
#include "openprinttag_lib.h"

static constexpr size_t JSON_SMALL_CAPACITY = 256;
static constexpr size_t JSON_MEDIUM_CAPACITY = 768;

using namespace io;
using namespace json;

static bool readIntValue(json_reader& reader, int& outValue) {
    if (reader.node_type() != json_node_type::value) {
        return false;
    }
    if (reader.value_type() == json_value_type::integer) {
        outValue = static_cast<int>(reader.value_int());
        return true;
    }
    if (reader.value_type() == json_value_type::real) {
        outValue = static_cast<int>(reader.value_real());
        return true;
    }
    return false;
}

static bool matchesUuid(const char* storedUuid, const char* uuid) {
    if (storedUuid == nullptr || uuid == nullptr) {
        return false;
    }
    if (strcmp(storedUuid, uuid) == 0) {
        return true;
    }
    // Spoolman extra field may store UUID as a quoted JSON string: "\"UUID\""
    const size_t uuidLen = strlen(uuid);
    const size_t storedLen = strlen(storedUuid);
    if (storedLen != uuidLen + 2) {
        return false;
    }
    return storedUuid[0] == '"' &&
           strncmp(storedUuid + 1, uuid, uuidLen) == 0 &&
           storedUuid[uuidLen + 1] == '"' &&
           storedUuid[uuidLen + 2] == '\0';
}

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
    auto append = [&]() {
        const char* v = reader.value();
        if (v == nullptr) return;
        while (*v != '\0' && written + 1 < outSize) {
            out[written++] = *v++;
        }
        out[written] = '\0';
    };

    append();
    if (node == json_node_type::value_part) {
        while (reader.read()) {
            json_node_type next = reader.node_type();
            if (next != json_node_type::value_part &&
                next != json_node_type::end_value_part) {
                return written > 0;
            }
            append();
            if (next == json_node_type::end_value_part) {
                break;
            }
        }
    }
    return written > 0;
}

static bool parseIdFromObject(const char* jsonText, int& outId) {
    outId = -1;
    const_buffer_stream stm((const uint8_t*)jsonText, strlen(jsonText));
    json_reader reader(stm);
    while (reader.read()) {
        if (reader.node_type() != json_node_type::field) continue;
        if (strcmp(reader.value(), "id") != 0) continue;
        if (reader.read() && readIntValue(reader, outId)) {
            return true;
        }
    }
    return false;
}

static bool parseVendorIdByName(const char* jsonText, const char* targetName, int& outId) {
    outId = -1;
    const_buffer_stream stm((const uint8_t*)jsonText, strlen(jsonText));
    json_reader reader(stm);

    while (reader.read()) {
        if (reader.node_type() != json_node_type::object) {
            continue;
        }
        const unsigned objectDepth = reader.depth();
        int candidateId = -1;
        char candidateName[64] = {0};
        while (reader.read()) {
            if (reader.node_type() == json_node_type::end_object && reader.depth() == objectDepth) {
                if (candidateName[0] != '\0' && strcasecmp(candidateName, targetName) == 0 && candidateId >= 0) {
                    outId = candidateId;
                    return true;
                }
                break;
            }
            if (reader.node_type() != json_node_type::field) continue;
            const char* field = reader.value();
            if (strcmp(field, "id") == 0) {
                if (reader.read()) readIntValue(reader, candidateId);
            } else if (strcmp(field, "name") == 0) {
                if (reader.read()) {
                    readStringValue(reader, candidateName, sizeof(candidateName));
                }
            }
        }
    }
    return false;
}

static bool parseFirstArrayItemId(const char* jsonText, int& outId) {
    outId = -1;
    const_buffer_stream stm((const uint8_t*)jsonText, strlen(jsonText));
    json_reader reader(stm);

    while (reader.read()) {
        if (reader.node_type() != json_node_type::object) continue;
        const unsigned objectDepth = reader.depth();
        while (reader.read()) {
            if (reader.node_type() == json_node_type::end_object && reader.depth() == objectDepth) {
                break;
            }
            if (reader.node_type() != json_node_type::field) continue;
            if (strcmp(reader.value(), "id") != 0) continue;
            if (reader.read() && readIntValue(reader, outId)) {
                return true;
            }
        }
    }
    return false;
}

static bool parseSpoolIdByUuid(const char* jsonText, const char* uuid, int& outId) {
    outId = -1;
    const_buffer_stream stm((const uint8_t*)jsonText, strlen(jsonText));
    json_reader reader(stm);

    while (reader.read()) {
        if (reader.node_type() != json_node_type::object) continue;
        const unsigned spoolDepth = reader.depth();
        int spoolId = -1;
        char tagUuid[80] = {0};
        while (reader.read()) {
            if (reader.node_type() == json_node_type::end_object && reader.depth() == spoolDepth) {
                if (spoolId >= 0 && matchesUuid(tagUuid, uuid)) {
                    outId = spoolId;
                    return true;
                }
                break;
            }
            if (reader.node_type() != json_node_type::field) continue;
            const char* field = reader.value();
            if (strcmp(field, "id") == 0) {
                if (reader.read()) readIntValue(reader, spoolId);
            } else if (strcmp(field, "extra") == 0) {
                if (!reader.read() || reader.node_type() != json_node_type::object) {
                    continue;
                }
                const unsigned extraDepth = reader.depth();
                while (reader.read()) {
                    if (reader.node_type() == json_node_type::end_object && reader.depth() == extraDepth) {
                        break;
                    }
                    if (reader.node_type() != json_node_type::field) continue;
                    if (strcmp(reader.value(), "openprinttag_uuid") != 0) continue;
                    if (reader.read()) {
                        readStringValue(reader, tagUuid, sizeof(tagUuid));
                    }
                }
            }
        }
    }
    return false;
}

static bool parseSpoolUuid(const char* jsonText, char* outUuid, size_t outUuidSize) {
    if (outUuid == nullptr || outUuidSize == 0) return false;
    outUuid[0] = '\0';
    const_buffer_stream stm((const uint8_t*)jsonText, strlen(jsonText));
    json_reader reader(stm);

    while (reader.read()) {
        if (reader.node_type() != json_node_type::field) continue;
        if (strcmp(reader.value(), "openprinttag_uuid") != 0) continue;
        if (!reader.read()) return false;
        return readStringValue(reader, outUuid, outUuidSize);
    }
    return false;
}

// --- File-local HTTP helpers ---

static int httpGet(const char* path, String& response) {
    const char* baseUrl = ConfigurationManager::getInstance().getSpoolmanURL();
    WiFiClient client;
    HTTPClient http;

    char url[256];
    snprintf(url, sizeof(url), "%s%s", baseUrl, path);
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

    char url[256];
    snprintf(url, sizeof(url), "%s%s", baseUrl, path);
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

    char url[256];
    snprintf(url, sizeof(url), "%s%s", baseUrl, path);
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
    char path[192];
    snprintf(path, sizeof(path), "/api/v1/vendor?name=%s", name);
    String response;
    int code = httpGet(path, response);

    Serial.printf("SpoolmanManager: get vendor '%s' code=%d\n", name, code);

    if (code == 200 || code == 201) {
        int id = -1;
        if (parseVendorIdByName(response.c_str(), name, id)) {
            Serial.printf("SpoolmanManager: Found vendor '%s' id=%d\n", name, id);
            return id;
        }
    }

    // Create new vendor
    StaticJsonDocument<JSON_SMALL_CAPACITY> createDoc;
    createDoc["name"] = name;
    String body;
    serializeJson(createDoc, body);

    code = httpPost("/api/v1/vendor", body.c_str(), response);
    if (code == 200) {
        int id = -1;
        if (parseIdFromObject(response.c_str(), id)) {
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
        int id = -1;
        if (parseFirstArrayItemId(response.c_str(), id)) {
            Serial.printf("SpoolmanManager: Found filament material=%s id=%d\n", material, id);
            return id;
        }
    }

    // Create new filament
    char colorHex[7];
    snprintf(colorHex, sizeof(colorHex), "%02X%02X%02X", req.color[0], req.color[1], req.color[2]);

    StaticJsonDocument<JSON_MEDIUM_CAPACITY> createDoc;
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
        int id = -1;
        if (parseIdFromObject(response.c_str(), id)) {
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
    if (code == 200) {
        int id = -1;
        if (parseSpoolIdByUuid(response.c_str(), uuid, id)) {
            Serial.printf("SpoolmanManager: Found spool uuid=%s id=%d in filament=%d\n",
                          uuid, id, filamentId);
            return id;
        }
    }

    // Fallback search across all spools to avoid duplicate UUID entries when
    // a prior sync used different filament metadata for the same physical tag.
    response = "";
    code = httpGet("/api/v1/spool", response);
    if (code == 200) {
        int id = -1;
        if (parseSpoolIdByUuid(response.c_str(), uuid, id)) {
            Serial.printf("SpoolmanManager: Found spool uuid=%s id=%d via global lookup\n",
                          uuid, id);
            return id;
        }
    }

    return -1;
}

static int createSpool(int filamentId, const SpoolmanSyncRequest& req) {
    char colorHex[7];
    snprintf(colorHex, sizeof(colorHex), "%02X%02X%02X", req.color[0], req.color[1], req.color[2]);

    StaticJsonDocument<JSON_MEDIUM_CAPACITY> doc;
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
        int id = -1;
        if (parseIdFromObject(response.c_str(), id)) {
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

    char tagUuid[80] = {0};
    if (!parseSpoolUuid(response.c_str(), tagUuid, sizeof(tagUuid))) {
        Serial.printf("SpoolmanManager: Spool %d has no extra field\n", spoolId);
        return false;
    }

    if (matchesUuid(tagUuid, uuid)) {
        return true;
    }

    Serial.printf("SpoolmanManager: Spool %d UUID mismatch: '%s' != '%s'\n", spoolId, tagUuid, uuid);
    return false;
}

static int findSpoolByUuidGlobal(const char* uuid) {
    String response;
    int code = httpGet("/api/v1/spool", response);
    if (code != 200) {
        return -1;
    }

    int id = -1;
    if (parseSpoolIdByUuid(response.c_str(), uuid, id)) {
        Serial.printf("SpoolmanManager: Recovered spool uuid=%s id=%d via global lookup\n",
                      uuid, id);
        return id;
    }

    return -1;
}

static bool updateSpool(int spoolId, int filamentId, float remainingWeight) {
    StaticJsonDocument<JSON_SMALL_CAPACITY> doc;
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

bool SpoolmanManager::getSpoolDetails(int32_t spoolmanId, SpoolDetails& outDetails) {
    // Initialize output structure
    memset(&outDetails, 0, sizeof(outDetails));
    outDetails.valid = false;
    outDetails.spoolman_id = -1;

    // Validate input
    if (spoolmanId <= 0) {
        Serial.printf("SpoolmanManager: getSpoolDetails - invalid spoolman_id=%d\n", spoolmanId);
        return false;
    }

    // Build API path
    char path[64];
    snprintf(path, sizeof(path), "/api/v1/spool/%d", spoolmanId);

    // Make HTTP GET request
    String response;
    int code = httpGet(path, response);

    if (code != 200) {
        Serial.printf("SpoolmanManager: getSpoolDetails(%d) returned HTTP %d\n", spoolmanId, code);
        return false;
    }

    // Debug: print first 200 chars of response
    Serial.printf("SpoolmanManager: getSpoolDetails(%d) response: %.200s%s\n",
                  spoolmanId, response.c_str(), response.length() > 200 ? "..." : "");

    // Parse JSON response using streaming parser
    const_buffer_stream stm((const uint8_t*)response.c_str(), response.length());
    json_reader reader(stm);

    bool inFilament = false;
    bool inVendor = false;
    unsigned filamentDepth = 0;
    unsigned vendorDepth = 0;
    bool hasId = false;
    bool hasMaterial = false;
    char currentField[64] = {0};  // Buffer to hold field name (reader.value() is not stable after read())

    while (reader.read()) {
        json_node_type nodeType = reader.node_type();

        // Track entry into nested objects
        if (nodeType == json_node_type::field) {
            strncpy(currentField, reader.value(), sizeof(currentField) - 1);  // Copy field name to stable buffer
            //Serial.printf("  [PARSE] field='%s' inFilament=%d inVendor=%d\n", currentField, inFilament, inVendor);

            if (strcmp(currentField, "filament") == 0) {
                if (reader.read() && reader.node_type() == json_node_type::object) {
                    inFilament = true;
                    filamentDepth = reader.depth();
                }
                continue;
            } else if (inFilament && strcmp(currentField, "vendor") == 0) {
                if (reader.read() && reader.node_type() == json_node_type::object) {
                    inVendor = true;
                    vendorDepth = reader.depth();
                }
                continue;
            }

            // Read next value
            if (!reader.read()) {
                //Serial.printf("  [PARSE] reader.read() failed for field '%s'\n", currentField);
                break;
            }
            //Serial.printf("  [PARSE] got value for field '%s', node_type=%d, inFilament=%d, inVendor=%d\n", currentField, reader.node_type(), inFilament, inVendor);

            // Extract fields based on context
            if (inVendor) {
                if (strcmp(currentField, "name") == 0) {
                    readStringValue(reader, outDetails.manufacturer, sizeof(outDetails.manufacturer));
                }
            } else if (inFilament) {
                if (strcmp(currentField, "material") == 0) {
                    //Serial.printf("  [PARSE] reading 'material' field\n");
                    if (readStringValue(reader, outDetails.material_type, sizeof(outDetails.material_type))) {
                        hasMaterial = true;
                        //Serial.printf("  [PARSE] SUCCESS: material=%s\n", outDetails.material_type);
                    } else {
                        //Serial.printf("  [PARSE] FAILED: readStringValue returned false\n");
                    }
                } else if (strcmp(currentField, "name") == 0) {
                    // Fallback to 'name' field if 'material' hasn't been set yet
                    // (real Spoolman API uses 'name' for material type in some responses)
                    if (outDetails.material_type[0] == '\0') {
                        if (readStringValue(reader, outDetails.material_type, sizeof(outDetails.material_type))) {
                            hasMaterial = true;
                        }
                    }
                } else if (strcmp(currentField, "color_hex") == 0) {
                    char colorBuf[8] = {0};
                    if (readStringValue(reader, colorBuf, sizeof(colorBuf))) {
                        // Ensure color has '#' prefix
                        if (colorBuf[0] == '#') {
                            strncpy(outDetails.color_hex, colorBuf, sizeof(outDetails.color_hex) - 1);
                        } else {
                            snprintf(outDetails.color_hex, sizeof(outDetails.color_hex), "#%s", colorBuf);
                        }
                    }
                } else if (strcmp(currentField, "weight") == 0) {
                    // Fallback capacity if initial_weight is not set
                    if (outDetails.initial_weight_g == 0.0f && reader.value_type() == json_value_type::real) {
                        outDetails.initial_weight_g = static_cast<float>(reader.value_real());
                    }
                }
            } else {
                // Top-level spool fields
                if (strcmp(currentField, "id") == 0) {
                    int id = -1;
                    //Serial.printf("  [PARSE] reading 'id' field, node_type=%d\n", reader.node_type());
                    if (readIntValue(reader, id)) {
                        outDetails.spoolman_id = id;
                        hasId = true;
                        //Serial.printf("  [PARSE] SUCCESS: id=%d\n", id);
                    } else {
                        //Serial.printf("  [PARSE] FAILED: readIntValue returned false\n");
                    }
                } else if (strcmp(currentField, "remaining_weight") == 0) {
                    if (reader.value_type() == json_value_type::real) {
                        outDetails.remaining_weight_g = static_cast<float>(reader.value_real());
                    } else if (reader.value_type() == json_value_type::integer) {
                        outDetails.remaining_weight_g = static_cast<float>(reader.value_int());
                    }
                } else if (strcmp(currentField, "initial_weight") == 0) {
                    if (reader.value_type() == json_value_type::real) {
                        outDetails.initial_weight_g = static_cast<float>(reader.value_real());
                    } else if (reader.value_type() == json_value_type::integer) {
                        outDetails.initial_weight_g = static_cast<float>(reader.value_int());
                    }
                }
            }
        } else if (nodeType == json_node_type::end_object) {
            // Track exit from nested objects
            if (inVendor && reader.depth() <= vendorDepth) {
                inVendor = false;
            } else if (inFilament && reader.depth() <= filamentDepth) {
                inFilament = false;
            }
        }
    }

    // Mark as valid if we got the essential fields
    outDetails.valid = hasId && hasMaterial;

    if (outDetails.valid) {
        Serial.printf("SpoolmanManager: getSpoolDetails(%d) success - %s %s, color=%s, %.1fg/%.1fg\n",
                      outDetails.spoolman_id,
                      outDetails.manufacturer,
                      outDetails.material_type,
                      outDetails.color_hex,
                      outDetails.remaining_weight_g,
                      outDetails.initial_weight_g);
    } else {
        Serial.printf("SpoolmanManager: getSpoolDetails(%d) incomplete - hasId=%d, hasMaterial=%d\n",
                      spoolmanId, hasId, hasMaterial);
    }

    return outDetails.valid;
}

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

int32_t SpoolmanManager::lookupCachedSpoolmanId(const char* spoolId) const {
    if (spoolId == nullptr || spoolId[0] == '\0') {
        return -1;
    }
    for (size_t i = 0; i < (sizeof(spoolIdCache_) / sizeof(spoolIdCache_[0])); ++i) {
        if (spoolIdCache_[i].spool_id[0] == '\0') {
            continue;
        }
        if (strcmp(spoolIdCache_[i].spool_id, spoolId) == 0 && spoolIdCache_[i].spoolman_id > 0) {
            return spoolIdCache_[i].spoolman_id;
        }
    }
    return -1;
}

void SpoolmanManager::storeCachedSpoolmanId(const char* spoolId, int32_t spoolmanId) {
    if (spoolId == nullptr || spoolId[0] == '\0' || spoolmanId <= 0) {
        return;
    }

    for (size_t i = 0; i < (sizeof(spoolIdCache_) / sizeof(spoolIdCache_[0])); ++i) {
        if (strcmp(spoolIdCache_[i].spool_id, spoolId) == 0) {
            spoolIdCache_[i].spoolman_id = spoolmanId;
            return;
        }
    }

    SpoolIdCacheEntry& slot = spoolIdCache_[spoolIdCacheWriteIndex_];
    strncpy(slot.spool_id, spoolId, sizeof(slot.spool_id) - 1);
    slot.spool_id[sizeof(slot.spool_id) - 1] = '\0';
    slot.spoolman_id = spoolmanId;
    spoolIdCacheWriteIndex_ = (spoolIdCacheWriteIndex_ + 1) % (sizeof(spoolIdCache_) / sizeof(spoolIdCache_[0]));
}

void SpoolmanManager::invalidateCachedSpoolmanId(const char* spoolId) {
    if (spoolId == nullptr || spoolId[0] == '\0') {
        return;
    }
    for (size_t i = 0; i < (sizeof(spoolIdCache_) / sizeof(spoolIdCache_[0])); ++i) {
        if (strcmp(spoolIdCache_[i].spool_id, spoolId) == 0) {
            spoolIdCache_[i].spoolman_id = -1;  // Invalidate the entry
            return;
        }
    }
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

    // Prefer a known-good ID for this spool UID over potentially stale tag data.
    int32_t preferredSpoolmanId = req.spoolman_id;
    int32_t cachedSpoolmanId = lookupCachedSpoolmanId(req.spool_id);
    if (cachedSpoolmanId > 0 && cachedSpoolmanId != req.spoolman_id) {
        Serial.printf("SpoolmanManager: Using cached spoolman_id=%d for spool %s (tag had %d)\n",
                      cachedSpoolmanId, req.spool_id, req.spoolman_id);
        preferredSpoolmanId = cachedSpoolmanId;
    }

    // Fast path: if we have a spoolman_id (from cache or tag), try direct lookup.
    if (preferredSpoolmanId > 0) {
        Serial.printf("SpoolmanManager: Fast path - looking up spool %d\n", preferredSpoolmanId);
        if (lookupSpoolById(preferredSpoolmanId, req.spool_id)) {
            // UUID matches - just update remaining weight
            // We still need a filament_id for updateSpool; get it from the spool response
            // But updateSpool only needs it for the PATCH body, and the spool already has it.
            // Simplify: use findOrCreateVendor/Filament to get filament_id for the update.
            int vendorId = findOrCreateVendor(req.manufacturer);
            int filamentId = (vendorId >= 0) ? findOrCreateFilament(vendorId, req) : -1;
            if (filamentId >= 0) {
                success = updateSpool(preferredSpoolmanId, filamentId, req.remaining_weight_g);
            } else {
                // Filament lookup failed but spool exists - update without changing filament_id
                // Use a minimal PATCH with just remaining_weight
                success = updateSpool(preferredSpoolmanId, -1, req.remaining_weight_g);
            }
            resolvedSpoolmanId = preferredSpoolmanId;
            if (success) {
                storeCachedSpoolmanId(req.spool_id, resolvedSpoolmanId);
            }
            xSemaphoreGive(httpMutex_);
            return success;
        }

        // Stale/mismatched spoolman_id on tag (common right after tag swaps/writeback):
        // recover by UUID before creating vendor/filament/spool to avoid duplicates.
        int existingSpoolId = findSpoolByUuidGlobal(req.spool_id);
        if (existingSpoolId > 0) {
            int vendorId = findOrCreateVendor(req.manufacturer);
            int filamentId = (vendorId >= 0) ? findOrCreateFilament(vendorId, req) : -1;
            if (filamentId >= 0) {
                success = updateSpool(existingSpoolId, filamentId, req.remaining_weight_g);
            } else {
                success = updateSpool(existingSpoolId, -1, req.remaining_weight_g);
            }
            if (success) {
                resolvedSpoolmanId = existingSpoolId;
                storeCachedSpoolmanId(req.spool_id, resolvedSpoolmanId);
                xSemaphoreGive(httpMutex_);
                return true;
            }
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
        storeCachedSpoolmanId(req.spool_id, resolvedSpoolmanId);
    }

    xSemaphoreGive(httpMutex_);
    return success;
}
