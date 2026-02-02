#ifndef APPLICATION_MANAGER_H
#define APPLICATION_MANAGER_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum class AppMessageType {
    PRINT_STARTED,
    PRINT_CANCELED,
    PRINT_FINISHED,
    SPOOL_SCANNED
};

struct AppMessage {
    AppMessageType type;
    union {
        struct {
            int job_id;
        } printStarted;
        struct {
            int job_id;
            float est_filament_used_grams;
        } printCanceled;
        struct {
            int job_id;
            float filament_used_grams;
        } printFinished;
        struct {
            char spool_id[64];
        } spoolScanned;
    } payload;
};

class ApplicationManager {
public:
    static ApplicationManager& getInstance();

    bool begin();
    bool sendMessage(const AppMessage& msg, uint32_t waitMs = 0);
    void processMessages();

private:
    ApplicationManager() = default;
    ApplicationManager(const ApplicationManager&) = delete;
    ApplicationManager& operator=(const ApplicationManager&) = delete;

    QueueHandle_t messageQueue = nullptr;
    static constexpr size_t QUEUE_SIZE = 16;
};

#endif // APPLICATION_MANAGER_H
