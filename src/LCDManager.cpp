#include "LCDManager.h"
#include <Arduino.h>
#include <cstring>

LCDManager::LCDManager(uint8_t lcd_Addr, uint8_t lcd_cols, uint8_t lcd_rows)
    : _lcd(lcd_Addr, lcd_cols, lcd_rows), _lcd_cols(lcd_cols), _messageQueue(nullptr), _taskHandle(nullptr),
      _lastChangeTime(0), _screenOff(false) {
}

void LCDManager::begin() {
    _lcd.init();
    _lcd.backlight();
    _lcd.clear();
    _messageQueue = xQueueCreate(8, sizeof(ScreenMessage));
    _lastChangeTime = millis();
}

void LCDManager::updateScreen(const std::string& line1, const std::string& line2) {
    ScreenMessage msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.line1, line1.c_str(), _lcd_cols);
    strncpy(msg.line2, line2.c_str(), _lcd_cols);
    xQueueSend(_messageQueue, &msg, 0);
}

void LCDManager::startTask() {
    xTaskCreatePinnedToCore(
        taskFunc,
        "LCDTask",
        2048,
        this,
        1,
        &_taskHandle,
        0  // Run on core 0
    );
    Serial.println("LCDManager: Task started on core 0");
}

void LCDManager::taskFunc(void* param) {
    LCDManager* self = static_cast<LCDManager*>(param);
    self->taskLoop();
}

void LCDManager::taskLoop() {
    while (true) {
        processQueue();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void LCDManager::processQueue() {
    ScreenMessage msg;
    if (xQueueReceive(_messageQueue, &msg, 0) == pdTRUE) {
        std::string line1(msg.line1);
        std::string line2(msg.line2);
        bool changed = false;

        if (line1 != _currentLine1) {
            _currentLine1 = line1;
            _lcd.setCursor(0, 0);
            _lcd.print(_currentLine1.c_str());
            for (size_t i = _currentLine1.length(); i < _lcd_cols; ++i) {
                _lcd.print(" ");
            }
            changed = true;
        }

        if (line2 != _currentLine2) {
            _currentLine2 = line2;
            _lcd.setCursor(0, 1);
            _lcd.print(_currentLine2.c_str());
            for (size_t i = _currentLine2.length(); i < _lcd_cols; ++i) {
                _lcd.print(" ");
            }
            changed = true;
        }

        if (changed) {
            _lastChangeTime = millis();
            if (_screenOff) {
                _lcd.backlight();
                _screenOff = false;
            }
        }
    }

    // Turn off screen after timeout
    if (!_screenOff && (millis() - _lastChangeTime >= SCREEN_TIMEOUT_MS)) {
        _lcd.noBacklight();
        _screenOff = true;
    }
}
