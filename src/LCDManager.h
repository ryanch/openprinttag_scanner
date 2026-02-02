#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H

#include <string>
#include <LiquidCrystal_I2C.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

struct ScreenMessage {
    char line1[17];
    char line2[17];
};

class LCDManager {
public:
    LCDManager(uint8_t lcd_Addr, uint8_t lcd_cols, uint8_t lcd_rows);
    void begin();
    void updateScreen(const std::string& line1, const std::string& line2);
    void startTask();

private:
    void processQueue();
    void taskLoop();
    static void taskFunc(void* param);

    LiquidCrystal_I2C _lcd;
    QueueHandle_t _messageQueue;
    TaskHandle_t _taskHandle;
    std::string _currentLine1;
    std::string _currentLine2;
    uint8_t _lcd_cols;
};

#endif // LCD_MANAGER_H
