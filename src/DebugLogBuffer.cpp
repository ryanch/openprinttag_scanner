#include "DebugLogBuffer.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

DebugLogBuffer& DebugLogBuffer::getInstance() {
    static DebugLogBuffer instance;
    return instance;
}

DebugLogBuffer::DebugLogBuffer()
    : mutex_(xSemaphoreCreateMutex()),
#ifdef NATIVE_TEST
      pendingQueue_(nullptr),
#else
      pendingQueue_(xQueueCreate(MAX_PENDING_LINES, sizeof(PendingLine))),
#endif
      nextSeq_(0),
      count_(0) {
    memset(lines_, 0, sizeof(lines_));
    memset(seqs_, 0, sizeof(seqs_));
}

bool DebugLogBuffer::lock() {
    if (mutex_ == nullptr) {
        return true;
    }
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void DebugLogBuffer::unlock() {
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void DebugLogBuffer::sanitizeLine(const char* input, char* output, size_t outputLen) {
    if (outputLen == 0) {
        return;
    }

    if (input == nullptr) {
        output[0] = '\0';
        return;
    }

    size_t len = strnlen(input, outputLen - 1);
    while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r')) {
        len--;
    }

    if (len > outputLen - 1) {
        len = outputLen - 1;
    }

    memcpy(output, input, len);
    output[len] = '\0';
}

void DebugLogBuffer::appendLocked(const char* text) {
    const uint32_t seq = nextSeq_;
    const size_t index = seq % MAX_LINES;
    sanitizeLine(text, lines_[index], sizeof(lines_[index]));
    seqs_[index] = seq;
    nextSeq_++;
    if (count_ < MAX_LINES) {
        count_++;
    }
}

void DebugLogBuffer::logf(const char* fmt, ...) {
    if (fmt == nullptr) {
        return;
    }

    char serialBuffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(serialBuffer, sizeof(serialBuffer), fmt, args);
    va_end(args);

    Serial.print(serialBuffer);
    enqueuePending(serialBuffer);
}

void DebugLogBuffer::logLine(const char* line) {
    Serial.println(line ? line : "");
    enqueuePending(line);
}

void DebugLogBuffer::logText(const char* text) {
    Serial.print(text ? text : "");
    enqueuePending(text);
}

void DebugLogBuffer::enqueuePending(const char* text) {
#ifdef NATIVE_TEST
    if (!lock()) {
        return;
    }
    appendLocked(text);
    unlock();
#else
    if (pendingQueue_ == nullptr) {
        return;
    }

    PendingLine line;
    sanitizeLine(text, line.text, sizeof(line.text));
    (void)xQueueSend(pendingQueue_, &line, 0);
#endif
}

void DebugLogBuffer::drainPending(size_t maxItems) {
#ifdef NATIVE_TEST
    (void)maxItems;
    return;
#else
    if (pendingQueue_ == nullptr || maxItems == 0) {
        return;
    }

    PendingLine line;
    size_t drained = 0;
    while (drained < maxItems && xQueueReceive(pendingQueue_, &line, 0) == pdTRUE) {
        if (lock()) {
            appendLocked(line.text);
            unlock();
        }
        drained++;
    }
#endif
}

DebugLogBuffer::SnapshotMeta DebugLogBuffer::getSnapshotMeta() {
    SnapshotMeta meta{0, 0, 0};

    if (!lock()) {
        return meta;
    }

    meta.nextSeq = nextSeq_;
    meta.count = count_;
    meta.oldestSeq = nextSeq_ - static_cast<uint32_t>(count_);

    unlock();
    return meta;
}

DebugLogBuffer::LookupResult DebugLogBuffer::getLineBySeq(uint32_t seq, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0) {
        return LookupResult::Eof;
    }
    out[0] = '\0';

    if (!lock()) {
        return LookupResult::Eof;
    }

    const uint32_t next = nextSeq_;
    const uint32_t oldest = (count_ == 0) ? next : (next - static_cast<uint32_t>(count_));

    if (seq < oldest) {
        unlock();
        return LookupResult::Stale;
    }

    if (seq >= next) {
        unlock();
        return LookupResult::Eof;
    }

    const size_t index = seq % MAX_LINES;
    if (seqs_[index] != seq) {
        unlock();
        return LookupResult::Stale;
    }

    strncpy(out, lines_[index], outLen - 1);
    out[outLen - 1] = '\0';

    unlock();
    return LookupResult::Ok;
}

void DebugLogBuffer::clear() {
    if (!lock()) {
        return;
    }

    nextSeq_ = 0;
    count_ = 0;
    memset(lines_, 0, sizeof(lines_));
    memset(seqs_, 0, sizeof(seqs_));

    unlock();
}
