#include "platform/NativePlatform.h"
#include "../../src/JsonPullHelpers.h"

#include <cstdio>
#include <cstring>

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

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
    } while (0)

using namespace io;
using namespace json;

static const char* simple_object_json = R"({
  "name": "Test",
  "value": 42,
  "price": 19.99,
  "active": true
})";

static const char* nested_object_json = R"({
  "job": {"id": 123, "progress": 0.45},
  "file": {"meta": {"filament used [g]": 45.3}}
})";

static const char* array_json = R"([
  {"name": "Prusa", "id": 1},
  {"name": "Generic", "id": 2}
])";

int test_extract_int_float_string_bool() {
    {
        const_buffer_stream stm((const uint8_t*)simple_object_json, strlen(simple_object_json));
        json_reader reader(stm);
        TEST_ASSERT(reader.read());
        JsonFieldExtractor extractor(reader);
        int32_t value = 0;
        TEST_ASSERT(extractor.extractInt("value", value));
        TEST_ASSERT(value == 42);
    }
    {
        const_buffer_stream stm((const uint8_t*)simple_object_json, strlen(simple_object_json));
        json_reader reader(stm);
        TEST_ASSERT(reader.read());
        JsonFieldExtractor extractor(reader);
        float price = 0.0f;
        TEST_ASSERT(extractor.extractFloat("price", price));
        TEST_ASSERT(price > 19.98f && price < 20.0f);
    }
    {
        const_buffer_stream stm((const uint8_t*)simple_object_json, strlen(simple_object_json));
        json_reader reader(stm);
        TEST_ASSERT(reader.read());
        JsonFieldExtractor extractor(reader);
        char name[16] = {0};
        TEST_ASSERT(extractor.extractString("name", name, sizeof(name)));
        TEST_ASSERT(strcmp(name, "Test") == 0);
    }
    {
        const_buffer_stream stm((const uint8_t*)simple_object_json, strlen(simple_object_json));
        json_reader reader(stm);
        TEST_ASSERT(reader.read());
        JsonFieldExtractor extractor(reader);
        bool active = false;
        TEST_ASSERT(extractor.extractBool("active", active));
        TEST_ASSERT(active);
    }
    return 0;
}

int test_navigate_nested_path() {
    const_buffer_stream stm((const uint8_t*)nested_object_json, strlen(nested_object_json));
    json_reader reader(stm);
    TEST_ASSERT(reader.read());
    JsonFieldExtractor extractor(reader);
    TEST_ASSERT(extractor.navigateToField("job.id"));
    TEST_ASSERT((int)reader.value_int() == 123);
    return 0;
}

int test_missing_and_mismatch() {
    {
        const_buffer_stream stm((const uint8_t*)simple_object_json, strlen(simple_object_json));
        json_reader reader(stm);
        TEST_ASSERT(reader.read());
        JsonFieldExtractor extractor(reader);
        int32_t missing = 7;
        TEST_ASSERT(!extractor.extractInt("does_not_exist", missing));
        TEST_ASSERT(missing == 7);
    }
    {
        const_buffer_stream stm((const uint8_t*)simple_object_json, strlen(simple_object_json));
        json_reader reader(stm);
        TEST_ASSERT(reader.read());
        JsonFieldExtractor extractor(reader);
        int32_t wrongType = 0;
        TEST_ASSERT(!extractor.extractInt("name", wrongType));
    }
    return 0;
}

int test_array_iteration() {
    const_buffer_stream stm((const uint8_t*)array_json, strlen(array_json));
    json_reader reader(stm);
    JsonFieldExtractor extractor(reader);
    TEST_ASSERT(extractor.enterArray(nullptr));
    TEST_ASSERT(extractor.nextArrayElement());
    char name[16] = {0};
    TEST_ASSERT(extractor.extractString("name", name, sizeof(name)));
    TEST_ASSERT(strcmp(name, "Prusa") == 0);
    return 0;
}

int main() {
    int passed = 0, failed = 0, total = 0;

    printf("\n=== JsonPullHelpers Tests ===\n\n");
    RUN_TEST(test_extract_int_float_string_bool);
    RUN_TEST(test_navigate_nested_path);
    RUN_TEST(test_missing_and_mismatch);
    RUN_TEST(test_array_iteration);

    printf("\n=== Results: %d/%d passed ===\n", passed, total);
    return failed > 0 ? 1 : 0;
}
