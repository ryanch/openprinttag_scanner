#ifndef I_PRINTER_LINK_STRATEGY_H
#define I_PRINTER_LINK_STRATEGY_H

#include <Arduino.h>

class IPrinterLinkStrategy {
public:
    virtual ~IPrinterLinkStrategy() = default;

    // Called each poll cycle - strategy updates its internal state
    virtual void update() = 0;

    // Job status queries
    virtual bool hasActiveJob() const = 0;
    virtual int getJobId() const = 0;
    virtual float getProgress() const = 0;
    virtual float getTotalFilamentGrams() const = 0;

    // Job state: "PRINTING", "PAUSED", "FINISHED", "STOPPED", "ERROR", or ""
    virtual const char* getJobState() const = 0;

    // Connection status
    virtual bool isConnected() const = 0;

    // Deferred filament fetch for a specific job.
    // Implementations must return data only for the requested job ID.
    virtual float fetchDeferredFilament(int expectedJobId) {
        (void)expectedJobId;
        return 0.0f;
    }
};

#endif // I_PRINTER_LINK_STRATEGY_H
