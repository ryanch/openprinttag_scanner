#ifndef APPLICATION_MANAGER_H
#define APPLICATION_MANAGER_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum class AppMessageType {
    PRINT_STARTED,
    PRINT_CANCELED,
    PRINT_FINISHED,
    SPOOL_DETECTED,     // Full spool info parsed from NFC
    SPOOL_UPDATED,      // Spool was written to successfully
};

struct SpoolDetectedPayload {
    char spool_id[64];           // UID hex string
    uint8_t material_type;       // OPT_MATERIAL_TYPE_PLA, etc.
    float kg_remaining;          // Remaining weight in kg
    uint8_t primary_color[4];    // RGBA color
    char material_name[32];      // Material name string
};

struct SpoolUpdatedPayload {
    char spool_id[64];           // Spool that was updated
    uint8_t update_type;         // NFCWriteType enum value
    bool success;                // Whether update succeeded
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
        SpoolDetectedPayload spoolDetected;
        SpoolUpdatedPayload spoolUpdated;
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
