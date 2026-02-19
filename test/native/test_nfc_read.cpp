// Tests for NFCManager read behavior
// Build with: cd test/native && make test_nfc_read
//
// These tests verify that NFCManager does NOT auto-format blank spools
// or spools with read errors. Currently expected to FAIL until the
// behavior is fixed.

#include "platform/NativePlatform.h"
#include "StubNFCConnection.h"
#include "FakeLCDManager.h"
#include "StubApplicationManager.h"

// Include the openprinttag library
extern "C" {
#include "openprinttag_lib.h"
}

// Test assertion helpers (minimal, local to this test)
#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            return 1; \
        } \
    } while(0)

#define TEST_ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("FAIL: %s != %s at %s:%d\n", #a, #b, __FILE__, __LINE__); \
            return 1; \
        } \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        printf("Running %s... ", #test_func); \
        int result = test_func(); \
        if (result == 0) { \
            printf("PASS\n"); \
            passed++; \
        } else { \
            failed++; \
        } \
        total++; \
    } while(0)

// Include real NFCManager implementation (unity build)
#include "NFCManager.cpp"

static StubNFCConnection* g_stubNfc = nullptr;
static LCDManager* g_lcd = nullptr;

void setup_nfc_test() {
    g_stubNfc = new StubNFCConnection();
    g_lcd = new LCDManager();

    auto& nfcMgr = NFCManager::getInstance();
    nfcMgr.setConnection(g_stubNfc);
    nfcMgr.begin();

    // Reset application manager message tracking
    ApplicationManager::getInstance().reset();
}

void teardown_nfc_test() {
    if (g_stubNfc) {
        g_stubNfc->resetTestState();
        delete g_stubNfc;
        g_stubNfc = nullptr;
    }
    if (g_lcd) {
        delete g_lcd;
        g_lcd = nullptr;
    }
}

// Test: Blank spool should NOT be automatically formatted
// EXPECTED TO FAIL with current code (auto-formats blank spools)
int test_blank_spool_not_formatted() {
    setup_nfc_test();

    // Setup: Tag present with blank data (all zeros - will fail NDEF parse)
    uint8_t testUid[7] = {0x04, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    g_stubNfc->setTagPresent(true);
    g_stubNfc->setTagUid(testUid, 7);

    uint8_t blankData[256] = {0};  // Blank tag - NDEF parse will fail
    g_stubNfc->setTagData(blankData, sizeof(blankData));

    // Act: Trigger scan
    NFCManager::getInstance().scanOnce();

    // Assert: NO writes should occur (blank spools should not be formatted)
    // This tests the current buggy behavior - should FAIL
    size_t writeCount = g_stubNfc->getPageWrites().size();
    printf("  [DEBUG] Write count: %zu (expected 0)\n", writeCount);

    TEST_ASSERT_EQ(writeCount, (size_t)0);

    teardown_nfc_test();
    return 0;
}

// Test: Read error should show LCD error, not attempt formatting
// EXPECTED TO FAIL with current code (attempts to format on read error)
int test_read_error_not_formatted() {
    setup_nfc_test();

    // Setup: Tag present but reads fail
    uint8_t testUid[7] = {0x04, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    g_stubNfc->setTagPresent(true);
    g_stubNfc->setTagUid(testUid, 7);
    g_stubNfc->setReadError(true);  // Simulate read failure

    // Act: Trigger scan
    NFCManager::getInstance().scanOnce();

    // Assert: NO writes should occur
    size_t writeCount = g_stubNfc->getPageWrites().size();
    printf("  [DEBUG] Write count: %zu (expected 0)\n", writeCount);

    TEST_ASSERT_EQ(writeCount, (size_t)0);

    teardown_nfc_test();
    return 0;
}

// Test: Blank tag removal should still send TAG_REMOVED to ApplicationManager
int test_blank_tag_removal_sends_tag_removed_message() {
    setup_nfc_test();

    uint8_t testUid[7] = {0x04, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    uint8_t blankData[256] = {0};

    g_stubNfc->setTagPresent(true);
    g_stubNfc->setTagUid(testUid, 7);
    g_stubNfc->setTagData(blankData, sizeof(blankData));
    NFCManager::getInstance().scanOnce();

    g_stubNfc->setTagPresent(false);
    NFCManager::getInstance().scanOnce();

    const auto& sent = ApplicationManager::getInstance().getSentMessages();
    bool sawTagRemoved = false;
    for (const auto& msg : sent) {
        if (msg.type == AppMessageType::TAG_REMOVED) {
            sawTagRemoved = true;
            TEST_ASSERT_EQ(strcmp(msg.payload.tagRemoved.spool_id, "04102030405060"), 0);
            TEST_ASSERT_EQ(msg.payload.tagRemoved.spoolman_id, -1);
            break;
        }
    }

    TEST_ASSERT(sawTagRemoved);

    teardown_nfc_test();
    return 0;
}

int main() {
    int passed = 0, failed = 0, total = 0;

    printf("\n=== NFC Read Behavior Tests ===\n");
    printf("(These tests are expected to FAIL until auto-format behavior is fixed)\n\n");

    RUN_TEST(test_blank_spool_not_formatted);
    RUN_TEST(test_read_error_not_formatted);
    RUN_TEST(test_blank_tag_removal_sends_tag_removed_message);

    printf("\n=== Results: %d/%d passed ===\n", passed, total);

    return failed > 0 ? 1 : 0;
}
