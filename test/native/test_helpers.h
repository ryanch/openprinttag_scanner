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
    msg.type = AppMessageType::PRINT_FINISHED;
    msg.payload.printFinished.job_id = jobId;
    msg.payload.printFinished.filament_used_grams = filamentUsedGrams;
    return msg;
}

inline AppMessage createPrintCanceled(int jobId, float estFilamentUsedGrams) {
    AppMessage msg;
    msg.type = AppMessageType::PRINT_CANCELED;
    msg.payload.printCanceled.job_id = jobId;
    msg.payload.printCanceled.est_filament_used_grams = estFilamentUsedGrams;
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
