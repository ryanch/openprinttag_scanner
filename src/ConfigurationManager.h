#ifndef CONFIGURATION_MANAGER_H
#define CONFIGURATION_MANAGER_H

#include <cstdint>

class ConfigurationManager {
public:
    static ConfigurationManager& getInstance();

    const char* getWiFiSSID() const;
    const char* getWiFiPassword() const;
    const char* getPrusaLinkURL() const;
    const char* getPrusaLinkAPIKey() const;
    uint32_t getPollIntervalMs() const;

private:
    ConfigurationManager() = default;
    ConfigurationManager(const ConfigurationManager&) = delete;
    ConfigurationManager& operator=(const ConfigurationManager&) = delete;
};

#endif // CONFIGURATION_MANAGER_H
