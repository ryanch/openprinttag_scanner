#ifndef NFC_WRITE_TYPES_H
#define NFC_WRITE_TYPES_H

#include <cstdint>

// Write request types - shared between production and test code
// These have no external dependencies

enum class NFCWriteType : uint8_t {
    REMOVE_WEIGHT,        // Subtract grams from spool
    CHANGE_COLOR,         // Set new primary color
    CHANGE_FILAMENT_TYPE, // Set new material type
    SET_CONSUMED_WEIGHT,  // Set absolute consumed weight
    SET_BRAND_NAME,       // Set manufacturer name
    FORMAT_NEW            // Format a blank tag with defaults
};

struct NFCWriteRequest {
    uint32_t request_id;         // Unique ID for deduplication
    NFCWriteType type;
    char expected_spool_id[64];  // Only write if this spool is present (empty = any)
    union {
        float grams_to_remove;
        uint8_t new_color[4];    // RGBA
        uint8_t new_material_type;
        float consumed_weight;   // Absolute consumed weight in grams
        char brand_name[64];     // Manufacturer name
    } data;
};

#endif // NFC_WRITE_TYPES_H
