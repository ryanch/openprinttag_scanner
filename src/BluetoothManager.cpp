#include "DebugLogBuffer.h"
#include "BluetoothManager.h"
#include "ApplicationManager.h"
#include "ConfigurationManager.h"
#include "HomeAssistantManager.h"
#include "NFCManager.h"
#include "LCDManager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
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

extern SemaphoreHandle_t g_httpMutex;
extern LCDManager lcdManager;

static const char* TAG = "BluetoothManager";

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
static String s_write_buffer;

// Forward declarations
static void process_command(const char* json);
static uint32_t s_request_id_counter = 0;

// Material type string to enum mapping
static uint8_t materialTypeFromString(const char* type) {
    if (strcmp(type, "PLA") == 0) return OPT_MATERIAL_TYPE_PLA;
    if (strcmp(type, "PETG") == 0) return OPT_MATERIAL_TYPE_PETG;
    if (strcmp(type, "ABS") == 0) return OPT_MATERIAL_TYPE_ABS;
    if (strcmp(type, "ASA") == 0) return OPT_MATERIAL_TYPE_ASA;
    if (strcmp(type, "TPU") == 0) return OPT_MATERIAL_TYPE_TPU;
    if (strcmp(type, "PC") == 0) return OPT_MATERIAL_TYPE_PC;
    if (strcmp(type, "Nylon") == 0) return OPT_MATERIAL_TYPE_PA6;
    if (strcmp(type, "PVA") == 0) return OPT_MATERIAL_TYPE_PVA;
    if (strcmp(type, "HIPS") == 0) return OPT_MATERIAL_TYPE_HIPS;
    return OPT_MATERIAL_TYPE_PLA; // default
}

// Material type enum to string mapping
static const char* materialTypeToString(uint8_t type) {
    switch (type) {
        case OPT_MATERIAL_TYPE_PLA: return "PLA";
        case OPT_MATERIAL_TYPE_PETG: return "PETG";
        case OPT_MATERIAL_TYPE_ABS: return "ABS";
        case OPT_MATERIAL_TYPE_ASA: return "ASA";
        case OPT_MATERIAL_TYPE_TPU: return "TPU";
        case OPT_MATERIAL_TYPE_PC: return "PC";
        case OPT_MATERIAL_TYPE_PA6: return "Nylon";
        case OPT_MATERIAL_TYPE_PVA: return "PVA";
        case OPT_MATERIAL_TYPE_HIPS: return "HIPS";
        default: return "PLA";
    }
}

// Parse #RRGGBB color to RGBA array
static bool parseHexColor(const char* hex, uint8_t* rgba) {
    if (hex[0] != '#' || strlen(hex) != 7) return false;
    unsigned int r, g, b;
    if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) != 3) return false;
    rgba[0] = r;
    rgba[1] = g;
    rgba[2] = b;
    rgba[3] = 255;
    return true;
}

/**
 * @brief Process a JSON command from the client
 */
static void process_command(const char* json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        DBG_LOGF("%s: JSON parse error: %s\n", TAG, error.c_str());
        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* command = doc["command"] | "";

    if (strcmp(command, "read_config") == 0) {
        String config = ConfigurationManager::getInstance().readConfig();
        if (s_config_read_char) {
            s_config_read_char->setValue(config.c_str());
        }
        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
        DBG_LOGF("%s: Config read requested, len=%d\n", TAG, config.length());
    }
    else if (strcmp(command, "write_config") == 0) {
        if (ConfigurationManager::getInstance().postConfigUpdate(json)) {
            lcdManager.setScreenTimeoutMs(ConfigurationManager::getInstance().getLcdTimeoutMs());
            ApplicationManager::getInstance().showStatusOnLCD();
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
            DBG_LOGF("%s: Config updated successfully\n", TAG);
        } else {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Failed to save config\"}");
            DBG_LOGF("%s: Config update failed\n", TAG);
        }
    }
    else if (strcmp(command, "list_spools") == 0) {
        CurrentSpoolState spool;
        if (!NFCManager::getInstance().getCurrentSpoolState(spool)) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
            if (s_config_write_char) {
                s_config_write_char->setValue(s_response_buffer);
            }
            return;
        }
        JsonDocument responseDoc;

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
            current["grams_remaining"] = (int)(full_weight - consumed);

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

        String response;
        serializeJson(responseDoc, response);
        if (s_config_read_char) {
            s_config_read_char->setValue(response.c_str());
        }
        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
        //DBG_LOGF("%s: list_spools completed\n", TAG);
    }
    else if (strcmp(command, "format_spool") == 0) {
        CurrentSpoolState spool;
        if (!NFCManager::getInstance().getCurrentSpoolState(spool)) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
            if (s_config_write_char) {
                s_config_write_char->setValue(s_response_buffer);
            }
            return;
        }

        if (!spool.present) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
            DBG_LOGF("%s: format_spool failed - no tag present\n", TAG);
        } else {
            const char* requestedId = doc["id"] | "";
            if (requestedId[0] != '\0' && strcmp(requestedId, spool.spool_id) != 0) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
                DBG_LOGF("%s: format_spool failed - ID mismatch\n", TAG);
            } else {
                NFCWriteRequest req;
                memset(&req, 0, sizeof(req));
                req.request_id = ++s_request_id_counter;
                req.type = NFCWriteType::FORMAT_NEW;
                strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                NFCManager::getInstance().enqueueWrite(req);
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                DBG_LOGF("%s: format_spool - enqueued FORMAT_NEW\n", TAG);
            }
        }
    }
    else if (strcmp(command, "update_spool") == 0) {
        CurrentSpoolState spool;
        if (!NFCManager::getInstance().getCurrentSpoolState(spool)) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Busy\"}");
            if (s_config_write_char) {
                s_config_write_char->setValue(s_response_buffer);
            }
            return;
        }

        if (!spool.present) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
            DBG_LOGF("%s: update_spool failed - no tag present\n", TAG);
        } else if (spool.blank_tag_present) {
            // Blank tag — enqueue FORMAT_NEW
            const char* requestedId = doc["id"] | "";
            if (requestedId[0] != '\0' && strcmp(requestedId, spool.spool_id) != 0) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
                DBG_LOGF("%s: update_spool (format) failed - ID mismatch\n", TAG);
            } else {
                NFCWriteRequest req;
                memset(&req, 0, sizeof(req));
                req.request_id = ++s_request_id_counter;
                req.type = NFCWriteType::FORMAT_NEW;
                strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                NFCManager::getInstance().enqueueWrite(req);
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                DBG_LOGF("%s: update_spool - enqueued FORMAT_NEW for blank tag\n", TAG);
            }
        } else if (!spool.tag_data_valid) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
            DBG_LOGF("%s: update_spool failed - tag data not valid\n", TAG);
        } else {
            // Verify spool ID matches
            const char* requestedId = doc["id"] | "";
            if (requestedId[0] != '\0' && strcmp(requestedId, spool.spool_id) != 0) {
                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Tag not in range\"}");
                DBG_LOGF("%s: update_spool failed - ID mismatch\n", TAG);
            } else {
                bool queued = false;

                // Check if type changed
                if (!doc["type"].isNull()) {
                    const char* newType = doc["type"] | "";
                    uint8_t newMaterial = materialTypeFromString(newType);
                    uint8_t currentMaterial = 0;
                    opt_get_material_type(&spool.tag_data, &currentMaterial);
                    if (newMaterial != currentMaterial) {
                        NFCWriteRequest req;
                        req.request_id = ++s_request_id_counter;
                        req.type = NFCWriteType::CHANGE_FILAMENT_TYPE;
                        strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                        req.data.new_material_type = newMaterial;
                        NFCManager::getInstance().enqueueWrite(req);
                        queued = true;
                    }
                }

                // Check if color changed
                if (!doc["color"].isNull()) {
                    const char* newColor = doc["color"] | "";
                    uint8_t newRgba[4];
                    if (parseHexColor(newColor, newRgba)) {
                        uint8_t currentColor[4];
                        opt_get_primary_color(&spool.tag_data, currentColor);
                        if (memcmp(newRgba, currentColor, 3) != 0) {
                            NFCWriteRequest req;
                            req.request_id = ++s_request_id_counter;
                            req.type = NFCWriteType::CHANGE_COLOR;
                            strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                            memcpy(req.data.new_color, newRgba, 4);
                            NFCManager::getInstance().enqueueWrite(req);
                            queued = true;
                        }
                    }
                }

                // Check if manufacturer changed
                if (!doc["manufacturer"].isNull()) {
                    const char* newBrand = doc["manufacturer"] | "";
                    char currentBrand[64] = {0};
                    opt_get_brand_name(&spool.tag_data, currentBrand, sizeof(currentBrand));
                    if (strcmp(newBrand, currentBrand) != 0) {
                        NFCWriteRequest req;
                        req.request_id = ++s_request_id_counter;
                        req.type = NFCWriteType::SET_BRAND_NAME;
                        strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                        strncpy(req.data.brand_name, newBrand, sizeof(req.data.brand_name) - 1);
                        req.data.brand_name[sizeof(req.data.brand_name) - 1] = '\0';
                        NFCManager::getInstance().enqueueWrite(req);
                        queued = true;
                    }
                }

                // Check if grams_remaining changed
                if (!doc["grams_remaining"].isNull()) {
                    float newRemaining = doc["grams_remaining"] | 0.0f;
                    float full_weight = 0.0f;
                    float currentConsumed = 0.0f;
                    opt_get_actual_full_weight(&spool.tag_data, &full_weight);
                    opt_get_consumed_weight(&spool.tag_data, &currentConsumed);
                    float currentRemaining = full_weight - currentConsumed;
                    float newConsumed = full_weight - newRemaining;
                    // Only update if difference is significant (>1g)
                    if (abs(newRemaining - currentRemaining) > 1.0f) {
                        NFCWriteRequest req;
                        req.request_id = ++s_request_id_counter;
                        req.type = NFCWriteType::SET_CONSUMED_WEIGHT;
                        strncpy(req.expected_spool_id, spool.spool_id, sizeof(req.expected_spool_id) - 1);
                        req.data.consumed_weight = newConsumed;
                        NFCManager::getInstance().enqueueWrite(req);
                        queued = true;
                    }
                }

                snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                DBG_LOGF("%s: update_spool completed, queued=%d\n", TAG, queued);
            }
        }
    }
    else if (strcmp(command, "test_spoolman") == 0) {
        const char* url = doc["url"] | "";
        if (strlen(url) == 0) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"error\",\"message\":\"Missing url\"}");
        } else if (xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"error\",\"message\":\"Device busy\"}");
        } else {
            WiFiClient client;
            HTTPClient http;
            String testUrl = String(url) + "/api/v1/info";
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
                JsonDocument infoDoc;
                DeserializationError err = deserializeJson(infoDoc, body);
                if (err) {
                    snprintf(s_response_buffer, sizeof(s_response_buffer),
                        "{\"status\":\"error\",\"message\":\"Invalid JSON from server\"}");
                } else if (infoDoc["version"].isNull()) {
                    snprintf(s_response_buffer, sizeof(s_response_buffer),
                        "{\"status\":\"error\",\"message\":\"Missing version in response\"}");
                } else {
                    snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
                }
            }
            DBG_LOGF("%s: test_spoolman %s -> %d\n", TAG, testUrl.c_str(), httpCode);
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
            DBG_LOGF("%s: test_mqtt restart -> %s (state=%d)\n", TAG,
                          connected ? "OK" : "FAIL", mqttState);
        }
    }
    else if (strcmp(command, "test_prusalink") == 0) {
        const char* url = doc["url"] | "";
        const char* apiKey = doc["api_key"] | "";
        if (strlen(url) == 0) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"error\",\"message\":\"Missing url\"}");
        } else if (xSemaphoreTake(g_httpMutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"error\",\"message\":\"Device busy\"}");
        } else {
            WiFiClient client;
            HTTPClient http;
            String testUrl = String(url) + "/api/v1/status";
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
            DBG_LOGF("%s: test_prusalink %s -> %d\n", TAG, testUrl.c_str(), httpCode);
        }
    }
    else if (strcmp(command, "log_meta") == 0) {
        DebugLogBuffer::SnapshotMeta meta = DebugLogBuffer::getInstance().getSnapshotMeta();
        snprintf(s_response_buffer, sizeof(s_response_buffer),
                 "{\"status\":\"ok\",\"oldest_seq\":%lu,\"next_seq\":%lu,\"count\":%u}",
                 static_cast<unsigned long>(meta.oldestSeq),
                 static_cast<unsigned long>(meta.nextSeq),
                 static_cast<unsigned>(meta.count));
    }
    else if (strcmp(command, "log_line") == 0) {
        uint32_t seq = doc["seq"] | 0;
        char line[DebugLogBuffer::MAX_LINE_LENGTH + 1];
        DebugLogBuffer::LookupResult result =
            DebugLogBuffer::getInstance().getLineBySeq(seq, line, sizeof(line));
        DebugLogBuffer::SnapshotMeta meta = DebugLogBuffer::getInstance().getSnapshotMeta();

        if (result == DebugLogBuffer::LookupResult::Ok) {
            if (s_config_read_char) {
                s_config_read_char->setValue(line);
            }
            snprintf(s_response_buffer, sizeof(s_response_buffer),
                     "{\"status\":\"ok\",\"seq\":%lu,\"next_seq\":%lu}",
                     static_cast<unsigned long>(seq),
                     static_cast<unsigned long>(seq + 1));
        } else if (result == DebugLogBuffer::LookupResult::Stale) {
            if (s_config_read_char) {
                s_config_read_char->setValue("");
            }
            snprintf(s_response_buffer, sizeof(s_response_buffer),
                     "{\"status\":\"stale\",\"oldest_seq\":%lu,\"next_seq\":%lu}",
                     static_cast<unsigned long>(meta.oldestSeq),
                     static_cast<unsigned long>(meta.nextSeq));
        } else {
            if (s_config_read_char) {
                s_config_read_char->setValue("");
            }
            snprintf(s_response_buffer, sizeof(s_response_buffer),
                     "{\"status\":\"eof\",\"next_seq\":%lu}",
                     static_cast<unsigned long>(meta.nextSeq));
        }
    }
    else {
        snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Unknown command\"}");
        DBG_LOGF("%s: Unknown command: %s\n", TAG, command);
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
        s_write_buffer = "";
        s_response_buffer[0] = '\0';
        DBG_LOGF("%s: Client connected\n", TAG);
    }

    void onDisconnect(BLEServer* pServer) override {
        s_is_connected = false;
        DBG_LOGF("%s: Client disconnected, restarting advertising\n", TAG);
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
            //DBG_LOGF("%s: Write received, len=%d\n", TAG, value.length());
            process_command(value.c_str());
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
    DBG_LOGF("%s: Initializing Bluetooth manager\n", TAG);

    // Generate device name from MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char device_name[24];
    snprintf(device_name, sizeof(device_name), "OpenPrintTag-%02X%02X", mac[4], mac[5]);
    DBG_LOGF("%s: BLE device name: %s\n", TAG, device_name);

    // Debug: Check controller state
    esp_bt_controller_status_t status = esp_bt_controller_get_status();
    DBG_LOGF("%s: BT controller status: %d (0=IDLE, 1=INITED, 2=ENABLED)\n", TAG, status);

    // Release classic BT memory first
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    DBG_LOGF("%s: mem_release result: %d\n", TAG, ret);

    // Initialize controller if not already
    if (status == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ret = esp_bt_controller_init(&bt_cfg);
        DBG_LOGF("%s: controller_init result: %d (0=OK, 259=INVALID_STATE)\n", TAG, ret);
        if (ret != ESP_OK) {
            return false;
        }
    }

    status = esp_bt_controller_get_status();
    DBG_LOGF("%s: BT controller status after init: %d\n", TAG, status);

    // Enable controller
    if (status == ESP_BT_CONTROLLER_STATUS_INITED) {
        ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        DBG_LOGF("%s: controller_enable result: %d\n", TAG, ret);
        if (ret != ESP_OK) {
            return false;
        }
    }

    DBG_LOGF("%s: BT controller enabled, initializing bluedroid\n", TAG);

    // Initialize BLE stack
    BLEDevice::init(device_name);

    // Create server
    s_server = BLEDevice::createServer();
    if (!s_server) {
        DBG_LOGF("%s: Failed to create BLE server\n", TAG);
        return false;
    }
    s_server->setCallbacks(&s_server_callbacks);

    // Create service
    BLEService* service = s_server->createService(SERVICE_UUID);
    if (!service) {
        DBG_LOGF("%s: Failed to create BLE service\n", TAG);
        return false;
    }

    // Create config read characteristic (READ)
    s_config_read_char = service->createCharacteristic(
        CONFIG_READ_UUID,
        BLECharacteristic::PROPERTY_READ
    );
    if (!s_config_read_char) {
        DBG_LOGF("%s: Failed to create read characteristic\n", TAG);
        return false;
    }

    // Create config write characteristic (READ + WRITE)
    s_config_write_char = service->createCharacteristic(
        CONFIG_WRITE_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    if (!s_config_write_char) {
        DBG_LOGF("%s: Failed to create write characteristic\n", TAG);
        return false;
    }
    s_config_write_char->setCallbacks(&s_write_callbacks);

    // Start service
    service->start();
    DBG_LOGF("%s: Service started\n", TAG);

    // Start advertising
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    s_is_advertising = true;

    DBG_LOGF("%s: Advertising started\n", TAG);
    DBG_LOGF("%s: Initialized successfully\n", TAG);
    return true;
}

void BluetoothManager::end() {
    DBG_LOGF("%s: Shutting down Bluetooth manager\n", TAG);

    // Stop advertising
    if (s_is_advertising) {
        BLEDevice::stopAdvertising();
        s_is_advertising = false;
    }

    // Deinit BLE
    BLEDevice::deinit(true);

    // Reset state
    s_server = nullptr;
    s_config_read_char = nullptr;
    s_config_write_char = nullptr;
    s_is_connected = false;
    s_is_advertising = false;

    DBG_LOGF("%s: Shutdown complete\n", TAG);
}

bool BluetoothManager::isAdvertising() const {
    return s_is_advertising;
}

bool BluetoothManager::isConnected() const {
    return s_is_connected;
}
