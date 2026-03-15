#include "LEDManager.h"

#include <cstdlib>

LEDManager::LEDManager()
    : _initialized(false), _pixel(1, 0, NEO_GRBW + NEO_KHZ800) {}
void LEDManager::begin(uint8_t pin) {
    _pixel.updateType(NEO_GRBW + NEO_KHZ800);    
    _pixel.setPin(pin);
    _pixel.begin();
    _pixel.setBrightness(64);
    _pixel.show();  // clear
    _initialized = true;
}

void LEDManager::setColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!_initialized) return;
    _pixel.setPixelColor(0, _pixel.Color(r, g, b, 0));
    _pixel.show();
}

void LEDManager::showOff() {
    setColor(0, 0, 0);
}

void LEDManager::showBooting() {
    if (!_initialized) return;
    _pixel.setPixelColor(0, _pixel.Color(0, 0, 0, 255));  // pure white via W channel
    _pixel.show();
}

void LEDManager::showWifiConnected() {
    setColor(0, 255, 255);  // cyan
}

void LEDManager::showWifiFailed() {
    setColor(255, 0, 0);
}

void LEDManager::showReady() {
    setColor(0, 0, 255);  // blue
}

void LEDManager::flashTagDetected() {
    if (!_initialized) return;
    setColor(255, 255, 0);  // yellow
    delay(100);
    showOff();
}

void LEDManager::flashParseFailed() {
    if (!_initialized) return;
    setColor(255, 0, 0);  // red
    delay(100);
    showOff();
}

void LEDManager::flashWriteSuccess() {
    if (!_initialized) return;
    setColor(0, 255, 0);  // green
    delay(100);
}

void LEDManager::flashWriteFailure() {
    if (!_initialized) return;
    setColor(255, 0, 0);  // red
    delay(100);
}

void LEDManager::showFilamentColor(uint8_t r, uint8_t g, uint8_t b) {
    setColor(r, g, b);
}

void LEDManager::setFilamentColorFromHex(const char* hex) {
    if (!_initialized || !hex) return;

    if (hex[0] == '#') {
        hex++;
    }

    char* endptr = nullptr;
    unsigned long color = strtoul(hex, &endptr, 16);
    if (endptr == hex) {
        return;
    }

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    showFilamentColor(r, g, b);
}