#ifndef STUB_APPLICATION_MANAGER_H
#define STUB_APPLICATION_MANAGER_H

#include <cstdint>
#include <vector>
#include <cstring>

class LCDManager;

// These types must match ApplicationManager.h exactly
enum class AppMessageType {
    PRINT_STARTED,
    PRINT_ENDED,
    SPOOL_DETECTED,
    SPOOL_UPDATED,
    BLANK_TAG_DETECTED,
    SPOOLMAN_SYNCED,
    TAG_REMOVED,
    HA_WRITE_TAG,
    HA_UPDATE_REMAINING,
};

enum class AutomationMode : uint8_t {
    SELF_DIRECTED = 0,
    CONTROLLED_BY_HOME_ASSISTANT = 1
};

enum class AppState { IDLE, MONITORING_PRINT };

struct SpoolDetectedPayload {
    char spool_id[17];
    uint8_t material_type;
    float kg_remaining;
    uint8_t primary_color[4];
    char material_name[32];
    float density;
    float diameter;
    float initial_weight_g;
    char manufacturer[33];
    int32_t spoolman_id;
    uint8_t suppress_spoolman_sync;
};

struct SpoolUpdatedPayload {
    char spool_id[17];
    uint8_t update_type;
    bool success;
    uint8_t suppress_sync;
    float kg_remaining;
};

struct BlankTagPayload {
    char spool_id[17];
};

struct SpoolmanSyncedPayload {
    char spool_id[17];
    bool success;
    float kg_remaining;
    int32_t spoolman_id;
};

struct TagRemovedPayload {
    char spool_id[17];
    float last_remaining_kg;
    int32_t spoolman_id;
};

struct HAWriteTagPayload {
    char expected_uid[17];
    uint8_t material_type;
    uint8_t color[4];
    char manufacturer[33];
    float initial_weight_g;
    float remaining_g;
    int32_t spoolman_id;
};

struct HAUpdateRemainingPayload {
    char expected_uid[17];
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
            float filament_used_grams;
            bool canceled;
        } printEnded;
        SpoolDetectedPayload spoolDetected;
        SpoolUpdatedPayload spoolUpdated;
        BlankTagPayload blankTag;
        SpoolmanSyncedPayload spoolmanSynced;
        TagRemovedPayload tagRemoved;
        HAWriteTagPayload haWriteTag;
        HAUpdateRemainingPayload haUpdateRemaining;
    } payload;
};

// Stub ApplicationManager for testing NFCManager in isolation
class ApplicationManager {
public:
    static ApplicationManager& getInstance() {
        static ApplicationManager instance;
        return instance;
    }

    bool begin(LCDManager* lcd = nullptr) {
        (void)lcd;
        return true;
    }

    bool sendMessage(const AppMessage& msg, uint32_t waitMs = 0) {
        (void)waitMs;
        sentMessages_.push_back(msg);
        return true;
    }

    // Test inspection
    const std::vector<AppMessage>& getSentMessages() const { return sentMessages_; }
    size_t getMessageCount() const { return sentMessages_.size(); }

    void reset() {
        sentMessages_.clear();
    }

private:
    ApplicationManager() = default;
    std::vector<AppMessage> sentMessages_;
};

#endif // STUB_APPLICATION_MANAGER_H
