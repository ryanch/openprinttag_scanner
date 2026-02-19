#ifndef FAKE_LCD_MANAGER_H
#define FAKE_LCD_MANAGER_H

#include <string>
#include <vector>

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

    void updateScreen(const std::string& line1, const std::string& line2) {
        lastLine1 = line1;
        lastLine2 = line2;
        lastLine3.clear();
        lastLine4.clear();
        updateCount++;
        history.push_back({line1, line2, "", "", 2});
    }

    void updateScreen(const std::string& line1, const std::string& line2, const std::string& line3, const std::string& line4) {
        lastLine1 = line1;
        lastLine2 = line2;
        lastLine3 = line3;
        lastLine4 = line4;
        updateCount++;
        history.push_back({line1, line2, line3, line4, 4});
    }

    void startTask() {}

    // Test inspection methods
    std::string lastLine1;
    std::string lastLine2;
    std::string lastLine3;
    std::string lastLine4;
    int updateCount = 0;
    std::vector<ScreenUpdate> history;

    void reset() {
        lastLine1.clear();
        lastLine2.clear();
        lastLine3.clear();
        lastLine4.clear();
        updateCount = 0;
        history.clear();
    }

    bool lastScreenContains(const std::string& text) const {
        return lastLine1.find(text) != std::string::npos ||
               lastLine2.find(text) != std::string::npos;
    }
};

#endif // FAKE_LCD_MANAGER_H
