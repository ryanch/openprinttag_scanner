#ifndef NFC_TYPES_H
#define NFC_TYPES_H

#include <cstdint>
#include <ctime>
#include "NFCWriteTypes.h"
#include "openprinttag_lib.h"

struct CurrentSpoolState {
    bool present;
    bool blank_tag_present;
    char spool_id[17];
    uint8_t uid[8];              // ISO15693 uses 8-byte UID
    uint8_t uid_length;
    opt_tag_t tag_data;          // Cached openprinttag data
    bool tag_data_valid;
};

// Recent spool entry for history tracking (RAM only)
struct RecentSpoolEntry {
    char spool_id[17];
    uint8_t material_type;
    uint8_t color[4];            // RGBA
    char manufacturer[33];
    int grams_remaining;
    time_t last_seen;  // Unix timestamp (seconds)
    bool valid;
    bool synced_to_spoolman;
    int32_t spoolman_id;
};

#endif // NFC_TYPES_H
