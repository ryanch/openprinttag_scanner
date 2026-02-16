#ifndef CONFIGURATION_MANAGER_H
#define CONFIGURATION_MANAGER_H

#include <Arduino.h>
#include <cstdint>

#define DEVICE_VERSION "0.63 BETA"

class ConfigurationManager {
public:
    static ConfigurationManager& getInstance();

    bool begin();  // Initialize NVS and load cached values
    String readConfig();  // Returns JSON (excludes wifi_pass)
    bool postConfigUpdate(const char* json);  // Partial update from JSON

    const char* getWiFiSSID() const;
    const char* getWiFiPassword() const;
    const char* getPrusaLinkURL() const;
    const char* getPrusaLinkAPIKey() const;
    const char* getSpoolmanURL() const;
    uint32_t getPollIntervalMs() const;

private:
    ConfigurationManager() = default;
    ConfigurationManager(const ConfigurationManager&) = delete;
    ConfigurationManager& operator=(const ConfigurationManager&) = delete;

    bool loadFromNVS();
    bool saveToNVS();

    // In-memory cache
    char _ssid[64];
    char _wifiPass[64];
    char _prusaLinkUrl[128];
    char _prusaLinkApiKey[64];
    char _spoolmanUrl[128];
    uint32_t _pollIntervalMs;
    bool _initialized = false;

    static constexpr const char* NVS_NAMESPACE = "opt_config";
};

#endif // CONFIGURATION_MANAGER_H
