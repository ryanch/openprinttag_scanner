#ifndef PRUSA_LINK_API_STRATEGY_H
#define PRUSA_LINK_API_STRATEGY_H

#include "IPrinterLinkStrategy.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class PrusaLinkAPIStrategy : public IPrinterLinkStrategy {
public:
    void setHttpMutex(SemaphoreHandle_t mutex) { httpMutex_ = mutex; }
    void update() override;

    bool hasActiveJob() const override { return hasJob; }
    int getJobId() const override { return jobId; }
    float getProgress() const override { return progress; }
    float getTotalFilamentGrams() const override { return totalFilamentG; }
    const char* getJobState() const override { return jobState; }
    bool isConnected() const override { return connected; }
    float fetchDeferredFilament(int expectedJobId) override;

private:
    static constexpr size_t BGCODE_BUF_SIZE = 8192;
    float fetchFilamentFromBgcode(const char* downloadRef);

    bool connected = false;
    bool hasJob = false;
    int jobId = -1;
    float progress = 0.0f;
    float totalFilamentG = 0.0f;
    char jobState[16] = {0};

    // Cache bgcode filament fetch (one attempt per job)
    int bgcodeFilamentJobId = -1;
    float bgcodeFilamentG = 0.0f;

    // Saved download ref for deferred bgcode fetch after print completes
    char savedDownloadRef[160] = {0};
    int savedDownloadRefJobId = -1;
    uint8_t bgcodeBuf_[BGCODE_BUF_SIZE] = {0};

    SemaphoreHandle_t httpMutex_ = nullptr;
};

#endif // PRUSA_LINK_API_STRATEGY_H
