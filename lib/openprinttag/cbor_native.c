/**
 * Minimal CBOR Implementation for Native Testing
 *
 * This provides a subset of the tinycbor API needed by openprinttag_lib.c.
 * Used only for native (non-ESP32) testing builds.
 */

#include "cbor.h"
#include <string.h>

/* CBOR type constants */
#define CBOR_MAJOR_UINT     0
#define CBOR_MAJOR_NINT     1
#define CBOR_MAJOR_BYTES    2
#define CBOR_MAJOR_TEXT     3
#define CBOR_MAJOR_ARRAY    4
#define CBOR_MAJOR_MAP      5
#define CBOR_MAJOR_TAG      6
#define CBOR_MAJOR_SIMPLE   7

#define CBOR_BREAK          0xFF
#define CBOR_SIMPLE_FALSE   0xF4
#define CBOR_SIMPLE_TRUE    0xF5
#define CBOR_SIMPLE_NULL    0xF6
#define CBOR_HALF_FLOAT     0xF9
#define CBOR_SINGLE_FLOAT   0xFA
#define CBOR_DOUBLE_FLOAT   0xFB

/* Internal type flags */
#define VALUE_TYPE_UINT     0
#define VALUE_TYPE_NINT     1
#define VALUE_TYPE_BYTES    2
#define VALUE_TYPE_TEXT     3
#define VALUE_TYPE_ARRAY    4
#define VALUE_TYPE_MAP      5
#define VALUE_TYPE_TAG      6
#define VALUE_TYPE_FLOAT16  7
#define VALUE_TYPE_FLOAT32  8
#define VALUE_TYPE_FLOAT64  9
#define VALUE_TYPE_SIMPLE   10
#define VALUE_TYPE_BREAK    11
#define VALUE_TYPE_INVALID  255

/* Parse a single CBOR item header */
static CborError parse_header(const uint8_t **ptr, const uint8_t *end,
                               uint8_t *type, uint64_t *value, bool *indefinite) {
    if (*ptr >= end) return CborErrorUnexpectedEOF;

    uint8_t byte = **ptr;
    (*ptr)++;

    uint8_t major = (byte >> 5) & 0x07;
    uint8_t info = byte & 0x1F;

    *indefinite = false;

    if (info < 24) {
        *value = info;
    } else if (info == 24) {
        if (*ptr >= end) return CborErrorUnexpectedEOF;
        *value = **ptr;
        (*ptr)++;
    } else if (info == 25) {
        if (*ptr + 1 >= end) return CborErrorUnexpectedEOF;
        *value = ((uint64_t)(*ptr)[0] << 8) | (*ptr)[1];
        *ptr += 2;
    } else if (info == 26) {
        if (*ptr + 3 >= end) return CborErrorUnexpectedEOF;
        *value = ((uint64_t)(*ptr)[0] << 24) | ((uint64_t)(*ptr)[1] << 16) |
                 ((uint64_t)(*ptr)[2] << 8) | (*ptr)[3];
        *ptr += 4;
    } else if (info == 27) {
        if (*ptr + 7 >= end) return CborErrorUnexpectedEOF;
        *value = ((uint64_t)(*ptr)[0] << 56) | ((uint64_t)(*ptr)[1] << 48) |
                 ((uint64_t)(*ptr)[2] << 40) | ((uint64_t)(*ptr)[3] << 32) |
                 ((uint64_t)(*ptr)[4] << 24) | ((uint64_t)(*ptr)[5] << 16) |
                 ((uint64_t)(*ptr)[6] << 8) | (*ptr)[7];
        *ptr += 8;
    } else if (info == 31) {
        *indefinite = true;
        *value = 0;
    } else {
        return CborErrorIllegalNumber;
    }

    switch (major) {
        case 0: *type = VALUE_TYPE_UINT; break;
        case 1: *type = VALUE_TYPE_NINT; break;
        case 2: *type = VALUE_TYPE_BYTES; break;
        case 3: *type = VALUE_TYPE_TEXT; break;
        case 4: *type = VALUE_TYPE_ARRAY; break;
        case 5: *type = VALUE_TYPE_MAP; break;
        case 6: *type = VALUE_TYPE_TAG; break;
        case 7:
            if (byte == CBOR_BREAK) {
                *type = VALUE_TYPE_BREAK;
            } else if (info == 25) {
                *type = VALUE_TYPE_FLOAT16;
            } else if (info == 26) {
                *type = VALUE_TYPE_FLOAT32;
            } else if (info == 27) {
                *type = VALUE_TYPE_FLOAT64;
            } else {
                *type = VALUE_TYPE_SIMPLE;
            }
            break;
    }

    return CborNoError;
}

/* Skip over a CBOR item */
static CborError skip_item(const uint8_t **ptr, const uint8_t *end) {
    uint8_t type;
    uint64_t value;
    bool indefinite;

    CborError err = parse_header(ptr, end, &type, &value, &indefinite);
    if (err != CborNoError) return err;

    switch (type) {
        case VALUE_TYPE_UINT:
        case VALUE_TYPE_NINT:
        case VALUE_TYPE_SIMPLE:
        case VALUE_TYPE_BREAK:
            return CborNoError;

        case VALUE_TYPE_FLOAT16:
        case VALUE_TYPE_FLOAT32:
        case VALUE_TYPE_FLOAT64:
            return CborNoError;

        case VALUE_TYPE_BYTES:
        case VALUE_TYPE_TEXT:
            if (indefinite) {
                while (1) {
                    if (*ptr >= end) return CborErrorUnexpectedEOF;
                    if (**ptr == CBOR_BREAK) {
                        (*ptr)++;
                        return CborNoError;
                    }
                    err = skip_item(ptr, end);
                    if (err != CborNoError) return err;
                }
            } else {
                if (*ptr + value > end) return CborErrorUnexpectedEOF;
                *ptr += value;
            }
            return CborNoError;

        case VALUE_TYPE_ARRAY:
            if (indefinite) {
                while (1) {
                    if (*ptr >= end) return CborErrorUnexpectedEOF;
                    if (**ptr == CBOR_BREAK) {
                        (*ptr)++;
                        return CborNoError;
                    }
                    err = skip_item(ptr, end);
                    if (err != CborNoError) return err;
                }
            } else {
                for (uint64_t i = 0; i < value; i++) {
                    err = skip_item(ptr, end);
                    if (err != CborNoError) return err;
                }
            }
            return CborNoError;

        case VALUE_TYPE_MAP:
            if (indefinite) {
                while (1) {
                    if (*ptr >= end) return CborErrorUnexpectedEOF;
                    if (**ptr == CBOR_BREAK) {
                        (*ptr)++;
                        return CborNoError;
                    }
                    err = skip_item(ptr, end);  /* key */
                    if (err != CborNoError) return err;
                    err = skip_item(ptr, end);  /* value */
                    if (err != CborNoError) return err;
                }
            } else {
                for (uint64_t i = 0; i < value; i++) {
                    err = skip_item(ptr, end);  /* key */
                    if (err != CborNoError) return err;
                    err = skip_item(ptr, end);  /* value */
                    if (err != CborNoError) return err;
                }
            }
            return CborNoError;

        case VALUE_TYPE_TAG:
            return skip_item(ptr, end);

        default:
            return CborErrorIllegalType;
    }
}

/* Initialize parser at current position */
static CborError init_value(CborValue *value) {
    if (value->source.ptr >= value->source.end) {
        value->type = VALUE_TYPE_INVALID;
        return CborNoError;
    }

    const uint8_t *ptr = value->source.ptr;
    uint8_t type;
    uint64_t val;
    bool indefinite;

    CborError err = parse_header(&ptr, value->source.end, &type, &val, &indefinite);
    if (err != CborNoError) return err;

    value->type = type;
    value->extra = indefinite ? 1 : 0;
    value->remaining = (uint32_t)val;

    return CborNoError;
}

CborError cbor_parser_init(const uint8_t *buffer, size_t size, uint32_t flags,
                           CborParser *parser, CborValue *value) {
    parser->end = buffer + size;
    parser->flags = flags;

    value->parser = parser;
    value->source.ptr = buffer;
    value->source.end = buffer + size;
    value->remaining = 0;
    value->extra = 0;
    value->flags = 0;

    return init_value(value);
}

bool cbor_value_at_end(const CborValue *value) {
    return value->source.ptr >= value->source.end ||
           value->type == VALUE_TYPE_BREAK ||
           value->type == VALUE_TYPE_INVALID;
}

bool cbor_value_is_map(const CborValue *value) {
    return value->type == VALUE_TYPE_MAP;
}

bool cbor_value_is_array(const CborValue *value) {
    return value->type == VALUE_TYPE_ARRAY;
}

bool cbor_value_is_integer(const CborValue *value) {
    return value->type == VALUE_TYPE_UINT || value->type == VALUE_TYPE_NINT;
}

bool cbor_value_is_float(const CborValue *value) {
    return value->type == VALUE_TYPE_FLOAT32;
}

bool cbor_value_is_double(const CborValue *value) {
    return value->type == VALUE_TYPE_FLOAT64;
}

bool cbor_value_is_half_float(const CborValue *value) {
    return value->type == VALUE_TYPE_FLOAT16;
}

bool cbor_value_is_text_string(const CborValue *value) {
    return value->type == VALUE_TYPE_TEXT;
}

bool cbor_value_is_byte_string(const CborValue *value) {
    return value->type == VALUE_TYPE_BYTES;
}

CborError cbor_value_enter_container(const CborValue *value, CborValue *element) {
    if (value->type != VALUE_TYPE_MAP && value->type != VALUE_TYPE_ARRAY) {
        return CborErrorIllegalType;
    }

    /* Find start of container contents */
    const uint8_t *ptr = value->source.ptr;
    uint8_t type;
    uint64_t val;
    bool indefinite;

    CborError err = parse_header(&ptr, value->source.end, &type, &val, &indefinite);
    if (err != CborNoError) return err;

    element->parser = value->parser;
    element->source.ptr = ptr;
    element->source.end = value->source.end;
    element->remaining = indefinite ? UINT32_MAX : (uint32_t)val;
    element->extra = indefinite ? 1 : 0;
    element->flags = value->type;  /* Remember parent type for map vs array */

    return init_value(element);
}

CborError cbor_value_leave_container(CborValue *value, const CborValue *element) {
    value->source.ptr = element->source.ptr;
    return init_value(value);
}

CborError cbor_value_advance(CborValue *value) {
    const uint8_t *ptr = value->source.ptr;

    CborError err = skip_item(&ptr, value->source.end);
    if (err != CborNoError) return err;

    value->source.ptr = ptr;

    if (value->remaining != UINT32_MAX && value->remaining > 0) {
        value->remaining--;
    }

    return init_value(value);
}

CborError cbor_value_get_int64(const CborValue *value, int64_t *result) {
    if (value->type != VALUE_TYPE_UINT && value->type != VALUE_TYPE_NINT) {
        return CborErrorIllegalType;
    }

    const uint8_t *ptr = value->source.ptr;
    uint8_t type;
    uint64_t val;
    bool indefinite;

    CborError err = parse_header(&ptr, value->source.end, &type, &val, &indefinite);
    if (err != CborNoError) return err;

    if (type == VALUE_TYPE_NINT) {
        *result = -1 - (int64_t)val;
    } else {
        *result = (int64_t)val;
    }

    return CborNoError;
}

CborError cbor_value_get_float(const CborValue *value, float *result) {
    if (value->type != VALUE_TYPE_FLOAT32) {
        return CborErrorIllegalType;
    }

    const uint8_t *ptr = value->source.ptr + 1;  /* Skip type byte */

    union {
        uint32_t u;
        float f;
    } u;

    u.u = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) |
          ((uint32_t)ptr[2] << 8) | ptr[3];
    *result = u.f;

    return CborNoError;
}

CborError cbor_value_get_double(const CborValue *value, double *result) {
    if (value->type != VALUE_TYPE_FLOAT64) {
        return CborErrorIllegalType;
    }

    const uint8_t *ptr = value->source.ptr + 1;  /* Skip type byte */

    union {
        uint64_t u;
        double d;
    } u;

    u.u = ((uint64_t)ptr[0] << 56) | ((uint64_t)ptr[1] << 48) |
          ((uint64_t)ptr[2] << 40) | ((uint64_t)ptr[3] << 32) |
          ((uint64_t)ptr[4] << 24) | ((uint64_t)ptr[5] << 16) |
          ((uint64_t)ptr[6] << 8) | ptr[7];
    *result = u.d;

    return CborNoError;
}

CborError cbor_value_get_half_float(const CborValue *value, uint16_t *result) {
    if (value->type != VALUE_TYPE_FLOAT16) {
        return CborErrorIllegalType;
    }

    const uint8_t *ptr = value->source.ptr + 1;  /* Skip type byte */
    *result = ((uint16_t)ptr[0] << 8) | ptr[1];

    return CborNoError;
}

CborError cbor_value_copy_text_string(const CborValue *value, char *buffer,
                                       size_t *buflen, CborValue *next) {
    if (value->type != VALUE_TYPE_TEXT) {
        return CborErrorIllegalType;
    }

    const uint8_t *ptr = value->source.ptr;
    uint8_t type;
    uint64_t len;
    bool indefinite;

    CborError err = parse_header(&ptr, value->source.end, &type, &len, &indefinite);
    if (err != CborNoError) return err;

    if (indefinite) {
        return CborErrorUnknownLength;
    }

    size_t copy_len = (len < *buflen) ? (size_t)len : *buflen - 1;
    memcpy(buffer, ptr, copy_len);
    buffer[copy_len] = '\0';

    if (len >= *buflen) {
        *buflen = copy_len;
        return CborErrorOutOfMemory;
    }

    *buflen = (size_t)len;

    if (next) {
        next->parser = value->parser;
        next->source.ptr = ptr + len;
        next->source.end = value->source.end;
        init_value(next);
    }

    return CborNoError;
}

CborError cbor_value_copy_byte_string(const CborValue *value, uint8_t *buffer,
                                       size_t *buflen, CborValue *next) {
    if (value->type != VALUE_TYPE_BYTES) {
        return CborErrorIllegalType;
    }

    const uint8_t *ptr = value->source.ptr;
    uint8_t type;
    uint64_t len;
    bool indefinite;

    CborError err = parse_header(&ptr, value->source.end, &type, &len, &indefinite);
    if (err != CborNoError) return err;

    if (indefinite) {
        return CborErrorUnknownLength;
    }

    size_t copy_len = (len < *buflen) ? (size_t)len : *buflen;
    memcpy(buffer, ptr, copy_len);

    if (len > *buflen) {
        *buflen = copy_len;
        return CborErrorOutOfMemory;
    }

    *buflen = (size_t)len;

    if (next) {
        next->parser = value->parser;
        next->source.ptr = ptr + len;
        next->source.end = value->source.end;
        init_value(next);
    }

    return CborNoError;
}
