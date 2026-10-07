/**
 * @file    pn532.h
 * @brief   NXP PN532 NFC controller over I2C (UM0701-02 Rev 02).
 */
#ifndef APP_PN532_H
#define APP_PN532_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"

#define PN532_UID_MAX   10U     /* ISO14443A triple-size UID */

typedef struct {
    uint8_t ic;         /* 0x32 for PN532 */
    uint8_t ver;
    uint8_t rev;
    uint8_t support;    /* bit0 ISO14443A, bit1 ISO14443B, bit2 ISO18092 */
} pn532_fw_t;

/* GetFirmwareVersion (UM0701-02 7.2.2). */
status_t pn532_get_firmware(pn532_fw_t *fw);

/* SAMConfiguration normal mode, P70_IRQ not driven (UM0701-02 7.2.10). */
status_t pn532_sam_config(void);

/* RFConfiguration item 0x05 MaxRetries (UM0701-02 7.3.1). */
status_t pn532_set_passive_retries(uint8_t retries);

/* RFConfiguration item 0x01: RF field on/off, AutoRFCA off. */
status_t pn532_rf_field(bool on);

/* InListPassiveTarget, 1 target, 106 kbps type A (UM0701-02 7.3.5).
 * STATUS_OK with *uid_len = 0 means no card was found. */
status_t pn532_read_uid(uint8_t uid[PN532_UID_MAX], uint8_t *uid_len);

#endif /* APP_PN532_H */
