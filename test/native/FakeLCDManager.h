#ifndef FAKE_LCD_MANAGER_H
#define FAKE_LCD_MANAGER_H

#include "LCDDisplayLogic.h"
#include <string>
#include <vector>
#include <cstdint>

struct ScreenUpdate {
    std::string line1;
    std::string line2;
    std::string line3;
    std::string line4;
    int lineCount;
};

class LCDManager {
public:
    LCDManager() = default;

    void begin() {}

    void updateScreen(const char* line1, const char* line2) {
        LCDDisplayMessage displayMsg = displayLogic.prepareTwoLineMessage(line1, line2, nowMs);
        applyDisplayMessage(displayMsg);
    }

    void updateScreen(const char* line1, const char* line2, const char* line3, const char* line4) {
        LCDDisplayMessage displayMsg = displayLogic.prepareFourLineMessage(line1, line2, line3, line4);
        applyDisplayMessage(displayMsg);
    }

    void startTask() {}

    // Test inspection methods
    std::string lastLine1;
    std::string lastLine2;
    std::string lastLine3;
    std::string lastLine4;
    int lastLineCount = 0;
    int updateCount = 0;
    std::vector<ScreenUpdate> history;

    void advanceTimeMs(uint64_t ms) { nowMs += ms; }

    void reset() {
        lastLine1.clear();
        lastLine2.clear();
        lastLine3.clear();
        lastLine4.clear();
        lastLineCount = 0;
        updateCount = 0;
        history.clear();
        nowMs = 0;
        displayLogic.reset();
    }

    bool lastScreenContains(const std::string& text) const {
        return lastLine1.find(text) != std::string::npos ||
               lastLine2.find(text) != std::string::npos ||
               lastLine3.find(text) != std::string::npos ||
               lastLine4.find(text) != std::string::npos;
    }

private:
    void applyDisplayMessage(const LCDDisplayMessage& msg) {
        lastLine1 = msg.line1;
        lastLine2 = msg.line2;
        lastLine3 = msg.line3;
        lastLine4 = msg.line4;
        lastLineCount = msg.lineCount;
        updateCount++;
        history.push_back({lastLine1, lastLine2, lastLine3, lastLine4, lastLineCount});
        displayLogic.noteDisplayedMessage(msg, nowMs);
    }

    uint64_t nowMs = 0;
    LCDDisplayLogic displayLogic;
};

#endif // FAKE_LCD_MANAGER_H
