#include "LCDManager.h"

LCDManager::LCDManager(uint8_t lcd_Addr, uint8_t lcd_cols, uint8_t lcd_rows)
    : _lcd(lcd_Addr, lcd_cols, lcd_rows), _lcd_cols(lcd_cols) {
}

void LCDManager::begin() {
    _lcd.init();
    _lcd.backlight();
    _lcd.clear();
}

void LCDManager::updateScreen(const std::string& line1, const std::string& line2) {
    ScreenMessage msg;
    msg.line1 = line1.substr(0, _lcd_cols);
    msg.line2 = line2.substr(0, _lcd_cols);
    _messageQueue.push(msg);
}

void LCDManager::processQueue() {
    if (!_messageQueue.empty()) {
        ScreenMessage msg = _messageQueue.front();
        _messageQueue.pop();

        if (msg.line1 != _currentLine1) {
            _currentLine1 = msg.line1;
            _lcd.setCursor(0, 0);
            _lcd.print(_currentLine1.c_str());
            // Pad with spaces to clear the rest of the line
            for (size_t i = _currentLine1.length(); i < _lcd_cols; ++i) {
                _lcd.print(" ");
            }
        }

        if (msg.line2 != _currentLine2) {
            _currentLine2 = msg.line2;
            _lcd.setCursor(0, 1);
            _lcd.print(_currentLine2.c_str());
            // Pad with spaces to clear the rest of the line
            for (size_t i = _currentLine2.length(); i < _lcd_cols; ++i) {
                _lcd.print(" ");
            }
        }
    }
}
