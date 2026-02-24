#include "DebugLogBuffer.h"
#include "HardwareNFCConnection.h"
#include <Arduino.h>
#include <cstring>

//#define ENABLE_NFC_DEBUG_LOGS

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

    DBG_LOGLN("HardwareNFCConnection: Starting PN5180...");
    nfc_->begin();
    DBG_LOGLN("HardwareNFCConnection: SPI begin done, resetting...");
    DBG_LOGF("HardwareNFCConnection: BUSY pin=%d before reset\n", digitalRead(PN5180_BUSY));

    // Manual reset with debug (PN5180::reset() has no timeout)
    digitalWrite(PN5180_RST, LOW);
    delay(10);
    digitalWrite(PN5180_RST, HIGH);
    DBG_LOGLN("HardwareNFCConnection: RST pin released, waiting for boot...");

    // Wait for BUSY to go LOW with timeout (chip boots with RF subsystem)
    unsigned long start = millis();
    while (digitalRead(PN5180_BUSY) == HIGH) {
        if (millis() - start > 2000) {
            DBG_LOGLN("HardwareNFCConnection: TIMEOUT waiting for BUSY LOW after reset!");
            break;
        }
        delay(1);
    }
    DBG_LOGF("HardwareNFCConnection: BUSY went LOW after %lums\n", millis() - start);

    // Wait for IDLE IRQ with timeout
    start = millis();
    uint32_t irqStatus = 0;
    while (0 == (irqStatus & (1 << 2))) {  // IDLE_IRQ_STAT
        nfc_->readRegister(IRQ_STATUS, &irqStatus);
        if (millis() - start > 2000) {
            DBG_LOGF("HardwareNFCConnection: TIMEOUT waiting for IDLE IRQ! IRQ=0x%08lX\n", irqStatus);
            break;
        }
        delay(1);
    }
    DBG_LOGF("HardwareNFCConnection: IDLE IRQ after %lums, IRQ=0x%08lX\n", millis() - start, irqStatus);
    nfc_->clearIRQStatus(0xffffffff);
    DBG_LOGLN("HardwareNFCConnection: Reset complete");

    // Read firmware version
    uint8_t firmwareVersion[2];
    nfc_->readEEprom(PN5180_FIRMWARE_VERSION, firmwareVersion, 2);
    DBG_LOGF("HardwareNFCConnection: PN5180 firmware: %d.%d\n",
                  firmwareVersion[1], firmwareVersion[0]);

    // Setup RF for ISO15693
    if (!nfc_->setupRF()) {
        DBG_LOGLN("HardwareNFCConnection: Failed to setup RF");
        return false;
    }

    // Set up HAL for openprinttag
    hal_.read_page = halReadPage;
    hal_.write_page = halWritePage;
    hal_.is_present = nullptr;
    hal_.user_ctx = this;

    DBG_LOGLN("HardwareNFCConnection: Initialized successfully");
    return true;
}

void HardwareNFCConnection::reset() {
    if (nfc_) {
        nfc_->reset();
    }
}

bool HardwareNFCConnection::hardwareReset() {
    if (!nfc_) return false;

    DBG_LOGLN("HardwareNFC: hardwareReset() - toggling RST pin");

    // Toggle RST pin to force full hardware reset
    digitalWrite(PN5180_RST, LOW);
    delay(10);
    digitalWrite(PN5180_RST, HIGH);

    // Wait for BUSY to go LOW with timeout
    unsigned long start = millis();
    while (digitalRead(PN5180_BUSY) == HIGH) {
        if (millis() - start > 2000) {
            DBG_LOGLN("HardwareNFC: hardwareReset TIMEOUT waiting for BUSY LOW");
            return false;
        }
        delay(1);
    }

    // Wait for IDLE IRQ with timeout
    start = millis();
    uint32_t irqStatus = 0;
    while (0 == (irqStatus & IDLE_IRQ_STAT)) {
        nfc_->readRegister(IRQ_STATUS, &irqStatus);
        if (millis() - start > 2000) {
            DBG_LOGF("HardwareNFC: hardwareReset TIMEOUT waiting for IDLE IRQ, IRQ=0x%08lX\n", irqStatus);
            return false;
        }
        delay(1);
    }
    nfc_->clearIRQStatus(0xffffffff);

    // Re-setup RF
    return setupRF();
}

bool HardwareNFCConnection::setupRF() {
    if (!nfc_) return false;

    if (!nfc_->loadRFConfig(0x0d, 0x8d)) return false;
    if (!nfc_->setRF_on()) return false;

    // not sure this is true, taking out:
    // This sequence is critical. After turning the RF field on, the PN5180
    // must be explicitly put into the Idle state and then the Transceive
    // state. This prepares the chip's internal state machine to handle
    // subsequent data transmission commands reliably. Omitting this leads
    // to intermittent failures on subsequent tag reads.
    //nfc_->writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);  // Idle/StopCom
    //nfc_->writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003);   // Transceive

    return true;
}

bool HardwareNFCConnection::detectTag(uint8_t* uid, uint8_t* uidLength) {
    if (!nfc_) {
        DBG_LOGLN("HardwareNFC: detectTag() called but nfc_ is null!");
        return false;
    }

    ISO15693ErrorCode err = nfc_->getInventory(uid);
    if (err == ISO15693_EC_OK) {
        // Reject phantom detections (all-zero UID = tag not fully powered)
        bool allZero = true;
        for (int i = 0; i < 8; i++) {
            if (uid[i] != 0) { allZero = false; break; }
        }
        if (allZero) return false;

        *uidLength = 8;  // ISO15693 uses 8-byte UID
        return true;
    }

    // Log non-OK errors periodically (not every poll to avoid spam)
    static uint32_t errCount = 0;
    errCount++;
    if (errCount % 200 == 1) {
        DBG_LOGF("HardwareNFC: getInventory err=%d (%s) [count=%lu]\n",
                      (int)err, nfc_->strerror(err), errCount);
    }
    return false;
}

void HardwareNFCConnection::setCurrentUid(const uint8_t* uid, uint8_t length) {
    memcpy(currentUid_, uid, length < 8 ? length : 8);
}

opt_nfc_hal_t* HardwareNFCConnection::getHal() {
    return &hal_;
}

void HardwareNFCConnection::logDiagnostics() {
    if (!nfc_) {
        DBG_LOGLN("HardwareNFC DIAG: nfc_ is null!");
        return;
    }

    uint32_t irqStatus, rfStatus, sysStatus;
    nfc_->readRegister(IRQ_STATUS, &irqStatus);
    nfc_->readRegister(RF_STATUS, &rfStatus);
    nfc_->readRegister(SYSTEM_STATUS, &sysStatus);

    uint8_t transceiverState = (rfStatus >> 24) & 0x07;
    bool rfFieldOn = (rfStatus & 0x01);  // TX_RF_STATUS bit
    bool extFieldDet = (rfStatus & 0x02);  // RF_DET_STATUS bit

    #ifdef ENABLE_NFC_DEBUG_LOGS

        DBG_LOGF("HardwareNFC DIAG: IRQ=0x%08lX RF=0x%08lX SYS=0x%08lX\n",
                    irqStatus, rfStatus, sysStatus);
        DBG_LOGF("HardwareNFC DIAG: RF_field=%s ext_field=%s transceiver=%u\n",
                    rfFieldOn ? "ON" : "OFF",
                    extFieldDet ? "YES" : "NO",
                    transceiverState);

        // Step-by-step RF activation test
        DBG_LOGLN("HardwareNFC DIAG: --- RF activation test ---");

    #endif
    

    // Step 1: Reset and check
    nfc_->reset();
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        DBG_LOGF("HardwareNFC DIAG: After reset: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif


    // Step 2: Load RF config
    nfc_->loadRFConfig(0x0d, 0x8d);
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        DBG_LOGF("HardwareNFC DIAG: After loadRFConfig: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif


    // Step 3: Turn RF on
    nfc_->setRF_on();
    nfc_->readRegister(RF_STATUS, &rfStatus);
    nfc_->readRegister(IRQ_STATUS, &irqStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        DBG_LOGF("HardwareNFC DIAG: After setRF_on: RF=0x%08lX IRQ=0x%08lX field=%s\n",
                    rfStatus, irqStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif

    // Step 4: Wait and check again
    delay(50);
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        DBG_LOGF("HardwareNFC DIAG: After 50ms wait: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif

    // Step 5: Set transceive (like setupRF does) and check
    nfc_->writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);  // Idle
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        DBG_LOGF("HardwareNFC DIAG: After Idle cmd: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");
    #endif


    nfc_->writeRegisterWithOrMask(SYSTEM_CONFIG, 0x00000003);  // Transceive
    nfc_->readRegister(RF_STATUS, &rfStatus);

    #ifdef ENABLE_NFC_DEBUG_LOGS
        DBG_LOGF("HardwareNFC DIAG: After Transceive cmd: RF=0x%08lX field=%s\n",
                    rfStatus, (rfStatus & 0x01) ? "ON" : "OFF");

        DBG_LOGLN("HardwareNFC DIAG: --- end test ---");
    #endif
}
