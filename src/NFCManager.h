#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <Adafruit_PN532.h>
#include "openprinttag_lib.h"

enum class NFCWriteType : uint8_t {
    REMOVE_WEIGHT,        // Subtract grams from spool
    CHANGE_COLOR,         // Set new primary color
    CHANGE_FILAMENT_TYPE  // Set new material type
};

struct NFCWriteRequest {
    uint32_t request_id;         // Unique ID for deduplication
    NFCWriteType type;
    union {
        float grams_to_remove;
        uint8_t new_color[4];    // RGBA
        uint8_t new_material_type;
    } data;
};

struct CurrentSpoolState {
    bool present;
    char spool_id[64];
    uint8_t uid[7];
    uint8_t uid_length;
    opt_tag_t tag_data;          // Cached openprinttag data
    bool tag_data_valid;
};

class NFCManager {
public:
    static NFCManager& getInstance();
    bool begin();                                    // Init hardware + queues
    void startScanTask();                            // Start FreeRTOS scan task
    bool enqueueWrite(const NFCWriteRequest& req);   // Queue a write request
    bool isRequestCompleted(uint32_t request_id);    // Check if request done

private:
    NFCManager() = default;
    NFCManager(const NFCManager&) = delete;
    NFCManager& operator=(const NFCManager&) = delete;

    // Hardware
    Adafruit_PN532* nfc = nullptr;

    // HAL for openprinttag
    opt_nfc_hal_t nfcHal;

    // Scan task
    static void scanTaskFunc(void* param);
    void scanLoop();

    // Internal operations
    bool readAndParseTag(uint8_t* uid, uint8_t uid_length);
    bool formatNewSpool();
    void sendSpoolDetectedMessage();
    void processWriteQueue();
    bool executeWrite(const NFCWriteRequest& request);
    void sendSpoolUpdatedMessage(uint32_t request_id, NFCWriteType type, bool success);

    // Deduplication
    void markRequestCompleted(uint32_t request_id);
    bool isDuplicateSpool(const uint8_t* uid, uint8_t uid_length);

    // State
    CurrentSpoolState currentSpool;
    uint8_t lastSeenUid[7];
    uint8_t lastSeenUidLength = 0;
    bool lastSeenValid = false;

    // Write queue (FreeRTOS)
    QueueHandle_t writeQueue = nullptr;
    SemaphoreHandle_t tagMutex = nullptr;

    // Completed request tracking (circular buffer)
    static constexpr size_t COMPLETED_REQUESTS_SIZE = 32;
    uint32_t completedRequests[COMPLETED_REQUESTS_SIZE];
    size_t completedRequestsIndex = 0;
    SemaphoreHandle_t completedMutex = nullptr;

    // Task handle
    TaskHandle_t scanTaskHandle = nullptr;

    // Pin definitions
    static constexpr int PN532_SCK = 14;
    static constexpr int PN532_MISO = 27;
    static constexpr int PN532_MOSI = 26;
    static constexpr int PN532_SS = 25;
};

#endif // NFC_MANAGER_H
