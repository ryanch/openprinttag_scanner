#ifndef DEBUG_LOG_BUFFER_H
#define DEBUG_LOG_BUFFER_H

#include "platform/Platform.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

class DebugLogBuffer {
public:
    static constexpr size_t MAX_LINES = 100;
    static constexpr size_t MAX_LINE_LENGTH = 100;
    static constexpr size_t MAX_PENDING_LINES = 32;

    enum class LookupResult : uint8_t {
        Ok = 0,
        Stale = 1,
        Eof = 2
    };

    struct SnapshotMeta {
        uint32_t oldestSeq;
        uint32_t nextSeq;
        size_t count;
    };

    static DebugLogBuffer& getInstance();

    void logf(const char* fmt, ...);
    void logLine(const char* line);
    void logLine(const String& line) { logLine(line.c_str()); }
    void logText(const char* text);
    void logText(const String& text) { logText(text.c_str()); }

    SnapshotMeta getSnapshotMeta();
    LookupResult getLineBySeq(uint32_t seq, char* out, size_t outLen);
    void drainPending(size_t maxItems = 32);

    void clear();

    void logLineValue(const char* value) { logLine(value ? value : ""); }
    void logValue(const char* value) { logText(value ? value : ""); }

    void logLineValue(const String& value) { logLine(value); }
    void logValue(const String& value) { logText(value); }

    template <typename T>
    typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, void>::type
    logLineValue(T value) {
        char buf[32];
        if (std::is_signed<T>::value) {
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
        } else {
            snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
        }
        logLine(buf);
    }

    template <typename T>
    typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, void>::type
    logValue(T value) {
        char buf[32];
        if (std::is_signed<T>::value) {
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
        } else {
            snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
        }
        logText(buf);
    }

    template <typename T>
    typename std::enable_if<std::is_floating_point<T>::value, void>::type
    logLineValue(T value) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(value));
        logLine(buf);
    }

    template <typename T>
    typename std::enable_if<std::is_floating_point<T>::value, void>::type
    logValue(T value) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(value));
        logText(buf);
    }

    void logLineValue(bool value) { logLine(value ? "true" : "false"); }
    void logValue(bool value) { logText(value ? "true" : "false"); }

    template <typename T>
    typename std::enable_if<!std::is_arithmetic<T>::value && !std::is_same<T, String>::value, void>::type
    logLineValue(const T& value) {
        String str(value);
        logLine(str.c_str());
    }

    template <typename T>
    typename std::enable_if<!std::is_arithmetic<T>::value && !std::is_same<T, String>::value, void>::type
    logValue(const T& value) {
        String str(value);
        logText(str.c_str());
    }

private:
    DebugLogBuffer();

    bool lock();
    void unlock();
    void appendLocked(const char* text);
    void enqueuePending(const char* text);
    static void sanitizeLine(const char* input, char* output, size_t outputLen);

    struct PendingLine {
        char text[MAX_LINE_LENGTH + 1];
    };

    SemaphoreHandle_t mutex_;
    QueueHandle_t pendingQueue_;
    char lines_[MAX_LINES][MAX_LINE_LENGTH + 1];
    uint32_t seqs_[MAX_LINES];
    uint32_t nextSeq_;
    size_t count_;
};

#define DBG_LOGF(...) DebugLogBuffer::getInstance().logf(__VA_ARGS__)
#define DBG_LOGLN(value) DebugLogBuffer::getInstance().logLineValue(value)
#define DBG_LOG(value) DebugLogBuffer::getInstance().logValue(value)

#endif
