#include "LCDManager.h"
#include <Arduino.h>
#include <cstring>

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

void LCDManager::updateScreen(const char* line1, const char* line2) {
    ScreenMessage msg;
    memset(&msg, 0, sizeof(msg));
    size_t copyLen = (_lcd_cols < 16) ? _lcd_cols : 16;
    auto copyLine = [copyLen](char* dst, const char* src) {
        if (src == nullptr) {
            dst[0] = '\0';
            return;
        }
        strncpy(dst, src, copyLen);
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

void LCDManager::updateScreen(const char* line1, const char* line2, const char* line3, const char* line4) {
    ScreenMessage msg;
    memset(&msg, 0, sizeof(msg));
    size_t copyLen = (_lcd_cols < 16) ? _lcd_cols : 16;
    auto copyLine = [copyLen](char* dst, const char* src) {
        if (src == nullptr) {
            dst[0] = '\0';
            return;
        }
        strncpy(dst, src, copyLen);
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
    auto renderLines = [this](const char* line1, const char* line2) {
        bool changed = false;

        if (strcmp(line1, _currentLine1) != 0) {
            strncpy(_currentLine1, line1, sizeof(_currentLine1) - 1);
            _currentLine1[sizeof(_currentLine1) - 1] = '\0';
            _lcd.setCursor(0, 0);
            _lcd.print(_currentLine1);
            for (size_t i = strlen(_currentLine1); i < _lcd_cols; ++i) {
                _lcd.print(" ");
            }
            changed = true;
        }

        if (strcmp(line2, _currentLine2) != 0) {
            strncpy(_currentLine2, line2, sizeof(_currentLine2) - 1);
            _currentLine2[sizeof(_currentLine2) - 1] = '\0';
            _lcd.setCursor(0, 1);
            _lcd.print(_currentLine2);
            for (size_t i = strlen(_currentLine2); i < _lcd_cols; ++i) {
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
        char previousLine1[17];
        char previousLine2[17];
        char previousLine3[17];
        char previousLine4[17];
        strncpy(previousLine1, _activeLine1, sizeof(previousLine1) - 1);
        strncpy(previousLine2, _activeLine2, sizeof(previousLine2) - 1);
        strncpy(previousLine3, _activeLine3, sizeof(previousLine3) - 1);
        strncpy(previousLine4, _activeLine4, sizeof(previousLine4) - 1);
        previousLine1[16] = '\0';
        previousLine2[16] = '\0';
        previousLine3[16] = '\0';
        previousLine4[16] = '\0';

        strncpy(_activeLine1, msg.line1, sizeof(_activeLine1) - 1);
        strncpy(_activeLine2, msg.line2, sizeof(_activeLine2) - 1);
        strncpy(_activeLine3, msg.line3, sizeof(_activeLine3) - 1);
        strncpy(_activeLine4, msg.line4, sizeof(_activeLine4) - 1);
        _activeLine1[16] = '\0';
        _activeLine2[16] = '\0';
        _activeLine3[16] = '\0';
        _activeLine4[16] = '\0';
        _activeLineCount = (msg.lineCount == 4) ? 4 : 2;
        _currentPage = 0;
        _lastPageSwitchTimeMs = millis();

        bool changed = renderLines(_activeLine1, _activeLine2);

        // Check if this is a new message (any line content or line count changed)
        bool isNewMessage = (strcmp(previousLine1, _activeLine1) != 0 ||
                            strcmp(previousLine2, _activeLine2) != 0 ||
                            strcmp(previousLine3, _activeLine3) != 0 ||
                            strcmp(previousLine4, _activeLine4) != 0 ||
                            previousActiveLineCount != _activeLineCount);

        if (isNewMessage) {
            markScreenActivity();
        }

        if (changed || previousActiveLineCount != _activeLineCount) {
            unsigned long nowMs = millis();
            LCDDisplayMessage displayedMsg;
            strncpy(displayedMsg.line1, msg.line1, sizeof(displayedMsg.line1) - 1);
            strncpy(displayedMsg.line2, msg.line2, sizeof(displayedMsg.line2) - 1);
            strncpy(displayedMsg.line3, msg.line3, sizeof(displayedMsg.line3) - 1);
            strncpy(displayedMsg.line4, msg.line4, sizeof(displayedMsg.line4) - 1);
            displayedMsg.line1[sizeof(displayedMsg.line1) - 1] = '\0';
            displayedMsg.line2[sizeof(displayedMsg.line2) - 1] = '\0';
            displayedMsg.line3[sizeof(displayedMsg.line3) - 1] = '\0';
            displayedMsg.line4[sizeof(displayedMsg.line4) - 1] = '\0';
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
