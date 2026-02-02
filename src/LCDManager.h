#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include <string>
#include <queue>
#include <LiquidCrystal_I2C.h>

struct ScreenMessage {
    std::string line1;
    std::string line2;
};

class LCDManager {
public:
    LCDManager(uint8_t lcd_Addr, uint8_t lcd_cols, uint8_t lcd_rows);
    void begin();
    void updateScreen(const std::string& line1, const std::string& line2);
    void processQueue();

private:
    LiquidCrystal_I2C _lcd;
    std::queue<ScreenMessage> _messageQueue;
    std::string _currentLine1;
    std::string _currentLine2;
    uint8_t _lcd_cols;
};

#endif // LCD_MANAGER_H
