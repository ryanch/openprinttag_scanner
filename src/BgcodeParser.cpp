#include "BgcodeParser.h"
#include <cstring>
#include <cstdlib>

#ifndef NATIVE_TEST
  #include <Arduino.h>
#else
  #include "platform/NativePlatform.h"
#endif

// Search a metadata block's key=value pairs for "filament used [g]=<value>"
static float searchMetadataForFilament(const uint8_t* data, size_t len) {
    const char* meta = (const char*)data;
    const char* end = meta + len;
    const char* needle = "filament used [g]=";
    const size_t needleLen = 18;

    const char* pos = meta;
    while (pos <= end - needleLen) {
        if (memcmp(pos, needle, needleLen) == 0) {
            const char* valStart = pos + needleLen;
            char valBuf[32];
            size_t i = 0;
            while (valStart + i < end && valStart[i] != '\n' && valStart[i] != '\0' && i < sizeof(valBuf) - 1) {
                valBuf[i] = valStart[i];
                i++;
            }
            valBuf[i] = '\0';
            return atof(valBuf);
        }
        while (pos < end && *pos != '\n') pos++;
        if (pos < end) pos++;
    }
    return 0.0f;
}

float parseBgcodeFilament(const uint8_t* data, size_t len) {
    // bgcode file header: magic("GCDE", 4) + version(4) + checksum_type(2) = 10 bytes
    if (len < 22 || memcmp(data, "GCDE", 4) != 0) {
        return 0.0f;
    }

    uint16_t checksumType;
    memcpy(&checksumType, data + 8, sizeof(uint16_t));
    size_t checksumSize = (checksumType == 1) ? 4 : 0;

    size_t offset = 10; // first block header

    while (offset + 12 <= len) {
        uint16_t blockType, compression;
        uint32_t uncompressedSize, compressedSize;

        memcpy(&blockType, data + offset, sizeof(uint16_t));
        memcpy(&compression, data + offset + 2, sizeof(uint16_t));
        memcpy(&uncompressedSize, data + offset + 4, sizeof(uint32_t));
        memcpy(&compressedSize, data + offset + 8, sizeof(uint32_t));

        // GCode blocks (type 1) have an extra raw_data_size uint32
        size_t headerSize = (blockType == 1) ? 16 : 12;
        size_t dataStart = offset + headerSize;
        size_t dataEnd = dataStart + compressedSize;

        // Search uncompressed metadata blocks (types 0,2,3,4) for filament key
        if (blockType != 1 && blockType != 5 && dataStart < len) {
            if (compression != 0) {
                Serial.printf("BgcodeParser: Skipping compressed metadata block type %u (compression=%u, size=%u)\n",
                         blockType, compression, uncompressedSize);
            } else {
                size_t searchLen = (dataEnd <= len) ? compressedSize : (len - dataStart);
                float result = searchMetadataForFilament(data + dataStart, searchLen);
                if (result > 0.0f) {
                    return result;
                }
            }
        }

        // If block data extends beyond our buffer, stop scanning
        if (dataEnd > len) {
            break;
        }

        offset = dataEnd + checksumSize;
    }

    return 0.0f;
}
