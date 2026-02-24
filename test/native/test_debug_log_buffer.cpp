#include <cstring>
#include <cstdio>

#include "DebugLogBuffer.h"

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL: %s != %s at %s:%d (got=%d expected=%d)\n", #a, #b, __FILE__, __LINE__, (int)(a), (int)(b)); \
        return 1; \
    } \
} while(0)

static int test_basic_meta_and_read() {
    auto& log = DebugLogBuffer::getInstance();
    log.clear();

    log.logLine("alpha");
    log.logLine("beta");

    auto meta = log.getSnapshotMeta();
    ASSERT_EQ_INT(meta.count, 2);
    ASSERT_EQ_INT(meta.oldestSeq, 0);
    ASSERT_EQ_INT(meta.nextSeq, 2);

    char out[DebugLogBuffer::MAX_LINE_LENGTH + 1];
    auto rc0 = log.getLineBySeq(0, out, sizeof(out));
    ASSERT_EQ_INT((int)rc0, (int)DebugLogBuffer::LookupResult::Ok);
    ASSERT_TRUE(std::strcmp(out, "alpha") == 0);

    auto rc1 = log.getLineBySeq(1, out, sizeof(out));
    ASSERT_EQ_INT((int)rc1, (int)DebugLogBuffer::LookupResult::Ok);
    ASSERT_TRUE(std::strcmp(out, "beta") == 0);

    auto rcEof = log.getLineBySeq(2, out, sizeof(out));
    ASSERT_EQ_INT((int)rcEof, (int)DebugLogBuffer::LookupResult::Eof);

    return 0;
}

static int test_wrap_and_stale() {
    auto& log = DebugLogBuffer::getInstance();
    log.clear();

    char buf[32];
    for (int i = 0; i < 110; ++i) {
        std::snprintf(buf, sizeof(buf), "line-%d", i);
        log.logLine(buf);
    }

    auto meta = log.getSnapshotMeta();
    ASSERT_EQ_INT(meta.count, 100);
    ASSERT_EQ_INT(meta.oldestSeq, 10);
    ASSERT_EQ_INT(meta.nextSeq, 110);

    char out[DebugLogBuffer::MAX_LINE_LENGTH + 1];
    auto rcStale = log.getLineBySeq(9, out, sizeof(out));
    ASSERT_EQ_INT((int)rcStale, (int)DebugLogBuffer::LookupResult::Stale);

    auto rcOk = log.getLineBySeq(10, out, sizeof(out));
    ASSERT_EQ_INT((int)rcOk, (int)DebugLogBuffer::LookupResult::Ok);
    ASSERT_TRUE(std::strcmp(out, "line-10") == 0);

    return 0;
}

static int test_max_length_truncation() {
    auto& log = DebugLogBuffer::getInstance();
    log.clear();

    char longLine[180];
    std::memset(longLine, 'X', sizeof(longLine) - 1);
    longLine[sizeof(longLine) - 1] = '\0';

    log.logLine(longLine);

    auto meta = log.getSnapshotMeta();
    ASSERT_EQ_INT(meta.count, 1);

    char out[DebugLogBuffer::MAX_LINE_LENGTH + 1];
    auto rc = log.getLineBySeq(meta.oldestSeq, out, sizeof(out));
    ASSERT_EQ_INT((int)rc, (int)DebugLogBuffer::LookupResult::Ok);
    ASSERT_EQ_INT((int)std::strlen(out), (int)DebugLogBuffer::MAX_LINE_LENGTH);

    return 0;
}

int main() {
    int passed = 0;
    int total = 3;

    printf("\n=== DebugLogBuffer Tests ===\n\n");

    if (test_basic_meta_and_read() == 0) { printf("test_basic_meta_and_read PASS\n"); passed++; }
    if (test_wrap_and_stale() == 0) { printf("test_wrap_and_stale PASS\n"); passed++; }
    if (test_max_length_truncation() == 0) { printf("test_max_length_truncation PASS\n"); passed++; }

    printf("\n=== Results: %d/%d passed ===\n", passed, total);
    return (passed == total) ? 0 : 1;
}
