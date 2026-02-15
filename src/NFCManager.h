#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#ifdef NATIVE_TEST
  #include "platform/NativePlatform.h"
#else
  #include <freertos/FreeRTOS.h>
  #include <freertos/queue.h>
  #include <freertos/semphr.h>
#endif
#include "NFCTypes.h"
#include "NFCConnectionI.h"

class NFCManager {
public:
    static NFCManager& getInstance();
    bool begin();                                    // Init hardware + queues
    void startScanTask();                            // Start FreeRTOS scan task
    bool enqueueWrite(const NFCWriteRequest& req);   // Queue a write request
    bool isRequestCompleted(uint32_t request_id);    // Check if request done
    void requestCurrentSpool();                      // Clear dedup to resend current spool
    bool scanOnce();                                 // Single scan cycle (for testing)
    const CurrentSpoolState& getCurrentSpoolState() const { return currentSpool; }

    // Dependency injection for testing
    void setConnection(NFCConnectionI* conn) { connection_ = conn; }

    // Recent spools history (RAM only)
    static constexpr size_t MAX_RECENT_SPOOLS = 10;
    size_t getRecentSpools(RecentSpoolEntry* entries, size_t maxEntries);

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
    void sendSpoolDetectedMessage();
    void sendBlankTagMessage();
    void processWriteQueue();
    bool executeWrite(const NFCWriteRequest& request);
    void sendSpoolUpdatedMessage(uint32_t request_id, NFCWriteType type, bool success);

    // Deduplication
    void markRequestCompleted(uint32_t request_id);
    bool isDuplicateSpool(const uint8_t* uid, uint8_t uid_length);

    // State
    CurrentSpoolState currentSpool;
    uint8_t lastSeenUid[8];      // ISO15693 uses 8-byte UID
    uint8_t lastSeenUidLength = 0;
    bool lastSeenValid = false;

    // Recent spools history (RAM only, most recent first)
    RecentSpoolEntry recentSpools[MAX_RECENT_SPOOLS];
    size_t recentSpoolsCount = 0;
    void addToRecentSpools();

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
};

#endif // NFC_MANAGER_H
