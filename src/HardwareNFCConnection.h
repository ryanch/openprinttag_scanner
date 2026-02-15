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

    // Diagnostics: log RF_STATUS, IRQ_STATUS, SYSTEM_STATUS registers
    void logDiagnostics();

private:
    PN5180ISO15693* nfc_ = nullptr;
    opt_nfc_hal_t hal_;
    uint8_t currentUid_[8];

    // PN5180 SPI pins (right side of ESP32, top to bottom)
    static constexpr int PN5180_RST   = 13;  // Hardware reset (active low)
    static constexpr int PN5180_NSS   = 14;  // SPI chip select (active low)
    static constexpr int PN5180_MOSI  = 27;  // SPI master-out slave-in
    static constexpr int PN5180_MISO  = 26;  // SPI master-in slave-out
    static constexpr int PN5180_SCK   = 25;  // SPI clock
    static constexpr int PN5180_BUSY  = 33;  // Busy signal (input)

    // PN5180 additional pins (active in future software updates)
    static constexpr int PN5180_GPIO  = 32;  // General purpose I/O (card detection)
    static constexpr int PN5180_IRQ   = 35;  // Interrupt request (active HIGH, input-only pin)
    static constexpr int PN5180_AUX   = 34;  // Auxiliary monitoring (input-only pin)
    // REQ (DWL_REQ): Not connected. Only needed for PN5180 firmware updates.

    // Static HAL callbacks
    static opt_error_t halReadPage(void* ctx, uint8_t page, uint8_t* buffer);
    static opt_error_t halWritePage(void* ctx, uint8_t page, const uint8_t* data);
};

#endif // HARDWARE_NFC_CONNECTION_H
