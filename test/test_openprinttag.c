/**
 * OpenPrintTag Library Unit Tests
 *
 * Tests for the OpenPrintTag NFC data library.
 * Can be compiled and run on host machine without ESP32.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "openprinttag_lib.h"

/*============================================================================
 * Test Framework Macros
 *============================================================================*/

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("  FAIL: %s:%d: expected %d, got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); \
        return 1; \
    } \
} while(0)

#define TEST_ASSERT_FLOAT_NEAR(expected, actual, epsilon) do { \
    float diff = fabsf((expected) - (actual)); \
    if (diff > (epsilon)) { \
        printf("  FAIL: %s:%d: expected %f, got %f (diff %f)\n", __FILE__, __LINE__, (expected), (actual), diff); \
        return 1; \
    } \
} while(0)

#define TEST_ASSERT_STR_EQUAL(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        printf("  FAIL: %s:%d: expected '%s', got '%s'\n", __FILE__, __LINE__, (expected), (actual)); \
        return 1; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    printf("Running %s...\n", #test_func); \
    int result = test_func(); \
    if (result == 0) { \
        printf("  PASS\n"); \
        tests_passed++; \
    } else { \
        tests_failed++; \
    } \
    tests_total++; \
} while(0)

/*============================================================================
 * Mock NFC HAL
 *============================================================================*/

typedef struct {
    uint8_t memory[OPT_MAX_TAG_SIZE];
    uint16_t size;
    bool simulate_disconnect;
    int disconnect_after_page;
    int pages_written;
} mock_nfc_t;

static opt_error_t mock_read_page(void *ctx, uint8_t page, uint8_t *buffer) {
    mock_nfc_t *nfc = (mock_nfc_t *)ctx;
    uint16_t offset = page * OPT_PAGE_SIZE;

    if (offset + OPT_PAGE_SIZE > nfc->size) {
        return OPT_ERR_NFC_READ;
    }

    memcpy(buffer, nfc->memory + offset, OPT_PAGE_SIZE);
    return OPT_OK;
}

static opt_error_t mock_write_page(void *ctx, uint8_t page, const uint8_t *data) {
    mock_nfc_t *nfc = (mock_nfc_t *)ctx;
    uint16_t offset = page * OPT_PAGE_SIZE;

    if (nfc->simulate_disconnect && nfc->pages_written >= nfc->disconnect_after_page) {
        return OPT_ERR_NFC_DISCONNECTED;
    }

    if (offset + OPT_PAGE_SIZE > nfc->size) {
        return OPT_ERR_NFC_WRITE;
    }

    memcpy(nfc->memory + offset, data, OPT_PAGE_SIZE);
    nfc->pages_written++;
    return OPT_OK;
}

static bool mock_is_present(void *ctx) {
    mock_nfc_t *nfc = (mock_nfc_t *)ctx;
    if (nfc->simulate_disconnect && nfc->pages_written >= nfc->disconnect_after_page) {
        return false;
    }
    return true;
}

static opt_nfc_hal_t create_mock_hal(mock_nfc_t *nfc) {
    opt_nfc_hal_t hal = {
        .read_page = mock_read_page,
        .write_page = mock_write_page,
        .is_present = mock_is_present,
        .user_ctx = nfc
    };
    return hal;
}

/*============================================================================
 * Test Reference Data
 *============================================================================*/

/* TODO: Add tests that load reference data from specs/OpenPrintTag/tests/encode_decode/ */

/*============================================================================
 * Test Functions
 *============================================================================*/

static int test_init(void) {
    opt_tag_t tag;

    opt_error_t err = opt_init(&tag);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_EQUAL(false, tag.initialized);
    TEST_ASSERT_EQUAL(0, tag.data_size);

    return 0;
}

static int test_format_empty_tag(void) {
    opt_tag_t tag;
    opt_init(&tag);

    /* Format a 540-byte tag (NTAG215) with 35-byte aux region */
    opt_error_t err = opt_format_empty_tag(&tag, 540, 35);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT(tag.initialized);
    TEST_ASSERT_EQUAL(540, tag.data_size);

    /* Check CC */
    TEST_ASSERT_EQUAL(0xE1, tag.data[0]);

    /* Check meta region is valid */
    TEST_ASSERT(tag.meta.valid);

    /* Check main region is valid */
    TEST_ASSERT(tag.main.valid);

    /* Check aux region is valid */
    TEST_ASSERT(tag.aux.valid);
    TEST_ASSERT(tag.aux.size >= 35);

    return 0;
}

static int test_format_and_set_fields(void) {
    opt_tag_t tag;
    opt_init(&tag);

    opt_error_t err = opt_format_empty_tag(&tag, 540, 35);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Set material class */
    err = opt_set_material_class(&tag, OPT_MATERIAL_CLASS_FFF);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Set material type */
    err = opt_set_material_type(&tag, OPT_MATERIAL_TYPE_PLA);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Set brand name */
    err = opt_set_brand_name(&tag, "Prusament");
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Set material name */
    err = opt_set_material_name(&tag, "Galaxy Black");
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Set weight */
    err = opt_set_actual_full_weight(&tag, 1000.0f);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Set density */
    err = opt_set_density(&tag, 1.24f);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Now read back the values */
    uint8_t mat_class;
    err = opt_get_material_class(&tag, &mat_class);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_EQUAL(OPT_MATERIAL_CLASS_FFF, mat_class);

    uint8_t mat_type;
    err = opt_get_material_type(&tag, &mat_type);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_EQUAL(OPT_MATERIAL_TYPE_PLA, mat_type);

    char brand[32];
    err = opt_get_brand_name(&tag, brand, sizeof(brand));
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_STR_EQUAL("Prusament", brand);

    char name[32];
    err = opt_get_material_name(&tag, name, sizeof(name));
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_STR_EQUAL("Galaxy Black", name);

    float weight;
    err = opt_get_actual_full_weight(&tag, &weight);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_FLOAT_NEAR(1000.0f, weight, 0.1f);

    float density;
    err = opt_get_density(&tag, &density);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_FLOAT_NEAR(1.24f, density, 0.01f);

    return 0;
}

static int test_aux_region(void) {
    opt_tag_t tag;
    opt_init(&tag);

    opt_error_t err = opt_format_empty_tag(&tag, 540, 35);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Initially consumed weight should be 0 */
    float consumed;
    err = opt_get_consumed_weight(&tag, &consumed);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_FLOAT_NEAR(0.0f, consumed, 0.001f);

    /* Set consumed weight */
    err = opt_set_consumed_weight(&tag, 50.0f);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Read back */
    err = opt_get_consumed_weight(&tag, &consumed);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_FLOAT_NEAR(50.0f, consumed, 0.1f);

    /* Add more consumed weight */
    err = opt_add_consumed_weight(&tag, 15.5f);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    err = opt_get_consumed_weight(&tag, &consumed);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_FLOAT_NEAR(65.5f, consumed, 0.1f);

    return 0;
}

static int test_remaining_calculations(void) {
    opt_tag_t tag;
    opt_init(&tag);

    opt_error_t err = opt_format_empty_tag(&tag, 540, 35);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Set up a full spool */
    opt_set_actual_full_weight(&tag, 1000.0f);
    opt_set_density(&tag, 1.24f);
    /* Diameter defaults to 1.75mm */

    /* Use 100g */
    opt_set_consumed_weight(&tag, 100.0f);

    /* Check remaining weight */
    float remaining_weight;
    err = opt_get_remaining_weight(&tag, &remaining_weight);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_FLOAT_NEAR(900.0f, remaining_weight, 0.1f);

    /* Check remaining percent */
    float remaining_percent;
    err = opt_get_remaining_percent(&tag, &remaining_percent);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_FLOAT_NEAR(90.0f, remaining_percent, 0.1f);

    /* Check remaining length calculation */
    float remaining_length;
    err = opt_get_remaining_length(&tag, &remaining_length);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    /* Expected: L = W / (pi * (d/2)^2 * rho) */
    /* = 900 / (3.14159 * (0.0875)^2 * 1.24) = ~30177 cm = ~301770 mm */
    TEST_ASSERT(remaining_length > 200000.0f);  /* Should be ~300m */
    TEST_ASSERT(remaining_length < 400000.0f);

    return 0;
}

static int test_weight_to_length(void) {
    /* Test the conversion formula */
    float length = opt_weight_to_length(1000.0f, 1.24f, 1.75f);

    /* For 1kg PLA (density 1.24), diameter 1.75mm:
     * Radius = 0.0875 cm
     * Area = pi * 0.0875^2 = 0.02405 cm^2
     * Length = 1000 / (0.02405 * 1.24) = ~33526 cm = ~335m = ~335260mm
     */
    TEST_ASSERT(length > 300000.0f);
    TEST_ASSERT(length < 400000.0f);

    /* Test reverse calculation */
    float weight = opt_length_to_weight(length, 1.24f, 1.75f);
    TEST_ASSERT_FLOAT_NEAR(1000.0f, weight, 1.0f);

    return 0;
}

static int test_filament_info(void) {
    opt_tag_t tag;
    opt_init(&tag);

    opt_error_t err = opt_format_empty_tag(&tag, 540, 35);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Set up tag data */
    opt_set_material_class(&tag, OPT_MATERIAL_CLASS_FFF);
    opt_set_material_type(&tag, OPT_MATERIAL_TYPE_PLA);
    opt_set_brand_name(&tag, "Prusament");
    opt_set_material_name(&tag, "PLA Galaxy Black");
    opt_set_actual_full_weight(&tag, 1012.0f);
    opt_set_density(&tag, 1.24f);

    uint8_t color[] = {0x3D, 0x3E, 0x3D, 0xFF};
    opt_set_primary_color(&tag, color);

    opt_set_consumed_weight(&tag, 200.0f);

    /* Get filament info */
    opt_filament_info_t info;
    err = opt_get_filament_info(&tag, &info);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    TEST_ASSERT_EQUAL(OPT_MATERIAL_CLASS_FFF, info.material_class);
    TEST_ASSERT_EQUAL(OPT_MATERIAL_TYPE_PLA, info.material_type);
    TEST_ASSERT_STR_EQUAL("Prusament", info.brand_name);
    TEST_ASSERT_STR_EQUAL("PLA Galaxy Black", info.material_name);
    TEST_ASSERT_FLOAT_NEAR(1012.0f, info.total_weight_g, 0.1f);
    TEST_ASSERT_FLOAT_NEAR(200.0f, info.consumed_weight_g, 0.1f);
    TEST_ASSERT_FLOAT_NEAR(812.0f, info.remaining_weight_g, 0.1f);
    TEST_ASSERT_FLOAT_NEAR(1.24f, info.density, 0.01f);
    TEST_ASSERT_EQUAL(0x3D, info.primary_color[0]);
    TEST_ASSERT_EQUAL(0x3E, info.primary_color[1]);
    TEST_ASSERT_EQUAL(0x3D, info.primary_color[2]);
    TEST_ASSERT_EQUAL(0xFF, info.primary_color[3]);
    TEST_ASSERT(info.has_aux);

    return 0;
}

static int test_mock_nfc_read_write(void) {
    mock_nfc_t mock = {0};
    mock.size = 540;
    opt_nfc_hal_t hal = create_mock_hal(&mock);

    /* Create and format a tag */
    opt_tag_t tag;
    opt_init(&tag);
    opt_format_empty_tag(&tag, 540, 35);

    /* Set some data */
    opt_set_material_class(&tag, OPT_MATERIAL_CLASS_FFF);
    opt_set_brand_name(&tag, "TestBrand");

    /* Write to mock NFC */
    opt_error_t err = opt_write_to_nfc(&tag, &hal);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Create a new tag and read from mock */
    opt_tag_t tag2;
    opt_init(&tag2);
    err = opt_read_from_nfc(&tag2, &hal, 0, 135);  /* 540/4 = 135 pages */
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Verify the data was read correctly */
    uint8_t mat_class;
    err = opt_get_material_class(&tag2, &mat_class);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_EQUAL(OPT_MATERIAL_CLASS_FFF, mat_class);

    char brand[32];
    err = opt_get_brand_name(&tag2, brand, sizeof(brand));
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_STR_EQUAL("TestBrand", brand);

    return 0;
}

static int test_dirty_page_tracking(void) {
    opt_tag_t tag;
    opt_init(&tag);
    opt_format_empty_tag(&tag, 540, 35);

    /* Clear dirty pages */
    opt_clear_dirty(&tag);

    /* Mark a range as dirty */
    opt_mark_dirty(&tag, 10, 8);  /* Pages 2-3 (offset 10-17) */

    /* Check dirty bits */
    TEST_ASSERT(tag.dirty_pages[0] & (1 << 2));  /* Page 2 */
    TEST_ASSERT(tag.dirty_pages[0] & (1 << 3));  /* Page 3 */
    TEST_ASSERT(tag.dirty_pages[0] & (1 << 4));  /* Page 4 */
    TEST_ASSERT(!(tag.dirty_pages[0] & (1 << 1)));  /* Page 1 should not be dirty */

    /* Clear and verify */
    opt_clear_dirty(&tag);
    TEST_ASSERT_EQUAL(0, tag.dirty_pages[0]);

    return 0;
}

static int test_incremental_write(void) {
    mock_nfc_t mock = {0};
    mock.size = 200;
    opt_nfc_hal_t hal = create_mock_hal(&mock);

    opt_tag_t tag;
    opt_init(&tag);
    opt_format_empty_tag(&tag, 200, 16);
    opt_set_brand_name(&tag, "Test");

    opt_write_state_t state;
    opt_error_t err = opt_write_start(&tag, &state);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_EQUAL(false, state.completed);
    TEST_ASSERT_EQUAL(0, state.start_page);
    TEST_ASSERT_EQUAL(0, state.current_page);

    /* Write incrementally */
    while (!state.completed) {
        err = opt_write_continue(&tag, &hal, &state);
        TEST_ASSERT_EQUAL(OPT_OK, err);
    }

    TEST_ASSERT(state.completed);
    TEST_ASSERT(mock.pages_written > 0);

    return 0;
}

static int test_disconnect_recovery(void) {
    mock_nfc_t mock = {0};
    mock.size = 200;
    mock.simulate_disconnect = true;
    mock.disconnect_after_page = 5;  /* Disconnect after 5 pages */
    opt_nfc_hal_t hal = create_mock_hal(&mock);

    opt_tag_t tag;
    opt_init(&tag);
    opt_format_empty_tag(&tag, 200, 16);

    opt_write_state_t state;
    opt_write_start(&tag, &state);

    opt_error_t err;
    int disconnect_count = 0;

    while (!state.completed) {
        err = opt_write_continue(&tag, &hal, &state);
        if (err == OPT_ERR_NFC_DISCONNECTED) {
            disconnect_count++;
            /* "Reconnect" by allowing more writes */
            mock.disconnect_after_page += 10;
        } else {
            TEST_ASSERT_EQUAL(OPT_OK, err);
        }
    }

    TEST_ASSERT(disconnect_count > 0);  /* We should have had at least one disconnect */
    TEST_ASSERT(state.completed);

    return 0;
}

static int test_error_strings(void) {
    TEST_ASSERT_STR_EQUAL("OK", opt_error_str(OPT_OK));
    TEST_ASSERT_STR_EQUAL("Invalid parameter", opt_error_str(OPT_ERR_INVALID_PARAM));
    TEST_ASSERT_STR_EQUAL("NDEF parse error", opt_error_str(OPT_ERR_NDEF_PARSE));
    TEST_ASSERT_STR_EQUAL("Field not found", opt_error_str(OPT_ERR_FIELD_NOT_FOUND));
    TEST_ASSERT_STR_EQUAL("NFC tag disconnected", opt_error_str(OPT_ERR_NFC_DISCONNECTED));

    return 0;
}

static int test_material_type_strings(void) {
    TEST_ASSERT_STR_EQUAL("PLA", opt_material_type_str(OPT_MATERIAL_TYPE_PLA));
    TEST_ASSERT_STR_EQUAL("PETG", opt_material_type_str(OPT_MATERIAL_TYPE_PETG));
    TEST_ASSERT_STR_EQUAL("TPU", opt_material_type_str(OPT_MATERIAL_TYPE_TPU));
    TEST_ASSERT_STR_EQUAL("ABS", opt_material_type_str(OPT_MATERIAL_TYPE_ABS));
    TEST_ASSERT_STR_EQUAL("PC", opt_material_type_str(OPT_MATERIAL_TYPE_PC));
    TEST_ASSERT_STR_EQUAL("PA12", opt_material_type_str(OPT_MATERIAL_TYPE_PA12));
    TEST_ASSERT_STR_EQUAL("Unknown", opt_material_type_str(100));  /* Invalid type */

    return 0;
}

static int test_default_diameter(void) {
    opt_tag_t tag;
    opt_init(&tag);
    opt_format_empty_tag(&tag, 540, 35);

    /* Don't set diameter, should default to 1.75mm */
    float diameter;
    opt_error_t err = opt_get_filament_diameter(&tag, &diameter);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_FLOAT_NEAR(1.75f, diameter, 0.001f);

    /* Now set a different diameter */
    err = opt_set_filament_diameter(&tag, 2.85f);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    err = opt_get_filament_diameter(&tag, &diameter);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_FLOAT_NEAR(2.85f, diameter, 0.01f);

    return 0;
}

static int test_temperature_fields(void) {
    opt_tag_t tag;
    opt_init(&tag);
    opt_format_empty_tag(&tag, 540, 35);

    /* Set temperatures */
    opt_set_min_print_temp(&tag, 205);
    opt_set_max_print_temp(&tag, 225);
    opt_set_preheat_temp(&tag, 170);
    opt_set_min_bed_temp(&tag, 40);
    opt_set_max_bed_temp(&tag, 60);

    /* Read back */
    int16_t temp;
    TEST_ASSERT_EQUAL(OPT_OK, opt_get_min_print_temp(&tag, &temp));
    TEST_ASSERT_EQUAL(205, temp);

    TEST_ASSERT_EQUAL(OPT_OK, opt_get_max_print_temp(&tag, &temp));
    TEST_ASSERT_EQUAL(225, temp);

    TEST_ASSERT_EQUAL(OPT_OK, opt_get_preheat_temp(&tag, &temp));
    TEST_ASSERT_EQUAL(170, temp);

    TEST_ASSERT_EQUAL(OPT_OK, opt_get_min_bed_temp(&tag, &temp));
    TEST_ASSERT_EQUAL(40, temp);

    TEST_ASSERT_EQUAL(OPT_OK, opt_get_max_bed_temp(&tag, &temp));
    TEST_ASSERT_EQUAL(60, temp);

    return 0;
}

static int test_color_field(void) {
    opt_tag_t tag;
    opt_init(&tag);
    opt_format_empty_tag(&tag, 540, 35);

    /* Set RGBA color */
    uint8_t set_color[] = {0xFF, 0x00, 0x80, 0xC0};
    opt_error_t err = opt_set_primary_color(&tag, set_color);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Read back */
    uint8_t get_color[4];
    err = opt_get_primary_color(&tag, get_color);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT_EQUAL(0xFF, get_color[0]);
    TEST_ASSERT_EQUAL(0x00, get_color[1]);
    TEST_ASSERT_EQUAL(0x80, get_color[2]);
    TEST_ASSERT_EQUAL(0xC0, get_color[3]);

    return 0;
}

static int test_no_aux_region(void) {
    opt_tag_t tag;
    opt_init(&tag);

    /* Format without aux region */
    opt_error_t err = opt_format_empty_tag(&tag, 540, 0);
    TEST_ASSERT_EQUAL(OPT_OK, err);
    TEST_ASSERT(tag.initialized);
    TEST_ASSERT(tag.meta.valid);
    TEST_ASSERT(tag.main.valid);
    TEST_ASSERT(!tag.aux.valid);

    /* Trying to set consumed weight should fail */
    err = opt_set_consumed_weight(&tag, 50.0f);
    TEST_ASSERT_EQUAL(OPT_ERR_INVALID_PARAM, err);

    return 0;
}

static int test_invalid_params(void) {
    opt_tag_t tag;
    opt_tag_t *null_tag = NULL;

    /* Test NULL pointer handling */
    TEST_ASSERT_EQUAL(OPT_ERR_INVALID_PARAM, opt_init(null_tag));
    TEST_ASSERT_EQUAL(OPT_ERR_INVALID_PARAM, opt_format_empty_tag(null_tag, 540, 35));

    /* Test invalid size */
    opt_init(&tag);
    TEST_ASSERT_EQUAL(OPT_ERR_INVALID_PARAM, opt_format_empty_tag(&tag, 30, 0));  /* Too small */
    TEST_ASSERT_EQUAL(OPT_ERR_INVALID_PARAM, opt_format_empty_tag(&tag, 1000, 0));  /* Too large */

    /* Test string too long */
    opt_format_empty_tag(&tag, 540, 35);
    char long_name[64];
    memset(long_name, 'A', 63);
    long_name[63] = '\0';
    TEST_ASSERT_EQUAL(OPT_ERR_INVALID_PARAM, opt_set_brand_name(&tag, long_name));

    return 0;
}

static int test_write_aux_only(void) {
    mock_nfc_t mock = {0};
    mock.size = 540;
    opt_nfc_hal_t hal = create_mock_hal(&mock);

    /* Create tag with full data */
    opt_tag_t tag;
    opt_init(&tag);
    opt_format_empty_tag(&tag, 540, 35);
    opt_set_brand_name(&tag, "TestBrand");
    opt_set_actual_full_weight(&tag, 1000.0f);
    opt_set_consumed_weight(&tag, 50.0f);

    /* First write everything */
    opt_error_t err = opt_write_to_nfc(&tag, &hal);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    int full_pages = mock.pages_written;
    mock.pages_written = 0;

    /* Now update only aux and write aux region */
    opt_add_consumed_weight(&tag, 25.0f);
    err = opt_write_aux_region(&tag, &hal);
    TEST_ASSERT_EQUAL(OPT_OK, err);

    /* Should have written fewer pages than full write */
    TEST_ASSERT(mock.pages_written < full_pages);
    TEST_ASSERT(mock.pages_written > 0);

    return 0;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    int tests_passed = 0;
    int tests_failed = 0;
    int tests_total = 0;

    printf("OpenPrintTag Library Unit Tests\n");
    printf("================================\n\n");

    RUN_TEST(test_init);
    RUN_TEST(test_format_empty_tag);
    RUN_TEST(test_format_and_set_fields);
    RUN_TEST(test_aux_region);
    RUN_TEST(test_remaining_calculations);
    RUN_TEST(test_weight_to_length);
    RUN_TEST(test_filament_info);
    RUN_TEST(test_mock_nfc_read_write);
    RUN_TEST(test_dirty_page_tracking);
    RUN_TEST(test_incremental_write);
    RUN_TEST(test_disconnect_recovery);
    RUN_TEST(test_error_strings);
    RUN_TEST(test_material_type_strings);
    RUN_TEST(test_default_diameter);
    RUN_TEST(test_temperature_fields);
    RUN_TEST(test_color_field);
    RUN_TEST(test_no_aux_region);
    RUN_TEST(test_invalid_params);
    RUN_TEST(test_write_aux_only);

    printf("\n================================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_total);

    return tests_failed > 0 ? 1 : 0;
}
