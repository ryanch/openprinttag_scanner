/**
 * CBOR Interface Header
 *
 * This header provides the CBOR interface used by openprinttag_lib.c.
 * On ESP-IDF, this is provided by the espressif/cbor component (tinycbor).
 * For native testing, a compatible implementation must be provided.
 */

#ifndef CBOR_H
#define CBOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CBOR error codes */
typedef enum CborError {
    CborNoError = 0,
    CborErrorUnknownLength = 1,
    CborErrorAdvancePastEOF = 2,
    CborErrorIO = 3,
    CborErrorOutOfMemory = 4,
    CborErrorIllegalType = 5,
    CborErrorInvalidType = 6,
    CborErrorUnexpectedEOF = 7,
    CborErrorUnexpectedBreak = 8,
    CborErrorIllegalNumber = 9,
    CborErrorIllegalSimpleType = 10,
} CborError;

/* Forward declarations */
struct CborEncoder;
struct CborParser;
struct CborValue;

typedef struct CborEncoder CborEncoder;
typedef struct CborParser CborParser;

/* Source tracking for parser */
typedef struct {
    const uint8_t *ptr;
    const uint8_t *end;
} CborSource;

/* CBOR Value structure */
typedef struct CborValue {
    CborSource source;
    const CborParser *parser;
    uint32_t remaining;
    uint16_t extra;
    uint8_t type;
    uint8_t flags;
} CborValue;

/* CBOR Parser structure */
struct CborParser {
    const uint8_t *end;
    uint32_t flags;
};

/* Parser initialization */
CborError cbor_parser_init(const uint8_t *buffer, size_t size, uint32_t flags,
                           CborParser *parser, CborValue *value);

/* Container operations */
CborError cbor_value_enter_container(const CborValue *value, CborValue *element);
CborError cbor_value_leave_container(CborValue *value, const CborValue *element);
CborError cbor_value_advance(CborValue *value);

/* Type checking */
bool cbor_value_at_end(const CborValue *value);
bool cbor_value_is_map(const CborValue *value);
bool cbor_value_is_array(const CborValue *value);
bool cbor_value_is_integer(const CborValue *value);
bool cbor_value_is_float(const CborValue *value);
bool cbor_value_is_double(const CborValue *value);
bool cbor_value_is_half_float(const CborValue *value);
bool cbor_value_is_text_string(const CborValue *value);
bool cbor_value_is_byte_string(const CborValue *value);

/* Value getters */
CborError cbor_value_get_int64(const CborValue *value, int64_t *result);
CborError cbor_value_get_float(const CborValue *value, float *result);
CborError cbor_value_get_double(const CborValue *value, double *result);
CborError cbor_value_get_half_float(const CborValue *value, uint16_t *result);
CborError cbor_value_copy_text_string(const CborValue *value, char *buffer,
                                       size_t *buflen, CborValue *next);
CborError cbor_value_copy_byte_string(const CborValue *value, uint8_t *buffer,
                                       size_t *buflen, CborValue *next);

#ifdef __cplusplus
}
#endif

#endif /* CBOR_H */
