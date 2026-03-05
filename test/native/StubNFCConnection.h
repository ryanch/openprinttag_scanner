#ifndef STUB_NFC_CONNECTION_H
#define STUB_NFC_CONNECTION_H

#include "NFCConnectionI.h"
#include <vector>
#include <cstring>

// Test stub for NFC connection - allows simulating tag presence and tracking writes
class StubNFCConnection : public NFCConnectionI {
public:
    StubNFCConnection() {
        memset(&hal_, 0, sizeof(hal_));
        memset(currentUid_, 0, sizeof(currentUid_));
        memset(tagData_, 0, sizeof(tagData_));

        hal_.read_page = halReadPage;
        hal_.write_page = halWritePage;
        hal_.is_present = nullptr;
        hal_.user_ctx = this;
    }

    ~StubNFCConnection() override = default;

    // NFCConnectionI interface
    bool begin() override { return true; }
    void reset() override {}
    bool hardwareReset() override { return true; }
    bool setupRF() override { return true; }

    bool detectTag(uint8_t* uid, uint8_t* uidLength) override {
        if (!tagPresent_) {
            return false;
        }
        memcpy(uid, tagUid_, tagUidLength_);
        *uidLength = tagUidLength_;
        return true;
    }

    void setCurrentUid(const uint8_t* uid, uint8_t length) override {
        memcpy(currentUid_, uid, length < 8 ? length : 8);
    }

    opt_nfc_hal_t* getHal() override { return &hal_; }

    // Test control methods
    void setTagPresent(bool present) { tagPresent_ = present; }

    void setTagUid(const uint8_t* uid, uint8_t length) {
        memcpy(tagUid_, uid, length < 8 ? length : 8);
        tagUidLength_ = length;
    }

    void setTagData(const uint8_t* data, size_t size) {
        size_t copySize = size < sizeof(tagData_) ? size : sizeof(tagData_);
        memcpy(tagData_, data, copySize);
        tagDataSize_ = copySize;
    }

    void setReadError(bool error) { readError_ = error; }
    void setWriteError(bool error) { writeError_ = error; }
    void setFailNextWrites(size_t count) { failNextWrites_ = count; }

    // Test inspection methods
    struct PageWrite {
        uint8_t page;
        uint8_t data[4];
    };

    const std::vector<PageWrite>& getPageWrites() const { return pageWrites_; }
    size_t getWriteCount() const { return pageWrites_.size(); }
    void clearPageWrites() { pageWrites_.clear(); }

    // Reset test state (not to be confused with hardware reset())
    void resetTestState() {
        tagPresent_ = false;
        readError_ = false;
        writeError_ = false;
        failNextWrites_ = 0;
        pageWrites_.clear();
        memset(tagData_, 0, sizeof(tagData_));
        tagDataSize_ = 0;
    }

private:
    opt_nfc_hal_t hal_;
    uint8_t currentUid_[8];

    // Tag simulation state
    bool tagPresent_ = false;
    uint8_t tagUid_[8] = {0};
    uint8_t tagUidLength_ = 8;
    uint8_t tagData_[320] = {0};
    size_t tagDataSize_ = 0;

    // Error simulation
    bool readError_ = false;
    bool writeError_ = false;
    size_t failNextWrites_ = 0;

    // Write tracking
    std::vector<PageWrite> pageWrites_;

    // Static HAL callbacks
    static opt_error_t halReadPage(void* ctx, uint8_t page, uint8_t* buffer) {
        StubNFCConnection* self = static_cast<StubNFCConnection*>(ctx);
        if (self->readError_) {
            return OPT_ERR_NFC_READ;
        }
        size_t offset = page * 4;
        if (offset + 4 <= self->tagDataSize_) {
            memcpy(buffer, self->tagData_ + offset, 4);
        } else {
            memset(buffer, 0, 4);
        }
        return OPT_OK;
    }

    static opt_error_t halWritePage(void* ctx, uint8_t page, const uint8_t* data) {
        StubNFCConnection* self = static_cast<StubNFCConnection*>(ctx);
        if (self->writeError_) {
            return OPT_ERR_NFC_WRITE;
        }
        if (self->failNextWrites_ > 0) {
            self->failNextWrites_--;
            return OPT_ERR_NFC_WRITE;
        }

        // Track the write
        PageWrite pw;
        pw.page = page;
        memcpy(pw.data, data, 4);
        self->pageWrites_.push_back(pw);

        // Update internal tag data
        size_t offset = page * 4;
        if (offset + 4 <= sizeof(self->tagData_)) {
            memcpy(self->tagData_ + offset, data, 4);
            if (offset + 4 > self->tagDataSize_) {
                self->tagDataSize_ = offset + 4;
            }
        }

        return OPT_OK;
    }
};

#endif // STUB_NFC_CONNECTION_H
