#ifndef BGCODE_PARSER_H
#define BGCODE_PARSER_H

#include <cstdint>
#include <cstddef>

// Parse a bgcode file header to extract "filament used [g]" from the FILE_METADATA block.
// Returns the filament weight in grams, or 0.0f if not found.
// data/len should be the first ~4KB of the bgcode file.
float parseBgcodeFilament(const uint8_t* data, size_t len);

#endif // BGCODE_PARSER_H
