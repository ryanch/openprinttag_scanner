#include "ConfigurationManager.h"
#include <Preferences.h>
#include <ArduinoJson.h>

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
    _pollIntervalMs = 5000;

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

    prefs.end();
    Serial.println("ConfigurationManager: Saved config to NVS");
    return true;
}

String ConfigurationManager::readConfig() {
    JsonDocument doc;

    doc["ssid"] = _ssid;
    // wifi_pass intentionally omitted for security
    doc["prusa_link_url"] = _prusaLinkUrl;
    doc["prusa_link_api_key"] = _prusaLinkApiKey;
    doc["spoolman_url"] = _spoolmanUrl;
    doc["poll_interval_ms"] = _pollIntervalMs;
    doc["device_version"] = DEVICE_VERSION;

    String output;
    serializeJson(doc, output);
    return output;
}

bool ConfigurationManager::postConfigUpdate(const char* json) {
    JsonDocument doc;
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
