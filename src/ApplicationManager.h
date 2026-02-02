#ifndef APPLICATION_MANAGER_H
#define APPLICATION_MANAGER_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class LCDManager;

enum class AppMessageType {
    PRINT_STARTED,
    PRINT_CANCELED,
    PRINT_FINISHED,
    SPOOL_DETECTED,     // Full spool info parsed from NFC
    SPOOL_UPDATED,      // Spool was written to successfully
};

enum class AppState { IDLE, MONITORING_PRINT };

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
    float kg_remaining;          // Remaining weight after update (kg)
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

    bool begin(LCDManager* lcd = nullptr);
    bool sendMessage(const AppMessage& msg, uint32_t waitMs = 0);
    void processMessages();

private:
    ApplicationManager() = default;
    ApplicationManager(const ApplicationManager&) = delete;
    ApplicationManager& operator=(const ApplicationManager&) = delete;

    QueueHandle_t messageQueue = nullptr;
    static constexpr size_t QUEUE_SIZE = 16;

    // LCD reference
    LCDManager* lcdManager = nullptr;

    // State machine
    AppState currentState = AppState::IDLE;
    char startingSpoolId[64] = {0};
    int currentJobId = 0;
    bool spoolChangedDuringPrint = false;
    char lastDisplayedSpoolId[64] = {0};

    // Handlers
    void handlePrintStarted(const AppMessage& msg);
    void handlePrintCanceled(const AppMessage& msg);
    void handlePrintFinished(const AppMessage& msg);
    void handleSpoolDetected(const AppMessage& msg);
    void handleSpoolUpdated(const AppMessage& msg);
    void finishPrint(float gramsUsed, bool canceled);
};

#endif // APPLICATION_MANAGER_H
