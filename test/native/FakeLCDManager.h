#ifndef FAKE_LCD_MANAGER_H
#define FAKE_LCD_MANAGER_H

#include <string>
#include <vector>

struct ScreenUpdate {
    std::string line1;
    std::string line2;
};

class LCDManager {
public:
    LCDManager() = default;

    void begin() {}

    void updateScreen(const std::string& line1, const std::string& line2) {
        lastLine1 = line1;
        lastLine2 = line2;
        updateCount++;
        history.push_back({line1, line2});
    }

    void startTask() {}

    // Test inspection methods
    std::string lastLine1;
    std::string lastLine2;
    int updateCount = 0;
    std::vector<ScreenUpdate> history;

    void reset() {
        lastLine1.clear();
        lastLine2.clear();
        updateCount = 0;
        history.clear();
    }

    bool lastScreenContains(const std::string& text) const {
        return lastLine1.find(text) != std::string::npos ||
               lastLine2.find(text) != std::string::npos;
    }
};

#endif // FAKE_LCD_MANAGER_H
