#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#ifdef NATIVE_TEST
  #include "platform/NativePlatform.h"
#else
  #include <freertos/FreeRTOS.h>
  #include <freertos/queue.h>
  #include <freertos/semphr.h>
  #include <esp_task_wdt.h>
#endif
#include "NFCTypes.h"
#include "NFCConnectionI.h"

class NFCManager {
public:
    static NFCManager& getInstance();
    bool begin();                                    // Init hardware + queues
    void startScanTask();                            // Start FreeRTOS scan task
    bool enqueueWrite(const NFCWriteRequest& req);   // Queue a write request
    bool enqueueRawWrite(const NFCWriteRequest& req, const uint8_t* data, size_t dataSize);
    bool writeSpoolmanDataToTag(int32_t spoolman_id, const char* expected_spool_id = nullptr);
    bool isRequestCompleted(uint32_t request_id);    // Check if request done
    void requestCurrentSpool();                      // Clear dedup to resend current spool
    bool scanOnce();                                 // Single scan cycle (for testing)
    bool getCurrentSpoolState(CurrentSpoolState& out);

    // Dependency injection for testing
    void setConnection(NFCConnectionI* conn) { connection_ = conn; }
    void resetWriteState() {
        rawWritePending_ = false;
        // Drain write queue
        if (writeQueue) {
            NFCWriteRequest dummy;
            while (xQueueReceive(writeQueue, &dummy, 0) == pdTRUE) {}
        }
    }

    // Recent spools history (RAM only)
    static constexpr size_t MAX_RECENT_SPOOLS = 10;
    size_t getRecentSpools(RecentSpoolEntry* entries, size_t maxEntries);
    void updateRecentSpoolSyncStatus(const char* spool_id, bool synced);

private:
    NFCManager() = default;
    NFCManager(const NFCManager&) = delete;
    NFCManager& operator=(const NFCManager&) = delete;

    // Hardware connection (injected or created internally)
    NFCConnectionI* connection_ = nullptr;
    bool ownsConnection_ = false;

    // Scan task
    static void scanTaskFunc(void* param);
    void scanLoop();

    // Internal operations
    bool readAndParseTag(uint8_t* uid, uint8_t uid_length);
    bool formatNewSpool();
    void sendSpoolDetectedMessage(bool suppress_spoolman_sync = false);
    void sendBlankTagMessage();
    void sendTagRemovedMessage();
    void processWriteQueue();
    bool executeWrite(const NFCWriteRequest& request);
    void sendSpoolUpdatedMessage(uint32_t request_id, NFCWriteType type, bool success);

    // Deduplication
    void markRequestCompleted(uint32_t request_id);
    bool isDuplicateSpool(const uint8_t* uid, uint8_t uid_length);
    uint32_t generateRequestId();

    // State
    CurrentSpoolState currentSpool;
    uint8_t lastSeenUid[8];      // ISO15693 uses 8-byte UID
    uint8_t lastSeenUidLength = 0;
    bool lastSeenValid = false;

    // Recent spools history (RAM only, most recent first)
    RecentSpoolEntry recentSpools[MAX_RECENT_SPOOLS];
    size_t recentSpoolsCount = 0;
    void addToRecentSpools();

    // Raw tag write sidecar buffer (filled by BLE context, consumed by scan task)
    static constexpr size_t RAW_WRITE_BUFFER_SIZE = 320;
    uint8_t rawWriteBuffer_[RAW_WRITE_BUFFER_SIZE];
    size_t rawWriteBufferSize_ = 0;
    bool rawWritePending_ = false;
    bool writeRawTag();
    opt_tag_t writeScratchTag_;   // Reused by scan task write path to avoid large stack frames

    // Write queue (FreeRTOS)
    QueueHandle_t writeQueue = nullptr;
    SemaphoreHandle_t tagMutex = nullptr;

    // Completed request tracking (circular buffer)
    static constexpr size_t COMPLETED_REQUESTS_SIZE = 32;
    uint32_t completedRequests[COMPLETED_REQUESTS_SIZE];
    size_t completedRequestsIndex = 0;
    SemaphoreHandle_t completedMutex = nullptr;
    static uint32_t s_write_request_id_counter;

    // Task handle
    TaskHandle_t scanTaskHandle = nullptr;

    // NFC watchdog: recovery after consecutive failures
    static constexpr uint32_t RECOVERY_THRESHOLD = 600;   // ~30s at 50ms/scan
    static constexpr uint32_t RESTART_THRESHOLD = 1200;    // ~60s at 50ms/scan
    static constexpr uint32_t NFC_WDT_TIMEOUT_S = 30;     // Task watchdog timeout
    uint32_t consecutiveFailures_ = 0;
    void attemptRecovery();

    // Write batch tracking: prevents tag re-reads during batched writes
    volatile bool suppressReDetection_ = false;
    char suppressReDetectionUid_[17] = {0};
    volatile bool batchHadSuppressSync_ = false;  // Track if any write in batch had suppress_sync
};

#endif // NFC_MANAGER_H
