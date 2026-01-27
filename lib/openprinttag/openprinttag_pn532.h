/**
 * OpenPrintTag PN532 HAL Adapter
 *
 * Provides NFC HAL implementation for PN532 NFC reader.
 * Compatible with pn532-esp-idf component.
 */

#ifndef OPENPRINTTAG_PN532_H
#define OPENPRINTTAG_PN532_H

#include "openprinttag_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration - actual definition in pn532.h */
struct pn532_t;
typedef struct pn532_t pn532_t;

/**
 * Read a page from NTAG using PN532.
 *
 * @param ctx   PN532 context (pn532_t*)
 * @param page  Page number to read
 * @param buf   Output buffer (4 bytes)
 * @return OPT_OK on success, OPT_ERR_NFC_READ on failure
 */
static inline opt_error_t opt_pn532_read_page(void *ctx, uint8_t page, uint8_t *buf) {
    pn532_t *pn532 = (pn532_t *)ctx;
    /* pn532_ntag2xx_ReadPage returns non-zero on success */
    extern uint8_t pn532_ntag2xx_ReadPage(pn532_t *obj, uint8_t page, uint8_t *buffer);
    return pn532_ntag2xx_ReadPage(pn532, page, buf) ? OPT_OK : OPT_ERR_NFC_READ;
}

/**
 * Write a page to NTAG using PN532.
 *
 * @param ctx   PN532 context (pn532_t*)
 * @param page  Page number to write
 * @param data  Data to write (4 bytes)
 * @return OPT_OK on success, OPT_ERR_NFC_WRITE on failure
 */
static inline opt_error_t opt_pn532_write_page(void *ctx, uint8_t page, const uint8_t *data) {
    pn532_t *pn532 = (pn532_t *)ctx;
    /* pn532_ntag2xx_WritePage returns non-zero on success */
    extern uint8_t pn532_ntag2xx_WritePage(pn532_t *obj, uint8_t page, uint8_t *data);
    return pn532_ntag2xx_WritePage(pn532, page, (uint8_t *)data) ? OPT_OK : OPT_ERR_NFC_WRITE;
}

/**
 * Create a HAL structure for PN532.
 *
 * @param pn532  Initialized PN532 context
 * @return HAL structure ready for use with opt_read_from_nfc/opt_write_to_nfc
 *
 * Example usage:
 *   pn532_t pn532;
 *   pn532_spi_init(&pn532, clk, miso, mosi, ss);
 *   pn532_begin(&pn532);
 *   pn532_SAMConfig(&pn532);
 *
 *   opt_nfc_hal_t hal = opt_create_pn532_hal(&pn532);
 *
 *   opt_tag_t tag;
 *   opt_init(&tag);
 *   opt_read_from_nfc(&tag, &hal, 4, 50);
 *   opt_parse_ndef(&tag);
 */
static inline opt_nfc_hal_t opt_create_pn532_hal(pn532_t *pn532) {
    opt_nfc_hal_t hal = {
        .read_page = opt_pn532_read_page,
        .write_page = opt_pn532_write_page,
        .is_present = NULL,  /* PN532 doesn't have a simple presence check */
        .user_ctx = pn532
    };
    return hal;
}

#ifdef __cplusplus
}
#endif

#endif /* OPENPRINTTAG_PN532_H */
