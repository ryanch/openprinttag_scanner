#include "ConfigurationManager.h"
#include "local_settings.h"

ConfigurationManager& ConfigurationManager::getInstance() {
    static ConfigurationManager instance;
    return instance;
}

const char* ConfigurationManager::getWiFiSSID() const {
    return WIFI_SSID;
}

const char* ConfigurationManager::getWiFiPassword() const {
    return WIFI_PASSWORD;
}

const char* ConfigurationManager::getPrusaLinkURL() const {
    return PRUSALINK_URL;
}

const char* ConfigurationManager::getPrusaLinkAPIKey() const {
    return PRUSALINK_API_KEY;
}

uint32_t ConfigurationManager::getPollIntervalMs() const {
    return POLL_INTERVAL_MS;
}
