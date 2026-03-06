#ifndef CONVERSION_UTILS_H
#define CONVERSION_UTILS_H

#include <cstdint>

/**
 * Shared data format conversion utilities
 * Used by BluetoothManager, NFCManager, HomeAssistantManager, SpoolmanManager
 */

/**
 * Convert material type string to enum
 * @param type Material type string (e.g., "PLA", "PETG", "ABS")
 * @return Material type enum from openprinttag_lib (e.g., OPT_MATERIAL_TYPE_PLA)
 *         Returns OPT_MATERIAL_TYPE_PLA as default if not recognized
 */
uint8_t materialTypeFromString(const char* type);

/**
 * Convert material type enum to string
 * @param type Material type enum from openprinttag_lib
 * @return Material type string (e.g., "PLA", "PETG", "ABS")
 *         Returns "PLA" as default if not recognized
 */
const char* materialTypeToString(uint8_t type);

/**
 * Parse hex color string to RGBA bytes
 * @param hex Color string in format "#RRGGBB" (7 characters)
 * @param rgba Output array of 4 bytes [R, G, B, A] (alpha set to 255)
 * @return true if parsing succeeded, false if invalid format
 */
bool parseHexColor(const char* hex, uint8_t* rgba);

/**
 * Get default density for a material type
 * @param material_type Material type enum from openprinttag_lib
 * @return Density in g/cm³
 *         PLA: 1.24, PETG: 1.27, ABS: 1.04, default: 1.20
 */
float getDefaultDensity(uint8_t material_type);

#endif // CONVERSION_UTILS_H
