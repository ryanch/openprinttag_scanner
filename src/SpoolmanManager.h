#ifndef SPOOLMAN_MANAGER_H
#define SPOOLMAN_MANAGER_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <string>

struct SpoolmanSyncRequest {
    char spool_id[17];           // NFC tag UID hex string
    uint8_t material_type;       // OPT_MATERIAL_TYPE_PLA, etc.
    char manufacturer[33];       // Brand name from tag
    uint8_t color[4];            // RGBA from tag
    float remaining_weight_g;    // Remaining weight in grams
    float initial_weight_g;      // Full spool weight in grams
    float density;               // g/cm3 (from tag, or default)
    float diameter;              // mm (from tag, or default)
    int32_t spoolman_id;         // Spoolman ID from tag (-1 if absent)
};

class SpoolmanManager {
public:
    static SpoolmanManager& getInstance();
    bool begin(SemaphoreHandle_t httpMutex);
    void startTask();
    bool enqueueSync(const SpoolmanSyncRequest& req);
    bool isConfigured() const;

private:
    SpoolmanManager() = default;
    SpoolmanManager(const SpoolmanManager&) = delete;
    SpoolmanManager& operator=(const SpoolmanManager&) = delete;

    static void taskFunc(void* param);
    void taskLoop();
    bool syncSpool(const SpoolmanSyncRequest& req, int& resolvedSpoolmanId);

    QueueHandle_t syncQueue = nullptr;
    SemaphoreHandle_t httpMutex_ = nullptr;
    TaskHandle_t taskHandle = nullptr;

    static constexpr size_t QUEUE_SIZE = 4;
    static constexpr size_t TASK_STACK_SIZE = 6144;
    static constexpr UBaseType_t TASK_PRIORITY = 1;
    static constexpr TickType_t HTTP_MUTEX_TIMEOUT = pdMS_TO_TICKS(10000);
};

#endif // SPOOLMAN_MANAGER_H
