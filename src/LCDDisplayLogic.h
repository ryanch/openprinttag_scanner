#ifndef LCD_DISPLAY_LOGIC_H
#define LCD_DISPLAY_LOGIC_H

#include <cstdint>
#include <cstring>

struct LCDDisplayMessage {
    char line1[17];
    char line2[17];
    char line3[17];
    char line4[17];
    uint8_t lineCount;
};

class LCDDisplayLogic {
public:
    LCDDisplayLogic() : _lastDisplayedLineCount(0), _lastDisplayedSinceMs(0), _hasLastDisplayedMessage(false) {}

    LCDDisplayMessage prepareTwoLineMessage(const char* line1,
                                            const char* line2,
                                            unsigned long nowMs) const {
        LCDDisplayMessage msg;
        copyLine(msg.line1, line1);
        copyLine(msg.line2, line2);
        msg.line3[0] = '\0';
        msg.line4[0] = '\0';
        msg.lineCount = 2;

        if (_hasLastDisplayedMessage &&
            _lastDisplayedLineCount == 2 &&
            (nowMs - _lastDisplayedSinceMs < RECENT_TWO_LINE_COMBINE_MS)) {
            copyLine(msg.line1, _lastDisplayedLine1);
            copyLine(msg.line2, _lastDisplayedLine2);
            copyLine(msg.line3, line1);
            copyLine(msg.line4, line2);
            msg.lineCount = 4;
        }

        return msg;
    }

    LCDDisplayMessage prepareFourLineMessage(const char* line1,
                                             const char* line2,
                                             const char* line3,
                                             const char* line4) const {
        LCDDisplayMessage msg;
        copyLine(msg.line1, line1);
        copyLine(msg.line2, line2);
        copyLine(msg.line3, line3);
        copyLine(msg.line4, line4);
        msg.lineCount = 4;
        return msg;
    }

    void noteDisplayedMessage(const LCDDisplayMessage& msg, unsigned long nowMs) {
        copyLine(_lastDisplayedLine1, msg.line1);
        copyLine(_lastDisplayedLine2, msg.line2);
        _lastDisplayedLineCount = msg.lineCount;
        _lastDisplayedSinceMs = nowMs;
        _hasLastDisplayedMessage = true;
    }

    void reset() {
        _lastDisplayedLine1[0] = '\0';
        _lastDisplayedLine2[0] = '\0';
        _lastDisplayedLineCount = 0;
        _lastDisplayedSinceMs = 0;
        _hasLastDisplayedMessage = false;
    }

private:
    static void copyLine(char* dst, const char* src) {
        if (src == nullptr) {
            dst[0] = '\0';
            return;
        }
        strncpy(dst, src, 16);
        dst[16] = '\0';
    }

    static const uint32_t RECENT_TWO_LINE_COMBINE_MS = 5000;
    char _lastDisplayedLine1[17] = {0};
    char _lastDisplayedLine2[17] = {0};
    uint8_t _lastDisplayedLineCount;
    unsigned long _lastDisplayedSinceMs;
    bool _hasLastDisplayedMessage;
};

#endif // LCD_DISPLAY_LOGIC_H
