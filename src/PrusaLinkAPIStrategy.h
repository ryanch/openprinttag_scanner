#ifndef PRUSA_LINK_API_STRATEGY_H
#define PRUSA_LINK_API_STRATEGY_H

#include "IPrinterLinkStrategy.h"

class PrusaLinkAPIStrategy : public IPrinterLinkStrategy {
public:
    void update() override;

    bool hasActiveJob() const override { return hasJob; }
    int getJobId() const override { return jobId; }
    float getProgress() const override { return progress; }
    float getTotalFilamentGrams() const override { return totalFilamentG; }
    String getJobState() const override { return jobState; }
    bool isConnected() const override { return connected; }

private:
    float fetchFilamentFromBgcode(const String& downloadRef);

    bool connected = false;
    bool hasJob = false;
    int jobId = -1;
    float progress = 0.0f;
    float totalFilamentG = 0.0f;
    String jobState = "";

    // Cache bgcode filament fetch (one attempt per job)
    int bgcodeFilamentJobId = -1;
    float bgcodeFilamentG = 0.0f;
};

#endif // PRUSA_LINK_API_STRATEGY_H
