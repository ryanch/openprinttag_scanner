#ifndef NFC_CONNECTION_I_H
#define NFC_CONNECTION_I_H

#include <cstdint>
#include "openprinttag_lib.h"

// Interface for NFC hardware abstraction
// Allows injection of test stubs without #ifdef guards
class NFCConnectionI {
public:
    virtual ~NFCConnectionI() = default;

    // Hardware initialization
    virtual bool begin() = 0;
    virtual void reset() = 0;
    virtual bool setupRF() = 0;

    // Full hardware reset + RF re-init for recovery from bad chip state
    virtual bool hardwareReset() = 0;

    // Tag detection - returns true if tag found, populates uid/uidLength
    virtual bool detectTag(uint8_t* uid, uint8_t* uidLength) = 0;

    // Set current UID for addressed read/write commands
    virtual void setCurrentUid(const uint8_t* uid, uint8_t length) = 0;

    // Get HAL for openprinttag library operations
    virtual opt_nfc_hal_t* getHal() = 0;
};

#endif // NFC_CONNECTION_I_H
