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
    SPOOL_DETECTED,         // Full spool info parsed from NFC
    SPOOL_UPDATED,          // Spool was written to successfully
    BLANK_TAG_DETECTED,     // Tag present but not OpenPrintTag format
    SPOOLMAN_SYNCED,        // Spoolman sync completed
    TAG_REMOVED,            // Tag was present, now gone
    HA_WRITE_TAG,           // HA commands full tag write
    HA_UPDATE_REMAINING,    // HA commands remaining weight update
};

enum class AutomationMode : uint8_t {
    SELF_DIRECTED = 0,              // Device acts autonomously + reports to HA
    CONTROLLED_BY_HOME_ASSISTANT = 1 // Device reports only, acts on HA commands
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

struct TagRemovedPayload {
    char spool_id[64];           // UID of removed tag
    float last_remaining_kg;     // Last known remaining (kg)
    int32_t spoolman_id;         // Last known spoolman ID
};

struct HAWriteTagPayload {
    char expected_uid[64];       // Required — must match current tag
    uint8_t material_type;
    uint8_t color[4];            // RGBA
    char manufacturer[64];
    float initial_weight_g;
    float remaining_g;
    int32_t spoolman_id;
};

struct HAUpdateRemainingPayload {
    char expected_uid[64];       // Required — must match current tag
    float remaining_g;
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
        TagRemovedPayload tagRemoved;
        HAWriteTagPayload haWriteTag;
        HAUpdateRemainingPayload haUpdateRemaining;
    } payload;
};

class ApplicationManager {
public:
    static ApplicationManager& getInstance();

    bool begin(LCDManager* lcd = nullptr);
    bool sendMessage(const AppMessage& msg, uint32_t waitMs = 0);
    void processMessages();
    void showStatusOnLCD();

    // Public for testing
    void handleMessage(const AppMessage& msg);
    AppState getState() const { return currentState; }
    const char* getStartingSpoolId() const { return startingSpoolId; }
    int getCurrentJobId() const { return currentJobId; }
    bool hasSpoolChangedDuringPrint() const { return spoolChangedDuringPrint; }
    AutomationMode getAutomationMode() const { return automationMode; }
    void setAutomationMode(AutomationMode mode) { automationMode = mode; }
#ifdef NATIVE_TEST
    void resetForTest() {
        currentState = AppState::IDLE;
        startingSpoolId[0] = '\0';
        currentJobId = 0;
        spoolChangedDuringPrint = false;
        lastDisplayedSpoolId[0] = '\0';
        lastDisplayedBlankId[0] = '\0';
        pendingStatusAfterTagRemoved = false;
        tagRemovedAtMs = 0;
        automationMode = AutomationMode::SELF_DIRECTED;
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
    bool pendingStatusAfterTagRemoved = false;
    uint32_t tagRemovedAtMs = 0;

    // Automation mode
    AutomationMode automationMode = AutomationMode::SELF_DIRECTED;

    // Handlers
    void handlePrintStarted(const AppMessage& msg);
    void handlePrintCanceled(const AppMessage& msg);
    void handlePrintFinished(const AppMessage& msg);
    void handleSpoolDetected(const AppMessage& msg);
    void handleSpoolUpdated(const AppMessage& msg);
    void handleBlankTagDetected(const AppMessage& msg);
    void handleSpoolmanSynced(const AppMessage& msg);
    void handleTagRemoved(const AppMessage& msg);
    void handleHAWriteTag(const AppMessage& msg);
    void handleHAUpdateRemaining(const AppMessage& msg);
    void finishPrint(float gramsUsed, bool canceled);
    void enqueueSpoolmanSync(const SpoolDetectedPayload& spool);
    void publishToHA(const char* topicSuffix, const char* payload, bool retained);
};

#endif // APPLICATION_MANAGER_H
