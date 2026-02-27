#include "DebugLogBuffer.h"
#include "LCDManager.h"
#include <Arduino.h>
#include <cstring>
#include <algorithm>

LCDManager::LCDManager(uint8_t lcd_Addr, uint8_t lcd_cols, uint8_t lcd_rows)
    : _lcd(lcd_Addr, lcd_cols, lcd_rows), _lcd_cols(lcd_cols), _messageQueue(nullptr), _taskHandle(nullptr),
      _activeLineCount(2), _currentPage(0), _lastPageSwitchTimeMs(0), _lastChangeTime(0),
      _screenOff(false), _screenTimeoutMs(DEFAULT_SCREEN_TIMEOUT_MS), _stateMux(portMUX_INITIALIZER_UNLOCKED) {}

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
    size_t copyLen = std::min(static_cast<size_t>(_lcd_cols), sizeof(msg.line1) - 1);
    auto copyLine = [copyLen](char* dst, const std::string& src) {
        strncpy(dst, src.c_str(), copyLen);
        dst[copyLen] = '\0';
    };

    unsigned long nowMs = millis();
    LCDDisplayMessage displayMsg;
    taskENTER_CRITICAL(&_stateMux);
    displayMsg = _displayLogic.prepareTwoLineMessage(line1, line2, nowMs);
    taskEXIT_CRITICAL(&_stateMux);

    copyLine(msg.line1, displayMsg.line1);
    copyLine(msg.line2, displayMsg.line2);
    copyLine(msg.line3, displayMsg.line3);
    copyLine(msg.line4, displayMsg.line4);
    msg.lineCount = displayMsg.lineCount;

    xQueueSend(_messageQueue, &msg, 0);
}

void LCDManager::updateScreen(const std::string& line1, const std::string& line2, const std::string& line3, const std::string& line4) {
    ScreenMessage msg;
    memset(&msg, 0, sizeof(msg));
    size_t copyLen = std::min(static_cast<size_t>(_lcd_cols), sizeof(msg.line1) - 1);
    auto copyLine = [copyLen](char* dst, const std::string& src) {
        strncpy(dst, src.c_str(), copyLen);
        dst[copyLen] = '\0';
    };

    LCDDisplayMessage displayMsg;
    taskENTER_CRITICAL(&_stateMux);
    displayMsg = _displayLogic.prepareFourLineMessage(line1, line2, line3, line4);
    taskEXIT_CRITICAL(&_stateMux);

    copyLine(msg.line1, displayMsg.line1);
    copyLine(msg.line2, displayMsg.line2);
    copyLine(msg.line3, displayMsg.line3);
    copyLine(msg.line4, displayMsg.line4);
    msg.lineCount = displayMsg.lineCount;

    xQueueSend(_messageQueue, &msg, 0);
}

void LCDManager::setScreenTimeoutMs(uint32_t timeoutMs) {
    taskENTER_CRITICAL(&_stateMux);
    _screenTimeoutMs = timeoutMs;
    _lastChangeTime = millis();
    bool wasScreenOff = _screenOff;
    _screenOff = false;
    taskEXIT_CRITICAL(&_stateMux);

    // If the display was off, wake it after timeout changes.
    if (wasScreenOff) {
        _lcd.backlight();
    }
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
    DBG_LOGLN("LCDManager: Task started on core 0");
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
    auto renderLines = [this](const std::string& line1, const std::string& line2) {
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

        return changed;
    };

    auto markScreenActivity = [this]() {
        bool wakeScreen = false;
        taskENTER_CRITICAL(&_stateMux);
        _lastChangeTime = millis();
        if (_screenOff) {
            _screenOff = false;
            wakeScreen = true;
        }
        taskEXIT_CRITICAL(&_stateMux);

        if (wakeScreen) {
            _lcd.backlight();
        }
    };

    ScreenMessage msg;
    if (xQueueReceive(_messageQueue, &msg, 0) == pdTRUE) {
        uint8_t previousActiveLineCount = _activeLineCount;
        std::string previousLine1 = _activeLine1;
        std::string previousLine2 = _activeLine2;
        std::string previousLine3 = _activeLine3;
        std::string previousLine4 = _activeLine4;

        _activeLine1 = std::string(msg.line1);
        _activeLine2 = std::string(msg.line2);
        _activeLine3 = std::string(msg.line3);
        _activeLine4 = std::string(msg.line4);
        _activeLineCount = (msg.lineCount == 4) ? 4 : 2;
        _currentPage = 0;
        _lastPageSwitchTimeMs = millis();

        bool changed = renderLines(_activeLine1, _activeLine2);

        // Check if this is a new message (any line content or line count changed)
        bool isNewMessage = (previousLine1 != _activeLine1 ||
                            previousLine2 != _activeLine2 ||
                            previousLine3 != _activeLine3 ||
                            previousLine4 != _activeLine4 ||
                            previousActiveLineCount != _activeLineCount);

        if (isNewMessage) {
            markScreenActivity();
        }

        if (changed || previousActiveLineCount != _activeLineCount) {
            unsigned long nowMs = millis();
            LCDDisplayMessage displayedMsg;
            displayedMsg.line1 = msg.line1;
            displayedMsg.line2 = msg.line2;
            displayedMsg.line3 = msg.line3;
            displayedMsg.line4 = msg.line4;
            displayedMsg.lineCount = _activeLineCount;
            taskENTER_CRITICAL(&_stateMux);
            _displayLogic.noteDisplayedMessage(displayedMsg, nowMs);
            taskEXIT_CRITICAL(&_stateMux);
        }
    }

    bool pageSwitched = false;
    if (_activeLineCount == 4 && (millis() - _lastPageSwitchTimeMs >= FOUR_LINE_PAGE_DURATION_MS)) {
        _currentPage = (_currentPage == 0) ? 1 : 0;
        _lastPageSwitchTimeMs = millis();
        pageSwitched = true;
    }

    if (pageSwitched) {
        if (_currentPage == 0) {
            renderLines(_activeLine1, _activeLine2);
        } else {
            renderLines(_activeLine3, _activeLine4);
        }
        // Don't reset screen timeout during page flips - only when new messages arrive
    }

    // Turn off screen after timeout. A timeout of 0 disables auto-off.
    uint32_t timeoutMs = 0;
    unsigned long lastChangeTime = 0;
    bool screenOff = false;
    taskENTER_CRITICAL(&_stateMux);
    timeoutMs = _screenTimeoutMs;
    lastChangeTime = _lastChangeTime;
    screenOff = _screenOff;
    taskEXIT_CRITICAL(&_stateMux);

    if (!screenOff && timeoutMs > 0 && (millis() - lastChangeTime >= timeoutMs)) {
        _lcd.noBacklight();
        taskENTER_CRITICAL(&_stateMux);
        _screenOff = true;
        taskEXIT_CRITICAL(&_stateMux);
    }
}
