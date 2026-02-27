#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "ApplicationManager.h"
#include <cstring>

// Message factory functions
inline AppMessage createSpoolDetected(const char* spoolId, uint8_t materialType,
                                       float kgRemaining, const char* materialName = "PLA",
                                       int32_t spoolmanId = -1) {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::SPOOL_DETECTED;
    strncpy(msg.payload.spoolDetected.spool_id, spoolId,
            sizeof(msg.payload.spoolDetected.spool_id) - 1);
    msg.payload.spoolDetected.spool_id[sizeof(msg.payload.spoolDetected.spool_id) - 1] = '\0';
    msg.payload.spoolDetected.material_type = materialType;
    msg.payload.spoolDetected.kg_remaining = kgRemaining;
    msg.payload.spoolDetected.primary_color[0] = 255;  // R
    msg.payload.spoolDetected.primary_color[1] = 255;  // G
    msg.payload.spoolDetected.primary_color[2] = 255;  // B
    msg.payload.spoolDetected.primary_color[3] = 255;  // A
    strncpy(msg.payload.spoolDetected.material_name, materialName,
            sizeof(msg.payload.spoolDetected.material_name) - 1);
    msg.payload.spoolDetected.material_name[sizeof(msg.payload.spoolDetected.material_name) - 1] = '\0';
    msg.payload.spoolDetected.density = 0.0f;
    msg.payload.spoolDetected.diameter = 0.0f;
    msg.payload.spoolDetected.initial_weight_g = kgRemaining * 1000.0f;
    msg.payload.spoolDetected.manufacturer[0] = '\0';
    msg.payload.spoolDetected.spoolman_id = spoolmanId;
    return msg;
}

inline AppMessage createPrintStarted(int jobId) {
    AppMessage msg;
    msg.type = AppMessageType::PRINT_STARTED;
    msg.payload.printStarted.job_id = jobId;
    return msg;
}

inline AppMessage createPrintFinished(int jobId, float filamentUsedGrams) {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::PRINT_ENDED;
    msg.payload.printEnded.job_id = jobId;
    msg.payload.printEnded.filament_used_grams = filamentUsedGrams;
    msg.payload.printEnded.canceled = false;
    return msg;
}

inline AppMessage createPrintCanceled(int jobId, float estFilamentUsedGrams) {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::PRINT_ENDED;
    msg.payload.printEnded.job_id = jobId;
    msg.payload.printEnded.filament_used_grams = estFilamentUsedGrams;
    msg.payload.printEnded.canceled = true;
    return msg;
}

inline AppMessage createSpoolUpdated(const char* spoolId, bool success,
                                      float kgRemaining, uint8_t updateType = 0) {
    AppMessage msg;
    msg.type = AppMessageType::SPOOL_UPDATED;
    strncpy(msg.payload.spoolUpdated.spool_id, spoolId,
            sizeof(msg.payload.spoolUpdated.spool_id) - 1);
    msg.payload.spoolUpdated.spool_id[sizeof(msg.payload.spoolUpdated.spool_id) - 1] = '\0';
    msg.payload.spoolUpdated.success = success;
    msg.payload.spoolUpdated.kg_remaining = kgRemaining;
    msg.payload.spoolUpdated.update_type = updateType;
    return msg;
}

inline AppMessage createTagRemoved(const char* spoolId, float lastRemainingKg = 0.0f,
                                    int32_t spoolmanId = -1) {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::TAG_REMOVED;
    strncpy(msg.payload.tagRemoved.spool_id, spoolId,
            sizeof(msg.payload.tagRemoved.spool_id) - 1);
    msg.payload.tagRemoved.last_remaining_kg = lastRemainingKg;
    msg.payload.tagRemoved.spoolman_id = spoolmanId;
    return msg;
}

inline AppMessage createBlankTagDetected(const char* spoolId) {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::BLANK_TAG_DETECTED;
    strncpy(msg.payload.blankTag.spool_id, spoolId,
            sizeof(msg.payload.blankTag.spool_id) - 1);
    msg.payload.blankTag.spool_id[sizeof(msg.payload.blankTag.spool_id) - 1] = '\0';
    return msg;
}

inline AppMessage createHAWriteTag(const char* expectedUid, uint8_t materialType,
                                    const uint8_t* color, const char* manufacturer,
                                    float initialWeightG, float remainingG,
                                    int32_t spoolmanId = -1) {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::HA_WRITE_TAG;
    strncpy(msg.payload.haWriteTag.expected_uid, expectedUid,
            sizeof(msg.payload.haWriteTag.expected_uid) - 1);
    msg.payload.haWriteTag.material_type = materialType;
    memcpy(msg.payload.haWriteTag.color, color, 4);
    strncpy(msg.payload.haWriteTag.manufacturer, manufacturer,
            sizeof(msg.payload.haWriteTag.manufacturer) - 1);
    msg.payload.haWriteTag.initial_weight_g = initialWeightG;
    msg.payload.haWriteTag.remaining_g = remainingG;
    msg.payload.haWriteTag.spoolman_id = spoolmanId;
    return msg;
}

inline AppMessage createHAUpdateRemaining(const char* expectedUid, float remainingG) {
    AppMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = AppMessageType::HA_UPDATE_REMAINING;
    strncpy(msg.payload.haUpdateRemaining.expected_uid, expectedUid,
            sizeof(msg.payload.haUpdateRemaining.expected_uid) - 1);
    msg.payload.haUpdateRemaining.remaining_g = remainingG;
    return msg;
}

// Test assertion helpers
#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            return 1; \
        } \
    } while(0)

#define TEST_ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("FAIL: %s != %s at %s:%d\n", #a, #b, __FILE__, __LINE__); \
            return 1; \
        } \
    } while(0)

#define TEST_ASSERT_STR_CONTAINS(str, substr) \
    do { \
        if (std::string(str).find(substr) == std::string::npos) { \
            printf("FAIL: \"%s\" does not contain \"%s\" at %s:%d\n", \
                   (str).c_str(), substr, __FILE__, __LINE__); \
            return 1; \
        } \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        printf("Running %s... ", #test_func); \
        int result = test_func(); \
        if (result == 0) { \
            printf("PASS\n"); \
            passed++; \
        } else { \
            failed++; \
        } \
        total++; \
    } while(0)

#endif // TEST_HELPERS_H
