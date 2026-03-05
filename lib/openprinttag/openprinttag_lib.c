/**
 * OpenPrintTag Library Implementation
 *
 * C library implementing the OpenPrintTag NFC data specification.
 * Uses tinycbor for CBOR encoding/decoding.
 */

#include "openprinttag_lib.h"
#include <string.h>
#include <math.h>
#include "cbor.h"

/*============================================================================
 * Internal Constants
 *============================================================================*/

#define PI 3.14159265358979323846f

/* CBOR type masks */
#define CBOR_TYPE_MASK      0xE0
#define CBOR_INFO_MASK      0x1F
#define CBOR_TYPE_UINT      0x00
#define CBOR_TYPE_NINT      0x20
#define CBOR_TYPE_BYTES     0x40
#define CBOR_TYPE_TEXT      0x60
#define CBOR_TYPE_ARRAY     0x80
#define CBOR_TYPE_MAP       0xA0
#define CBOR_TYPE_TAG       0xC0
#define CBOR_TYPE_FLOAT     0xE0

/* CBOR special values */
#define CBOR_BREAK          0xFF
#define CBOR_INDEF_MAP      0xBF
#define CBOR_INDEF_ARRAY    0x9F
#define CBOR_HALF_FLOAT     0xF9
#define CBOR_SINGLE_FLOAT   0xFA
#define CBOR_DOUBLE_FLOAT   0xFB

/*============================================================================
 * Material Type String Table
 *============================================================================*/

static const char* material_type_strings[] = {
    "PLA",   /* 0 */
    "PETG",  /* 1 */
    "TPU",   /* 2 */
    "ABS",   /* 3 */
    "ASA",   /* 4 */
    "PC",    /* 5 */
    "PCTG",  /* 6 */
    "PP",    /* 7 */
    "PA6",   /* 8 */
    "PA11",  /* 9 */
    "PA12",  /* 10 */
    "PA66",  /* 11 */
    "CPE",   /* 12 */
    "TPE",   /* 13 */
    "HIPS",  /* 14 */
    "PHA",   /* 15 */
    "PET",   /* 16 */
    "PEI",   /* 17 */
    "PBT",   /* 18 */
    "PVB",   /* 19 */
    "PVA",   /* 20 */
    "PEKK",  /* 21 */
    "PEEK",  /* 22 */
    "BVOH",  /* 23 */
    "TPC",   /* 24 */
    "PPS",   /* 25 */
    "PPSU",  /* 26 */
    "PVC",   /* 27 */
    "PEBA",  /* 28 */
    "PVDF",  /* 29 */
    "PPA",   /* 30 */
    "PCL",   /* 31 */
    "PES",   /* 32 */
    "PMMA",  /* 33 */
    "POM",   /* 34 */
    "PPE",   /* 35 */
    "PS",    /* 36 */
    "PSU",   /* 37 */
    "TPI",   /* 38 */
    "SBS",   /* 39 */
    "OBC",   /* 40 */
    "EVA",   /* 41 */
};

#define NUM_MATERIAL_TYPES (sizeof(material_type_strings) / sizeof(material_type_strings[0]))

/*============================================================================
 * Internal CBOR Helper Functions
 *============================================================================*/

/* Find a field by integer key in CBOR map data */
static opt_error_t cbor_find_field(const uint8_t *data, size_t len,
                                   uint16_t key, CborValue *out_value,
                                   CborParser *out_parser) {
    CborError err;

    err = cbor_parser_init(data, len, 0, out_parser, out_value);
    if (err != CborNoError) {
        return OPT_ERR_CBOR_DECODE;
    }

    if (!cbor_value_is_map(out_value)) {
        return OPT_ERR_CBOR_DECODE;
    }

    CborValue map_iter;
    err = cbor_value_enter_container(out_value, &map_iter);
    if (err != CborNoError) {
        return OPT_ERR_CBOR_DECODE;
    }

    while (!cbor_value_at_end(&map_iter)) {
        /* Get key */
        if (!cbor_value_is_integer(&map_iter)) {
            /* Skip non-integer keys */
            err = cbor_value_advance(&map_iter);
            if (err != CborNoError) return OPT_ERR_CBOR_DECODE;
            err = cbor_value_advance(&map_iter);
            if (err != CborNoError) return OPT_ERR_CBOR_DECODE;
            continue;
        }

        int64_t map_key;
        err = cbor_value_get_int64(&map_iter, &map_key);
        if (err != CborNoError) return OPT_ERR_CBOR_DECODE;

        err = cbor_value_advance(&map_iter);
        if (err != CborNoError) return OPT_ERR_CBOR_DECODE;

        if (map_key == key) {
            *out_value = map_iter;
            return OPT_OK;
        }

        err = cbor_value_advance(&map_iter);
        if (err != CborNoError) return OPT_ERR_CBOR_DECODE;
    }

    return OPT_ERR_FIELD_NOT_FOUND;
}

/* Get integer value from CborValue */
static opt_error_t cbor_get_int(CborValue *val, int64_t *out) {
    if (cbor_value_is_integer(val)) {
        return cbor_value_get_int64(val, out) == CborNoError ? OPT_OK : OPT_ERR_CBOR_DECODE;
    }
    return OPT_ERR_CBOR_DECODE;
}

/* Get float value from CborValue (handles int, half, float, double) */
static opt_error_t cbor_get_float(CborValue *val, float *out) {
    if (cbor_value_is_integer(val)) {
        int64_t ival;
        if (cbor_value_get_int64(val, &ival) == CborNoError) {
            *out = (float)ival;
            return OPT_OK;
        }
    } else if (cbor_value_is_float(val)) {
        float fval;
        if (cbor_value_get_float(val, &fval) == CborNoError) {
            *out = fval;
            return OPT_OK;
        }
    } else if (cbor_value_is_double(val)) {
        double dval;
        if (cbor_value_get_double(val, &dval) == CborNoError) {
            *out = (float)dval;
            return OPT_OK;
        }
    } else if (cbor_value_is_half_float(val)) {
        uint16_t hval;
        if (cbor_value_get_half_float(val, &hval) == CborNoError) {
            /* Convert half float to float */
            uint32_t sign = (hval >> 15) & 0x1;
            uint32_t exp = (hval >> 10) & 0x1F;
            uint32_t mant = hval & 0x3FF;

            if (exp == 0) {
                if (mant == 0) {
                    *out = sign ? -0.0f : 0.0f;
                } else {
                    /* Subnormal */
                    *out = (sign ? -1.0f : 1.0f) * ldexpf((float)mant, -24);
                }
            } else if (exp == 31) {
                if (mant == 0) {
                    *out = sign ? -INFINITY : INFINITY;
                } else {
                    *out = NAN;
                }
            } else {
                *out = (sign ? -1.0f : 1.0f) * ldexpf(1.0f + mant / 1024.0f, exp - 15);
            }
            return OPT_OK;
        }
    }
    return OPT_ERR_CBOR_DECODE;
}

/* Get string value from CborValue */
static opt_error_t cbor_get_string(CborValue *val, char *buf, size_t size, size_t *actual) {
    if (!cbor_value_is_text_string(val)) {
        return OPT_ERR_CBOR_DECODE;
    }

    size_t len = size;
    CborError err = cbor_value_copy_text_string(val, buf, &len, NULL);
    if (err == CborErrorOutOfMemory) {
        /* String truncated but we got what we could */
        buf[size - 1] = '\0';
        if (actual) *actual = size;
        return OPT_OK;
    }
    if (err != CborNoError) {
        return OPT_ERR_CBOR_DECODE;
    }
    if (actual) *actual = len;
    return OPT_OK;
}

/* Get byte string value from CborValue */
static opt_error_t cbor_get_bytes(CborValue *val, uint8_t *buf, size_t size, size_t *actual) {
    if (!cbor_value_is_byte_string(val)) {
        return OPT_ERR_CBOR_DECODE;
    }

    size_t len = size;
    CborError err = cbor_value_copy_byte_string(val, buf, &len, NULL);
    if (err == CborErrorOutOfMemory) {
        if (actual) *actual = size;
        return OPT_OK;
    }
    if (err != CborNoError) {
        return OPT_ERR_CBOR_DECODE;
    }
    if (actual) *actual = len;
    return OPT_OK;
}

/*============================================================================
 * Internal CBOR Encoding Helpers
 *============================================================================*/

/* Start encoding an indefinite map in a region */
static opt_error_t cbor_start_indef_map(uint8_t *buf, size_t *offset) {
    buf[*offset] = CBOR_INDEF_MAP;
    (*offset)++;
    return OPT_OK;
}

/* End an indefinite container */
static opt_error_t cbor_end_indef(uint8_t *buf, size_t *offset) {
    buf[*offset] = CBOR_BREAK;
    (*offset)++;
    return OPT_OK;
}

/* Encode an integer key */
static opt_error_t cbor_encode_int_key(uint8_t *buf, size_t buf_size, size_t *offset, int key) {
    if (*offset >= buf_size) return OPT_ERR_BUFFER_TOO_SMALL;

    if (key >= 0 && key <= 23) {
        buf[*offset] = (uint8_t)(CBOR_TYPE_UINT | key);
        (*offset)++;
    } else if (key >= 0 && key <= 255) {
        if (*offset + 1 >= buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = CBOR_TYPE_UINT | 24;
        buf[*offset + 1] = (uint8_t)key;
        *offset += 2;
    } else if (key >= 0 && key <= 65535) {
        if (*offset + 2 >= buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = CBOR_TYPE_UINT | 25;
        buf[*offset + 1] = (uint8_t)(key >> 8);
        buf[*offset + 2] = (uint8_t)(key & 0xFF);
        *offset += 3;
    } else {
        return OPT_ERR_INVALID_PARAM;
    }
    return OPT_OK;
}

/* Encode an integer value */
static opt_error_t cbor_encode_int_val(uint8_t *buf, size_t buf_size, size_t *offset, int64_t val) {
    uint8_t type = (val >= 0) ? CBOR_TYPE_UINT : CBOR_TYPE_NINT;
    uint64_t uval = (val >= 0) ? (uint64_t)val : (uint64_t)(-(val + 1));

    if (uval <= 23) {
        if (*offset >= buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = type | (uint8_t)uval;
        (*offset)++;
    } else if (uval <= 255) {
        if (*offset + 1 >= buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = type | 24;
        buf[*offset + 1] = (uint8_t)uval;
        *offset += 2;
    } else if (uval <= 65535) {
        if (*offset + 2 >= buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = type | 25;
        buf[*offset + 1] = (uint8_t)(uval >> 8);
        buf[*offset + 2] = (uint8_t)(uval & 0xFF);
        *offset += 3;
    } else if (uval <= 0xFFFFFFFF) {
        if (*offset + 4 >= buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = type | 26;
        buf[*offset + 1] = (uint8_t)(uval >> 24);
        buf[*offset + 2] = (uint8_t)(uval >> 16);
        buf[*offset + 3] = (uint8_t)(uval >> 8);
        buf[*offset + 4] = (uint8_t)(uval & 0xFF);
        *offset += 5;
    } else {
        if (*offset + 8 >= buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = type | 27;
        buf[*offset + 1] = (uint8_t)(uval >> 56);
        buf[*offset + 2] = (uint8_t)(uval >> 48);
        buf[*offset + 3] = (uint8_t)(uval >> 40);
        buf[*offset + 4] = (uint8_t)(uval >> 32);
        buf[*offset + 5] = (uint8_t)(uval >> 24);
        buf[*offset + 6] = (uint8_t)(uval >> 16);
        buf[*offset + 7] = (uint8_t)(uval >> 8);
        buf[*offset + 8] = (uint8_t)(uval & 0xFF);
        *offset += 9;
    }
    return OPT_OK;
}

/* Encode a text string */
static opt_error_t cbor_encode_string(uint8_t *buf, size_t buf_size, size_t *offset, const char *str) {
    size_t len = strlen(str);

    if (len <= 23) {
        if (*offset + 1 + len > buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = CBOR_TYPE_TEXT | (uint8_t)len;
        (*offset)++;
    } else if (len <= 255) {
        if (*offset + 2 + len > buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = CBOR_TYPE_TEXT | 24;
        buf[*offset + 1] = (uint8_t)len;
        *offset += 2;
    } else {
        if (*offset + 3 + len > buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = CBOR_TYPE_TEXT | 25;
        buf[*offset + 1] = (uint8_t)(len >> 8);
        buf[*offset + 2] = (uint8_t)(len & 0xFF);
        *offset += 3;
    }

    memcpy(buf + *offset, str, len);
    *offset += len;
    return OPT_OK;
}

/* Encode a byte string */
static opt_error_t cbor_encode_bytes(uint8_t *buf, size_t buf_size, size_t *offset,
                                     const uint8_t *data, size_t len) {
    if (len <= 23) {
        if (*offset + 1 + len > buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = CBOR_TYPE_BYTES | (uint8_t)len;
        (*offset)++;
    } else if (len <= 255) {
        if (*offset + 2 + len > buf_size) return OPT_ERR_BUFFER_TOO_SMALL;
        buf[*offset] = CBOR_TYPE_BYTES | 24;
        buf[*offset + 1] = (uint8_t)len;
        *offset += 2;
    } else {
        return OPT_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(buf + *offset, data, len);
    *offset += len;
    return OPT_OK;
}

/* Encode a float using CompactFloat logic */
static opt_error_t cbor_encode_compact_float(uint8_t *buf, size_t buf_size, size_t *offset, float val) {
    /* If value is an integer, encode as int */
    if (val == (float)(int32_t)val && val >= -2147483648.0f && val <= 2147483647.0f) {
        return cbor_encode_int_val(buf, buf_size, offset, (int64_t)val);
    }

    /* Try half precision if within acceptable precision */
    /* For now, just use single precision float */
    if (*offset + 5 > buf_size) return OPT_ERR_BUFFER_TOO_SMALL;

    buf[*offset] = CBOR_SINGLE_FLOAT;
    union {
        float f;
        uint32_t u;
    } u;
    u.f = val;
    buf[*offset + 1] = (uint8_t)(u.u >> 24);
    buf[*offset + 2] = (uint8_t)(u.u >> 16);
    buf[*offset + 3] = (uint8_t)(u.u >> 8);
    buf[*offset + 4] = (uint8_t)(u.u & 0xFF);
    *offset += 5;

    return OPT_OK;
}

/*============================================================================
 * NDEF Parsing
 *============================================================================*/

/* Parse TLV length (1 or 3 bytes) */
static uint16_t parse_tlv_length(const uint8_t *data, size_t *offset) {
    uint8_t first = data[*offset];
    (*offset)++;

    if (first == 0xFF) {
        uint16_t len = ((uint16_t)data[*offset] << 8) | data[*offset + 1];
        *offset += 2;
        return len;
    }
    return first;
}

/* Parse NDEF record header and find payload */
static opt_error_t parse_ndef_record(const uint8_t *data, size_t data_len, size_t *offset,
                                     uint16_t *payload_offset, uint16_t *payload_len,
                                     bool *is_openprinttag) {
    if (*offset >= data_len) return OPT_ERR_NDEF_PARSE;

    uint8_t flags = data[*offset];
    (*offset)++;

    bool mb = (flags & 0x80) != 0;  /* Message begin */
    bool me = (flags & 0x40) != 0;  /* Message end */
    bool cf = (flags & 0x20) != 0;  /* Chunk flag */
    bool sr = (flags & 0x10) != 0;  /* Short record */
    bool il = (flags & 0x08) != 0;  /* ID length present */
    uint8_t tnf = flags & 0x07;     /* Type name format */

    (void)mb; (void)me; (void)cf;

    if (*offset >= data_len) return OPT_ERR_NDEF_PARSE;
    uint8_t type_len = data[*offset];
    (*offset)++;

    uint32_t plen;
    if (sr) {
        if (*offset >= data_len) return OPT_ERR_NDEF_PARSE;
        plen = data[*offset];
        (*offset)++;
    } else {
        if (*offset + 3 >= data_len) return OPT_ERR_NDEF_PARSE;
        plen = ((uint32_t)data[*offset] << 24) |
               ((uint32_t)data[*offset + 1] << 16) |
               ((uint32_t)data[*offset + 2] << 8) |
               data[*offset + 3];
        *offset += 4;
    }

    uint8_t id_len = 0;
    if (il) {
        if (*offset >= data_len) return OPT_ERR_NDEF_PARSE;
        id_len = data[*offset];
        (*offset)++;
    }

    /* Check type */
    *is_openprinttag = false;
    if (tnf == 0x02 && type_len == OPT_MIME_TYPE_LEN) {  /* Media type */
        if (*offset + type_len <= data_len) {
            if (memcmp(data + *offset, OPT_MIME_TYPE, OPT_MIME_TYPE_LEN) == 0) {
                *is_openprinttag = true;
            }
        }
    }

    *offset += type_len;
    *offset += id_len;

    *payload_offset = (uint16_t)*offset;
    *payload_len = (uint16_t)plen;

    *offset += plen;

    return OPT_OK;
}

/*============================================================================
 * Region Field Access Helpers
 *============================================================================*/

static opt_error_t get_region_int_field(const opt_tag_t *tag, const opt_region_info_t *region,
                                        uint16_t key, int64_t *out) {
    if (!tag->initialized || !region->valid) {
        return OPT_ERR_NOT_INITIALIZED;
    }

    const uint8_t *region_data = tag->data + tag->payload_offset + region->offset;
    CborParser parser;
    CborValue value;

    opt_error_t err = cbor_find_field(region_data, region->size, key, &value, &parser);
    if (err != OPT_OK) return err;

    return cbor_get_int(&value, out);
}

static opt_error_t get_region_float_field(const opt_tag_t *tag, const opt_region_info_t *region,
                                          uint16_t key, float *out) {
    if (!tag->initialized || !region->valid) {
        return OPT_ERR_NOT_INITIALIZED;
    }

    const uint8_t *region_data = tag->data + tag->payload_offset + region->offset;
    CborParser parser;
    CborValue value;

    opt_error_t err = cbor_find_field(region_data, region->size, key, &value, &parser);
    if (err != OPT_OK) return err;

    return cbor_get_float(&value, out);
}

static opt_error_t get_region_string_field(const opt_tag_t *tag, const opt_region_info_t *region,
                                           uint16_t key, char *buf, size_t size) {
    if (!tag->initialized || !region->valid) {
        return OPT_ERR_NOT_INITIALIZED;
    }

    const uint8_t *region_data = tag->data + tag->payload_offset + region->offset;
    CborParser parser;
    CborValue value;

    opt_error_t err = cbor_find_field(region_data, region->size, key, &value, &parser);
    if (err != OPT_OK) return err;

    return cbor_get_string(&value, buf, size, NULL);
}

static opt_error_t get_region_bytes_field(const opt_tag_t *tag, const opt_region_info_t *region,
                                          uint16_t key, uint8_t *buf, size_t size, size_t *actual) {
    if (!tag->initialized || !region->valid) {
        return OPT_ERR_NOT_INITIALIZED;
    }

    const uint8_t *region_data = tag->data + tag->payload_offset + region->offset;
    CborParser parser;
    CborValue value;

    opt_error_t err = cbor_find_field(region_data, region->size, key, &value, &parser);
    if (err != OPT_OK) return err;

    return cbor_get_bytes(&value, buf, size, actual);
}

/*============================================================================
 * Region Update Helpers
 *============================================================================*/

/* Structure to hold field updates */
typedef struct {
    uint16_t key;
    enum { FIELD_INT, FIELD_FLOAT, FIELD_STRING, FIELD_BYTES } type;
    union {
        int64_t int_val;
        float float_val;
        const char *str_val;
        struct { const uint8_t *data; size_t len; } bytes_val;
    };
} opt_field_update_t;

/* Update a region with new field value, preserving other fields */
static opt_error_t update_region_field(opt_tag_t *tag, opt_region_info_t *region,
                                       const opt_field_update_t *update) {
    if (!tag->initialized || !region->valid) {
        return OPT_ERR_NOT_INITIALIZED;
    }

    uint8_t *region_data = tag->data + tag->payload_offset + region->offset;
    uint8_t new_buf[512];
    size_t new_offset = 0;

    /* Start indefinite map */
    cbor_start_indef_map(new_buf, &new_offset);

    /* First, copy all existing fields except the one we're updating */
    CborParser parser;
    CborValue root;
    if (cbor_parser_init(region_data, region->size, 0, &parser, &root) == CborNoError &&
        cbor_value_is_map(&root)) {

        CborValue map_iter;
        if (cbor_value_enter_container(&root, &map_iter) == CborNoError) {
            while (!cbor_value_at_end(&map_iter)) {
                if (cbor_value_is_integer(&map_iter)) {
                    int64_t key;
                    cbor_value_get_int64(&map_iter, &key);

                    /* Skip the field we're updating */
                    if (key == update->key) {
                        cbor_value_advance(&map_iter);
                        cbor_value_advance(&map_iter);
                        continue;
                    }

                    /* Copy key */
                    cbor_encode_int_key(new_buf, sizeof(new_buf), &new_offset, (int)key);

                    /* Advance to value */
                    cbor_value_advance(&map_iter);

                    /* Copy value based on type */
                    if (cbor_value_is_integer(&map_iter)) {
                        int64_t val;
                        cbor_value_get_int64(&map_iter, &val);
                        cbor_encode_int_val(new_buf, sizeof(new_buf), &new_offset, val);
                    } else if (cbor_value_is_float(&map_iter) || cbor_value_is_double(&map_iter) ||
                               cbor_value_is_half_float(&map_iter)) {
                        float val;
                        cbor_get_float(&map_iter, &val);
                        cbor_encode_compact_float(new_buf, sizeof(new_buf), &new_offset, val);
                    } else if (cbor_value_is_text_string(&map_iter)) {
                        char str[256];
                        size_t len = sizeof(str);
                        cbor_value_copy_text_string(&map_iter, str, &len, NULL);
                        cbor_encode_string(new_buf, sizeof(new_buf), &new_offset, str);
                    } else if (cbor_value_is_byte_string(&map_iter)) {
                        uint8_t bytes[256];
                        size_t len = sizeof(bytes);
                        cbor_value_copy_byte_string(&map_iter, bytes, &len, NULL);
                        cbor_encode_bytes(new_buf, sizeof(new_buf), &new_offset, bytes, len);
                    } else if (cbor_value_is_array(&map_iter)) {
                        /* Copy array (for tags/certifications) */
                        CborValue arr_iter;
                        int count = 0;
                        int64_t vals[32];

                        cbor_value_enter_container(&map_iter, &arr_iter);
                        while (!cbor_value_at_end(&arr_iter) && count < 32) {
                            if (cbor_value_is_integer(&arr_iter)) {
                                cbor_value_get_int64(&arr_iter, &vals[count]);
                                count++;
                            }
                            cbor_value_advance(&arr_iter);
                        }

                        /* Encode indefinite array */
                        new_buf[new_offset++] = CBOR_INDEF_ARRAY;
                        for (int i = 0; i < count; i++) {
                            cbor_encode_int_val(new_buf, sizeof(new_buf), &new_offset, vals[i]);
                        }
                        new_buf[new_offset++] = CBOR_BREAK;
                    }

                    cbor_value_advance(&map_iter);
                } else {
                    cbor_value_advance(&map_iter);
                    cbor_value_advance(&map_iter);
                }
            }
        }
    }

    /* Add the new/updated field */
    opt_error_t err = cbor_encode_int_key(new_buf, sizeof(new_buf), &new_offset, update->key);
    if (err != OPT_OK) return err;

    switch (update->type) {
        case FIELD_INT:
            err = cbor_encode_int_val(new_buf, sizeof(new_buf), &new_offset, update->int_val);
            break;
        case FIELD_FLOAT:
            err = cbor_encode_compact_float(new_buf, sizeof(new_buf), &new_offset, update->float_val);
            break;
        case FIELD_STRING:
            err = cbor_encode_string(new_buf, sizeof(new_buf), &new_offset, update->str_val);
            break;
        case FIELD_BYTES:
            err = cbor_encode_bytes(new_buf, sizeof(new_buf), &new_offset,
                                   update->bytes_val.data, update->bytes_val.len);
            break;
    }
    if (err != OPT_OK) return err;

    /* End map */
    cbor_end_indef(new_buf, &new_offset);

    /* Check if it fits */
    if (new_offset > region->size) {
        return OPT_ERR_REGION_OVERFLOW;
    }

    /* Copy to region and zero rest */
    memcpy(region_data, new_buf, new_offset);
    memset(region_data + new_offset, 0, region->size - new_offset);

    /* Mark pages dirty */
    opt_mark_dirty(tag, tag->payload_offset + region->offset, region->size);

    return OPT_OK;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

opt_error_t opt_init(opt_tag_t *tag) {
    if (!tag) return OPT_ERR_INVALID_PARAM;

    memset(tag, 0, sizeof(opt_tag_t));
    return OPT_OK;
}

opt_error_t opt_format_empty_tag(opt_tag_t *tag, uint16_t size, uint16_t aux_size) {
    if (!tag || size < 64 || size > OPT_MAX_TAG_SIZE) {
        return OPT_ERR_INVALID_PARAM;
    }

    memset(tag, 0, sizeof(opt_tag_t));
    tag->data_size = size;

    /* Build NDEF structure */
    size_t offset = 0;

    /* Capability Container (4 bytes) */
    tag->data[offset++] = OPT_CC_MAGIC;  /* Magic */
    tag->data[offset++] = 0x40;          /* Version 1.0, read access */
    tag->data[offset++] = size / 8;      /* Data size / 8 */
    tag->data[offset++] = 0x01;          /* Read/write access */

    /* NDEF TLV */
    tag->data[offset++] = OPT_TLV_NDEF;

    /* Calculate payload size */
    uint16_t meta_size = 12;  /* Meta region: needs space for main_offset and aux_offset */

    if (aux_size > 0 && aux_size < 16) aux_size = 16;  /* Minimum aux size */

    /* Determine whether we need short or long NDEF record format.
     * We need to account for the NDEF header size which depends on payload length,
     * and the TLV length which depends on ndef_total. Solve iteratively. */

    /* First estimate with short record (SR=1): header = flags(1) + type_len(1) + payload_len(1) + type(28) = 31 */
    uint16_t ndef_header_size = 1 + 1 + 1 + OPT_MIME_TYPE_LEN;
    /* Estimate available space assuming 1-byte TLV length */
    uint16_t available = size - (uint16_t)offset - 1 - ndef_header_size - 1;  /* -1 TLV len, -1 terminator */
    uint16_t main_size = available - meta_size - aux_size;
    uint16_t payload_size = meta_size + main_size + aux_size;

    bool use_long_record = (payload_size > 255);
    uint16_t ndef_total;

    if (use_long_record) {
        /* Long record: flags(1) + type_len(1) + payload_len(4) + type(28) = 34 */
        ndef_header_size = 1 + 1 + 4 + OPT_MIME_TYPE_LEN;
    }

    /* Recalculate with correct header size, assuming 3-byte TLV as upper bound */
    available = size - (uint16_t)offset - 3 - ndef_header_size - 1;
    main_size = available - meta_size - aux_size;
    payload_size = meta_size + main_size + aux_size;
    ndef_total = ndef_header_size + payload_size;

    /* Now check if 1-byte TLV length suffices; if so, reclaim the 2 bytes */
    if (ndef_total <= 254) {
        available += 2;
        main_size = available - meta_size - aux_size;
        payload_size = meta_size + main_size + aux_size;
        ndef_total = ndef_header_size + payload_size;
    }

    /* Re-check long record with final payload */
    use_long_record = (payload_size > 255);
    if (use_long_record) {
        ndef_header_size = 1 + 1 + 4 + OPT_MIME_TYPE_LEN;
    } else {
        ndef_header_size = 1 + 1 + 1 + OPT_MIME_TYPE_LEN;
    }
    ndef_total = ndef_header_size + payload_size;

    /* TLV length */
    if (ndef_total <= 254) {
        tag->data[offset++] = (uint8_t)ndef_total;
    } else {
        tag->data[offset++] = 0xFF;
        tag->data[offset++] = (uint8_t)(ndef_total >> 8);
        tag->data[offset++] = (uint8_t)(ndef_total & 0xFF);
    }

    /* NDEF record header */
    if (use_long_record) {
        tag->data[offset++] = 0xC2;  /* MB=1, ME=1, CF=0, SR=0, IL=0, TNF=2 (media) */
        tag->data[offset++] = OPT_MIME_TYPE_LEN;
        tag->data[offset++] = (uint8_t)(payload_size >> 24);
        tag->data[offset++] = (uint8_t)(payload_size >> 16);
        tag->data[offset++] = (uint8_t)(payload_size >> 8);
        tag->data[offset++] = (uint8_t)(payload_size & 0xFF);
    } else {
        tag->data[offset++] = 0xD2;  /* MB=1, ME=1, CF=0, SR=1, IL=0, TNF=2 (media) */
        tag->data[offset++] = OPT_MIME_TYPE_LEN;
        tag->data[offset++] = (uint8_t)payload_size;
    }
    memcpy(tag->data + offset, OPT_MIME_TYPE, OPT_MIME_TYPE_LEN);
    offset += OPT_MIME_TYPE_LEN;

    /* Payload starts here */
    tag->payload_offset = offset;
    tag->payload_size = payload_size;

    /* Meta region */
    tag->meta.offset = 0;
    tag->meta.size = meta_size;
    tag->meta.valid = true;

    /* Main region */
    tag->main.offset = meta_size;
    tag->main.size = main_size;
    tag->main.valid = true;

    /* Aux region */
    if (aux_size > 0) {
        tag->aux.offset = meta_size + main_size;
        tag->aux.size = aux_size;
        tag->aux.valid = true;
    }

    /* Write meta region with region offsets */
    size_t meta_offset = 0;
    uint8_t *meta_data = tag->data + tag->payload_offset;
    cbor_start_indef_map(meta_data, &meta_offset);
    /* Always encode main_region_offset so parser knows where main starts */
    cbor_encode_int_key(meta_data, meta_size, &meta_offset, OPT_META_MAIN_OFFSET);
    cbor_encode_int_val(meta_data, meta_size, &meta_offset, tag->main.offset);
    if (aux_size > 0) {
        cbor_encode_int_key(meta_data, meta_size, &meta_offset, OPT_META_AUX_OFFSET);
        cbor_encode_int_val(meta_data, meta_size, &meta_offset, tag->aux.offset);
    }
    cbor_end_indef(meta_data, &meta_offset);

    /* Initialize main region with empty map */
    uint8_t *main_data = tag->data + tag->payload_offset + tag->main.offset;
    main_data[0] = CBOR_INDEF_MAP;
    main_data[1] = CBOR_BREAK;

    /* Initialize aux region with empty map */
    if (aux_size > 0) {
        uint8_t *aux_data = tag->data + tag->payload_offset + tag->aux.offset;
        aux_data[0] = CBOR_INDEF_MAP;
        aux_data[1] = CBOR_BREAK;
    }

    /* Move offset past payload */
    offset += payload_size;

    /* TLV Terminator */
    tag->data[offset++] = OPT_TLV_TERMINATOR;

    tag->initialized = true;

    return OPT_OK;
}

opt_error_t opt_read_from_nfc(opt_tag_t *tag, const opt_nfc_hal_t *hal,
                              uint8_t start_page, uint8_t num_pages) {
    if (!tag || !hal || !hal->read_page) {
        return OPT_ERR_INVALID_PARAM;
    }

    /* Read pages into tag->data */
    uint16_t byte_offset = start_page * OPT_PAGE_SIZE;

    for (uint8_t i = 0; i < num_pages; i++) {
        uint16_t data_offset = byte_offset + (i * OPT_PAGE_SIZE);
        if (data_offset + OPT_PAGE_SIZE > OPT_MAX_TAG_SIZE) break;

        opt_error_t err = hal->read_page(hal->user_ctx, start_page + i,
                                         tag->data + data_offset);
        if (err != OPT_OK) return err;
    }

    tag->data_size = byte_offset + (num_pages * OPT_PAGE_SIZE);
    if (tag->data_size > OPT_MAX_TAG_SIZE) {
        tag->data_size = OPT_MAX_TAG_SIZE;
    }

    return opt_parse_ndef(tag);
}

opt_error_t opt_write_to_nfc(opt_tag_t *tag, const opt_nfc_hal_t *hal) {
    if (!tag || !hal || !hal->write_page || !tag->initialized) {
        return OPT_ERR_INVALID_PARAM;
    }

    uint8_t num_pages = (tag->data_size + OPT_PAGE_SIZE - 1) / OPT_PAGE_SIZE;

    for (uint8_t i = 0; i < num_pages; i++) {
        uint8_t page = i;
        opt_error_t err = hal->write_page(hal->user_ctx, page, tag->data + (i * OPT_PAGE_SIZE));
        if (err != OPT_OK) return err;
    }

    opt_clear_dirty(tag);
    return OPT_OK;
}

opt_error_t opt_write_dirty_pages(opt_tag_t *tag, const opt_nfc_hal_t *hal) {
    if (!tag || !hal || !hal->write_page || !tag->initialized) {
        return OPT_ERR_INVALID_PARAM;
    }

    uint8_t num_pages = (tag->data_size + OPT_PAGE_SIZE - 1) / OPT_PAGE_SIZE;

    for (uint8_t i = 0; i < num_pages; i++) {
        uint8_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;

        if (tag->dirty_pages[byte_idx] & (1 << bit_idx)) {
            uint8_t page = i;
            opt_error_t err = hal->write_page(hal->user_ctx, page, tag->data + (i * OPT_PAGE_SIZE));
            if (err != OPT_OK) return err;
        }
    }

    opt_clear_dirty(tag);
    return OPT_OK;
}

opt_error_t opt_write_aux_region(opt_tag_t *tag, const opt_nfc_hal_t *hal) {
    if (!tag || !hal || !hal->write_page || !tag->initialized || !tag->aux.valid) {
        return OPT_ERR_INVALID_PARAM;
    }

    uint16_t aux_start = tag->payload_offset + tag->aux.offset;
    uint16_t aux_end = aux_start + tag->aux.size;

    uint8_t start_page = aux_start / OPT_PAGE_SIZE;
    uint8_t end_page = (aux_end + OPT_PAGE_SIZE - 1) / OPT_PAGE_SIZE;

    for (uint8_t i = start_page; i < end_page; i++) {
        uint8_t page = i;
        opt_error_t err = hal->write_page(hal->user_ctx, page, tag->data + (i * OPT_PAGE_SIZE));
        if (err != OPT_OK) return err;
    }

    return OPT_OK;
}

opt_error_t opt_parse_ndef(opt_tag_t *tag) {
    if (!tag || tag->data_size < 8) {
        return OPT_ERR_INVALID_PARAM;
    }

    /* Check capability container */
    if (tag->data[0] != OPT_CC_MAGIC) {
        return OPT_ERR_NDEF_PARSE;
    }

    /* Find NDEF TLV */
    size_t offset = 4;  /* Skip CC */

    while (offset < tag->data_size) {
        uint8_t tlv_type = tag->data[offset];
        offset++;

        if (tlv_type == OPT_TLV_TERMINATOR) {
            return OPT_ERR_NDEF_PARSE;  /* Didn't find NDEF */
        }

        if (tlv_type == 0x00) {
            continue;  /* Null TLV, skip */
        }

        uint16_t tlv_len = parse_tlv_length(tag->data, &offset);

        if (tlv_type == OPT_TLV_NDEF) {
            /* Found NDEF TLV, parse records */
            size_t ndef_end = offset + tlv_len;

            while (offset < ndef_end) {
                uint16_t payload_off, payload_len;
                bool is_opt;

                opt_error_t err = parse_ndef_record(tag->data, tag->data_size, &offset,
                                                    &payload_off, &payload_len, &is_opt);
                if (err != OPT_OK) return err;

                if (is_opt) {
                    /* Found OpenPrintTag record */
                    tag->payload_offset = payload_off;
                    tag->payload_size = payload_len;

                    /* Parse meta region to find other regions */
                    const uint8_t *payload = tag->data + payload_off;

                    /* Meta is at start of payload */
                    CborParser parser;
                    CborValue root;
                    if (cbor_parser_init(payload, payload_len, 0, &parser, &root) != CborNoError) {
                        return OPT_ERR_CBOR_DECODE;
                    }

                    /* Determine meta size as the byte-length of the first CBOR item.
                     * This avoids accidentally scanning into main/aux region data. */
                    CborValue after_meta = root;
                    if (cbor_value_advance(&after_meta) != CborNoError) {
                        return OPT_ERR_CBOR_DECODE;
                    }

                    size_t meta_size = after_meta.source.ptr - payload;
                    if (meta_size == 0 || meta_size > payload_len) {
                        return OPT_ERR_CBOR_DECODE;
                    }
                    tag->meta.offset = 0;
                    tag->meta.size = meta_size;
                    tag->meta.valid = true;

                    /* Parse meta fields */
                    CborValue value;
                    int64_t main_offset = meta_size;
                    int64_t main_size = 0;
                    int64_t aux_offset = 0;
                    int64_t aux_size = 0;

                    if (cbor_find_field(payload, meta_size, OPT_META_MAIN_OFFSET, &value, &parser) == OPT_OK) {
                        cbor_get_int(&value, &main_offset);
                    }
                    if (cbor_find_field(payload, meta_size, OPT_META_MAIN_SIZE, &value, &parser) == OPT_OK) {
                        cbor_get_int(&value, &main_size);
                    }
                    if (cbor_find_field(payload, meta_size, OPT_META_AUX_OFFSET, &value, &parser) == OPT_OK) {
                        cbor_get_int(&value, &aux_offset);
                    }
                    if (cbor_find_field(payload, meta_size, OPT_META_AUX_SIZE, &value, &parser) == OPT_OK) {
                        cbor_get_int(&value, &aux_size);
                    }

                    if (main_offset < 0 || main_offset >= (int64_t)payload_len) {
                        return OPT_ERR_CBOR_DECODE;
                    }
                    if (aux_offset < 0 || aux_offset > (int64_t)payload_len) {
                        return OPT_ERR_CBOR_DECODE;
                    }
                    if (aux_offset > 0 && aux_offset < main_offset) {
                        return OPT_ERR_CBOR_DECODE;
                    }

                    /* Set up main region */
                    tag->main.offset = (uint16_t)main_offset;
                    if (main_size > 0) {
                        tag->main.size = (uint16_t)main_size;
                    } else if (aux_offset > 0) {
                        tag->main.size = (uint16_t)(aux_offset - main_offset);
                    } else {
                        tag->main.size = payload_len - (uint16_t)main_offset;
                    }
                    tag->main.valid = true;

                    /* Set up aux region if present */
                    if (aux_offset > 0) {
                        tag->aux.offset = (uint16_t)aux_offset;
                        if (aux_size > 0) {
                            tag->aux.size = (uint16_t)aux_size;
                        } else {
                            tag->aux.size = payload_len - (uint16_t)aux_offset;
                        }
                        tag->aux.valid = true;
                    }

                    tag->initialized = true;
                    return OPT_OK;
                }
            }
            break;
        } else {
            /* Skip this TLV */
            offset += tlv_len;
        }
    }

    return OPT_ERR_NDEF_PARSE;
}

/*============================================================================
 * Field Getters
 *============================================================================*/

opt_error_t opt_get_material_class(const opt_tag_t *tag, uint8_t *value) {
    int64_t val;
    opt_error_t err = get_region_int_field(tag, &tag->main, OPT_MAIN_MATERIAL_CLASS, &val);
    if (err == OPT_OK) *value = (uint8_t)val;
    return err;
}

opt_error_t opt_get_material_type(const opt_tag_t *tag, uint8_t *value) {
    int64_t val;
    opt_error_t err = get_region_int_field(tag, &tag->main, OPT_MAIN_MATERIAL_TYPE, &val);
    if (err == OPT_OK) *value = (uint8_t)val;
    return err;
}

opt_error_t opt_get_material_name(const opt_tag_t *tag, char *buf, size_t size) {
    return get_region_string_field(tag, &tag->main, OPT_MAIN_MATERIAL_NAME, buf, size);
}

opt_error_t opt_get_brand_name(const opt_tag_t *tag, char *buf, size_t size) {
    return get_region_string_field(tag, &tag->main, OPT_MAIN_BRAND_NAME, buf, size);
}

opt_error_t opt_get_primary_color(const opt_tag_t *tag, uint8_t rgba[4]) {
    size_t actual = 0;
    rgba[3] = 0xFF;  /* Default alpha to opaque */
    opt_error_t err = get_region_bytes_field(tag, &tag->main, OPT_MAIN_PRIMARY_COLOR, rgba, 4, &actual);
    if (err == OPT_OK && actual == 3) {
        rgba[3] = 0xFF;  /* RGB only, set alpha to opaque */
    }
    return err;
}

opt_error_t opt_get_nominal_full_weight(const opt_tag_t *tag, float *grams) {
    return get_region_float_field(tag, &tag->main, OPT_MAIN_NOMINAL_FULL_WEIGHT, grams);
}

opt_error_t opt_get_actual_full_weight(const opt_tag_t *tag, float *grams) {
    opt_error_t err = get_region_float_field(tag, &tag->main, OPT_MAIN_ACTUAL_FULL_WEIGHT, grams);
    if (err == OPT_ERR_FIELD_NOT_FOUND) {
        /* Fall back to nominal */
        return opt_get_nominal_full_weight(tag, grams);
    }
    return err;
}

opt_error_t opt_get_actual_full_length(const opt_tag_t *tag, float *mm) {
    opt_error_t err = get_region_float_field(tag, &tag->main, OPT_MAIN_ACTUAL_FULL_LENGTH, mm);
    if (err == OPT_ERR_FIELD_NOT_FOUND) {
        /* Fall back to nominal */
        return get_region_float_field(tag, &tag->main, OPT_MAIN_NOMINAL_FULL_LENGTH, mm);
    }
    return err;
}

opt_error_t opt_get_nominal_full_length(const opt_tag_t *tag, float *mm) {
    return get_region_float_field(tag, &tag->main, OPT_MAIN_NOMINAL_FULL_LENGTH, mm);
}

opt_error_t opt_get_density(const opt_tag_t *tag, float *g_per_cm3) {
    return get_region_float_field(tag, &tag->main, OPT_MAIN_DENSITY, g_per_cm3);
}

opt_error_t opt_get_filament_diameter(const opt_tag_t *tag, float *mm) {
    opt_error_t err = get_region_float_field(tag, &tag->main, OPT_MAIN_FILAMENT_DIAMETER, mm);
    if (err == OPT_ERR_FIELD_NOT_FOUND) {
        *mm = OPT_DEFAULT_DIAMETER;
        return OPT_OK;
    }
    return err;
}

opt_error_t opt_get_empty_container_weight(const opt_tag_t *tag, float *grams) {
    return get_region_float_field(tag, &tag->main, OPT_MAIN_EMPTY_CONTAINER_WEIGHT, grams);
}

opt_error_t opt_get_min_print_temp(const opt_tag_t *tag, int16_t *celsius) {
    int64_t val;
    opt_error_t err = get_region_int_field(tag, &tag->main, OPT_MAIN_MIN_PRINT_TEMP, &val);
    if (err == OPT_OK) *celsius = (int16_t)val;
    return err;
}

opt_error_t opt_get_max_print_temp(const opt_tag_t *tag, int16_t *celsius) {
    int64_t val;
    opt_error_t err = get_region_int_field(tag, &tag->main, OPT_MAIN_MAX_PRINT_TEMP, &val);
    if (err == OPT_OK) *celsius = (int16_t)val;
    return err;
}

opt_error_t opt_get_preheat_temp(const opt_tag_t *tag, int16_t *celsius) {
    int64_t val;
    opt_error_t err = get_region_int_field(tag, &tag->main, OPT_MAIN_PREHEAT_TEMP, &val);
    if (err == OPT_OK) *celsius = (int16_t)val;
    return err;
}

opt_error_t opt_get_min_bed_temp(const opt_tag_t *tag, int16_t *celsius) {
    int64_t val;
    opt_error_t err = get_region_int_field(tag, &tag->main, OPT_MAIN_MIN_BED_TEMP, &val);
    if (err == OPT_OK) *celsius = (int16_t)val;
    return err;
}

opt_error_t opt_get_max_bed_temp(const opt_tag_t *tag, int16_t *celsius) {
    int64_t val;
    opt_error_t err = get_region_int_field(tag, &tag->main, OPT_MAIN_MAX_BED_TEMP, &val);
    if (err == OPT_OK) *celsius = (int16_t)val;
    return err;
}

opt_error_t opt_get_gtin(const opt_tag_t *tag, uint64_t *gtin) {
    int64_t val;
    opt_error_t err = get_region_int_field(tag, &tag->main, OPT_MAIN_GTIN, &val);
    if (err == OPT_OK) *gtin = (uint64_t)val;
    return err;
}

opt_error_t opt_get_manufactured_date(const opt_tag_t *tag, uint32_t *timestamp) {
    int64_t val;
    opt_error_t err = get_region_int_field(tag, &tag->main, OPT_MAIN_MANUFACTURED_DATE, &val);
    if (err == OPT_OK) *timestamp = (uint32_t)val;
    return err;
}

/*============================================================================
 * Field Setters
 *============================================================================*/

opt_error_t opt_set_material_class(opt_tag_t *tag, uint8_t value) {
    opt_field_update_t update = { .key = OPT_MAIN_MATERIAL_CLASS, .type = FIELD_INT, .int_val = value };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_material_type(opt_tag_t *tag, uint8_t value) {
    opt_field_update_t update = { .key = OPT_MAIN_MATERIAL_TYPE, .type = FIELD_INT, .int_val = value };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_material_name(opt_tag_t *tag, const char *name) {
    if (!name || strlen(name) > 31) return OPT_ERR_INVALID_PARAM;
    opt_field_update_t update = { .key = OPT_MAIN_MATERIAL_NAME, .type = FIELD_STRING, .str_val = name };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_brand_name(opt_tag_t *tag, const char *name) {
    if (!name || strlen(name) > 31) return OPT_ERR_INVALID_PARAM;
    opt_field_update_t update = { .key = OPT_MAIN_BRAND_NAME, .type = FIELD_STRING, .str_val = name };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_primary_color(opt_tag_t *tag, const uint8_t rgba[4]) {
    opt_field_update_t update = {
        .key = OPT_MAIN_PRIMARY_COLOR,
        .type = FIELD_BYTES,
        .bytes_val = { .data = rgba, .len = 4 }
    };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_nominal_full_weight(opt_tag_t *tag, float grams) {
    opt_field_update_t update = { .key = OPT_MAIN_NOMINAL_FULL_WEIGHT, .type = FIELD_FLOAT, .float_val = grams };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_actual_full_weight(opt_tag_t *tag, float grams) {
    opt_field_update_t update = { .key = OPT_MAIN_ACTUAL_FULL_WEIGHT, .type = FIELD_FLOAT, .float_val = grams };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_actual_full_length(opt_tag_t *tag, float mm) {
    opt_field_update_t update = { .key = OPT_MAIN_ACTUAL_FULL_LENGTH, .type = FIELD_FLOAT, .float_val = mm };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_density(opt_tag_t *tag, float g_per_cm3) {
    opt_field_update_t update = { .key = OPT_MAIN_DENSITY, .type = FIELD_FLOAT, .float_val = g_per_cm3 };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_filament_diameter(opt_tag_t *tag, float mm) {
    opt_field_update_t update = { .key = OPT_MAIN_FILAMENT_DIAMETER, .type = FIELD_FLOAT, .float_val = mm };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_min_print_temp(opt_tag_t *tag, int16_t celsius) {
    opt_field_update_t update = { .key = OPT_MAIN_MIN_PRINT_TEMP, .type = FIELD_INT, .int_val = celsius };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_max_print_temp(opt_tag_t *tag, int16_t celsius) {
    opt_field_update_t update = { .key = OPT_MAIN_MAX_PRINT_TEMP, .type = FIELD_INT, .int_val = celsius };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_preheat_temp(opt_tag_t *tag, int16_t celsius) {
    opt_field_update_t update = { .key = OPT_MAIN_PREHEAT_TEMP, .type = FIELD_INT, .int_val = celsius };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_min_bed_temp(opt_tag_t *tag, int16_t celsius) {
    opt_field_update_t update = { .key = OPT_MAIN_MIN_BED_TEMP, .type = FIELD_INT, .int_val = celsius };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_max_bed_temp(opt_tag_t *tag, int16_t celsius) {
    opt_field_update_t update = { .key = OPT_MAIN_MAX_BED_TEMP, .type = FIELD_INT, .int_val = celsius };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_gtin(opt_tag_t *tag, uint64_t gtin) {
    opt_field_update_t update = { .key = OPT_MAIN_GTIN, .type = FIELD_INT, .int_val = (int64_t)gtin };
    return update_region_field(tag, &tag->main, &update);
}

opt_error_t opt_set_manufactured_date(opt_tag_t *tag, uint32_t timestamp) {
    opt_field_update_t update = { .key = OPT_MAIN_MANUFACTURED_DATE, .type = FIELD_INT, .int_val = timestamp };
    return update_region_field(tag, &tag->main, &update);
}

/*============================================================================
 * Aux Region Accessors
 *============================================================================*/

opt_error_t opt_get_consumed_weight(const opt_tag_t *tag, float *grams) {
    if (!tag->aux.valid) {
        *grams = 0.0f;
        return OPT_OK;
    }
    opt_error_t err = get_region_float_field(tag, &tag->aux, OPT_AUX_CONSUMED_WEIGHT, grams);
    if (err == OPT_ERR_FIELD_NOT_FOUND) {
        *grams = 0.0f;
        return OPT_OK;
    }
    return err;
}

opt_error_t opt_set_consumed_weight(opt_tag_t *tag, float grams) {
    if (!tag->aux.valid) return OPT_ERR_INVALID_PARAM;
    opt_field_update_t update = { .key = OPT_AUX_CONSUMED_WEIGHT, .type = FIELD_FLOAT, .float_val = grams };
    return update_region_field(tag, &tag->aux, &update);
}

opt_error_t opt_add_consumed_weight(opt_tag_t *tag, float grams_delta) {
    float current = 0;
    opt_error_t err = opt_get_consumed_weight(tag, &current);
    if (err != OPT_OK) return err;

    float new_consumed = current + grams_delta;

    // Clamp consumed weight to not exceed total weight
    float total = 0;
    err = opt_get_actual_full_weight(tag, &total);
    if (err == OPT_OK && new_consumed > total) {
        new_consumed = total;
    }

    return opt_set_consumed_weight(tag, new_consumed);
}

opt_error_t opt_get_gp_spoolman_id(const opt_tag_t *tag, int32_t *id) {
    if (!tag || !id) return OPT_ERR_INVALID_PARAM;
    if (!tag->aux.valid) return OPT_ERR_FIELD_NOT_FOUND;

    /* Preferred path: gp_range_user == "openscan" */
    char user[16] = {0};
    opt_error_t err = get_region_string_field(tag, &tag->aux, OPT_AUX_GP_RANGE_USER, user, sizeof(user));
    if (err == OPT_OK && strcmp(user, OPT_GP_RANGE_USER_OPENSCAN) == 0) {
        int64_t val;
        err = get_region_int_field(tag, &tag->aux, OPT_AUX_GP_SPOOLMAN_ID, &val);
        if (err != OPT_OK) return err;
        *id = (int32_t)val;
        return OPT_OK;
    }

    /* Compatibility fallback: accept raw spoolman-id key without gp_range_user.
     * This keeps reads working for tags where aux space is too tight to add user key. */
    int64_t val;
    err = get_region_int_field(tag, &tag->aux, OPT_AUX_GP_SPOOLMAN_ID, &val);
    if (err != OPT_OK) return err;
    *id = (int32_t)val;
    return OPT_OK;
}

opt_error_t opt_set_gp_spoolman_id(opt_tag_t *tag, int32_t id) {
    if (!tag) return OPT_ERR_INVALID_PARAM;
    if (!tag->aux.valid) return OPT_ERR_INVALID_PARAM;

    /* Set the spoolman ID first (critical field). */
    opt_field_update_t id_update = { .key = OPT_AUX_GP_SPOOLMAN_ID, .type = FIELD_INT,
                                     .int_val = id };
    opt_error_t err = update_region_field(tag, &tag->aux, &id_update);
    if (err == OPT_ERR_REGION_OVERFLOW) {
        /* Tight aux fallback: write a minimal one-field map with spoolman ID. */
        uint8_t *aux_data = tag->data + tag->payload_offset + tag->aux.offset;
        size_t offset = 0;
        if (tag->aux.size < 2) return OPT_ERR_REGION_OVERFLOW;
        aux_data[offset++] = 0xA1;  /* definite map with one key/value pair */

        err = cbor_encode_int_key(aux_data, tag->aux.size, &offset, OPT_AUX_GP_SPOOLMAN_ID);
        if (err != OPT_OK) return err;
        err = cbor_encode_int_val(aux_data, tag->aux.size, &offset, id);
        if (err != OPT_OK) return err;

        if (offset > tag->aux.size) {
            return OPT_ERR_REGION_OVERFLOW;
        }
        memset(aux_data + offset, 0, tag->aux.size - offset);
        opt_mark_dirty(tag, tag->payload_offset + tag->aux.offset, tag->aux.size);
        return OPT_OK;
    }
    if (err != OPT_OK) return err;

    /* Best-effort namespace marker. If region is full, keep the ID we just wrote. */
    opt_field_update_t user_update = { .key = OPT_AUX_GP_RANGE_USER, .type = FIELD_STRING,
                                       .str_val = OPT_GP_RANGE_USER_OPENSCAN };
    err = update_region_field(tag, &tag->aux, &user_update);
    if (err == OPT_ERR_REGION_OVERFLOW) {
        return OPT_OK;
    }
    return err;
}

/*============================================================================
 * High-Level Functions
 *============================================================================*/

opt_error_t opt_get_filament_info(const opt_tag_t *tag, opt_filament_info_t *info) {
    if (!tag || !info || !tag->initialized) {
        return OPT_ERR_INVALID_PARAM;
    }

    memset(info, 0, sizeof(opt_filament_info_t));

    /* Material class (required) */
    opt_get_material_class(tag, &info->material_class);

    /* Material type */
    opt_get_material_type(tag, &info->material_type);

    /* Names */
    opt_get_material_name(tag, info->material_name, sizeof(info->material_name));
    opt_get_brand_name(tag, info->brand_name, sizeof(info->brand_name));

    /* Color */
    opt_get_primary_color(tag, info->primary_color);

    /* Weight and measurements */
    opt_get_actual_full_weight(tag, &info->total_weight_g);
    opt_get_density(tag, &info->density);
    opt_get_filament_diameter(tag, &info->diameter_mm);

    /* Consumed weight */
    info->has_aux = tag->aux.valid;
    if (info->has_aux) {
        opt_get_consumed_weight(tag, &info->consumed_weight_g);
    }

    /* Calculate remaining */
    info->remaining_weight_g = info->total_weight_g - info->consumed_weight_g;
    if (info->remaining_weight_g < 0) info->remaining_weight_g = 0;

    if (info->total_weight_g > 0) {
        info->remaining_percent = (info->remaining_weight_g / info->total_weight_g) * 100.0f;
    }

    if (info->density > 0 && info->diameter_mm > 0) {
        info->remaining_length_mm = opt_weight_to_length(info->remaining_weight_g,
                                                          info->density, info->diameter_mm);
    }

    return OPT_OK;
}

opt_error_t opt_get_remaining_weight(const opt_tag_t *tag, float *grams) {
    float total, consumed;
    opt_error_t err = opt_get_actual_full_weight(tag, &total);
    if (err != OPT_OK) return err;

    err = opt_get_consumed_weight(tag, &consumed);
    if (err != OPT_OK) return err;

    *grams = total - consumed;
    if (*grams < 0) *grams = 0;
    return OPT_OK;
}

opt_error_t opt_get_remaining_length(const opt_tag_t *tag, float *mm) {
    float weight, density, diameter;

    opt_error_t err = opt_get_remaining_weight(tag, &weight);
    if (err != OPT_OK) return err;

    err = opt_get_density(tag, &density);
    if (err != OPT_OK) return err;

    err = opt_get_filament_diameter(tag, &diameter);
    if (err != OPT_OK) return err;

    *mm = opt_weight_to_length(weight, density, diameter);
    return OPT_OK;
}

opt_error_t opt_get_remaining_percent(const opt_tag_t *tag, float *percent) {
    float total, remaining;

    opt_error_t err = opt_get_actual_full_weight(tag, &total);
    if (err != OPT_OK) return err;

    err = opt_get_remaining_weight(tag, &remaining);
    if (err != OPT_OK) return err;

    if (total <= 0) {
        *percent = 0;
        return OPT_OK;
    }

    *percent = (remaining / total) * 100.0f;
    return OPT_OK;
}

/*============================================================================
 * Disconnect Recovery Functions
 *============================================================================*/

opt_error_t opt_write_start(opt_tag_t *tag, opt_write_state_t *state) {
    if (!tag || !state || !tag->initialized) {
        return OPT_ERR_INVALID_PARAM;
    }

    state->start_page = 0;
    state->current_page = 0;
    state->end_page = (tag->data_size + OPT_PAGE_SIZE - 1) / OPT_PAGE_SIZE;
    state->completed = false;

    return OPT_OK;
}

opt_error_t opt_write_continue(opt_tag_t *tag, const opt_nfc_hal_t *hal,
                               opt_write_state_t *state) {
    if (!tag || !hal || !hal->write_page || !state) {
        return OPT_ERR_INVALID_PARAM;
    }

    if (state->completed) {
        return OPT_OK;
    }

    /* Check if tag is present */
    if (hal->is_present && !hal->is_present(hal->user_ctx)) {
        return OPT_ERR_NFC_DISCONNECTED;
    }

    /* Write one page */
    uint16_t data_offset = state->current_page * OPT_PAGE_SIZE;
    opt_error_t err = hal->write_page(hal->user_ctx, state->current_page,
                                      tag->data + data_offset);
    if (err != OPT_OK) {
        return err;
    }

    state->current_page++;
    if (state->current_page >= state->end_page) {
        state->completed = true;
    }

    return OPT_OK;
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

float opt_weight_to_length(float weight_g, float density, float diameter_mm) {
    /* L = W / (pi * (d/2)^2 * rho) */
    /* where W=weight(g), d=diameter(mm), rho=density(g/cm^3) */
    float radius_cm = diameter_mm / 20.0f;  /* mm to cm, then /2 */
    float area_cm2 = PI * radius_cm * radius_cm;
    float length_cm = weight_g / (area_cm2 * density);
    return length_cm * 10.0f;  /* cm to mm */
}

float opt_length_to_weight(float length_mm, float density, float diameter_mm) {
    /* W = L * pi * (d/2)^2 * rho */
    float radius_cm = diameter_mm / 20.0f;
    float area_cm2 = PI * radius_cm * radius_cm;
    float length_cm = length_mm / 10.0f;
    return length_cm * area_cm2 * density;
}

void opt_mark_dirty(opt_tag_t *tag, uint16_t offset, uint16_t len) {
    if (!tag) return;

    uint16_t start_page = offset / OPT_PAGE_SIZE;
    uint16_t end_page = (offset + len + OPT_PAGE_SIZE - 1) / OPT_PAGE_SIZE;

    for (uint16_t p = start_page; p < end_page && p < OPT_MAX_TAG_SIZE / OPT_PAGE_SIZE; p++) {
        uint8_t byte_idx = p / 8;
        uint8_t bit_idx = p % 8;
        tag->dirty_pages[byte_idx] |= (1 << bit_idx);
    }
}

void opt_clear_dirty(opt_tag_t *tag) {
    if (!tag) return;
    memset(tag->dirty_pages, 0, sizeof(tag->dirty_pages));
}

const char* opt_error_str(opt_error_t err) {
    switch (err) {
        case OPT_OK:                    return "OK";
        case OPT_ERR_INVALID_PARAM:     return "Invalid parameter";
        case OPT_ERR_BUFFER_TOO_SMALL:  return "Buffer too small";
        case OPT_ERR_CBOR_ENCODE:       return "CBOR encode error";
        case OPT_ERR_CBOR_DECODE:       return "CBOR decode error";
        case OPT_ERR_FIELD_NOT_FOUND:   return "Field not found";
        case OPT_ERR_NDEF_PARSE:        return "NDEF parse error";
        case OPT_ERR_NFC_READ:          return "NFC read error";
        case OPT_ERR_NFC_WRITE:         return "NFC write error";
        case OPT_ERR_NFC_DISCONNECTED:  return "NFC tag disconnected";
        case OPT_ERR_REGION_OVERFLOW:   return "Region overflow";
        case OPT_ERR_INVALID_TAG:       return "Invalid tag";
        case OPT_ERR_NOT_INITIALIZED:   return "Not initialized";
        default:                        return "Unknown error";
    }
}

const char* opt_material_type_str(opt_material_type_t type) {
    if (type < NUM_MATERIAL_TYPES) {
        return material_type_strings[type];
    }
    return "Unknown";
}
