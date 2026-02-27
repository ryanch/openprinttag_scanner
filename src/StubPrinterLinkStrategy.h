#ifndef STUB_PRINTER_LINK_STRATEGY_H
#define STUB_PRINTER_LINK_STRATEGY_H

#include "IPrinterLinkStrategy.h"

class StubPrinterLinkStrategy : public IPrinterLinkStrategy {
public:
    void update() override;

    bool hasActiveJob() const override { return hasJob; }
    int getJobId() const override { return jobId; }
    float getProgress() const override { return progress; }
    float getTotalFilamentGrams() const override { return totalFilamentG; }
    const char* getJobState() const override { return jobState; }
    bool isConnected() const override { return true; }  // Always connected

private:
    static constexpr int STUB_JOB_ID = 999;
    static constexpr float STUB_TOTAL_FILAMENT_G = 167.0f;
    static constexpr unsigned long JOB_START_DELAY_MS = 5000;      // 5 seconds
    static constexpr unsigned long PRINT_DURATION_MS = 60000;      // 60 seconds

    unsigned long startTimeMs = 0;
    bool started = false;

    bool hasJob = false;
    int jobId = -1;
    float progress = 0.0f;
    float totalFilamentG = 0.0f;
    char jobState[16] = {0};
};

#endif // STUB_PRINTER_LINK_STRATEGY_H
