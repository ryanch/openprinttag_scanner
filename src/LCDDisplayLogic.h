#ifndef LCD_DISPLAY_LOGIC_H
#define LCD_DISPLAY_LOGIC_H

#include <cstdint>
#include <string>

struct LCDDisplayMessage {
    std::string line1;
    std::string line2;
    std::string line3;
    std::string line4;
    uint8_t lineCount;
};

class LCDDisplayLogic {
public:
    LCDDisplayLogic() : _lastDisplayedLineCount(0), _lastDisplayedSinceMs(0), _hasLastDisplayedMessage(false) {}

    LCDDisplayMessage prepareTwoLineMessage(const std::string& line1,
                                            const std::string& line2,
                                            unsigned long nowMs) const {
        LCDDisplayMessage msg;
        msg.line1 = line1;
        msg.line2 = line2;
        msg.line3.clear();
        msg.line4.clear();
        msg.lineCount = 2;

        if (_hasLastDisplayedMessage &&
            _lastDisplayedLineCount == 2 &&
            (nowMs - _lastDisplayedSinceMs < RECENT_TWO_LINE_COMBINE_MS)) {
            msg.line1 = _lastDisplayedLine1;
            msg.line2 = _lastDisplayedLine2;
            msg.line3 = line1;
            msg.line4 = line2;
            msg.lineCount = 4;
        }

        return msg;
    }

    LCDDisplayMessage prepareFourLineMessage(const std::string& line1,
                                             const std::string& line2,
                                             const std::string& line3,
                                             const std::string& line4) const {
        LCDDisplayMessage msg;
        msg.line1 = line1;
        msg.line2 = line2;
        msg.line3 = line3;
        msg.line4 = line4;
        msg.lineCount = 4;
        return msg;
    }

    void noteDisplayedMessage(const LCDDisplayMessage& msg, unsigned long nowMs) {
        _lastDisplayedLine1 = msg.line1;
        _lastDisplayedLine2 = msg.line2;
        _lastDisplayedLineCount = msg.lineCount;
        _lastDisplayedSinceMs = nowMs;
        _hasLastDisplayedMessage = true;
    }

    void reset() {
        _lastDisplayedLine1.clear();
        _lastDisplayedLine2.clear();
        _lastDisplayedLineCount = 0;
        _lastDisplayedSinceMs = 0;
        _hasLastDisplayedMessage = false;
    }

private:
    static const uint32_t RECENT_TWO_LINE_COMBINE_MS = 5000;
    std::string _lastDisplayedLine1;
    std::string _lastDisplayedLine2;
    uint8_t _lastDisplayedLineCount;
    unsigned long _lastDisplayedSinceMs;
    bool _hasLastDisplayedMessage;
};

#endif // LCD_DISPLAY_LOGIC_H
