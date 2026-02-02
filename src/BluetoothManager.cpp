#include "BluetoothManager.h"
#include "ConfigurationManager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_mac.h"
#include "esp_bt.h"
#include "esp_bt_main.h"

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

/**
 * @brief Process a JSON command from the client
 */
static void process_command(const char* json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.printf("%s: JSON parse error: %s\n", TAG, error.c_str());
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
        Serial.printf("%s: Config read requested, len=%d\n", TAG, config.length());
    }
    else if (strcmp(command, "write_config") == 0) {
        if (ConfigurationManager::getInstance().postConfigUpdate(json)) {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"status\":\"ok\"}");
            Serial.printf("%s: Config updated successfully\n", TAG);
        } else {
            snprintf(s_response_buffer, sizeof(s_response_buffer), "{\"error\":\"Failed to save config\"}");
            Serial.printf("%s: Config update failed\n", TAG);
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
        s_write_buffer = "";
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
            Serial.printf("%s: Write received, len=%d\n", TAG, value.length());
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
    Serial.printf("%s: Initializing Bluetooth manager\n", TAG);

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
