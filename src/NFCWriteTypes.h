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
    FORMAT_NEW,           // Format a blank tag with defaults
    WRITE_SPOOLMAN_ID,    // Write Spoolman spool ID to aux region
    WRITE_RAW_TAG,        // Write raw binary data to entire tag
    SET_INITIAL_WEIGHT    // Set initial/full weight of spool
};

struct NFCWriteRequest {
    uint32_t request_id;         // Unique ID for deduplication
    NFCWriteType type;
    uint8_t suppress_sync;       // If 1, don't trigger Spoolman sync (used for batched writes like Mode B)
    char expected_spool_id[17];  // Only write if this spool is present (empty = any)
    union {
        float grams_to_remove;
        uint8_t new_color[4];    // RGBA
        uint8_t new_material_type;
        float consumed_weight;   // Absolute consumed weight in grams
        char brand_name[33];     // Manufacturer name
        int32_t spoolman_id;     // Spoolman spool ID for tag write-back
    } data;
};

#endif // NFC_WRITE_TYPES_H
