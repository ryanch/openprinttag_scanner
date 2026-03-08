#ifndef TESTABLE_APPLICATION_MANAGER_H
#define TESTABLE_APPLICATION_MANAGER_H

#include "ApplicationManager.h"
#include "FakeLCDManager.h"

// Test wrapper that provides direct access to ApplicationManager
// bypassing FreeRTOS queues for native testing
class TestableApplicationManager {
public:
    TestableApplicationManager(LCDManager* lcd) : lcd_(lcd) {
        // Initialize the singleton with our fake LCD
        auto& app = ApplicationManager::getInstance();
        app.resetForTest();
        app.begin(lcd);
    }

    void injectMessage(const AppMessage& msg) {
        ApplicationManager::getInstance().handleMessage(msg);
    }

    void processMessages() {
        ApplicationManager::getInstance().processMessages();
    }

    AppState getState() const {
        return ApplicationManager::getInstance().getState();
    }

    const char* getStartingSpoolId() const {
        return ApplicationManager::getInstance().getStartingSpoolId();
    }

    int getCurrentJobId() const {
        return ApplicationManager::getInstance().getCurrentJobId();
    }

    bool hasSpoolChangedDuringPrint() const {
        return ApplicationManager::getInstance().hasSpoolChangedDuringPrint();
    }

    LCDManager* getLCD() { return lcd_; }

private:
    LCDManager* lcd_;
};

#endif // TESTABLE_APPLICATION_MANAGER_H
