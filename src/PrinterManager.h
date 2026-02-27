#ifndef PRINTER_MANAGER_H
#define PRINTER_MANAGER_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "IPrinterLinkStrategy.h"

enum class PrinterState {
    IDLE,
    TRACKING
};

class PrinterManager {
public:
    static PrinterManager& getInstance();

    void begin();
    void poll();
    void startPollingTask();
    void setStrategy(IPrinterLinkStrategy* strategy);
    bool isConnected() const;

private:
    PrinterManager() = default;
    PrinterManager(const PrinterManager&) = delete;
    PrinterManager& operator=(const PrinterManager&) = delete;

    static void pollingTaskFunc(void* param);

    void handleJobDetected(int jobId, float totalFilamentG);
    void resolveAndSendJobEnd(int jobId, float progressPercent);
    void handleJobDisappeared();

    PrinterState state = PrinterState::IDLE;
    int currentJobId = -1;
    float currentJobTotalFilamentG = 0.0f;
    float lastProgressPercent = 0.0f;

    IPrinterLinkStrategy* strategy = nullptr;

    TaskHandle_t pollingTaskHandle = nullptr;
    static constexpr size_t POLLING_TASK_STACK_SIZE = 6144;
    static constexpr UBaseType_t POLLING_TASK_PRIORITY = 1;
};

#endif // PRINTER_MANAGER_H
