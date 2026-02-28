#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// JSON capacity for configuration (WiFi, URLs, API keys, MQTT settings)
static constexpr size_t JSON_CONFIG_CAPACITY = 768;

ConfigurationManager& ConfigurationManager::getInstance() {
    static ConfigurationManager instance;
    return instance;
}

bool ConfigurationManager::begin() {
    if (_initialized) {
        return true;
    }

    // Start with empty defaults
    memset(_ssid, 0, sizeof(_ssid));
    memset(_wifiPass, 0, sizeof(_wifiPass));
    memset(_prusaLinkUrl, 0, sizeof(_prusaLinkUrl));
    memset(_prusaLinkApiKey, 0, sizeof(_prusaLinkApiKey));
    memset(_spoolmanUrl, 0, sizeof(_spoolmanUrl));
    _pollIntervalMs = 10000;
    _lcdTimeoutMs = 15 * 60 * 1000; // 15 mins
    _haEnabled = false;
    memset(_haMqttHost, 0, sizeof(_haMqttHost));
    _haMqttPort = 1883;
    memset(_haMqttUser, 0, sizeof(_haMqttUser));
    memset(_haMqttPass, 0, sizeof(_haMqttPass));
    _automationMode = 0;

    // Load from NVS if configured
    loadFromNVS();

    _initialized = true;
    Serial.println("ConfigurationManager: Initialized");
    return true;
}

bool ConfigurationManager::loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {  // Read-only mode
        Serial.println("ConfigurationManager: No NVS namespace found, using defaults");
        return false;
    }

    if (prefs.isKey("ssid")) {
        prefs.getString("ssid", _ssid, sizeof(_ssid));
    }
    if (prefs.isKey("wifi_pass")) {
        prefs.getString("wifi_pass", _wifiPass, sizeof(_wifiPass));
    }
    if (prefs.isKey("prusa_url")) {
        prefs.getString("prusa_url", _prusaLinkUrl, sizeof(_prusaLinkUrl));
    }
    if (prefs.isKey("prusa_key")) {
        prefs.getString("prusa_key", _prusaLinkApiKey, sizeof(_prusaLinkApiKey));
    }
    if (prefs.isKey("spoolman_url")) {
        prefs.getString("spoolman_url", _spoolmanUrl, sizeof(_spoolmanUrl));
    }
    if (prefs.isKey("poll_ms")) {
        _pollIntervalMs = prefs.getUInt("poll_ms", _pollIntervalMs);
    }
    if (prefs.isKey("lcd_to_ms")) {
        _lcdTimeoutMs = prefs.getUInt("lcd_to_ms", _lcdTimeoutMs);
    }
    if (prefs.isKey("ha_enabled")) {
        _haEnabled = prefs.getBool("ha_enabled", false);
    }
    if (prefs.isKey("ha_mqtt_host")) {
        prefs.getString("ha_mqtt_host", _haMqttHost, sizeof(_haMqttHost));
    }
    if (prefs.isKey("ha_mqtt_port")) {
        _haMqttPort = prefs.getUShort("ha_mqtt_port", 1883);
    }
    if (prefs.isKey("ha_mqtt_user")) {
        prefs.getString("ha_mqtt_user", _haMqttUser, sizeof(_haMqttUser));
    }
    if (prefs.isKey("ha_mqtt_pass")) {
        prefs.getString("ha_mqtt_pass", _haMqttPass, sizeof(_haMqttPass));
    }
    if (prefs.isKey("auto_mode")) {
        _automationMode = prefs.getUChar("auto_mode", 0);
    }

    prefs.end();
    Serial.println("ConfigurationManager: Loaded config from NVS");
    return true;
}

bool ConfigurationManager::saveToNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {  // Read-write mode
        Serial.println("ConfigurationManager: Failed to open NVS for writing");
        return false;
    }

    prefs.putString("ssid", _ssid);
    prefs.putString("wifi_pass", _wifiPass);
    prefs.putString("prusa_url", _prusaLinkUrl);
    prefs.putString("prusa_key", _prusaLinkApiKey);
    prefs.putString("spoolman_url", _spoolmanUrl);
    prefs.putUInt("poll_ms", _pollIntervalMs);
    prefs.putUInt("lcd_to_ms", _lcdTimeoutMs);
    prefs.putBool("ha_enabled", _haEnabled);
    prefs.putString("ha_mqtt_host", _haMqttHost);
    prefs.putUShort("ha_mqtt_port", _haMqttPort);
    prefs.putString("ha_mqtt_user", _haMqttUser);
    prefs.putString("ha_mqtt_pass", _haMqttPass);
    prefs.putUChar("auto_mode", _automationMode);

    prefs.end();
    Serial.println("ConfigurationManager: Saved config to NVS");
    return true;
}

size_t ConfigurationManager::readConfig(char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return 0;
    }

    StaticJsonDocument<JSON_CONFIG_CAPACITY> doc;

    doc["ssid"] = _ssid;
    // wifi_pass intentionally omitted for security
    doc["prusa_link_url"] = _prusaLinkUrl;
    doc["prusa_link_api_key"] = _prusaLinkApiKey;
    doc["spoolman_url"] = _spoolmanUrl;
    doc["poll_interval_ms"] = _pollIntervalMs;
    doc["lcd_timeout_ms"] = _lcdTimeoutMs;
    doc["ha_enabled"] = _haEnabled;
    doc["ha_mqtt_host"] = _haMqttHost;
    doc["ha_mqtt_port"] = _haMqttPort;
    doc["ha_mqtt_user"] = _haMqttUser;
    // ha_mqtt_pass intentionally omitted for security
    doc["automation_mode"] = _automationMode;
    doc["device_version"] = DEVICE_VERSION;

    size_t written = serializeJson(doc, out, outSize);
    if (written >= outSize) {
        out[outSize - 1] = '\0';
    }
    return written;
}

bool ConfigurationManager::postConfigUpdate(const char* json) {
    StaticJsonDocument<JSON_CONFIG_CAPACITY> doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.print("ConfigurationManager: JSON parse error: ");
        Serial.println(error.c_str());
        return false;
    }

    // Update only fields that are present (ignore "command" field)
    if (doc["ssid"].is<const char*>()) {
        strncpy(_ssid, doc["ssid"].as<const char*>(), sizeof(_ssid) - 1);
        _ssid[sizeof(_ssid) - 1] = '\0';
    }
    if (doc["wifi_pass"].is<const char*>()) {
        strncpy(_wifiPass, doc["wifi_pass"].as<const char*>(), sizeof(_wifiPass) - 1);
        _wifiPass[sizeof(_wifiPass) - 1] = '\0';
    }
    if (doc["prusa_link_url"].is<const char*>()) {
        strncpy(_prusaLinkUrl, doc["prusa_link_url"].as<const char*>(), sizeof(_prusaLinkUrl) - 1);
        _prusaLinkUrl[sizeof(_prusaLinkUrl) - 1] = '\0';
    }
    if (doc["prusa_link_api_key"].is<const char*>()) {
        strncpy(_prusaLinkApiKey, doc["prusa_link_api_key"].as<const char*>(), sizeof(_prusaLinkApiKey) - 1);
        _prusaLinkApiKey[sizeof(_prusaLinkApiKey) - 1] = '\0';
    }
    if (doc["spoolman_url"].is<const char*>()) {
        strncpy(_spoolmanUrl, doc["spoolman_url"].as<const char*>(), sizeof(_spoolmanUrl) - 1);
        _spoolmanUrl[sizeof(_spoolmanUrl) - 1] = '\0';
    }
    if (doc["poll_interval_ms"].is<uint32_t>()) {
        _pollIntervalMs = doc["poll_interval_ms"].as<uint32_t>();
    }
    if (doc["lcd_timeout_ms"].is<uint32_t>()) {
        _lcdTimeoutMs = doc["lcd_timeout_ms"].as<uint32_t>();
    }
    if (doc["ha_enabled"].is<bool>()) {
        _haEnabled = doc["ha_enabled"].as<bool>();
    }
    if (doc["ha_mqtt_host"].is<const char*>()) {
        strncpy(_haMqttHost, doc["ha_mqtt_host"].as<const char*>(), sizeof(_haMqttHost) - 1);
        _haMqttHost[sizeof(_haMqttHost) - 1] = '\0';
    }
    if (doc["ha_mqtt_port"].is<uint16_t>()) {
        _haMqttPort = doc["ha_mqtt_port"].as<uint16_t>();
    }
    if (doc["ha_mqtt_user"].is<const char*>()) {
        strncpy(_haMqttUser, doc["ha_mqtt_user"].as<const char*>(), sizeof(_haMqttUser) - 1);
        _haMqttUser[sizeof(_haMqttUser) - 1] = '\0';
    }
    if (doc["ha_mqtt_pass"].is<const char*>()) {
        strncpy(_haMqttPass, doc["ha_mqtt_pass"].as<const char*>(), sizeof(_haMqttPass) - 1);
        _haMqttPass[sizeof(_haMqttPass) - 1] = '\0';
    }
    if (doc["automation_mode"].is<uint8_t>()) {
        _automationMode = doc["automation_mode"].as<uint8_t>();
        // Notify ApplicationManager immediately so mode change takes effect without reboot
        ApplicationManager::getInstance().setAutomationMode(static_cast<AutomationMode>(_automationMode));
        Serial.printf("ConfigurationManager: Automation mode updated to %s\n",
                      _automationMode == 0 ? "SELF_DIRECTED" : "CONTROLLED_BY_HA");
    }

    return saveToNVS();
}

const char* ConfigurationManager::getWiFiSSID() const {
    return _ssid;
}

const char* ConfigurationManager::getWiFiPassword() const {
    return _wifiPass;
}

const char* ConfigurationManager::getPrusaLinkURL() const {
    return _prusaLinkUrl;
}

const char* ConfigurationManager::getPrusaLinkAPIKey() const {
    return _prusaLinkApiKey;
}

const char* ConfigurationManager::getSpoolmanURL() const {
    return _spoolmanUrl;
}

uint32_t ConfigurationManager::getPollIntervalMs() const {
    return _pollIntervalMs;
}

uint32_t ConfigurationManager::getLcdTimeoutMs() const {
    return _lcdTimeoutMs;
}

bool ConfigurationManager::getHAEnabled() const {
    return _haEnabled;
}

const char* ConfigurationManager::getHAMqttHost() const {
    return _haMqttHost;
}

uint16_t ConfigurationManager::getHAMqttPort() const {
    return _haMqttPort;
}

const char* ConfigurationManager::getHAMqttUser() const {
    return _haMqttUser;
}

const char* ConfigurationManager::getHAMqttPass() const {
    return _haMqttPass;
}

uint8_t ConfigurationManager::getAutomationMode() const {
    return _automationMode;
}
