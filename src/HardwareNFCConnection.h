#ifndef HARDWARE_NFC_CONNECTION_H
#define HARDWARE_NFC_CONNECTION_H

#include "NFCConnectionI.h"
#include <PN5180.h>
#include <PN5180ISO15693.h>

// Production NFC connection using PN5180 hardware
class HardwareNFCConnection : public NFCConnectionI {
public:
    HardwareNFCConnection();
    ~HardwareNFCConnection() override;

    bool begin() override;
    void reset() override;
    bool setupRF() override;
    bool detectTag(uint8_t* uid, uint8_t* uidLength) override;
    void setCurrentUid(const uint8_t* uid, uint8_t length) override;
    opt_nfc_hal_t* getHal() override;

private:
    PN5180ISO15693* nfc_ = nullptr;
    opt_nfc_hal_t hal_;
    uint8_t currentUid_[8];

    // PN5180 control pins (uses hardware SPI: GPIO 18=SCK, 19=MISO, 23=MOSI)
    static constexpr int PN5180_NSS  = 5;   // SPI chip select
    static constexpr int PN5180_BUSY = 16;  // Busy signal
    static constexpr int PN5180_RST  = 17;  // Reset

    // Static HAL callbacks
    static opt_error_t halReadPage(void* ctx, uint8_t page, uint8_t* buffer);
    static opt_error_t halWritePage(void* ctx, uint8_t page, const uint8_t* data);
};

#endif // HARDWARE_NFC_CONNECTION_H
