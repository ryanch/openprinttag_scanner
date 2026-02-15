#ifndef STUB_APPLICATION_MANAGER_H
#define STUB_APPLICATION_MANAGER_H

#include <cstdint>
#include <vector>
#include <cstring>

class LCDManager;

// These types must match ApplicationManager.h exactly
enum class AppMessageType {
    PRINT_STARTED,
    PRINT_CANCELED,
    PRINT_FINISHED,
    SPOOL_DETECTED,
    SPOOL_UPDATED,
    BLANK_TAG_DETECTED,
};

enum class AppState { IDLE, MONITORING_PRINT };

struct SpoolDetectedPayload {
    char spool_id[64];
    uint8_t material_type;
    float kg_remaining;
    uint8_t primary_color[4];
    char material_name[32];
};

struct SpoolUpdatedPayload {
    char spool_id[64];
    uint8_t update_type;
    bool success;
    float kg_remaining;
};

struct BlankTagPayload {
    char spool_id[64];
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
