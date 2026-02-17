#ifndef TEST_NFC_MANAGER_H
#define TEST_NFC_MANAGER_H

#include "NFCTypes.h"
#include <vector>
#include <cstring>

// Test implementation of NFCManager that tracks write requests
// Uses shared types from NFCTypes.h - no duplication
class NFCManager {
public:
    static NFCManager& getInstance() {
        static NFCManager instance;
        return instance;
    }

    bool enqueueWrite(const NFCWriteRequest& req) {
        writeRequests_.push_back(req);
        return true;
    }

    void requestCurrentSpool() {
        requestCurrentSpoolCalled_ = true;
    }

    bool getCurrentSpoolState(CurrentSpoolState& out) {
        out = currentSpool_;
        return true;
    }

    void updateRecentSpoolSyncStatus(const char* spool_id, bool synced) {
        (void)spool_id;
        (void)synced;
    }

    // Test inspection methods
    size_t getWriteCount() const { return writeRequests_.size(); }

    const std::vector<NFCWriteRequest>& getWriteRequests() const {
        return writeRequests_;
    }

    bool hasWriteForSpool(const char* spoolId) const {
        for (const auto& req : writeRequests_) {
            if (strcmp(req.expected_spool_id, spoolId) == 0) return true;
        }
        return false;
    }

    bool wasRequestCurrentSpoolCalled() const {
        return requestCurrentSpoolCalled_;
    }

    // Set current spool for testing UID validation
    void setCurrentSpool(const CurrentSpoolState& spool) {
        currentSpool_ = spool;
    }

    // Reset for test isolation
    void reset() {
        writeRequests_.clear();
        requestCurrentSpoolCalled_ = false;
        memset(&currentSpool_, 0, sizeof(currentSpool_));
    }

private:
    NFCManager() { memset(&currentSpool_, 0, sizeof(currentSpool_)); }
    std::vector<NFCWriteRequest> writeRequests_;
    bool requestCurrentSpoolCalled_ = false;
    CurrentSpoolState currentSpool_;
};

#endif // TEST_NFC_MANAGER_H
