#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Adafruit_NeoPixel.h>

class LEDManager {
public:
    LEDManager();

    void begin(uint8_t pin);

    void showOff();
    void showBooting();
    void showWifiConnected();
    void showWifiFailed();
    void showReady();

    void flashTagDetected();
    void flashParseFailed();
    void flashWriteSuccess();
    void flashWriteFailure();

    void showFilamentColor(uint8_t r, uint8_t g, uint8_t b);
    void setFilamentColorFromHex(const char* hex);

private:
    bool _initialized;
    Adafruit_NeoPixel _pixel;

    void setColor(uint8_t r, uint8_t g, uint8_t b);
};

#endif