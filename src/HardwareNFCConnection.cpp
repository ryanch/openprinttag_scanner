#include "HardwareNFCConnection.h"
#include <Arduino.h>
#include <cstring>

HardwareNFCConnection::HardwareNFCConnection() {
    memset(currentUid_, 0, sizeof(currentUid_));
    memset(&hal_, 0, sizeof(hal_));
}

HardwareNFCConnection::~HardwareNFCConnection() {
    delete nfc_;
}

opt_error_t HardwareNFCConnection::halReadPage(void* ctx, uint8_t page, uint8_t* buffer) {
    HardwareNFCConnection* self = static_cast<HardwareNFCConnection*>(ctx);
    ISO15693ErrorCode err = self->nfc_->readSingleBlock(self->currentUid_, page, buffer, 4);
    return (err == ISO15693_EC_OK) ? OPT_OK : OPT_ERR_NFC_READ;
}

opt_error_t HardwareNFCConnection::halWritePage(void* ctx, uint8_t page, const uint8_t* data) {
    HardwareNFCConnection* self = static_cast<HardwareNFCConnection*>(ctx);
    ISO15693ErrorCode err = self->nfc_->writeSingleBlock(self->currentUid_, page, const_cast<uint8_t*>(data), 4);
    return (err == ISO15693_EC_OK) ? OPT_OK : OPT_ERR_NFC_WRITE;
}

bool HardwareNFCConnection::begin() {
    // Configure additional input pins for future use
    pinMode(PN5180_IRQ, INPUT);    // Interrupt (active HIGH)
    pinMode(PN5180_GPIO, INPUT);   // Card detection
    pinMode(PN5180_AUX, INPUT);    // Auxiliary/monitoring

    nfc_ = new PN5180ISO15693(PN5180_NSS, PN5180_BUSY, PN5180_RST,
                               PN5180_SCK, PN5180_MISO, PN5180_MOSI);

    Serial.println("HardwareNFCConnection: Starting PN5180...");
    nfc_->begin();
    nfc_->reset();

    // Read firmware version
    uint8_t firmwareVersion[2];
    nfc_->readEEprom(PN5180_FIRMWARE_VERSION, firmwareVersion, 2);
    Serial.printf("HardwareNFCConnection: PN5180 firmware: %d.%d\n",
                  firmwareVersion[1], firmwareVersion[0]);

    // Setup RF for ISO15693
    if (!nfc_->setupRF()) {
        Serial.println("HardwareNFCConnection: Failed to setup RF");
        return false;
    }

    // Set up HAL for openprinttag
    hal_.read_page = halReadPage;
    hal_.write_page = halWritePage;
    hal_.is_present = nullptr;
    hal_.user_ctx = this;

    Serial.println("HardwareNFCConnection: Initialized successfully");
    return true;
}

void HardwareNFCConnection::reset() {
    if (nfc_) {
        nfc_->reset();
    }
}

bool HardwareNFCConnection::setupRF() {
    if (nfc_) {
        return nfc_->setupRF();
    }
    return false;
}

bool HardwareNFCConnection::detectTag(uint8_t* uid, uint8_t* uidLength) {
    if (!nfc_) {
        return false;
    }

    ISO15693ErrorCode err = nfc_->getInventory(uid);
    if (err == ISO15693_EC_OK) {
        *uidLength = 8;  // ISO15693 uses 8-byte UID
        return true;
    }
    return false;
}

void HardwareNFCConnection::setCurrentUid(const uint8_t* uid, uint8_t length) {
    memcpy(currentUid_, uid, length < 8 ? length : 8);
}

opt_nfc_hal_t* HardwareNFCConnection::getHal() {
    return &hal_;
}
