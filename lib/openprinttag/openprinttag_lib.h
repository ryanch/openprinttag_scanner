/**
 * OpenPrintTag Library
 *
 * C library implementing the OpenPrintTag NFC data specification for
 * reading/writing filament data to NFC tags.
 *
 * This library is ESP-IDF independent and can be tested on host machines.
 * Compatible with PN532 NFC readers using pn532_ntag2xx_ReadPage/WritePage.
 */

#ifndef OPENPRINTTAG_LIB_H
#define OPENPRINTTAG_LIB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define OPT_MAX_TAG_SIZE        924     /* Max supported tag size */
#define OPT_PAGE_SIZE           4       /* NTAG page size */
#define OPT_MAX_STRING_LEN      32      /* Max string field length */
#define OPT_MIME_TYPE           "application/vnd.openprinttag"
#define OPT_MIME_TYPE_LEN       28

/* NDEF constants */
#define OPT_CC_MAGIC            0xE1    /* Capability container magic */
#define OPT_TLV_NDEF            0x03    /* NDEF TLV tag */
#define OPT_TLV_TERMINATOR      0xFE    /* TLV terminator */

/* Default filament diameter if not specified (mm) */
#define OPT_DEFAULT_DIAMETER    1.75f

/*============================================================================
 * Field Keys (from OpenPrintTag spec)
 *============================================================================*/

/* Meta region keys */
#define OPT_META_MAIN_OFFSET    0
#define OPT_META_MAIN_SIZE      1
#define OPT_META_AUX_OFFSET     2
#define OPT_META_AUX_SIZE       3

/* Main region keys */
#define OPT_MAIN_INSTANCE_UUID          0
#define OPT_MAIN_PACKAGE_UUID           1
#define OPT_MAIN_MATERIAL_UUID          2
#define OPT_MAIN_BRAND_UUID             3
#define OPT_MAIN_GTIN                   4
#define OPT_MAIN_BRAND_INSTANCE_ID      5
#define OPT_MAIN_BRAND_PACKAGE_ID       6
#define OPT_MAIN_BRAND_MATERIAL_ID      7
#define OPT_MAIN_MATERIAL_CLASS         8
#define OPT_MAIN_MATERIAL_TYPE          9
#define OPT_MAIN_MATERIAL_NAME          10
#define OPT_MAIN_BRAND_NAME             11
#define OPT_MAIN_WRITE_PROTECTION       13
#define OPT_MAIN_MANUFACTURED_DATE      14
#define OPT_MAIN_EXPIRATION_DATE        15
#define OPT_MAIN_NOMINAL_FULL_WEIGHT    16
#define OPT_MAIN_ACTUAL_FULL_WEIGHT     17
#define OPT_MAIN_EMPTY_CONTAINER_WEIGHT 18
#define OPT_MAIN_PRIMARY_COLOR          19
#define OPT_MAIN_SECONDARY_COLOR_0      20
#define OPT_MAIN_SECONDARY_COLOR_1      21
#define OPT_MAIN_SECONDARY_COLOR_2      22
#define OPT_MAIN_SECONDARY_COLOR_3      23
#define OPT_MAIN_SECONDARY_COLOR_4      24
#define OPT_MAIN_TRANSMISSION_DISTANCE  27
#define OPT_MAIN_TAGS                   28
#define OPT_MAIN_DENSITY                29
#define OPT_MAIN_FILAMENT_DIAMETER      30
#define OPT_MAIN_SHORE_HARDNESS_A       31
#define OPT_MAIN_SHORE_HARDNESS_D       32
#define OPT_MAIN_MIN_NOZZLE_DIAMETER    33
#define OPT_MAIN_MIN_PRINT_TEMP         34
#define OPT_MAIN_MAX_PRINT_TEMP         35
#define OPT_MAIN_PREHEAT_TEMP           36
#define OPT_MAIN_MIN_BED_TEMP           37
#define OPT_MAIN_MAX_BED_TEMP           38
#define OPT_MAIN_MIN_CHAMBER_TEMP       39
#define OPT_MAIN_MAX_CHAMBER_TEMP       40
#define OPT_MAIN_CHAMBER_TEMP           41
#define OPT_MAIN_CONTAINER_WIDTH        42
#define OPT_MAIN_CONTAINER_OUTER_DIAM   43
#define OPT_MAIN_CONTAINER_INNER_DIAM   44
#define OPT_MAIN_CONTAINER_HOLE_DIAM    45
#define OPT_MAIN_VISCOSITY_18C          46
#define OPT_MAIN_VISCOSITY_25C          47
#define OPT_MAIN_VISCOSITY_40C          48
#define OPT_MAIN_VISCOSITY_60C          49
#define OPT_MAIN_CONTAINER_VOLUME       50
#define OPT_MAIN_CURE_WAVELENGTH        51
#define OPT_MAIN_MATERIAL_ABBREVIATION  52
#define OPT_MAIN_NOMINAL_FULL_LENGTH    53
#define OPT_MAIN_ACTUAL_FULL_LENGTH     54
#define OPT_MAIN_COUNTRY_OF_ORIGIN      55
#define OPT_MAIN_CERTIFICATIONS         56
#define OPT_MAIN_DRYING_TEMP            57
#define OPT_MAIN_DRYING_TIME            58

/* Aux region keys */
#define OPT_AUX_CONSUMED_WEIGHT         0
#define OPT_AUX_WORKGROUP               1
#define OPT_AUX_GP_RANGE_USER           2
#define OPT_AUX_LAST_STIR_TIME          3

/* General purpose key range (65400-65534) */
#define OPT_AUX_GP_SPOOLMAN_ID          65400
#define OPT_GP_RANGE_USER_OPENSCAN      "openscan"

/*============================================================================
 * Material Class Enum
 *============================================================================*/

typedef enum {
    OPT_MATERIAL_CLASS_FFF = 0,
    OPT_MATERIAL_CLASS_SLA = 1,
} opt_material_class_t;

/*============================================================================
 * Material Type Enum (FFF materials)
 *============================================================================*/

typedef enum {
    OPT_MATERIAL_TYPE_PLA   = 0,
    OPT_MATERIAL_TYPE_PETG  = 1,
    OPT_MATERIAL_TYPE_TPU   = 2,
    OPT_MATERIAL_TYPE_ABS   = 3,
    OPT_MATERIAL_TYPE_ASA   = 4,
    OPT_MATERIAL_TYPE_PC    = 5,
    OPT_MATERIAL_TYPE_PCTG  = 6,
    OPT_MATERIAL_TYPE_PP    = 7,
    OPT_MATERIAL_TYPE_PA6   = 8,
    OPT_MATERIAL_TYPE_PA11  = 9,
    OPT_MATERIAL_TYPE_PA12  = 10,
    OPT_MATERIAL_TYPE_PA66  = 11,
    OPT_MATERIAL_TYPE_CPE   = 12,
    OPT_MATERIAL_TYPE_TPE   = 13,
    OPT_MATERIAL_TYPE_HIPS  = 14,
    OPT_MATERIAL_TYPE_PHA   = 15,
    OPT_MATERIAL_TYPE_PET   = 16,
    OPT_MATERIAL_TYPE_PEI   = 17,
    OPT_MATERIAL_TYPE_PBT   = 18,
    OPT_MATERIAL_TYPE_PVB   = 19,
    OPT_MATERIAL_TYPE_PVA   = 20,
    OPT_MATERIAL_TYPE_PEKK  = 21,
    OPT_MATERIAL_TYPE_PEEK  = 22,
    OPT_MATERIAL_TYPE_BVOH  = 23,
    OPT_MATERIAL_TYPE_TPC   = 24,
    OPT_MATERIAL_TYPE_PPS   = 25,
    OPT_MATERIAL_TYPE_PPSU  = 26,
    OPT_MATERIAL_TYPE_PVC   = 27,
    OPT_MATERIAL_TYPE_PEBA  = 28,
    OPT_MATERIAL_TYPE_PVDF  = 29,
    OPT_MATERIAL_TYPE_PPA   = 30,
    OPT_MATERIAL_TYPE_PCL   = 31,
    OPT_MATERIAL_TYPE_PES   = 32,
    OPT_MATERIAL_TYPE_PMMA  = 33,
    OPT_MATERIAL_TYPE_POM   = 34,
    OPT_MATERIAL_TYPE_PPE   = 35,
    OPT_MATERIAL_TYPE_PS    = 36,
    OPT_MATERIAL_TYPE_PSU   = 37,
    OPT_MATERIAL_TYPE_TPI   = 38,
    OPT_MATERIAL_TYPE_SBS   = 39,
    OPT_MATERIAL_TYPE_OBC   = 40,
    OPT_MATERIAL_TYPE_EVA   = 41,
} opt_material_type_t;

/*============================================================================
 * Error Codes
 *============================================================================*/

typedef enum {
    OPT_OK = 0,
    OPT_ERR_INVALID_PARAM,
    OPT_ERR_BUFFER_TOO_SMALL,
    OPT_ERR_CBOR_ENCODE,
    OPT_ERR_CBOR_DECODE,
    OPT_ERR_FIELD_NOT_FOUND,
    OPT_ERR_NDEF_PARSE,
    OPT_ERR_NFC_READ,
    OPT_ERR_NFC_WRITE,
    OPT_ERR_NFC_DISCONNECTED,
    OPT_ERR_REGION_OVERFLOW,
    OPT_ERR_INVALID_TAG,
    OPT_ERR_NOT_INITIALIZED,
} opt_error_t;

/*============================================================================
 * NFC Hardware Abstraction Layer
 *============================================================================*/

typedef opt_error_t (*opt_nfc_read_page_fn)(void *ctx, uint8_t page, uint8_t *buffer);
typedef opt_error_t (*opt_nfc_write_page_fn)(void *ctx, uint8_t page, const uint8_t *data);
typedef bool (*opt_nfc_is_present_fn)(void *ctx);

typedef struct {
    opt_nfc_read_page_fn  read_page;     /* Required: read 4-byte page */
    opt_nfc_write_page_fn write_page;    /* Required: write 4-byte page */
    opt_nfc_is_present_fn is_present;    /* Optional: check if tag present */
    void *user_ctx;                       /* User context for callbacks */
} opt_nfc_hal_t;

/*============================================================================
 * Region Info Structure
 *============================================================================*/

typedef struct {
    uint16_t offset;    /* Offset within payload */
    uint16_t size;      /* Allocated size */
    bool     valid;     /* Region exists and is valid */
} opt_region_info_t;

/*============================================================================
 * Tag Context
 *============================================================================*/

#define OPT_DIRTY_PAGES_SIZE ((OPT_MAX_TAG_SIZE / OPT_PAGE_SIZE + 7) / 8)

typedef struct {
    uint8_t  data[OPT_MAX_TAG_SIZE];      /* Raw tag data */
    uint16_t data_size;                    /* Total data size */
    uint16_t payload_offset;               /* NDEF payload offset in data */
    uint16_t payload_size;                 /* NDEF payload size */

    opt_region_info_t meta;                /* Meta region info */
    opt_region_info_t main;                /* Main region info */
    opt_region_info_t aux;                 /* Aux region info */

    uint8_t  dirty_pages[OPT_DIRTY_PAGES_SIZE];  /* Dirty page bitmap */
    bool     initialized;                   /* Tag has been initialized */
} opt_tag_t;

/*============================================================================
 * Filament Info Structure (High-Level)
 *============================================================================*/

typedef struct {
    uint8_t  material_class;               /* 0=FFF, 1=SLA */
    uint8_t  material_type;                /* PLA=0, PETG=1, etc. */
    char     material_name[OPT_MAX_STRING_LEN];
    char     brand_name[OPT_MAX_STRING_LEN];
    uint8_t  primary_color[4];             /* RGBA */
    float    total_weight_g;               /* Full spool weight */
    float    consumed_weight_g;            /* Used weight */
    float    remaining_weight_g;           /* Calculated remaining */
    float    remaining_length_mm;          /* Calculated remaining */
    float    remaining_percent;            /* Calculated remaining */
    float    density;                      /* g/cm^3 */
    float    diameter_mm;                  /* Filament diameter */
    bool     has_aux;                      /* Has aux region data */
} opt_filament_info_t;

/*============================================================================
 * Write State (for disconnect recovery)
 *============================================================================*/

typedef struct {
    uint8_t  start_page;                   /* First page to write */
    uint8_t  current_page;                 /* Current page being written */
    uint8_t  end_page;                     /* Last page to write (exclusive) */
    bool     completed;                    /* Write operation complete */
} opt_write_state_t;

/*============================================================================
 * Initialization Functions
 *============================================================================*/

/**
 * Initialize a tag context to empty state.
 *
 * @param tag   Tag context to initialize
 * @return OPT_OK on success
 */
opt_error_t opt_init(opt_tag_t *tag);

/**
 * Format a tag context with empty NDEF structure.
 *
 * @param tag       Tag context to format
 * @param size      Total tag size in bytes (e.g., 540 for NTAG215)
 * @param aux_size  Size to reserve for aux region (0 for no aux)
 * @return OPT_OK on success
 */
opt_error_t opt_format_empty_tag(opt_tag_t *tag, uint16_t size, uint16_t aux_size);

/*============================================================================
 * NFC Operations
 *============================================================================*/

/**
 * Read tag data from NFC using HAL.
 *
 * @param tag        Tag context to read into
 * @param hal        NFC hardware abstraction layer
 * @param start_page First page to read (typically 0 for ISO 15693)
 * @param num_pages  Number of pages to read
 * @return OPT_OK on success
 */
opt_error_t opt_read_from_nfc(opt_tag_t *tag, const opt_nfc_hal_t *hal,
                              uint8_t start_page, uint8_t num_pages);

/**
 * Write entire tag data to NFC.
 *
 * @param tag   Tag context to write
 * @param hal   NFC hardware abstraction layer
 * @return OPT_OK on success
 */
opt_error_t opt_write_to_nfc(opt_tag_t *tag, const opt_nfc_hal_t *hal);

/**
 * Write only dirty (modified) pages to NFC.
 *
 * @param tag   Tag context to write
 * @param hal   NFC hardware abstraction layer
 * @return OPT_OK on success
 */
opt_error_t opt_write_dirty_pages(opt_tag_t *tag, const opt_nfc_hal_t *hal);

/**
 * Write only the aux region to NFC.
 *
 * @param tag   Tag context to write
 * @param hal   NFC hardware abstraction layer
 * @return OPT_OK on success
 */
opt_error_t opt_write_aux_region(opt_tag_t *tag, const opt_nfc_hal_t *hal);

/*============================================================================
 * Buffer Parsing
 *============================================================================*/

/**
 * Parse NDEF structure from tag data buffer.
 * Call after reading data into tag->data.
 *
 * @param tag   Tag context to parse
 * @return OPT_OK on success
 */
opt_error_t opt_parse_ndef(opt_tag_t *tag);

/*============================================================================
 * Field Getters (Main Region)
 *============================================================================*/

opt_error_t opt_get_material_class(const opt_tag_t *tag, uint8_t *value);
opt_error_t opt_get_material_type(const opt_tag_t *tag, uint8_t *value);
opt_error_t opt_get_material_name(const opt_tag_t *tag, char *buf, size_t size);
opt_error_t opt_get_brand_name(const opt_tag_t *tag, char *buf, size_t size);
opt_error_t opt_get_primary_color(const opt_tag_t *tag, uint8_t rgba[4]);
opt_error_t opt_get_nominal_full_weight(const opt_tag_t *tag, float *grams);
opt_error_t opt_get_actual_full_weight(const opt_tag_t *tag, float *grams);
opt_error_t opt_get_actual_full_length(const opt_tag_t *tag, float *mm);
opt_error_t opt_get_nominal_full_length(const opt_tag_t *tag, float *mm);
opt_error_t opt_get_density(const opt_tag_t *tag, float *g_per_cm3);
opt_error_t opt_get_filament_diameter(const opt_tag_t *tag, float *mm);
opt_error_t opt_get_empty_container_weight(const opt_tag_t *tag, float *grams);
opt_error_t opt_get_min_print_temp(const opt_tag_t *tag, int16_t *celsius);
opt_error_t opt_get_max_print_temp(const opt_tag_t *tag, int16_t *celsius);
opt_error_t opt_get_preheat_temp(const opt_tag_t *tag, int16_t *celsius);
opt_error_t opt_get_min_bed_temp(const opt_tag_t *tag, int16_t *celsius);
opt_error_t opt_get_max_bed_temp(const opt_tag_t *tag, int16_t *celsius);
opt_error_t opt_get_gtin(const opt_tag_t *tag, uint64_t *gtin);
opt_error_t opt_get_manufactured_date(const opt_tag_t *tag, uint32_t *timestamp);

/*============================================================================
 * Field Setters (Main Region)
 *============================================================================*/

opt_error_t opt_set_material_class(opt_tag_t *tag, uint8_t value);
opt_error_t opt_set_material_type(opt_tag_t *tag, uint8_t value);
opt_error_t opt_set_material_name(opt_tag_t *tag, const char *name);
opt_error_t opt_set_brand_name(opt_tag_t *tag, const char *name);
opt_error_t opt_set_primary_color(opt_tag_t *tag, const uint8_t rgba[4]);
opt_error_t opt_set_nominal_full_weight(opt_tag_t *tag, float grams);
opt_error_t opt_set_actual_full_weight(opt_tag_t *tag, float grams);
opt_error_t opt_set_actual_full_length(opt_tag_t *tag, float mm);
opt_error_t opt_set_density(opt_tag_t *tag, float g_per_cm3);
opt_error_t opt_set_filament_diameter(opt_tag_t *tag, float mm);
opt_error_t opt_set_min_print_temp(opt_tag_t *tag, int16_t celsius);
opt_error_t opt_set_max_print_temp(opt_tag_t *tag, int16_t celsius);
opt_error_t opt_set_preheat_temp(opt_tag_t *tag, int16_t celsius);
opt_error_t opt_set_min_bed_temp(opt_tag_t *tag, int16_t celsius);
opt_error_t opt_set_max_bed_temp(opt_tag_t *tag, int16_t celsius);
opt_error_t opt_set_gtin(opt_tag_t *tag, uint64_t gtin);
opt_error_t opt_set_manufactured_date(opt_tag_t *tag, uint32_t timestamp);

/*============================================================================
 * Field Accessors (Aux Region)
 *============================================================================*/

opt_error_t opt_get_consumed_weight(const opt_tag_t *tag, float *grams);
opt_error_t opt_set_consumed_weight(opt_tag_t *tag, float grams);
opt_error_t opt_add_consumed_weight(opt_tag_t *tag, float grams_delta);

opt_error_t opt_get_gp_spoolman_id(const opt_tag_t *tag, int32_t *id);
opt_error_t opt_set_gp_spoolman_id(opt_tag_t *tag, int32_t id);

/*============================================================================
 * High-Level Functions
 *============================================================================*/

/**
 * Get complete filament info from tag.
 *
 * @param tag   Parsed tag context
 * @param info  Output filament info structure
 * @return OPT_OK on success
 */
opt_error_t opt_get_filament_info(const opt_tag_t *tag, opt_filament_info_t *info);

/**
 * Calculate remaining weight based on full weight and consumed.
 *
 * @param tag    Parsed tag context
 * @param grams  Output remaining weight in grams
 * @return OPT_OK on success
 */
opt_error_t opt_get_remaining_weight(const opt_tag_t *tag, float *grams);

/**
 * Calculate remaining length based on weight, density, and diameter.
 *
 * @param tag   Parsed tag context
 * @param mm    Output remaining length in mm
 * @return OPT_OK on success
 */
opt_error_t opt_get_remaining_length(const opt_tag_t *tag, float *mm);

/**
 * Calculate remaining percentage.
 *
 * @param tag     Parsed tag context
 * @param percent Output remaining percentage (0-100)
 * @return OPT_OK on success
 */
opt_error_t opt_get_remaining_percent(const opt_tag_t *tag, float *percent);

/*============================================================================
 * Disconnect Recovery Functions
 *============================================================================*/

/**
 * Start an incremental write operation.
 *
 * @param tag   Tag context to write
 * @param state State structure to track progress
 * @return OPT_OK on success
 */
opt_error_t opt_write_start(opt_tag_t *tag, opt_write_state_t *state);

/**
 * Continue an incremental write operation.
 * Writes one page and updates state. Can be resumed after disconnect.
 *
 * @param tag   Tag context to write
 * @param hal   NFC hardware abstraction layer
 * @param state State structure tracking progress
 * @return OPT_OK on success, OPT_ERR_NFC_DISCONNECTED if tag removed
 */
opt_error_t opt_write_continue(opt_tag_t *tag, const opt_nfc_hal_t *hal,
                               opt_write_state_t *state);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * Convert weight to length.
 *
 * @param weight_g    Weight in grams
 * @param density     Density in g/cm^3
 * @param diameter_mm Filament diameter in mm
 * @return Length in mm
 */
float opt_weight_to_length(float weight_g, float density, float diameter_mm);

/**
 * Convert length to weight.
 *
 * @param length_mm   Length in mm
 * @param density     Density in g/cm^3
 * @param diameter_mm Filament diameter in mm
 * @return Weight in grams
 */
float opt_length_to_weight(float length_mm, float density, float diameter_mm);

/**
 * Mark pages as dirty for a byte range in the tag data.
 *
 * @param tag    Tag context
 * @param offset Start offset in bytes
 * @param len    Length in bytes
 */
void opt_mark_dirty(opt_tag_t *tag, uint16_t offset, uint16_t len);

/**
 * Clear all dirty page flags.
 *
 * @param tag   Tag context
 */
void opt_clear_dirty(opt_tag_t *tag);

/**
 * Get error message string for error code.
 *
 * @param err   Error code
 * @return Static error message string
 */
const char* opt_error_str(opt_error_t err);

/**
 * Get material type abbreviation string.
 *
 * @param type  Material type enum value
 * @return Static abbreviation string (e.g., "PLA", "PETG")
 */
const char* opt_material_type_str(opt_material_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* OPENPRINTTAG_LIB_H */
