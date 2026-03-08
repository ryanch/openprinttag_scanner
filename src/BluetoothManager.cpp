#include "BluetoothManager.h"
#include "ApplicationManager.h"
#include "ConfigurationManager.h"
#include "HomeAssistantManager.h"
#include "NFCManager.h"
#include "LCDManager.h"
#include "ConversionUtils.h"
#include "SpoolmanManager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <json.hpp>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_mac.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include <time.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <base64.hpp>

extern SemaphoreHandle_t g_httpMutex;
extern LCDManager lcdManager;

static const char* TAG = "BluetoothManager";

static constexpr size_t JSON_RESPONSE_CAPACITY = 2048;

// Custom UUIDs
#define SERVICE_UUID        "0a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b0001"
#define CONFIG_READ_UUID    "0a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b0002"
#define CONFIG_WRITE_UUID   "0a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b0003"

// Buffer sizes
#define BLE_MAX_CONFIG_SIZE 1024
#define BLE_RESPONSE_SIZE 128

// Static state
static BLEServer* s_server = nullptr;
static BLECharacteristic* s_config_read_char = nullptr;
static BLECharacteristic* s_config_write_char = nullptr;
static bool s_is_connected = false;
static bool s_is_advertising = false;
static char s_response_buffer[BLE_RESPONSE_SIZE];
static SemaphoreHandle_t s_ble_command_mutex = nullptr;
static CurrentSpoolState s_spool_state_buffer;  // Reusable buffer (protected by s_ble_command_mutex)

// Forward declarations
static void process_command(const char* json);
static uint32_t s_request_id_counter = 0;

using namespace io;
using namespace json;

struct ParsedBleCommand {
    char command[32];
    char id[32];
    char type[16];
    char color[16];
    char manufacturer[64];
    char url[160];
    char api_key[96];
    char data[420];  // Base64-encoded raw tag data (~416 chars for 312 bytes)
    float grams_remaining;
    float initial_weight;
    int32_t spoolman_id;
    bool has_command;
    bool has_id;
    bool has_type;
    bool has_color;
    bool has_manufacturer;
    bool has_grams_remaining;
    bool has_initial_weight;
    bool has_spoolman_id;
    bool has_url;
    bool has_api_key;
    bool has_data;
};

static void initParsedBleCommand(ParsedBleCommand& out) {
    memset(&out, 0, sizeof(out));
    out.grams_remaining = 0.0f;
    out.initial_weight = 0.0f;
    out.spoolman_id = -1;
}

// Additional static buffers for stack overflow prevention (protected by s_ble_command_mutex)
static ParsedBleCommand s_parsed_command_buffer;
static SpoolDetails s_spool_details_buffer;

static bool readStringValue(json_reader& reader, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return false;
    out[0] = '\0';
    json_node_type node = reader.node_type();
    if (node != json_node_type::value &&
        node != json_node_type::value_part &&
        node != json_node_type::end_value_part) {
        return false;
    }

    size_t written = 0;
    auto append = [&]() {
        const char* value = reader.value();
        if (value == nullptr) return;
        while (*value != '\0' && written + 1 < outSize) {
            out[written++] = *value++;
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

static bool parseBleCommand(const char* jsonText, ParsedBleCommand& out) {
    initParsedBleCommand(out);
    const_buffer_stream stm((const uint8_t*)jsonText, strlen(jsonText));
    json_reader reader(stm);
    if (!reader.read() || reader.node_type() != json_node_type::object) {
        return false;
    }
    const unsigned rootDepth = reader.depth();

    while (reader.read()) {
        if (reader.node_type() == json_node_type::end_object && reader.depth() == rootDepth) {
            return out.has_command;
        }
        if (reader.node_type() != json_node_type::field || reader.depth() != rootDepth) {
            continue;
        }
        char field[32];
        const char* fieldValue = reader.value();
        if (fieldValue == nullptr) {
            continue;
        }
        strncpy(field, fieldValue, sizeof(field) - 1);
        field[sizeof(field) - 1] = '\0';
        if (!reader.read()) {
            return false;
        }

        if (strcmp(field, "command") == 0) {
            out.has_command = readStringValue(reader, out.command, sizeof(out.command));
        } else if (strcmp(field, "id") == 0) {
            out.has_id = readStringValue(reader, out.id, sizeof(out.id));
        } else if (strcmp(field, "type") == 0) {
            out.has_type = readStringValue(reader, out.type, sizeof(out.type));
        } else if (strcmp(field, "color") == 0) {
            out.has_color = readStringValue(reader, out.color, sizeof(out.color));
        } else if (strcmp(field, "manufacturer") == 0) {
            out.has_manufacturer = readStringValue(reader, out.manufacturer, sizeof(out.manufacturer));
        } else if (strcmp(field, "grams_remaining") == 0 &&
                   reader.node_type() == json_node_type::value &&
                   (reader.value_type() == json_value_type::real || reader.value_type() == json_value_type::integer)) {
            out.grams_remaining = static_cast<float>(reader.value_real());
            out.has_grams_remaining = true;
        } else if (strcmp(field, "spoolman_id") == 0 &&
                   reader.node_type() == json_node_type::value &&
                   (reader.value_type() == json_value_type::real || reader.value_type() == json_value_type::integer)) {
            out.spoolman_id = static_cast<int32_t>(reader.value_real());
            out.has_spoolman_id = true;
        } else if (strcmp(field, "initial_weight") == 0 &&
                   reader.node_type() == json_node_type::value &&
                   (reader.value_type() == json_value_type::real ||
                    reader.value_type() == json_value_type::integer)) {
            out.initial_weight = static_cast<float>(reader.value_real());
            out.has_initial_weight = true;
        } else if (strcmp(field, "url") == 0) {
            out.has_url = readStringValue(reader, out.url, sizeof(out.url));
        } else if (strcmp(field, "api_key") == 0) {
            out.has_api_key = readStringValue(reader, out.api_key, sizeof(out.api_key));
        } else if (strcmp(field, "data") == 0) {
            out.has_data = readStringValue(reader, out.data, sizeof(out.data));
        }
    }
    return out.has_command;
}

static bool jsonHasTopLevelField(const char* jsonText, const char* fieldName) {
    const_buffer_stream stm((const uint8_t*)jsonText, strlen(jsonText));
    json_reader reader(stm);
    if (!reader.read() || reader.node_type() != json_node_type::object) {
        return false;
    }
    const unsigned rootDepth = reader.depth();
    while (reader.read()) {
        if (reader.node_type() == json_node_type::end_object && reader.depth() == rootDepth) {
            break;
        }
        if (reader.node_type() == json_node_type::field &&
            reader.depth() == rootDepth &&
            strcmp(reader.value(), fieldName) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Process a JSON command from the client
 */
static void process_command(const char* json) {
    ParsedBleCommand& cmd = s_parsed_command_buffer;  // Use static buffer to reduce stack usage
    if (!parseBleCommand(json, cmd)) {
        Serial.printf("%s: JSON parse error\n", TAG);
        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* command = cmd.command;

    if (strcmp(command, "read_config") == 0) {
        char config[512];
        ConfigurationManager::getInstance().readConfig(config, sizeof(config));
        if (s_config_read_char) {
            s_config_read_char->setValue(config);
        }
        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
        Serial.printf("%s: Config read requested, len=%u\n", TAG, static_cast<unsigned>(strlen(config)));
    }
    else if (strcmp(command, "write_config") == 0) {
        if (ConfigurationManager::getInstance().postConfigUpdate(json)) {
            lcdManager.setScreenTimeoutMs(ConfigurationManager::getInstance().getLcdTimeoutMs());
            ApplicationManager::getInstance().showStatusOnLCD();
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
            Serial.printf("%s: Config updated successfully\n", TAG);
        } else {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Failed to save config\"}");
            Serial.printf("%s: Config update failed\n", TAG);
        }
    }
    else if (strcmp(command, "list_spools") == 0) {
        // Use static buffer to avoid stack overflow
        CurrentSpoolState& spool = s_spool_state_buffer;
        if (!NFCManager::getInstance().getCurrentSpoolState(spool)) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
            if (s_config_write_char) {
                s_config_write_char->setValue(s_response_buffer);
            }
            return;
        }
        StaticJsonDocument<JSON_RESPONSE_CAPACITY> responseDoc;

        if (spool.present && spool.tag_data_valid) {
            JsonObject current = responseDoc["current"].to<JsonObject>();
            current["id"] = spool.spool_id;

            // Get material type
            uint8_t material_type = 0;
            opt_get_material_type(&spool.tag_data, &material_type);
            current["type"] = materialTypeToString(material_type);

            // Get color as #RRGGBB
            uint8_t color[4];
            if (opt_get_primary_color(&spool.tag_data, color) == OPT_OK) {
                char colorHex[8];
                snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X", color[0], color[1], color[2]);
                current["color"] = colorHex;
            } else {
                current["color"] = "#000000";
            }

            // Get manufacturer/brand name
            char brand[64] = {0};
            if (opt_get_brand_name(&spool.tag_data, brand, sizeof(brand)) == OPT_OK && brand[0] != '\0') {
                current["manufacturer"] = brand;
            } else {
                current["manufacturer"] = "";
            }

            // Calculate grams remaining
            float full_weight = 0.0f;
            float consumed = 0.0f;
            opt_get_actual_full_weight(&spool.tag_data, &full_weight);
            opt_get_consumed_weight(&spool.tag_data, &consumed);
            int grams_remaining = (int)(full_weight - consumed);
            Serial.printf("BluetoothManager: list_spools - full_weight=%.2fg, consumed=%.2fg, remaining=%dg\n",
                          full_weight, consumed, grams_remaining);
            current["grams_remaining"] = grams_remaining;

            current["last_seen"] = time(nullptr);

            int32_t spoolmanId = -1;
            opt_get_gp_spoolman_id(&spool.tag_data, &spoolmanId);
            if (spoolmanId > 0) current["spoolman_id"] = spoolmanId;
        } else if (spool.present && spool.blank_tag_present) {
            JsonObject current = responseDoc["current"].to<JsonObject>();
            current["id"] = spool.spool_id;
            current["blank"] = true;
        } else {
            responseDoc["current"] = nullptr;
        }

        // Get recent spools
        JsonArray recentArray = responseDoc["recent"].to<JsonArray>();
        RecentSpoolEntry recentEntries[NFCManager::MAX_RECENT_SPOOLS];
        size_t recentCount = NFCManager::getInstance().getRecentSpools(recentEntries, NFCManager::MAX_RECENT_SPOOLS);
        for (size_t i = 0; i < recentCount; i++) {
            JsonObject recentObj = recentArray.add<JsonObject>();
            recentObj["id"] = recentEntries[i].spool_id;
            recentObj["type"] = materialTypeToString(recentEntries[i].material_type);
            char colorHex[8];
            snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X",
                     recentEntries[i].color[0], recentEntries[i].color[1], recentEntries[i].color[2]);
            recentObj["color"] = colorHex;
            recentObj["manufacturer"] = recentEntries[i].manufacturer;
            recentObj["grams_remaining"] = recentEntries[i].grams_remaining;
            recentObj["last_seen"] = recentEntries[i].last_seen;
            if (recentEntries[i].spoolman_id > 0)
                recentObj["spoolman_id"] = recentEntries[i].spoolman_id;
        }

        char response[1024];
        serializeJson(responseDoc, response, sizeof(response));
        if (s_config_read_char) {
            s_config_read_char->setValue(response);
        }
        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
        //Serial.printf("%s: list_spools completed\n", TAG);
    }
    else if (strcmp(command, "format_spool") == 0) {
        // Use static buffer to avoid stack overflow
        CurrentSpoolState& spool = s_spool_state_buffer;
        if (!NFCManager::getInstance().getCurrentSpoolState(spool)) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
            if (s_config_write_char) {
                s_config_write_char->setValue(s_response_buffer);
            }
            return;
        }

        if (!spool.present) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
            Serial.printf("%s: format_spool failed - no tag present\n", TAG);
        } else {
            const char* requestedId = cmd.has_id ? cmd.id : "";
            if (requestedId[0] != '\0' && strcmp(requestedId, spool.spool_id) != 0) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
                Serial.printf("%s: format_spool failed - ID mismatch\n", TAG);
            } else {
                NFCWriteRequest req;
                memset(&req, 0, sizeof(req));
                req.request_id = ++s_request_id_counter;
                req.type = NFCWriteType::FORMAT_NEW;
                strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                if (!NFCManager::getInstance().enqueueWrite(req)) {
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
                    Serial.printf("%s: format_spool failed - write queue full\n", TAG);
                } else {
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                    Serial.printf("%s: format_spool - enqueued FORMAT_NEW\n", TAG);
                }
            }
        }
    }
    else if (strcmp(command, "update_spool") == 0) {
        // Use static buffer to avoid stack overflow
        CurrentSpoolState& spool = s_spool_state_buffer;
        if (!NFCManager::getInstance().getCurrentSpoolState(spool)) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
            if (s_config_write_char) {
                s_config_write_char->setValue(s_response_buffer);
            }
            return;
        }

        if (!spool.present) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
            Serial.printf("%s: update_spool failed - no tag present\n", TAG);
        } else if (spool.blank_tag_present) {
            // Blank tag — enqueue FORMAT_NEW
            const char* requestedId = cmd.has_id ? cmd.id : "";
            if (requestedId[0] != '\0' && strcmp(requestedId, spool.spool_id) != 0) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
                Serial.printf("%s: update_spool (format) failed - ID mismatch\n", TAG);
            } else {
                NFCWriteRequest req;
                memset(&req, 0, sizeof(req));
                req.request_id = ++s_request_id_counter;
                req.type = NFCWriteType::FORMAT_NEW;
                strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                if (!NFCManager::getInstance().enqueueWrite(req)) {
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
                    Serial.printf("%s: update_spool failed - write queue full (blank format)\n", TAG);
                } else {
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                    Serial.printf("%s: update_spool - enqueued FORMAT_NEW for blank tag\n", TAG);
                }
            }
        } else if (!spool.tag_data_valid) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
            Serial.printf("%s: update_spool failed - tag data not valid\n", TAG);
        } else {
            // Verify spool ID matches
            const char* requestedId = cmd.has_id ? cmd.id : "";
            if (requestedId[0] != '\0' && strcmp(requestedId, spool.spool_id) != 0) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
                Serial.printf("%s: update_spool failed - ID mismatch\n", TAG);
            } else {
                bool queued = false;
                int queuedCount = 0;
                bool enqueueFailed = false;

                auto enqueueOrFail = [&](NFCWriteRequest& req, const char* label) {
                    if (enqueueFailed) {
                        return;
                    }
                    if (!NFCManager::getInstance().enqueueWrite(req)) {
                        enqueueFailed = true;
                        Serial.printf("%s: update_spool failed - write queue full while enqueuing %s\n", TAG, label);
                        return;
                    }
                    queued = true;
                    queuedCount++;
                };

                // Check if type changed
                if (cmd.has_type) {
                    uint8_t newMaterial = materialTypeFromString(cmd.type);
                    uint8_t currentMaterial = 0;
                    opt_get_material_type(&spool.tag_data, &currentMaterial);
                    if (newMaterial != currentMaterial) {
                        NFCWriteRequest req;
                        memset(&req, 0, sizeof(req));
                        req.request_id = ++s_request_id_counter;
                        req.type = NFCWriteType::CHANGE_FILAMENT_TYPE;
                        strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                        req.data.new_material_type = newMaterial;
                        enqueueOrFail(req, "type");
                    }
                }

                // Check if color changed
                if (!enqueueFailed && cmd.has_color) {
                    const char* newColor = cmd.color;
                    uint8_t newRgba[4];
                    if (parseHexColor(newColor, newRgba)) {
                        uint8_t currentColor[4];
                        opt_get_primary_color(&spool.tag_data, currentColor);
                        if (memcmp(newRgba, currentColor, 3) != 0) {
                            NFCWriteRequest req;
                            memset(&req, 0, sizeof(req));
                            req.request_id = ++s_request_id_counter;
                            req.type = NFCWriteType::CHANGE_COLOR;
                            strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                            memcpy(req.data.new_color, newRgba, 4);
                            enqueueOrFail(req, "color");
                        }
                    }
                }

                // Check if manufacturer changed
                if (!enqueueFailed && cmd.has_manufacturer) {
                    const char* newBrand = cmd.manufacturer;
                    char currentBrand[64] = {0};
                    opt_get_brand_name(&spool.tag_data, currentBrand, sizeof(currentBrand));
                    if (strcmp(newBrand, currentBrand) != 0) {
                        NFCWriteRequest req;
                        memset(&req, 0, sizeof(req));
                        req.request_id = ++s_request_id_counter;
                        req.type = NFCWriteType::SET_BRAND_NAME;
                        strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                        strncpy(req.data.brand_name, newBrand, sizeof(req.data.brand_name) - 1);
                        req.data.brand_name[sizeof(req.data.brand_name) - 1] = '\0';
                        enqueueOrFail(req, "manufacturer");
                    }
                }

                // Check if grams_remaining changed
                if (!enqueueFailed && cmd.has_grams_remaining) {
                    float newRemaining = cmd.grams_remaining;
                    float full_weight = 0.0f;
                    float currentConsumed = 0.0f;
                    opt_get_actual_full_weight(&spool.tag_data, &full_weight);
                    opt_get_consumed_weight(&spool.tag_data, &currentConsumed);
                    float currentRemaining = full_weight - currentConsumed;
                    float newConsumed = full_weight - newRemaining;
                    // Only update if difference is significant (>=1g)
                    if (abs(newRemaining - currentRemaining) >= 1.0f) {
                        NFCWriteRequest req;
                        memset(&req, 0, sizeof(req));
                        req.request_id = ++s_request_id_counter;
                        req.type = NFCWriteType::SET_CONSUMED_WEIGHT;
                        strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                        req.data.consumed_weight = newConsumed;
                        enqueueOrFail(req, "grams_remaining");
                    }
                }

                if (enqueueFailed) {
                    if (queuedCount > 0) {
                        snprintf(s_response_buffer, sizeof(s_response_buffer),
                                 "{\"error\":\"Busy\",\"queued\":%d}", queuedCount);
                        Serial.printf("%s: update_spool partial enqueue, queued=%d\n", TAG, queuedCount);
                    } else {
                        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
                        Serial.printf("%s: update_spool enqueue failed before any write\n", TAG);
                    }
                } else {
                    if (!queued) {
                        // No tag fields changed, so no write was enqueued.
                        // Force a re-read so SPOOL_DETECTED is re-emitted and sync paths still run.
                        NFCManager::getInstance().requestCurrentSpool();
                        Serial.printf("%s: update_spool no-op, requested fresh spool read\n", TAG);
                    }
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                    Serial.printf("%s: update_spool completed, queued=%d\n", TAG, queuedCount);
                }
            }
        }
    }
    else if (strcmp(command, "test_spoolman") == 0) {
        const char* url = cmd.has_url ? cmd.url : "";
        if (strlen(url) == 0) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"error\",\"message\":\"Missing url\"}");
        } else if (xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"error\",\"message\":\"Device busy\"}");
        } else {
            WiFiClient client;
            HTTPClient http;
            char testUrl[192];
            snprintf(testUrl, sizeof(testUrl), "%s/api/v1/info", url);
            http.begin(client, testUrl);
            http.setTimeout(5000);
            int httpCode = http.GET();
            String body = http.getString();
            http.end();
            xSemaphoreGive(g_httpMutex);
            if (httpCode != 200) {
                snprintf(s_response_buffer, sizeof(s_response_buffer),
                    "{\"status\":\"error\",\"message\":\"HTTP %d\",\"code\":%d}", httpCode, httpCode);
            } else {
                if (!jsonHasTopLevelField(body.c_str(), "version")) {
                    snprintf(s_response_buffer, sizeof(s_response_buffer),
                        "{\"status\":\"error\",\"message\":\"Missing version in response\"}");
                } else {
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                }
            }
            Serial.printf("%s: test_spoolman %s -> %d\n", TAG, testUrl, httpCode);
        }
    }
    else if (strcmp(command, "test_mqtt") == 0) {
        auto& config = ConfigurationManager::getInstance();
        auto& haManager = HomeAssistantManager::getInstance();

        if (!config.getHAEnabled()) {
            snprintf(s_response_buffer, sizeof(s_response_buffer),
                     "{\"status\":\"error\",\"message\":\"Home Assistant must be enabled\"}");
        } else if (strlen(config.getHAMqttHost()) == 0) {
            snprintf(s_response_buffer, sizeof(s_response_buffer),
                     "{\"status\":\"error\",\"message\":\"MQTT host not configured\"}");
        } else {
            int mqttState = -1;
            bool connected = haManager.restartAndTestConnection(10000, &mqttState);
            if (connected) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
            } else {
                snprintf(s_response_buffer, sizeof(s_response_buffer),
                         "{\"status\":\"error\",\"message\":\"MQTT state %d\"}", mqttState);
            }
            Serial.printf("%s: test_mqtt restart -> %s (state=%d)\n", TAG,
                          connected ? "OK" : "FAIL", mqttState);
        }
    }
    else if (strcmp(command, "write_raw_tag") == 0) {
        // Use static buffer to avoid stack overflow
        CurrentSpoolState& spool = s_spool_state_buffer;
        if (!NFCManager::getInstance().getCurrentSpoolState(spool)) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
            if (s_config_write_char) {
                s_config_write_char->setValue(s_response_buffer);
            }
            return;
        }

        if (!spool.present) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
            Serial.printf("%s: write_raw_tag failed - no tag present\n", TAG);
        } else if (!cmd.has_data) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Missing data\"}");
            Serial.printf("%s: write_raw_tag failed - no data field\n", TAG);
        } else {
            const char* requestedId = cmd.has_id ? cmd.id : "";
            if (requestedId[0] != '\0' && strcmp(requestedId, spool.spool_id) != 0) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
                Serial.printf("%s: write_raw_tag failed - ID mismatch\n", TAG);
            } else {
                // Decode base64 data
                uint8_t decoded[320];
                unsigned int dataLen = strlen(cmd.data);
                unsigned int decodedLen = decode_base64((const unsigned char*)cmd.data, dataLen, decoded);

                if (decodedLen == 0 || decodedLen > 320) {
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Invalid data size\"}");
                    Serial.printf("%s: write_raw_tag failed - decoded size %u\n", TAG, decodedLen);
                } else if (decoded[0] != 0xE1) {
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Invalid data (bad CC)\"}");
                    Serial.printf("%s: write_raw_tag failed - first byte 0x%02X != 0xE1\n", TAG, decoded[0]);
                } else {
                    NFCWriteRequest req;
                    memset(&req, 0, sizeof(req));
                    req.request_id = ++s_request_id_counter;
                    req.type = NFCWriteType::WRITE_RAW_TAG;
                    strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                    if (!NFCManager::getInstance().enqueueRawWrite(req, decoded, decodedLen)) {
                        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
                        Serial.printf("%s: write_raw_tag failed - enqueue failed\n", TAG);
                    } else {
                        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                        Serial.printf("%s: write_raw_tag - enqueued %u bytes\n", TAG, decodedLen);
                    }
                }
            }
        }
    }
    else if (strcmp(command, "test_prusalink") == 0) {
        const char* url = cmd.has_url ? cmd.url : "";
        const char* apiKey = cmd.has_api_key ? cmd.api_key : "";
        if (strlen(url) == 0) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"error\",\"message\":\"Missing url\"}");
        } else if (xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"error\",\"message\":\"Device busy\"}");
        } else {
            WiFiClient client;
            HTTPClient http;
            char testUrl[192];
            snprintf(testUrl, sizeof(testUrl), "%s/api/v1/status", url);
            http.begin(client, testUrl);
            http.setTimeout(5000);
            if (strlen(apiKey) > 0) {
                http.addHeader("X-Api-Key", apiKey);
            }
            int httpCode = http.GET();
            http.end();
            xSemaphoreGive(g_httpMutex);
            if (httpCode == 200) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
            } else {
                snprintf(s_response_buffer, sizeof(s_response_buffer),
                    "{\"status\":\"error\",\"message\":\"HTTP %d\",\"code\":%d}", httpCode, httpCode);
            }
            Serial.printf("%s: test_prusalink %s -> %d\n", TAG, testUrl, httpCode);
        }
    }
    else if (strcmp(command, "get_spoolman_spool") == 0) {
        // Validate spoolman_id
        if (!cmd.has_spoolman_id || cmd.spoolman_id <= 0) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Invalid spoolman_id\"}");
            Serial.printf("%s: get_spoolman_spool failed - invalid spoolman_id\n", TAG);
        } else if (!SpoolmanManager::getInstance().isConfigured()) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Spoolman not configured\"}");
            Serial.printf("%s: get_spoolman_spool failed - Spoolman not configured\n", TAG);
        } else if (xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Device busy\"}");
            Serial.printf("%s: get_spoolman_spool failed - HTTP mutex timeout\n", TAG);
        } else {
            // Use static buffer to avoid stack overflow
            SpoolDetails& details = s_spool_details_buffer;
            bool success = SpoolmanManager::getInstance().getSpoolDetails(cmd.spoolman_id, details);
            xSemaphoreGive(g_httpMutex);

            if (!success || !details.valid) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Failed to fetch spool\"}");
                Serial.printf("%s: get_spoolman_spool failed - API error for ID %d\n", TAG, cmd.spoolman_id);
            } else {
                // Build JSON response
                StaticJsonDocument<JSON_RESPONSE_CAPACITY> responseDoc;
                responseDoc["spoolman_id"] = details.spoolman_id;
                responseDoc["material"] = details.material_type;
                responseDoc["color"] = details.color_hex;
                responseDoc["vendor_name"] = details.manufacturer;
                responseDoc["remaining_weight_g"] = details.remaining_weight_g;
                responseDoc["initial_weight_g"] = details.initial_weight_g;

                char response[1024];
                serializeJson(responseDoc, response, sizeof(response));
                if (s_config_read_char) {
                    s_config_read_char->setValue(response);
                }
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                Serial.printf("%s: get_spoolman_spool(%d) success\n", TAG, cmd.spoolman_id);
            }
        }
    }
    else if (strcmp(command, "write_spoolman_spool") == 0) {
        // Validate spoolman_id
        if (!cmd.has_spoolman_id || cmd.spoolman_id <= 0) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Invalid spoolman_id\"}");
            Serial.printf("%s: write_spoolman_spool failed - invalid spoolman_id\n", TAG);
        } else if (!cmd.has_type || !cmd.has_initial_weight || !cmd.has_grams_remaining) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Missing required fields\"}");
            Serial.printf("%s: write_spoolman_spool failed - missing type, initial_weight, or grams_remaining\n", TAG);
        } else {
            // Get current spool state (using static buffer to avoid stack overflow and heap fragmentation)
            if (!NFCManager::getInstance().getCurrentSpoolState(s_spool_state_buffer)) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
                if (s_config_write_char) {
                    s_config_write_char->setValue(s_response_buffer);
                }
                return;
            }

            // Validate tag state
            if (!s_spool_state_buffer.present) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
                Serial.printf("%s: write_spoolman_spool failed - no tag present\n", TAG);
            } else if (s_spool_state_buffer.blank_tag_present) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag is blank - format first\"}");
                Serial.printf("%s: write_spoolman_spool failed - blank tag\n", TAG);
            } else if (!s_spool_state_buffer.tag_data_valid) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
                Serial.printf("%s: write_spoolman_spool failed - tag data not valid\n", TAG);
            } else {
                // Use static buffer to avoid stack overflow
                SpoolDetails& details = s_spool_details_buffer;
                details.spoolman_id = cmd.spoolman_id;
                strncpy(details.material_type, cmd.type, sizeof(details.material_type) - 1);
                details.material_type[sizeof(details.material_type) - 1] = '\0';
                strncpy(details.color_hex, cmd.color, sizeof(details.color_hex) - 1);
                details.color_hex[sizeof(details.color_hex) - 1] = '\0';
                strncpy(details.manufacturer, cmd.manufacturer, sizeof(details.manufacturer) - 1);
                details.manufacturer[sizeof(details.manufacturer) - 1] = '\0';
                details.initial_weight_g = cmd.initial_weight;
                details.remaining_weight_g = cmd.grams_remaining;
                details.valid = true;
                Serial.printf("%s: write_spoolman_spool - using direct fields\n", TAG);

                // Enqueue NFC writes - reuse single req struct to minimize stack usage
                bool enqueueFailed = false;
                int queuedCount = 0;
                NFCWriteRequest req;  // Single reusable request struct

                auto enqueueOrFail = [&](const char* label) {
                    if (enqueueFailed) {
                        return;
                    }
                    if (!NFCManager::getInstance().enqueueWrite(req)) {
                        enqueueFailed = true;
                        Serial.printf("%s: write_spoolman_spool failed - write queue full while enqueuing %s\n", TAG, label);
                        return;
                    }
                    queuedCount++;
                };

                // 1. CHANGE_FILAMENT_TYPE
                uint8_t materialType = materialTypeFromString(details.material_type);
                memset(&req, 0, sizeof(req));
                req.request_id = ++s_request_id_counter;
                req.type = NFCWriteType::CHANGE_FILAMENT_TYPE;
                req.suppress_sync = 1;  // Mode B: suppress sync during batch
                strncpy(req.expected_spool_id, s_spool_state_buffer.spool_id, sizeof(req.expected_spool_id) - 1);
                req.expected_spool_id[sizeof(req.expected_spool_id) - 1] = '\0';
                req.data.new_material_type = materialType;
                Serial.printf("BluetoothManager: Enqueuing CHANGE_FILAMENT_TYPE with type=%u (%s)\n",
                              materialType, details.material_type);
                enqueueOrFail("type");

                // 2. SET_INITIAL_WEIGHT (before consumed weight, so total is known)
                if (!enqueueFailed) {
                    memset(&req, 0, sizeof(req));
                    req.request_id = ++s_request_id_counter;
                    req.type = NFCWriteType::SET_INITIAL_WEIGHT;
                    req.suppress_sync = 1;  // Mode B: suppress sync during batch
                    strncpy(req.expected_spool_id, s_spool_state_buffer.spool_id, sizeof(req.expected_spool_id) - 1);
                    req.expected_spool_id[sizeof(req.expected_spool_id) - 1] = '\0';
                    req.data.consumed_weight = details.initial_weight_g;  // Use consumed_weight field to pass initial weight
                    Serial.printf("BluetoothManager: Enqueuing SET_INITIAL_WEIGHT with %.2fg\n", details.initial_weight_g);
                    enqueueOrFail("initial_weight");
                }

                // 3. CHANGE_COLOR
                if (!enqueueFailed) {
                    uint8_t newRgba[4];
                    if (parseHexColor(details.color_hex, newRgba)) {
                        memset(&req, 0, sizeof(req));
                        req.request_id = ++s_request_id_counter;
                        req.type = NFCWriteType::CHANGE_COLOR;
                        req.suppress_sync = 1;  // Mode B: suppress sync during batch
                        strncpy(req.expected_spool_id, s_spool_state_buffer.spool_id, sizeof(req.expected_spool_id) - 1);
                        req.expected_spool_id[sizeof(req.expected_spool_id) - 1] = '\0';
                        memcpy(req.data.new_color, newRgba, 4);
                        enqueueOrFail("color");
                    }
                }

                // 4. SET_BRAND_NAME (truncate to 32 chars)
                if (!enqueueFailed) {
                    memset(&req, 0, sizeof(req));
                    req.request_id = ++s_request_id_counter;
                    req.type = NFCWriteType::SET_BRAND_NAME;
                    req.suppress_sync = 1;  // Mode B: suppress sync during batch
                    strncpy(req.expected_spool_id, s_spool_state_buffer.spool_id, sizeof(req.expected_spool_id) - 1);
                    req.expected_spool_id[sizeof(req.expected_spool_id) - 1] = '\0';
                    strncpy(req.data.brand_name, details.manufacturer, sizeof(req.data.brand_name) - 1);
                    req.data.brand_name[sizeof(req.data.brand_name) - 1] = '\0';
                    enqueueOrFail("manufacturer");
                }

                // 5. SET_CONSUMED_WEIGHT
                if (!enqueueFailed) {
                    float consumedWeight = details.initial_weight_g - details.remaining_weight_g;
                    memset(&req, 0, sizeof(req));
                    req.request_id = ++s_request_id_counter;
                    req.type = NFCWriteType::SET_CONSUMED_WEIGHT;
                    req.suppress_sync = 1;  // Mode B: suppress sync during batch
                    strncpy(req.expected_spool_id, s_spool_state_buffer.spool_id, sizeof(req.expected_spool_id) - 1);
                    req.expected_spool_id[sizeof(req.expected_spool_id) - 1] = '\0';
                    req.data.consumed_weight = consumedWeight;
                    Serial.printf("BluetoothManager: Enqueuing SET_CONSUMED_WEIGHT with %.2fg (initial=%.2fg, remaining=%.2fg)\n",
                                  consumedWeight, details.initial_weight_g, details.remaining_weight_g);
                    enqueueOrFail("consumed_weight");
                }

                // 6. WRITE_SPOOLMAN_ID
                if (!enqueueFailed) {
                    memset(&req, 0, sizeof(req));
                    req.request_id = ++s_request_id_counter;
                    req.type = NFCWriteType::WRITE_SPOOLMAN_ID;
                    req.suppress_sync = 1;  // Mode B: suppress sync during batch
                    strncpy(req.expected_spool_id, s_spool_state_buffer.spool_id, sizeof(req.expected_spool_id) - 1);
                    req.expected_spool_id[sizeof(req.expected_spool_id) - 1] = '\0';
                    req.data.spoolman_id = details.spoolman_id;
                    enqueueOrFail("spoolman_id");
                }

                if (enqueueFailed) {
                    if (queuedCount > 0) {
                        snprintf(s_response_buffer, sizeof(s_response_buffer),
                                 "{\"error\":\"Busy\",\"queued\":%d}", queuedCount);
                        Serial.printf("%s: write_spoolman_spool partial enqueue, queued=%d\n", TAG, queuedCount);
                    } else {
                        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
                        Serial.printf("%s: write_spoolman_spool enqueue failed\n", TAG);
                    }
                } else {
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                    Serial.printf("%s: write_spoolman_spool completed, queued=%d writes\n", TAG, queuedCount);
                }
            }
        }
    }
    else {
        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Unknown command\"}");
        Serial.printf("%s: Unknown command: %s\n", TAG, command);
    }

    // Update write characteristic with response
    if (s_config_write_char) {
        s_config_write_char->setValue(s_response_buffer);
    }
}

/**
 * @brief Server callbacks for connection events
 */
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        s_is_connected = true;
        s_is_advertising = false;
        s_response_buffer[0] = '\0';
        Serial.printf("%s: Client connected\n", TAG);
    }

    void onDisconnect(BLEServer* pServer) override {
        s_is_connected = false;
        Serial.printf("%s: Client disconnected, restarting advertising\n", TAG);
        // Restart advertising
        BLEDevice::startAdvertising();
        s_is_advertising = true;
    }
};

/**
 * @brief Characteristic callbacks for write events
 */
class WriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            //Serial.printf("%s: Write received, len=%d\n", TAG, value.length());

            // Protect shared state with mutex
            if (s_ble_command_mutex && xSemaphoreTake(s_ble_command_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                process_command(value.c_str());
                xSemaphoreGive(s_ble_command_mutex);
            } else {
                Serial.printf("%s: Failed to acquire BLE command mutex\n", TAG);
            }
        }
    }
};

// Static callback instances
static ServerCallbacks s_server_callbacks;
static WriteCallbacks s_write_callbacks;

// Public API

BluetoothManager& BluetoothManager::getInstance() {
    static BluetoothManager instance;
    return instance;
}

bool BluetoothManager::begin() {
    Serial.printf("%s: Initializing Bluetooth manager\n", TAG);

    // Create command processing mutex
    if (s_ble_command_mutex == nullptr) {
        s_ble_command_mutex = xSemaphoreCreateMutex();
        if (s_ble_command_mutex == nullptr) {
            Serial.printf("%s: Failed to create BLE command mutex\n", TAG);
            return false;
        }
    }

    // Generate device name from MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char device_name[24];
    snprintf(device_name, sizeof(device_name), "OpenPrintTag-%02X%02X", mac[4], mac[5]);
    Serial.printf("%s: BLE device name: %s\n", TAG, device_name);

    // Debug: Check controller state
    esp_bt_controller_status_t status = esp_bt_controller_get_status();
    Serial.printf("%s: BT controller status: %d (0=IDLE, 1=INITED, 2=ENABLED)\n", TAG, status);

    // Release classic BT memory first
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    Serial.printf("%s: mem_release result: %d\n", TAG, ret);

    // Initialize controller if not already
    if (status == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ret = esp_bt_controller_init(&bt_cfg);
        Serial.printf("%s: controller_init result: %d (0=OK, 259=INVALID_STATE)\n", TAG, ret);
        if (ret != ESP_OK) {
            return false;
        }
    }

    status = esp_bt_controller_get_status();
    Serial.printf("%s: BT controller status after init: %d\n", TAG, status);

    // Enable controller
    if (status == ESP_BT_CONTROLLER_STATUS_INITED) {
        ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        Serial.printf("%s: controller_enable result: %d\n", TAG, ret);
        if (ret != ESP_OK) {
            return false;
        }
    }

    Serial.printf("%s: BT controller enabled, initializing bluedroid\n", TAG);

    // Initialize BLE stack
    BLEDevice::init(device_name);

    // Create server
    s_server = BLEDevice::createServer();
    if (!s_server) {
        Serial.printf("%s: Failed to create BLE server\n", TAG);
        return false;
    }
    s_server->setCallbacks(&s_server_callbacks);

    // Create service
    BLEService* service = s_server->createService(SERVICE_UUID);
    if (!service) {
        Serial.printf("%s: Failed to create BLE service\n", TAG);
        return false;
    }

    // Create config read characteristic (READ)
    s_config_read_char = service->createCharacteristic(
        CONFIG_READ_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    if (!s_config_read_char) {
        Serial.printf("%s: Failed to create read characteristic\n", TAG);
        return false;
    }

    // Create config write characteristic (READ + WRITE)
    s_config_write_char = service->createCharacteristic(
        CONFIG_WRITE_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    if (!s_config_write_char) {
        Serial.printf("%s: Failed to create write characteristic\n", TAG);
        return false;
    }
    s_config_write_char->setCallbacks(&s_write_callbacks);

    // Start service
    service->start();
    Serial.printf("%s: Service started\n", TAG);

    // Start advertising
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    s_is_advertising = true;

    Serial.printf("%s: Advertising started\n", TAG);
    Serial.printf("%s: Initialized successfully\n", TAG);
    return true;
}

void BluetoothManager::end() {
    Serial.printf("%s: Shutting down Bluetooth manager\n", TAG);

    // Stop advertising
    if (s_is_advertising) {
        BLEDevice::stopAdvertising();
        s_is_advertising = false;
    }

    // Deinit BLE
    BLEDevice::deinit(true);

    // Delete mutex
    if (s_ble_command_mutex != nullptr) {
        vSemaphoreDelete(s_ble_command_mutex);
        s_ble_command_mutex = nullptr;
    }

    // Reset state
    s_server = nullptr;
    s_config_read_char = nullptr;
    s_config_write_char = nullptr;
    s_is_connected = false;
    s_is_advertising = false;

    Serial.printf("%s: Shutdown complete\n", TAG);
}

bool BluetoothManager::isAdvertising() const {
    return s_is_advertising;
}

bool BluetoothManager::isConnected() const {
    return s_is_connected;
}
