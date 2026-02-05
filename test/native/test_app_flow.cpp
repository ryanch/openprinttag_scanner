// Native tests for ApplicationManager flow
// Build with: cd test/native && make

#include "platform/NativePlatform.h"
#include "FakeLCDManager.h"
#include "test_helpers.h"

// Include ApplicationManager implementation directly (unity build)
#include "ApplicationManager.cpp"

#include "TestableApplicationManager.h"

// Reset singleton state between tests (hacky but works for testing)
static LCDManager* g_lcd = nullptr;
static TestableApplicationManager* g_app = nullptr;

void setup_test() {
    if (g_lcd) delete g_lcd;
    if (g_app) delete g_app;
    g_lcd = new LCDManager();
    g_app = new TestableApplicationManager(g_lcd);
}

void teardown_test() {
    // Singleton persists, but LCD is reset
    if (g_lcd) g_lcd->reset();
}

// Test: Spool detected shows info on LCD
int test_spool_detected_shows_lcd() {
    setup_test();

    g_app->injectMessage(createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));

    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "PLA");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "850");

    teardown_test();
    return 0;
}

// Test: Print started transitions to MONITORING state
int test_print_started_transitions_state() {
    setup_test();

    g_app->injectMessage(createPrintStarted(123));

    TEST_ASSERT_EQ(g_app->getState(), AppState::MONITORING_PRINT);
    TEST_ASSERT_EQ(g_app->getCurrentJobId(), 123);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Print Started");

    teardown_test();
    return 0;
}

// Test: Complete print cycle - spool detected, print, finish, update
int test_complete_print_cycle() {
    setup_test();

    // 1. Spool detected at boot
    g_app->injectMessage(createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "PLA");

    // 2. Print starts
    g_app->injectMessage(createPrintStarted(123));
    TEST_ASSERT_EQ(g_app->getState(), AppState::MONITORING_PRINT);

    // 3. Spool detected again during print (captured as starting spool)
    g_app->injectMessage(createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));
    TEST_ASSERT(strcmp(g_app->getStartingSpoolId(), "SPOOL001") == 0);

    // 4. Print finishes with 50g used
    g_app->injectMessage(createPrintFinished(123, 50.0f));
    TEST_ASSERT_EQ(g_app->getState(), AppState::IDLE);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Updating spool");

    // 5. Spool update confirmed (normally from NFC manager)
    g_app->injectMessage(createSpoolUpdated("SPOOL001", true, 0.800f));
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Spool Updated");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "800");

    teardown_test();
    return 0;
}

// Test: Spool swap during print prevents update
int test_spool_swap_during_print() {
    setup_test();

    // Print starts
    g_app->injectMessage(createPrintStarted(456));

    // First spool detected
    g_app->injectMessage(createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));
    TEST_ASSERT(strcmp(g_app->getStartingSpoolId(), "SPOOL001") == 0);

    // Different spool detected (swap!)
    g_app->injectMessage(createSpoolDetected("SPOOL002", OPT_MATERIAL_TYPE_PETG, 0.500f, "PETG"));
    TEST_ASSERT(g_app->hasSpoolChangedDuringPrint());

    // Print finishes
    g_app->injectMessage(createPrintFinished(456, 30.0f));
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Spool changed");

    teardown_test();
    return 0;
}

// Test: Print canceled with estimated filament
int test_print_canceled() {
    setup_test();

    // Print starts
    g_app->injectMessage(createPrintStarted(789));

    // Spool detected
    g_app->injectMessage(createSpoolDetected("SPOOL003", OPT_MATERIAL_TYPE_ABS, 1.0f, "ABS"));

    // Print canceled with estimate
    g_app->injectMessage(createPrintCanceled(789, 25.0f));
    TEST_ASSERT_EQ(g_app->getState(), AppState::IDLE);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Updating spool");

    teardown_test();
    return 0;
}

// Test: No spool detected during print
int test_no_spool_during_print() {
    setup_test();

    // Print starts without any spool
    g_app->injectMessage(createPrintStarted(111));

    // Print finishes
    g_app->injectMessage(createPrintFinished(111, 20.0f));
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "No spool");

    teardown_test();
    return 0;
}

// Test: Zero filament used
int test_zero_filament_used() {
    setup_test();

    g_app->injectMessage(createPrintStarted(222));
    g_app->injectMessage(createSpoolDetected("SPOOL004", OPT_MATERIAL_TYPE_TPU, 0.5f, "TPU"));
    g_app->injectMessage(createPrintFinished(222, 0.0f));

    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Print done");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "No filament");

    teardown_test();
    return 0;
}

// Test: Spool update failure
int test_spool_update_failure() {
    setup_test();

    g_app->injectMessage(createSpoolUpdated("SPOOL005", false, 0.0f));
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Spool Update");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "Failed");

    teardown_test();
    return 0;
}

// Test: Duplicate spool detection doesn't update LCD repeatedly
int test_duplicate_spool_detection() {
    setup_test();

    g_app->injectMessage(createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));
    int firstCount = g_lcd->updateCount;

    // Same spool again - should not update LCD
    g_app->injectMessage(createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));
    TEST_ASSERT_EQ(g_lcd->updateCount, firstCount);

    // Different spool - should update
    g_app->injectMessage(createSpoolDetected("SPOOL002", OPT_MATERIAL_TYPE_PETG, 0.500f, "PETG"));
    TEST_ASSERT_EQ(g_lcd->updateCount, firstCount + 1);

    teardown_test();
    return 0;
}

int main() {
    int passed = 0, failed = 0, total = 0;

    printf("\n=== ApplicationManager Flow Tests ===\n\n");

    RUN_TEST(test_spool_detected_shows_lcd);
    RUN_TEST(test_print_started_transitions_state);
    RUN_TEST(test_complete_print_cycle);
    RUN_TEST(test_spool_swap_during_print);
    RUN_TEST(test_print_canceled);
    RUN_TEST(test_no_spool_during_print);
    RUN_TEST(test_zero_filament_used);
    RUN_TEST(test_spool_update_failure);
    RUN_TEST(test_duplicate_spool_detection);

    printf("\n=== Results: %d/%d passed ===\n", passed, total);

    return failed > 0 ? 1 : 0;
}
