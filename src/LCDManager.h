#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include <string>
#include <cstdint>
#include <LiquidCrystal_I2C.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

struct ScreenMessage {
    char line1[17];
    char line2[17];
    char line3[17];
    char line4[17];
    uint8_t lineCount;
};

class LCDManager {
public:
    LCDManager(uint8_t lcd_Addr, uint8_t lcd_cols, uint8_t lcd_rows);
    void begin();
    void updateScreen(const std::string& line1, const std::string& line2);
    void updateScreen(const std::string& line1, const std::string& line2, const std::string& line3, const std::string& line4);
    void startTask();
    void setScreenTimeoutMs(uint32_t timeoutMs);

private:
    void processQueue();
    void taskLoop();
    static void taskFunc(void* param);

    LiquidCrystal_I2C _lcd;
    QueueHandle_t _messageQueue;
    TaskHandle_t _taskHandle;
    std::string _currentLine1;
    std::string _currentLine2;
    std::string _activeLine1;
    std::string _activeLine2;
    std::string _activeLine3;
    std::string _activeLine4;
    uint8_t _activeLineCount;
    uint8_t _currentPage;
    unsigned long _lastPageSwitchTimeMs;
    uint8_t _lcd_cols;
    unsigned long _lastChangeTime;
    bool _screenOff;
    uint32_t _screenTimeoutMs;
    portMUX_TYPE _stateMux;
    static const uint32_t DEFAULT_SCREEN_TIMEOUT_MS = 15 * 60 * 1000; // 15 minutes
    static const uint32_t FOUR_LINE_PAGE_DURATION_MS = 5000;
};

#endif // LCD_MANAGER_H
