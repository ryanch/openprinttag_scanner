#include "HomeAssistantManager.h"
#include "ApplicationManager.h"
#include "ConfigurationManager.h"

#ifndef NATIVE_TEST
  #include <Arduino.h>
  #include <WiFi.h>
  #include <ArduinoJson.h>
  #include <esp_heap_caps.h>
  #include "NFCManager.h"
  #include "esp_mac.h"
#else
  #include "platform/NativePlatform.h"
#endif

#include <cstring>

HomeAssistantManager& HomeAssistantManager::getInstance() {
    static HomeAssistantManager instance;
    return instance;
}

void HomeAssistantManager::getDeviceId(char* buf, size_t bufSize) {
    if (bufSize < 7) { buf[0] = '\0'; return; }
#ifndef NATIVE_TEST
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, bufSize, "%02x%02x%02x", mac[3], mac[4], mac[5]);
#else
    strncpy(buf, "aabb12", bufSize);
#endif
}

bool HomeAssistantManager::isConfigured() const {
#ifndef NATIVE_TEST
    auto& config = ConfigurationManager::getInstance();
    return config.getHAEnabled() && strlen(config.getHAMqttHost()) > 0;
#else
    return false;
#endif
}

bool HomeAssistantManager::isConnected() const {
    return connected_;
}

bool HomeAssistantManager::begin() {
    publishQueue = xQueueCreate(QUEUE_SIZE, sizeof(HAPublishRequest));
    if (publishQueue == nullptr) {
        Serial.println("HomeAssistantManager: Failed to create publish queue");
        return false;
    }
#ifndef NATIVE_TEST
    if (taskControlMutex == nullptr) {
        taskControlMutex = xSemaphoreCreateMutex();
        if (taskControlMutex == nullptr) {
            Serial.println("HomeAssistantManager: Failed to create task control mutex");
            return false;
        }
    }
#endif

    getDeviceId(deviceId_, sizeof(deviceId_));
    Serial.printf("HomeAssistantManager: Initialized (device_id=%s)\n", deviceId_);
    return true;
}

bool HomeAssistantManager::enqueuePublish(const HAPublishRequest& req) {
    if (publishQueue == nullptr) return false;
    // Overwrite oldest if full (drop semantics for state-based retained messages)
    return xQueueSend(publishQueue, &req, 0) == pdTRUE;
}

#ifndef NATIVE_TEST

void HomeAssistantManager::startTask() {
    if (taskControlMutex == nullptr) {
        Serial.println("HomeAssistantManager: task control mutex not initialized");
        return;
    }
    if (xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("HomeAssistantManager: startTask mutex timeout");
        return;
    }

    auto& config = ConfigurationManager::getInstance();
    const char* host = config.getHAMqttHost();
    size_t hostLen = strlen(host);
    bool enabled = config.getHAEnabled();

    Serial.printf("HomeAssistantManager: Config snapshot enabled=%s host='%s' host_len=%u port=%u user_set=%s\n",
                  enabled ? "true" : "false",
                  host,
                  static_cast<unsigned>(hostLen),
                  static_cast<unsigned>(config.getHAMqttPort()),
                  strlen(config.getHAMqttUser()) > 0 ? "true" : "false");

    if (!isConfigured()) {
        Serial.printf("HomeAssistantManager: Not configured, skipping task start (enabled=%s, host_len=%u)\n",
                      enabled ? "true" : "false",
                      static_cast<unsigned>(hostLen));
        xSemaphoreGive(taskControlMutex);
        return;
    }

    if (taskHandle != nullptr) {
        Serial.println("HomeAssistantManager: Task already running");
        xSemaphoreGive(taskControlMutex);
        return;
    }

    stopRequested_ = false;
    connected_ = false;
    lastMqttState_ = -1;
    reconnectDelay_ = 1000;
    lastReconnectAttempt_ = 0;

    BaseType_t rc = xTaskCreatePinnedToCore(
        taskFunc,
        "HATask",
        TASK_STACK_SIZE,
        this,
        TASK_PRIORITY,
        &taskHandle,
        1  // Core 1 (align with other network/application tasks)
    );
    if (rc != pdPASS || taskHandle == nullptr) {
        size_t free8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        Serial.printf("HomeAssistantManager: Failed to start task (rc=%ld, free_heap=%u)\n",
                      static_cast<long>(rc),
                      static_cast<unsigned>(free8bit));
        taskHandle = nullptr;
        xSemaphoreGive(taskControlMutex);
        return;
    }

    Serial.printf("HomeAssistantManager: Task started (stack=%u, free_heap=%u)\n",
                  static_cast<unsigned>(TASK_STACK_SIZE),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
    xSemaphoreGive(taskControlMutex);
}

void HomeAssistantManager::taskFunc(void* param) {
    Serial.println("HomeAssistantManager: taskFunc entered");
    static_cast<HomeAssistantManager*>(param)->taskLoop();
}

void HomeAssistantManager::stopTask() {
    if (taskControlMutex == nullptr) return;
    if (xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("HomeAssistantManager: stopTask mutex timeout");
        return;
    }

    if (taskHandle == nullptr) {
        connected_ = false;
        stopRequested_ = false;
        xSemaphoreGive(taskControlMutex);
        return;
    }

    stopRequested_ = true;
    xSemaphoreGive(taskControlMutex);

    uint32_t startMs = millis();
    while (true) {
        if (xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool stopped = (taskHandle == nullptr);
            xSemaphoreGive(taskControlMutex);
            if (stopped) break;
        }
        if (millis() - startMs >= 5000) {
            Serial.println("HomeAssistantManager: stopTask timeout waiting for task exit");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool HomeAssistantManager::restartAndTestConnection(uint32_t timeoutMs, int* mqttStateOut) {
    stopTask();
    startTask();

    if (taskControlMutex != nullptr &&
        xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        bool started = (taskHandle != nullptr);
        xSemaphoreGive(taskControlMutex);
        if (!started) {
            if (mqttStateOut != nullptr) *mqttStateOut = getLastMqttState();
            return false;
        }
    }

    uint32_t startMs = millis();
    while (millis() - startMs < timeoutMs) {
        if (isConnected()) {
            if (mqttStateOut != nullptr) *mqttStateOut = 0;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (mqttStateOut != nullptr) {
        *mqttStateOut = getLastMqttState();
    }
    return false;
}

void HomeAssistantManager::taskLoop() {
    Serial.printf("HomeAssistantManager: taskLoop entered (core=%d, wifi_status=%d)\n",
                  xPortGetCoreID(), static_cast<int>(WiFi.status()));
    auto& config = ConfigurationManager::getInstance();

    mqttClient.setClient(wifiClient);
    mqttClient.setServer(config.getHAMqttHost(), config.getHAMqttPort());
    mqttClient.setBufferSize(512);
    mqttClient.setCallback(mqttCallback);

    Serial.printf("HomeAssistantManager: Connecting to MQTT broker %s:%d\n",
                  config.getHAMqttHost(), config.getHAMqttPort());

    uint32_t lastHeartbeatMs = 0;
    while (true) {
        if (stopRequested_) {
            Serial.println("HomeAssistantManager: Stop requested");
            break;
        }

        uint32_t now = millis();
        if (now - lastHeartbeatMs >= 5000) {
            lastHeartbeatMs = now;
            Serial.printf("HomeAssistantManager: heartbeat connected=%s wifi=%d mqtt_state=%d stack_hw=%u\n",
                          mqttClient.connected() ? "true" : "false",
                          static_cast<int>(WiFi.status()),
                          mqttClient.state(),
                          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        }

        if (!mqttClient.connected()) {
            if (now - lastReconnectAttempt_ >= reconnectDelay_) {
                lastReconnectAttempt_ = now;
                if (reconnect()) {
                    connected_ = true;
                    lastMqttState_ = 0;
                    reconnectDelay_ = 1000; // Reset backoff
                    Serial.println("HomeAssistantManager: MQTT connected, publishing discovery/state");
                    publishDiscovery();
                    subscribeCommands();
                    publishAvailability("online");
                    Serial.println("HomeAssistantManager: Connected to MQTT broker");
                } else {
                    connected_ = false;
                    // Exponential backoff
                    reconnectDelay_ = (reconnectDelay_ < MAX_RECONNECT_DELAY)
                        ? reconnectDelay_ * 2 : MAX_RECONNECT_DELAY;
                    lastMqttState_ = mqttClient.state();
                    Serial.printf("HomeAssistantManager: MQTT connect failed, retry in %lums\n",
                                  reconnectDelay_);
                }
            }
        }

        // Drain publish queue
        HAPublishRequest req;
        while (xQueueReceive(publishQueue, &req, 0) == pdTRUE) {
            if (mqttClient.connected()) {
                mqttClient.publish(req.topic, req.payload, req.retained);
            }
        }

        if (mqttClient.connected()) {
            mqttClient.loop();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (mqttClient.connected()) {
        publishAvailability("offline");
        mqttClient.disconnect();
    }
    connected_ = false;

    if (taskControlMutex != nullptr &&
        xSemaphoreTake(taskControlMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        taskHandle = nullptr;
        stopRequested_ = false;
        xSemaphoreGive(taskControlMutex);
    } else {
        taskHandle = nullptr;
        stopRequested_ = false;
    }
    Serial.println("HomeAssistantManager: taskLoop exiting");
    vTaskDelete(nullptr);
}

bool HomeAssistantManager::reconnect() {
    auto& config = ConfigurationManager::getInstance();

    // Build client ID
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "openprinttag_%s", deviceId_);

    // Build LWT topic
    char lwtTopic[64];
    snprintf(lwtTopic, sizeof(lwtTopic), "openprinttag/%s/availability", deviceId_);

    bool result;
    if (strlen(config.getHAMqttUser()) > 0) {
        result = mqttClient.connect(clientId,
                                     config.getHAMqttUser(),
                                     config.getHAMqttPass(),
                                     lwtTopic, 0, true, "offline");
    } else {
        result = mqttClient.connect(clientId, lwtTopic, 0, true, "offline");
    }

    if (!result) {
        lastMqttState_ = mqttClient.state();
        Serial.printf("HomeAssistantManager: reconnect failed (mqtt_state=%d wifi_status=%d host=%s port=%u)\n",
                      mqttClient.state(),
                      static_cast<int>(WiFi.status()),
                      config.getHAMqttHost(),
                      static_cast<unsigned>(config.getHAMqttPort()));
    }
    return result;
}

void HomeAssistantManager::publishAvailability(const char* state) {
    char topic[64];
    snprintf(topic, sizeof(topic), "openprinttag/%s/availability", deviceId_);
    mqttClient.publish(topic, state, true);
}

void HomeAssistantManager::subscribeCommands() {
    char topic[64];
    snprintf(topic, sizeof(topic), "openprinttag/%s/cmd/#", deviceId_);
    mqttClient.subscribe(topic);
    Serial.printf("HomeAssistantManager: Subscribed to %s\n", topic);
}

void HomeAssistantManager::publishDiscovery() {
    // Discovery payloads use abbreviated HA keys to fit in 512-byte buffer
    char baseTopic[48];
    snprintf(baseTopic, sizeof(baseTopic), "openprinttag/%s", deviceId_);

    // Device block (shared across entities, abbreviated after first)
    // First entity includes full device info
    JsonDocument doc;

    auto publishEntity = [&](const char* component, const char* objectId,
                             const char* name, const char* valTpl,
                             const char* stateTopic,
                             const char* devCla, const char* unitOfMeas,
                             const char* icon, bool isFirstEntity) {
        doc.clear();
        doc["~"] = baseTopic;
        doc["name"] = name;

        char uniqueId[64];
        snprintf(uniqueId, sizeof(uniqueId), "openprinttag_%s_%s", deviceId_, objectId);
        doc["unique_id"] = uniqueId;
        doc["stat_t"] = stateTopic;
        doc["val_tpl"] = valTpl;
        doc["avty_t"] = "~/availability";

        if (devCla && devCla[0]) doc["dev_cla"] = devCla;
        if (unitOfMeas && unitOfMeas[0]) doc["unit_of_meas"] = unitOfMeas;
        if (icon && icon[0]) doc["ic"] = icon;

        JsonObject dev = doc["dev"].to<JsonObject>();
        dev["ids"].to<JsonArray>().add(String("openprinttag_") + deviceId_);
        if (isFirstEntity) {
            dev["name"] = "OpenPrintTag Scanner";
            dev["mf"] = "OpenPrintTag";
            dev["sw"] = DEVICE_VERSION;
        }

        char discoveryTopic[128];
        snprintf(discoveryTopic, sizeof(discoveryTopic),
                 "homeassistant/%s/openprinttag_%s/%s/config",
                 component, deviceId_, objectId);

        char payload[512];
        serializeJson(doc, payload, sizeof(payload));
        bool ok = mqttClient.publish(discoveryTopic, payload, true);
        Serial.printf("HomeAssistantManager: Discovery publish %s -> %s\n",
                      discoveryTopic, ok ? "OK" : "FAIL");
    };

    // Binary Sensor: Tag Present
    publishEntity("binary_sensor", "tag_present", "Tag Present",
                  "{{ 'ON' if value_json.present else 'OFF' }}",
                  "~/tag/state", "occupancy", "", "", true);

    // Sensor: Spool UID
    publishEntity("sensor", "spool_uid", "Spool UID",
                  "{{ value_json.uid | default('') }}",
                  "~/tag/state", "", "", "mdi:nfc-variant", false);

    // Sensor: Remaining Weight
    publishEntity("sensor", "remaining_weight", "Remaining Filament",
                  "{{ value_json.remaining_g | default(0) }}",
                  "~/tag/state", "weight", "g", "mdi:weight-gram", false);

    // Sensor: Material Type
    publishEntity("sensor", "material_type", "Material Type",
                  "{{ value_json.material_type | default('') }}",
                  "~/tag/state", "", "", "mdi:printer-3d-nozzle", false);

    // Sensor: Color
    publishEntity("sensor", "color", "Filament Color",
                  "{{ value_json.color | default('') }}",
                  "~/tag/state", "", "", "mdi:palette", false);

    // Sensor: Printer State
    publishEntity("sensor", "printer_state", "Printer State",
                  "{{ value_json.state | default('unknown') }}",
                  "~/printer/state", "", "", "mdi:printer-3d", false);

    Serial.println("HomeAssistantManager: Discovery payloads published");
}

// Static callback - routes to instance
void HomeAssistantManager::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    // Null-terminate payload
    char buf[384];
    size_t copyLen = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
    memcpy(buf, payload, copyLen);
    buf[copyLen] = '\0';

    getInstance().handleCommand(topic, buf);
}

void HomeAssistantManager::handleCommand(const char* topic, const char* payload) {
    Serial.printf("HomeAssistantManager: Command received: %s\n", topic);

    // Parse topic to extract command name
    // Format: openprinttag/{id}/cmd/{command}
    char cmdPrefix[64];
    snprintf(cmdPrefix, sizeof(cmdPrefix), "openprinttag/%s/cmd/", deviceId_);

    if (strncmp(topic, cmdPrefix, strlen(cmdPrefix)) != 0) {
        Serial.println("HomeAssistantManager: Unknown topic prefix");
        return;
    }
    const char* command = topic + strlen(cmdPrefix);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("HomeAssistantManager: JSON parse error: %s\n", err.c_str());
        publishCommandResponse(command, false, "invalid_json");
        return;
    }

    // Validate UID against current tag
    const char* uid = doc["uid"] | "";
    if (strlen(uid) == 0) {
        publishCommandResponse(command, false, "missing_uid");
        return;
    }

    CurrentSpoolState spool;
    if (!NFCManager::getInstance().getCurrentSpoolState(spool) || !spool.present) {
        publishCommandResponse(command, false, "no_tag_present");
        return;
    }

    if (strcmp(uid, spool.spool_id) != 0) {
        // UID mismatch — include expected/actual in response
        char errPayload[256];
        snprintf(errPayload, sizeof(errPayload),
                 "{\"command\":\"%s\",\"success\":false,\"error\":\"uid_mismatch\","
                 "\"expected\":\"%s\",\"actual\":\"%s\"}",
                 command, uid, spool.spool_id);
        char respTopic[64];
        snprintf(respTopic, sizeof(respTopic), "openprinttag/%s/cmd/response", deviceId_);
        mqttClient.publish(respTopic, errPayload, false);
        return;
    }

    if (strcmp(command, "write_tag") == 0) {
        AppMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = AppMessageType::HA_WRITE_TAG;
        strncpy(msg.payload.haWriteTag.expected_uid, uid,
                sizeof(msg.payload.haWriteTag.expected_uid) - 1);

        // Parse material type string
        const char* filamentType = doc["filament_type"] | "PLA";
        msg.payload.haWriteTag.material_type = materialTypeFromString(filamentType);

        // Parse color (#RRGGBB)
        const char* color = doc["color"] | "#FFFFFF";
        parseHexColor(color, msg.payload.haWriteTag.color);

        // Manufacturer
        const char* mfr = doc["manufacturer"] | "";
        strncpy(msg.payload.haWriteTag.manufacturer, mfr,
                sizeof(msg.payload.haWriteTag.manufacturer) - 1);

        msg.payload.haWriteTag.initial_weight_g = doc["initial_weight_g"] | 1000.0f;
        msg.payload.haWriteTag.remaining_g = doc["remaining_g"] | 1000.0f;
        msg.payload.haWriteTag.spoolman_id = doc["spoolman_id"] | -1;

        ApplicationManager::getInstance().sendMessage(msg);
        publishCommandResponse(command, true, nullptr);

    } else if (strcmp(command, "update_remaining") == 0) {
        float remainingG = doc["remaining_g"] | -1.0f;
        if (remainingG < 0) {
            publishCommandResponse(command, false, "missing_remaining_g");
            return;
        }

        // Compute consumed weight from current tag data
        float fullWeight = 0.0f;
        if (spool.tag_data_valid) {
            opt_get_actual_full_weight(&spool.tag_data, &fullWeight);
        }
        float consumed = fullWeight - remainingG;
        if (consumed < 0) consumed = 0;

        AppMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = AppMessageType::HA_UPDATE_REMAINING;
        strncpy(msg.payload.haUpdateRemaining.expected_uid, uid,
                sizeof(msg.payload.haUpdateRemaining.expected_uid) - 1);
        msg.payload.haUpdateRemaining.remaining_g = consumed; // Actually consumed weight

        ApplicationManager::getInstance().sendMessage(msg);
        publishCommandResponse(command, true, nullptr);

    } else {
        publishCommandResponse(command, false, "unknown_command");
    }
}

void HomeAssistantManager::publishCommandResponse(const char* command, bool success, const char* error) {
    char respTopic[64];
    snprintf(respTopic, sizeof(respTopic), "openprinttag/%s/cmd/response", deviceId_);

    char respPayload[128];
    if (success) {
        snprintf(respPayload, sizeof(respPayload),
                 "{\"command\":\"%s\",\"success\":true}", command);
    } else {
        snprintf(respPayload, sizeof(respPayload),
                 "{\"command\":\"%s\",\"success\":false,\"error\":\"%s\"}",
                 command, error ? error : "unknown");
    }
    mqttClient.publish(respTopic, respPayload, false);
}

// Helper functions shared with BluetoothManager (duplicated here to avoid cross-dependency)
uint8_t HomeAssistantManager::materialTypeFromString(const char* type) {
    if (strcmp(type, "PLA") == 0) return OPT_MATERIAL_TYPE_PLA;
    if (strcmp(type, "PETG") == 0) return OPT_MATERIAL_TYPE_PETG;
    if (strcmp(type, "ABS") == 0) return OPT_MATERIAL_TYPE_ABS;
    if (strcmp(type, "ASA") == 0) return OPT_MATERIAL_TYPE_ASA;
    if (strcmp(type, "TPU") == 0) return OPT_MATERIAL_TYPE_TPU;
    if (strcmp(type, "PC") == 0) return OPT_MATERIAL_TYPE_PC;
    if (strcmp(type, "Nylon") == 0) return OPT_MATERIAL_TYPE_PA6;
    if (strcmp(type, "PVA") == 0) return OPT_MATERIAL_TYPE_PVA;
    if (strcmp(type, "HIPS") == 0) return OPT_MATERIAL_TYPE_HIPS;
    return OPT_MATERIAL_TYPE_PLA;
}

const char* HomeAssistantManager::materialTypeToString(uint8_t type) {
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

bool HomeAssistantManager::parseHexColor(const char* hex, uint8_t* rgba) {
    if (hex[0] != '#' || strlen(hex) != 7) {
        rgba[0] = rgba[1] = rgba[2] = 255;
        rgba[3] = 255;
        return false;
    }
    unsigned int r, g, b;
    if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) != 3) {
        rgba[0] = rgba[1] = rgba[2] = 255;
        rgba[3] = 255;
        return false;
    }
    rgba[0] = r;
    rgba[1] = g;
    rgba[2] = b;
    rgba[3] = 255;
    return true;
}

#endif // !NATIVE_TEST

int HomeAssistantManager::getLastMqttState() const {
#ifndef NATIVE_TEST
    return lastMqttState_;
#else
    return -1;
#endif
}

#ifdef NATIVE_TEST
void HomeAssistantManager::stopTask() {
}

bool HomeAssistantManager::restartAndTestConnection(uint32_t timeoutMs, int* mqttStateOut) {
    (void)timeoutMs;
    if (mqttStateOut != nullptr) *mqttStateOut = -1;
    return false;
}
#endif
