#ifndef APPLICATION_MANAGER_H
#define APPLICATION_MANAGER_H

#include <cstdint>

#ifdef NATIVE_TEST
  #include "platform/NativePlatform.h"
#else
  #include <freertos/FreeRTOS.h>
  #include <freertos/queue.h>
#endif

class LCDManager;

enum class AppMessageType {
    PRINT_STARTED,
    PRINT_CANCELED,
    PRINT_FINISHED,
    SPOOL_DETECTED,     // Full spool info parsed from NFC
    SPOOL_UPDATED,      // Spool was written to successfully
    BLANK_TAG_DETECTED, // Tag present but not OpenPrintTag format
    SPOOLMAN_SYNCED,    // Spoolman sync completed
};

enum class AppState { IDLE, MONITORING_PRINT };

struct SpoolDetectedPayload {
    char spool_id[64];           // UID hex string
    uint8_t material_type;       // OPT_MATERIAL_TYPE_PLA, etc.
    float kg_remaining;          // Remaining weight in kg
    uint8_t primary_color[4];    // RGBA color
    char material_name[32];      // Material name string
    float density;               // g/cm3 (0 if not available)
    float diameter;              // mm (0 if not available)
    float initial_weight_g;      // Full spool weight in grams
    char manufacturer[64];       // Brand name from tag
    int32_t spoolman_id;         // Spoolman ID from tag (-1 if absent)
};

struct SpoolUpdatedPayload {
    char spool_id[64];           // Spool that was updated
    uint8_t update_type;         // NFCWriteType enum value
    bool success;                // Whether update succeeded
    float kg_remaining;          // Remaining weight after update (kg)
};

struct BlankTagPayload {
    char spool_id[64];           // UID hex string
};

struct SpoolmanSyncedPayload {
    char spool_id[64];
    bool success;
    float kg_remaining;          // Remaining weight for LCD display
    int32_t spoolman_id;         // Resolved Spoolman spool ID (-1 if unknown)
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
        BlankTagPayload blankTag;
        SpoolmanSyncedPayload spoolmanSynced;
    } payload;
};

class ApplicationManager {
public:
    static ApplicationManager& getInstance();

    bool begin(LCDManager* lcd = nullptr);
    bool sendMessage(const AppMessage& msg, uint32_t waitMs = 0);
    void processMessages();

    // Public for testing
    void handleMessage(const AppMessage& msg);
    AppState getState() const { return currentState; }
    const char* getStartingSpoolId() const { return startingSpoolId; }
    int getCurrentJobId() const { return currentJobId; }
    bool hasSpoolChangedDuringPrint() const { return spoolChangedDuringPrint; }
#ifdef NATIVE_TEST
    void resetForTest() {
        currentState = AppState::IDLE;
        startingSpoolId[0] = '\0';
        currentJobId = 0;
        spoolChangedDuringPrint = false;
        lastDisplayedSpoolId[0] = '\0';
        lastDisplayedBlankId[0] = '\0';
    }
#endif

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
    char lastDisplayedBlankId[64] = {0};

    // Handlers
    void handlePrintStarted(const AppMessage& msg);
    void handlePrintCanceled(const AppMessage& msg);
    void handlePrintFinished(const AppMessage& msg);
    void handleSpoolDetected(const AppMessage& msg);
    void handleSpoolUpdated(const AppMessage& msg);
    void handleBlankTagDetected(const AppMessage& msg);
    void handleSpoolmanSynced(const AppMessage& msg);
    void finishPrint(float gramsUsed, bool canceled);
    void enqueueSpoolmanSync(const SpoolDetectedPayload& spool);
};

#endif // APPLICATION_MANAGER_H
