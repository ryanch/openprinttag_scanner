// Native tests for ApplicationManager flow
// Build with: cd test/native && make

#include "platform/NativePlatform.h"
#include "FakeLCDManager.h"
#include "test_helpers.h"
#include "TestNFCManager.h"
#include <thread>
#include <chrono>

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
    NFCManager::getInstance().reset();
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
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Spool");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "Scanned");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine3, "PLA");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine4, "850");

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
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Spool");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine3, "PLA");

    // 2. Print starts
    g_app->injectMessage(createPrintStarted(123));
    TEST_ASSERT_EQ(g_app->getState(), AppState::MONITORING_PRINT);

    // 3. Spool detected again during print (captured as starting spool)
    g_app->injectMessage(createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));
    TEST_ASSERT(strcmp(g_app->getStartingSpoolId(), "SPOOL001") == 0);

    // 4. Print finishes with 50g used
    g_app->injectMessage(createPrintFinished(123, 50.0f));
    TEST_ASSERT_EQ(g_app->getState(), AppState::IDLE);
    TEST_ASSERT(g_lcd->lastScreenContains("Updating spool"));

    // Verify NFC write was enqueued with correct data
    auto& nfcMgr = NFCManager::getInstance();
    TEST_ASSERT_EQ(nfcMgr.getWriteCount(), 1);
    TEST_ASSERT(nfcMgr.hasWriteForSpool("SPOOL001"));
    const auto& req = nfcMgr.getWriteRequests()[0];
    TEST_ASSERT_EQ(req.type, NFCWriteType::REMOVE_WEIGHT);
    TEST_ASSERT_EQ(req.data.grams_to_remove, 50.0f);

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

    // Verify no NFC write was enqueued due to spool swap
    TEST_ASSERT_EQ(NFCManager::getInstance().getWriteCount(), 0);

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
    TEST_ASSERT(g_lcd->lastScreenContains("No spool"));

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

// Test: Job disappeared at high progress sends PRINT_FINISHED with total filament
// This simulates the fixed PrinterManager behavior where a job vanishing at >=95%
// is treated as completed, sending PRINT_FINISHED with the full filament amount
int test_high_progress_disappearance_deducts_filament() {
    setup_test();

    // 1. Spool detected
    g_app->injectMessage(createSpoolDetected("SPOOL_HP", OPT_MATERIAL_TYPE_PLA, 0.500f, "PLA"));

    // 2. Print starts
    g_app->injectMessage(createPrintStarted(999));
    TEST_ASSERT_EQ(g_app->getState(), AppState::MONITORING_PRINT);

    // 3. Spool captured during print
    g_app->injectMessage(createSpoolDetected("SPOOL_HP", OPT_MATERIAL_TYPE_PLA, 0.500f, "PLA"));
    TEST_ASSERT(strcmp(g_app->getStartingSpoolId(), "SPOOL_HP") == 0);

    // 4. Job disappears at high progress — PrinterManager now sends PRINT_FINISHED
    //    with total filament (e.g. 75g) instead of PRINT_CANCELED with 0g
    g_app->injectMessage(createPrintFinished(999, 75.0f));
    TEST_ASSERT_EQ(g_app->getState(), AppState::IDLE);
    TEST_ASSERT(g_lcd->lastScreenContains("Updating spool"));

    // Verify NFC write was enqueued with correct filament amount
    auto& nfcMgr = NFCManager::getInstance();
    TEST_ASSERT_EQ(nfcMgr.getWriteCount(), 1);
    TEST_ASSERT(nfcMgr.hasWriteForSpool("SPOOL_HP"));
    const auto& req = nfcMgr.getWriteRequests()[0];
    TEST_ASSERT_EQ(req.type, NFCWriteType::REMOVE_WEIGHT);
    TEST_ASSERT_EQ(req.data.grams_to_remove, 75.0f);

    teardown_test();
    return 0;
}

// Test: CONTROLLED_BY_HA mode does not auto-update NFC tag on print finish
int test_controlled_mode_no_auto_weight_update() {
    setup_test();

    auto& app = ApplicationManager::getInstance();
    app.setAutomationMode(AutomationMode::CONTROLLED_BY_HOME_ASSISTANT);

    // Print starts
    g_app->injectMessage(createPrintStarted(100));

    // Spool detected
    g_app->injectMessage(createSpoolDetected("SPOOL_HA", OPT_MATERIAL_TYPE_PLA, 0.500f, "PLA"));

    // Print finishes with filament used
    g_app->injectMessage(createPrintFinished(100, 50.0f));
    TEST_ASSERT_EQ(g_app->getState(), AppState::IDLE);

    // No NFC write should be enqueued in CONTROLLED_BY_HA mode
    TEST_ASSERT_EQ(NFCManager::getInstance().getWriteCount(), 0);

    // LCD should show HA controlled message
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "HA controlled");

    teardown_test();
    return 0;
}

// Test: SELF_DIRECTED mode auto-updates NFC tag on print finish (existing behavior)
int test_self_directed_auto_weight_update() {
    setup_test();

    auto& app = ApplicationManager::getInstance();
    app.setAutomationMode(AutomationMode::SELF_DIRECTED);

    g_app->injectMessage(createPrintStarted(101));
    g_app->injectMessage(createSpoolDetected("SPOOL_SD", OPT_MATERIAL_TYPE_PLA, 0.500f, "PLA"));
    g_app->injectMessage(createPrintFinished(101, 50.0f));

    // NFC write SHOULD be enqueued in SELF_DIRECTED mode
    TEST_ASSERT_EQ(NFCManager::getInstance().getWriteCount(), 1);
    TEST_ASSERT(NFCManager::getInstance().hasWriteForSpool("SPOOL_SD"));

    teardown_test();
    return 0;
}

// Test: HA_WRITE_TAG enqueues multiple NFC write requests
int test_ha_write_tag_enqueues_writes() {
    setup_test();

    uint8_t color[4] = {0, 255, 0, 255};
    g_app->injectMessage(createHAWriteTag("SPOOL001", OPT_MATERIAL_TYPE_PETG,
                                           color, "Bambu", 1000.0f, 750.0f, 99));

    auto& nfcMgr = NFCManager::getInstance();
    // Should enqueue: material type, color, brand name, consumed weight, spoolman ID = 5 writes
    TEST_ASSERT_EQ(nfcMgr.getWriteCount(), 5);

    // Verify write types
    const auto& reqs = nfcMgr.getWriteRequests();
    TEST_ASSERT_EQ(reqs[0].type, NFCWriteType::CHANGE_FILAMENT_TYPE);
    TEST_ASSERT_EQ(reqs[1].type, NFCWriteType::CHANGE_COLOR);
    TEST_ASSERT_EQ(reqs[2].type, NFCWriteType::SET_BRAND_NAME);
    TEST_ASSERT_EQ(reqs[3].type, NFCWriteType::SET_CONSUMED_WEIGHT);
    TEST_ASSERT_EQ(reqs[4].type, NFCWriteType::WRITE_SPOOLMAN_ID);

    // All should target the expected UID
    for (const auto& req : reqs) {
        TEST_ASSERT(strcmp(req.expected_spool_id, "SPOOL001") == 0);
    }

    teardown_test();
    return 0;
}

// Test: HA_WRITE_TAG works in both automation modes
int test_ha_write_works_in_both_modes() {
    // Test CONTROLLED_BY_HA mode
    setup_test();
    auto& app = ApplicationManager::getInstance();
    app.setAutomationMode(AutomationMode::CONTROLLED_BY_HOME_ASSISTANT);

    uint8_t color[4] = {255, 0, 0, 255};
    g_app->injectMessage(createHAWriteTag("SPOOL_C", OPT_MATERIAL_TYPE_ABS,
                                           color, "Test", 500.0f, 400.0f));
    TEST_ASSERT(NFCManager::getInstance().getWriteCount() > 0);
    teardown_test();

    // Test SELF_DIRECTED mode
    setup_test();
    auto& app2 = ApplicationManager::getInstance();
    app2.setAutomationMode(AutomationMode::SELF_DIRECTED);

    g_app->injectMessage(createHAWriteTag("SPOOL_S", OPT_MATERIAL_TYPE_ABS,
                                           color, "Test", 500.0f, 400.0f));
    TEST_ASSERT(NFCManager::getInstance().getWriteCount() > 0);
    teardown_test();

    return 0;
}

// Test: HA_UPDATE_REMAINING enqueues SET_CONSUMED_WEIGHT write
int test_ha_update_remaining_enqueues_write() {
    setup_test();

    g_app->injectMessage(createHAUpdateRemaining("SPOOL001", 200.0f));

    auto& nfcMgr = NFCManager::getInstance();
    TEST_ASSERT_EQ(nfcMgr.getWriteCount(), 1);
    const auto& req = nfcMgr.getWriteRequests()[0];
    TEST_ASSERT_EQ(req.type, NFCWriteType::SET_CONSUMED_WEIGHT);
    TEST_ASSERT(strcmp(req.expected_spool_id, "SPOOL001") == 0);

    teardown_test();
    return 0;
}

// Test: TAG_REMOVED message clears displayed spool ID
int test_tag_removed_clears_display_state() {
    setup_test();

    // Detect a spool first
    g_app->injectMessage(createSpoolDetected("SPOOL_TR", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);

    // Tag removed
    g_app->injectMessage(createTagRemoved("SPOOL_TR", 0.850f));

    // Same spool again should update LCD (dedup state was cleared)
    g_app->injectMessage(createSpoolDetected("SPOOL_TR", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));
    TEST_ASSERT_EQ(g_lcd->updateCount, 2);

    teardown_test();
    return 0;
}

// Test: TAG_REMOVED triggers status screen only after absence timeout
int test_tag_removed_delayed_status_screen() {
    setup_test();

    g_app->injectMessage(createSpoolDetected("SPOOL_TIMEOUT", OPT_MATERIAL_TYPE_PLA, 0.500f, "PLA"));
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);

    g_app->injectMessage(createTagRemoved("SPOOL_TIMEOUT", 0.500f));

    ApplicationManager::getInstance().processMessages();
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ApplicationManager::getInstance().processMessages();

    TEST_ASSERT_EQ(g_lcd->updateCount, 2);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "NFC+");

    teardown_test();
    return 0;
}

// Test: Blank/unknown tags use scanned 4-line LCD treatment
int test_blank_tag_shows_scanned_4line_message() {
    setup_test();

    g_app->injectMessage(createBlankTagDetected("BLANK001"));

    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Spool");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "Scanned");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine3, "Unknown Tag");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine4, "Use app");

    teardown_test();
    return 0;
}

// Test: Default automation mode is SELF_DIRECTED (existing tests unaffected)
int test_default_automation_mode() {
    setup_test();

    TEST_ASSERT_EQ(ApplicationManager::getInstance().getAutomationMode(),
                   AutomationMode::SELF_DIRECTED);

    teardown_test();
    return 0;
}

// Test: Delayed Type/Remain display after spool updated (no Spoolman)
int test_delayed_type_remain_after_spool_updated_no_spoolman() {
    setup_test();

    // Scan a spool first
    auto detectMsg = createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA");
    g_app->injectMessage(detectMsg);
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine3, "PLA");
    g_lcd->reset();

    // Set up TestNFCManager with the spool data so handleSpoolUpdated can retrieve material_name
    CurrentSpoolState spoolState = {0};
    spoolState.tag_data_valid = true;
    opt_format_empty_tag(&spoolState.tag_data, 888, 64);
    opt_set_material_name(&spoolState.tag_data, "PLA");
    NFCManager::getInstance().setCurrentSpool(spoolState);

    // Send SPOOL_UPDATED message (no Spoolman configured)
    AppMessage updateMsg;
    updateMsg.type = AppMessageType::SPOOL_UPDATED;
    strncpy(updateMsg.payload.spoolUpdated.spool_id, "SPOOL001", sizeof(updateMsg.payload.spoolUpdated.spool_id) - 1);
    updateMsg.payload.spoolUpdated.update_type = static_cast<uint8_t>(NFCWriteType::REMOVE_WEIGHT);
    updateMsg.payload.spoolUpdated.success = true;
    updateMsg.payload.spoolUpdated.suppress_sync = 0;
    updateMsg.payload.spoolUpdated.kg_remaining = 0.750f;

    g_app->injectMessage(updateMsg);
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Updated");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "750");
    g_lcd->reset();

    // Advance time by TYPE_REMAIN_DISPLAY_DELAY_MS
    advance_milliseconds(25);  // 25ms in test mode

    // Process messages to trigger delayed display
    g_app->processMessages();

    // Verify Type/Remain display appeared (1 more LCD update)
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Type");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "PLA");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "Remain");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "750");

    teardown_test();
    return 0;
}

// Test: Delayed Type/Remain display after Spoolman sync
int test_delayed_type_remain_after_spoolman_synced() {
    setup_test();

    // Scan a spool first
    auto detectMsg = createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA");
    g_app->injectMessage(detectMsg);
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    g_lcd->reset();

    // Set up TestNFCManager with the spool data so both handlers can retrieve material_name
    CurrentSpoolState spoolState = {0};
    spoolState.tag_data_valid = true;
    opt_format_empty_tag(&spoolState.tag_data, 888, 64);
    opt_set_material_name(&spoolState.tag_data, "PLA");
    NFCManager::getInstance().setCurrentSpool(spoolState);

    // Send SPOOL_UPDATED message (will show "Updated: Xg" / "Syncing Spoolman")
    AppMessage updateMsg;
    updateMsg.type = AppMessageType::SPOOL_UPDATED;
    strncpy(updateMsg.payload.spoolUpdated.spool_id, "SPOOL001", sizeof(updateMsg.payload.spoolUpdated.spool_id) - 1);
    updateMsg.payload.spoolUpdated.update_type = static_cast<uint8_t>(NFCWriteType::REMOVE_WEIGHT);
    updateMsg.payload.spoolUpdated.success = true;
    updateMsg.payload.spoolUpdated.suppress_sync = 0;
    updateMsg.payload.spoolUpdated.kg_remaining = 0.750f;

    g_app->injectMessage(updateMsg);
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    g_lcd->reset();

    // Send SPOOLMAN_SYNCED message (will show "Updated: Xg" / "Spoolman OK!")
    AppMessage syncMsg;
    syncMsg.type = AppMessageType::SPOOLMAN_SYNCED;
    strncpy(syncMsg.payload.spoolmanSynced.spool_id, "SPOOL001", sizeof(syncMsg.payload.spoolmanSynced.spool_id) - 1);
    syncMsg.payload.spoolmanSynced.success = true;
    syncMsg.payload.spoolmanSynced.kg_remaining = 0.750f;
    syncMsg.payload.spoolmanSynced.spoolman_id = 123;

    g_app->injectMessage(syncMsg);
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    // After successful sync, should immediately show Type/Remain (not delayed anymore)
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "Type");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine1, "PLA");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "Remain");
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine2, "750");

    teardown_test();
    return 0;
}

// Test: Delayed Type/Remain display canceled by new spool scan
int test_delayed_type_remain_canceled_by_new_scan() {
    setup_test();

    // Scan a spool first
    g_app->injectMessage(createSpoolDetected("SPOOL001", OPT_MATERIAL_TYPE_PLA, 0.850f, "PLA"));
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    g_lcd->reset();

    // Send SPOOL_UPDATED message
    AppMessage updateMsg;
    updateMsg.type = AppMessageType::SPOOL_UPDATED;
    strncpy(updateMsg.payload.spoolUpdated.spool_id, "SPOOL001", sizeof(updateMsg.payload.spoolUpdated.spool_id) - 1);
    updateMsg.payload.spoolUpdated.update_type = static_cast<uint8_t>(NFCWriteType::REMOVE_WEIGHT);
    updateMsg.payload.spoolUpdated.success = true;
    updateMsg.payload.spoolUpdated.suppress_sync = 0;
    updateMsg.payload.spoolUpdated.kg_remaining = 0.750f;

    g_app->injectMessage(updateMsg);
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    g_lcd->reset();

    // Advance time by 10ms (mid-delay)
    advance_milliseconds(10);

    // Scan a new spool (should cancel pending Type/Remain display)
    g_app->injectMessage(createSpoolDetected("SPOOL002", OPT_MATERIAL_TYPE_PETG, 0.500f, "PETG"));
    TEST_ASSERT_EQ(g_lcd->updateCount, 1);
    TEST_ASSERT_STR_CONTAINS(g_lcd->lastLine3, "PETG");
    g_lcd->reset();

    // Advance time by TYPE_REMAIN_DISPLAY_DELAY_MS + 10ms
    advance_milliseconds(35);

    // Process messages - should NOT show Type/Remain from the first spool
    g_app->processMessages();

    // Verify no duplicate Type/Remain from SPOOL001
    // The LCD should only show SPOOL002's info (from the SPOOL_DETECTED)
    TEST_ASSERT_EQ(g_lcd->updateCount, 0);  // No new messages

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
    RUN_TEST(test_high_progress_disappearance_deducts_filament);

    // HA / Automation mode tests
    RUN_TEST(test_controlled_mode_no_auto_weight_update);
    RUN_TEST(test_self_directed_auto_weight_update);
    RUN_TEST(test_ha_write_tag_enqueues_writes);
    RUN_TEST(test_ha_write_works_in_both_modes);
    RUN_TEST(test_ha_update_remaining_enqueues_write);
    RUN_TEST(test_tag_removed_clears_display_state);
    RUN_TEST(test_tag_removed_delayed_status_screen);
    RUN_TEST(test_blank_tag_shows_scanned_4line_message);
    RUN_TEST(test_default_automation_mode);

    // Delayed Type/Remain display tests
    RUN_TEST(test_delayed_type_remain_after_spool_updated_no_spoolman);
    RUN_TEST(test_delayed_type_remain_after_spoolman_synced);
    RUN_TEST(test_delayed_type_remain_canceled_by_new_scan);

    printf("\n=== Results: %d/%d passed ===\n", passed, total);

    return failed > 0 ? 1 : 0;
}
