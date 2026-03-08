#ifndef NATIVE_PLATFORM_H
#define NATIVE_PLATFORM_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdarg>
#include <string>
#include <queue>
#include <vector>
#include <chrono>
#include <cstdio>

// FreeRTOS type stubs
typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define pdFAIL pdFALSE
#define portMAX_DELAY 0xFFFFFFFF

inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }

// Simple queue implementation for native testing
template<typename T, size_t MaxSize = 16>
class NativeQueue {
public:
    bool send(const T& item) {
        if (items.size() >= MaxSize) return false;
        items.push(item);
        return true;
    }

    bool receive(T& item) {
        if (items.empty()) return false;
        item = items.front();
        items.pop();
        return true;
    }

    size_t size() const { return items.size(); }
    bool empty() const { return items.empty(); }

private:
    std::queue<T> items;
};

// Global queue registry for native testing (simple approach)
class NativeQueueRegistry {
public:
    static NativeQueueRegistry& getInstance() {
        static NativeQueueRegistry instance;
        return instance;
    }

    void* createQueue(size_t /*itemSize*/, size_t /*queueSize*/) {
        // For simplicity, we allocate a generic byte buffer queue
        // Real implementation would use type-erased queue
        return new std::queue<std::vector<uint8_t>>();
    }

    void deleteQueue(void* queue) {
        delete static_cast<std::queue<std::vector<uint8_t>>*>(queue);
    }

private:
    NativeQueueRegistry() = default;
};

// Functional byte-buffer queue for native testing
struct NativeByteQueue {
    std::queue<std::vector<uint8_t>> items;
    size_t itemSize;
    size_t maxItems;
};

inline QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize) {
    auto* q = new NativeByteQueue();
    q->itemSize = itemSize;
    q->maxItems = length;
    return reinterpret_cast<QueueHandle_t>(q);
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t wait) {
    (void)wait;
    if (!queue) return pdFALSE;
    auto* q = reinterpret_cast<NativeByteQueue*>(queue);
    if (q->items.size() >= q->maxItems) return pdFALSE;
    std::vector<uint8_t> data(q->itemSize);
    memcpy(data.data(), item, q->itemSize);
    q->items.push(std::move(data));
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t wait) {
    (void)wait;
    if (!queue) return pdFALSE;
    auto* q = reinterpret_cast<NativeByteQueue*>(queue);
    if (q->items.empty()) return pdFALSE;
    memcpy(item, q->items.front().data(), q->itemSize);
    q->items.pop();
    return pdTRUE;
}

inline void vQueueDelete(QueueHandle_t queue) {
    if (queue) delete reinterpret_cast<NativeByteQueue*>(queue);
}

// FreeRTOS semaphore stubs
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return nullptr; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait) {
    (void)sem; (void)wait;
    return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    (void)sem;
    return pdTRUE;
}

// FreeRTOS task stubs
inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
inline BaseType_t xTaskCreatePinnedToCore(
    void (*taskFunc)(void*), const char* name, uint32_t stackSize,
    void* param, UBaseType_t priority, TaskHandle_t* handle, int core) {
    (void)taskFunc; (void)name; (void)stackSize; (void)param;
    (void)priority; (void)handle; (void)core;
    return pdPASS;
}

// FreeRTOS queue peek
inline BaseType_t xQueuePeek(QueueHandle_t queue, void* item, TickType_t wait) {
    (void)wait;
    if (!queue) return pdFALSE;
    auto* q = reinterpret_cast<NativeByteQueue*>(queue);
    if (q->items.empty()) return pdFALSE;
    memcpy(item, q->items.front().data(), q->itemSize);
    return pdTRUE;
}

// Arduino timing functions
// Global test time for advancing milliseconds in tests
static uint32_t g_test_millis_offset = 0;

inline unsigned long millis() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() + g_test_millis_offset;
}

// Helper to advance test time (for testing delayed operations)
inline void advance_milliseconds(uint32_t ms) {
    g_test_millis_offset += ms;
}

inline void delay(unsigned long ms) {
    (void)ms; // No-op for tests - we don't want to actually wait
}

// Arduino String class stub (minimal implementation)
class String {
public:
    String() : data_() {}
    String(const char* str) : data_(str ? str : "") {}
    String(const std::string& str) : data_(str) {}

    const char* c_str() const { return data_.c_str(); }
    size_t length() const { return data_.length(); }
    bool isEmpty() const { return data_.empty(); }

    String& operator=(const char* str) { data_ = str ? str : ""; return *this; }
    String& operator+=(const char* str) { data_ += str ? str : ""; return *this; }
    String operator+(const char* str) const { return String(data_ + (str ? str : "")); }

    bool operator==(const String& other) const { return data_ == other.data_; }
    bool operator!=(const String& other) const { return data_ != other.data_; }

private:
    std::string data_;
};

// Serial stub for native testing
class SerialStub {
public:
    void begin(unsigned long baud) { (void)baud; }
    void print(const char* str) { printf("%s", str); }
    void println(const char* str) { printf("%s\n", str); }
    void println() { printf("\n"); }
    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
};

extern SerialStub Serial;

#endif // NATIVE_PLATFORM_H
