// Tests for NFCManager raw write functionality
// Build with: cd test/native && make test_raw_write

#include "platform/NativePlatform.h"
#include "StubNFCConnection.h"
#include "FakeLCDManager.h"
#include "StubApplicationManager.h"

extern "C" {
#include "openprinttag_lib.h"
}

#include <cstdio>
#include <cstring>

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

// Unity build — pulls in real NFCManager
#include "NFCManager.cpp"

static StubNFCConnection* g_stubNfc = nullptr;
static LCDManager* g_lcd = nullptr;

// Test bin file data (loaded once)
static uint8_t g_binData[320];
static size_t g_binDataSize = 0;

static bool loadBinFile() {
    FILE* f = fopen("../res/openprinttag_PETG_Jet_Black.bin", "rb");
    if (!f) {
        f = fopen("test/res/openprinttag_PETG_Jet_Black.bin", "rb");
    }
    if (!f) {
        printf("ERROR: Could not open bin file\n");
        return false;
    }
    g_binDataSize = fread(g_binData, 1, sizeof(g_binData), f);
    fclose(f);
    return g_binDataSize > 0;
}

static bool sawSuccessfulSpoolUpdate() {
    const auto& msgs = ApplicationManager::getInstance().getSentMessages();
    for (const auto& msg : msgs) {
        if (msg.type == AppMessageType::SPOOL_UPDATED) {
            return msg.payload.spoolUpdated.success;
        }
    }
    return false;
}

static bool sawSpoolDetectedWithId(int32_t spoolmanId) {
    const auto& msgs = ApplicationManager::getInstance().getSentMessages();
    for (const auto& msg : msgs) {
        if (msg.type == AppMessageType::SPOOL_DETECTED &&
            msg.payload.spoolDetected.spoolman_id == spoolmanId) {
            return true;
        }
    }
    return false;
}

void setup_test() {
    g_stubNfc = new StubNFCConnection();
    g_lcd = new LCDManager();

    auto& nfcMgr = NFCManager::getInstance();
    nfcMgr.setConnection(g_stubNfc);
    nfcMgr.begin();

    ApplicationManager::getInstance().reset();
}

void teardown_test() {
    // Reset raw write state (singleton persists across tests)
    NFCManager::getInstance().resetWriteState();

    // Drain the write queue
    NFCManager::getInstance().requestCurrentSpool();

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

// Helper: set up a tag with valid data so it's detected as present
void setupTagPresent(const uint8_t* uid, uint8_t uidLen) {
    g_stubNfc->setTagPresent(true);
    g_stubNfc->setTagUid(uid, uidLen);
    g_stubNfc->setTagData(g_binData, g_binDataSize);

    // Do an initial scan to detect the tag
    NFCManager::getInstance().scanOnce();
}

// Test: enqueueRawWrite writes correct data to tag pages
int test_raw_write_writes_pages() {
    setup_test();

    uint8_t uid[8] = {0x17, 0x5A, 0xEC, 0x18, 0x09, 0x01, 0x04, 0xE0};
    setupTagPresent(uid, 8);
    g_stubNfc->clearPageWrites();

    // Enqueue raw write
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = 100;
    req.type = NFCWriteType::WRITE_RAW_TAG;
    strncpy(req.expected_spool_id, "175AEC18090104E0", sizeof(req.expected_spool_id) - 1);

    bool enqueued = NFCManager::getInstance().enqueueRawWrite(req, g_binData, g_binDataSize);
    TEST_ASSERT(enqueued);

    // Process the write by scanning (processWriteQueue is called during scanOnce)
    NFCManager::getInstance().scanOnce();

    // Should have written pages (312 bytes = 78 pages of 4 bytes each)
    size_t writeCount = g_stubNfc->getPageWrites().size();
    printf("  [DEBUG] Page writes: %zu\n", writeCount);
    TEST_ASSERT(writeCount > 0);

    // Verify first page was written with correct data
    const auto& firstWrite = g_stubNfc->getPageWrites()[0];
    TEST_ASSERT_EQ(firstWrite.data[0], g_binData[0]);  // 0xE1

    // Verify SPOOL_DETECTED message was sent
    const auto& msgs = ApplicationManager::getInstance().getSentMessages();
    bool sawSpoolDetected = false;
    for (const auto& msg : msgs) {
        if (msg.type == AppMessageType::SPOOL_DETECTED) {
            sawSpoolDetected = true;
        }
    }
    TEST_ASSERT(sawSpoolDetected);

    teardown_test();
    return 0;
}

// Test: raw write rejected when no tag present
int test_raw_write_rejected_no_tag() {
    setup_test();

    // No tag present — don't call setupTagPresent
    g_stubNfc->setTagPresent(false);
    NFCManager::getInstance().scanOnce();  // Detect no tag

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = 200;
    req.type = NFCWriteType::WRITE_RAW_TAG;
    req.expected_spool_id[0] = '\0';

    bool enqueued = NFCManager::getInstance().enqueueRawWrite(req, g_binData, g_binDataSize);
    TEST_ASSERT(enqueued);  // enqueue itself succeeds

    // But processWriteQueue should skip since no tag is present
    NFCManager::getInstance().scanOnce();

    // No pages should have been written
    size_t writeCount = g_stubNfc->getPageWrites().size();
    printf("  [DEBUG] Write count (expected 0): %zu\n", writeCount);
    TEST_ASSERT_EQ(writeCount, (size_t)0);

    teardown_test();
    return 0;
}

// Test: raw write rejected when spool ID mismatch
int test_raw_write_rejected_id_mismatch() {
    setup_test();

    uint8_t uid[8] = {0x17, 0x5A, 0xEC, 0x18, 0x09, 0x01, 0x04, 0xE0};
    setupTagPresent(uid, 8);
    g_stubNfc->clearPageWrites();

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = 300;
    req.type = NFCWriteType::WRITE_RAW_TAG;
    strncpy(req.expected_spool_id, "DEADBEEFCAFEBABE", sizeof(req.expected_spool_id) - 1);

    bool enqueued = NFCManager::getInstance().enqueueRawWrite(req, g_binData, g_binDataSize);
    TEST_ASSERT(enqueued);

    NFCManager::getInstance().scanOnce();

    // Write should have been attempted (dequeued) but rejected due to ID mismatch
    // The write-to-NFC should NOT have occurred
    // Check that no NFC page writes happened
    size_t writeCount = g_stubNfc->getPageWrites().size();
    printf("  [DEBUG] Write count (expected 0): %zu\n", writeCount);
    TEST_ASSERT_EQ(writeCount, (size_t)0);

    teardown_test();
    return 0;
}

// Test: double enqueue is blocked (rawWritePending flag)
int test_raw_write_double_enqueue_blocked() {
    setup_test();

    uint8_t uid[8] = {0x17, 0x5A, 0xEC, 0x18, 0x09, 0x01, 0x04, 0xE0};
    setupTagPresent(uid, 8);

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = 400;
    req.type = NFCWriteType::WRITE_RAW_TAG;
    strncpy(req.expected_spool_id, "175AEC18090104E0", sizeof(req.expected_spool_id) - 1);

    bool first = NFCManager::getInstance().enqueueRawWrite(req, g_binData, g_binDataSize);
    TEST_ASSERT(first);

    // Second enqueue should fail — pending flag is still set
    req.request_id = 401;
    bool second = NFCManager::getInstance().enqueueRawWrite(req, g_binData, g_binDataSize);
    TEST_ASSERT(!second);

    // Process the first write so it clears
    NFCManager::getInstance().scanOnce();

    teardown_test();
    return 0;
}

// Test: after raw write, tag is re-detected on next scan
int test_raw_write_clears_dedup() {
    setup_test();

    uint8_t uid[8] = {0x17, 0x5A, 0xEC, 0x18, 0x09, 0x01, 0x04, 0xE0};
    setupTagPresent(uid, 8);
    ApplicationManager::getInstance().reset();

    // Enqueue and process raw write
    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = 500;
    req.type = NFCWriteType::WRITE_RAW_TAG;
    strncpy(req.expected_spool_id, "175AEC18090104E0", sizeof(req.expected_spool_id) - 1);

    NFCManager::getInstance().enqueueRawWrite(req, g_binData, g_binDataSize);
    NFCManager::getInstance().scanOnce();

    ApplicationManager::getInstance().reset();

    // Next scan should re-detect the spool (lastSeenValid was cleared by writeRawTag)
    NFCManager::getInstance().scanOnce();

    const auto& msgs = ApplicationManager::getInstance().getSentMessages();
    bool sawSpoolDetected = false;
    for (const auto& msg : msgs) {
        if (msg.type == AppMessageType::SPOOL_DETECTED) {
            sawSpoolDetected = true;
        }
    }
    TEST_ASSERT(sawSpoolDetected);

    teardown_test();
    return 0;
}

// Test: write spoolman ID against real bin tag and verify readback
int test_write_spoolman_id_real_bin() {
    setup_test();

    uint8_t uid[8] = {0x17, 0x5A, 0xEC, 0x18, 0x09, 0x01, 0x04, 0xE0};
    setupTagPresent(uid, 8);
    ApplicationManager::getInstance().reset();

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = 600;
    req.type = NFCWriteType::WRITE_SPOOLMAN_ID;
    strncpy(req.expected_spool_id, "175AEC18090104E0", sizeof(req.expected_spool_id) - 1);
    req.data.spoolman_id = 4321;

    TEST_ASSERT(NFCManager::getInstance().enqueueWrite(req));
    NFCManager::getInstance().scanOnce();
    TEST_ASSERT(sawSuccessfulSpoolUpdate());

    ApplicationManager::getInstance().reset();
    NFCManager::getInstance().requestCurrentSpool();
    NFCManager::getInstance().scanOnce();
    TEST_ASSERT(sawSpoolDetectedWithId(4321));

    teardown_test();
    return 0;
}

// Test: dirty-write failure falls back to raw write and still succeeds
int test_dirty_write_fallback_to_raw() {
    setup_test();

    uint8_t uid[8] = {0x17, 0x5A, 0xEC, 0x18, 0x09, 0x01, 0x04, 0xE0};
    setupTagPresent(uid, 8);
    g_stubNfc->clearPageWrites();
    ApplicationManager::getInstance().reset();

    NFCWriteRequest req;
    memset(&req, 0, sizeof(req));
    req.request_id = 700;
    req.type = NFCWriteType::CHANGE_COLOR;
    strncpy(req.expected_spool_id, "175AEC18090104E0", sizeof(req.expected_spool_id) - 1);
    req.data.new_color[0] = 0x10;
    req.data.new_color[1] = 0x20;
    req.data.new_color[2] = 0x30;
    req.data.new_color[3] = 0xFF;

    TEST_ASSERT(NFCManager::getInstance().enqueueWrite(req));

    // Force first write failure (dirty-page write), then allow fallback raw write to proceed.
    g_stubNfc->setFailNextWrites(1);
    NFCManager::getInstance().scanOnce();

    TEST_ASSERT(sawSuccessfulSpoolUpdate());
    TEST_ASSERT(g_stubNfc->getWriteCount() > 1);

    teardown_test();
    return 0;
}

int main() {
    if (!loadBinFile()) {
        printf("FATAL: Could not load test bin file\n");
        return 1;
    }
    printf("Loaded bin file: %zu bytes\n", g_binDataSize);

    int passed = 0, failed = 0, total = 0;

    printf("\n=== Raw Write Tests ===\n\n");

    RUN_TEST(test_raw_write_writes_pages);
    RUN_TEST(test_raw_write_rejected_no_tag);
    RUN_TEST(test_raw_write_rejected_id_mismatch);
    RUN_TEST(test_raw_write_double_enqueue_blocked);
    RUN_TEST(test_raw_write_clears_dedup);
    RUN_TEST(test_write_spoolman_id_real_bin);
    RUN_TEST(test_dirty_write_fallback_to_raw);

    printf("\n=== Results: %d/%d passed ===\n", passed, total);

    return failed > 0 ? 1 : 0;
}
