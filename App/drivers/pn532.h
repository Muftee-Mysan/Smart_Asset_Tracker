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

/* Boot sequence (CLAUDE.md 6.5): GetFirmwareVersion (retried once, the first
 * command after power-up may fail), SAMConfiguration, MaxRetries
 * (PN532_PASSIVE_RETRIES), RF field off, PowerDown. Fills *fw when given. */
status_t pn532_init(pn532_fw_t *fw);

/* PowerDown with I2C as the only wake-up source (UM0701-02 7.2.11). The
 * next command wakes the chip; its first frame may be NACKed and is
 * retried. */
status_t pn532_power_down(void);

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
