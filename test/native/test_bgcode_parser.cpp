#include "platform/NativePlatform.h"
#include "BgcodeParser.cpp"

#include <cstdio>
#include <cstdlib>
#include <cmath>

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
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

static uint8_t* g_bgcodeData = nullptr;
static size_t g_bgcodeLen = 0;

bool loadSampleBgcode() {
    const char* paths[] = {
        "../../test/res/sample.bgcode",
        "../res/sample.bgcode",
        "test/res/sample.bgcode",
    };

    FILE* f = nullptr;
    for (const char* path : paths) {
        f = fopen(path, "rb");
        if (f) break;
    }
    if (!f) {
        printf("ERROR: Could not open sample.bgcode\n");
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read first 8KB (simulating the Range request size)
    size_t readSize = fileSize < 8192 ? (size_t)fileSize : 8192;
    g_bgcodeData = (uint8_t*)malloc(readSize);
    g_bgcodeLen = fread(g_bgcodeData, 1, readSize, f);
    fclose(f);
    return g_bgcodeLen > 0;
}

// Test: Parse real bgcode file and extract filament weight
int test_parse_sample_bgcode_filament() {
    float filament = parseBgcodeFilament(g_bgcodeData, g_bgcodeLen);
    printf("(parsed %.2fg) ", filament);
    TEST_ASSERT(filament > 0.0f);
    // sample.bgcode contains "filament used [g]=9.18"
    TEST_ASSERT(fabs(filament - 9.18f) < 0.01f);
    return 0;
}

// Test: Invalid magic returns 0
int test_parse_invalid_magic() {
    uint8_t garbage[64] = {0};
    float filament = parseBgcodeFilament(garbage, sizeof(garbage));
    TEST_ASSERT(filament == 0.0f);
    return 0;
}

// Test: Truncated data returns 0
int test_parse_truncated_header() {
    float filament = parseBgcodeFilament(g_bgcodeData, 20);
    TEST_ASSERT(filament == 0.0f);
    return 0;
}

// Test: Valid magic but no filament key in metadata
int test_parse_no_filament_key() {
    // Build a minimal bgcode with FILE_METADATA containing no filament info
    uint8_t data[64] = {0};
    // Magic "GCDE"
    memcpy(data, "GCDE", 4);
    // Version 1
    uint32_t version = 1;
    memcpy(data + 4, &version, 4);
    // Checksum type 0 (none)
    uint16_t checksumType = 0;
    memcpy(data + 8, &checksumType, 2);
    // Block header at offset 10: type=0 (FILE_METADATA), compression=0
    uint16_t blockType = 0, compression = 0;
    memcpy(data + 10, &blockType, 2);
    memcpy(data + 12, &compression, 2);
    // Metadata: "Producer=Test\n" (14 bytes)
    const char* meta = "Producer=Test\n";
    uint32_t metaLen = 14;
    memcpy(data + 14, &metaLen, 4); // uncompressed_size
    memcpy(data + 18, &metaLen, 4); // compressed_size
    memcpy(data + 22, meta, metaLen);

    float filament = parseBgcodeFilament(data, 22 + metaLen);
    TEST_ASSERT(filament == 0.0f);
    return 0;
}

int main() {
    int passed = 0, failed = 0, total = 0;

    printf("\n=== Bgcode Parser Tests ===\n\n");

    if (!loadSampleBgcode()) {
        printf("FATAL: Could not load sample.bgcode\n");
        return 1;
    }

    RUN_TEST(test_parse_sample_bgcode_filament);
    RUN_TEST(test_parse_invalid_magic);
    RUN_TEST(test_parse_truncated_header);
    RUN_TEST(test_parse_no_filament_key);

    printf("\n=== Results: %d/%d passed ===\n", passed, total);

    free(g_bgcodeData);
    return failed > 0 ? 1 : 0;
}
