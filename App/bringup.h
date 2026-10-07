/**
 * @file    bringup.h
 * @brief   Phase 1 hardware bring-up test (APP_MODE_BRINGUP).
 */
#ifndef APP_BRINGUP_H
#define APP_BRINGUP_H

#include <stdint.h>
#include "pn532.h"

#define BRINGUP_I2C_MAX 8U
#define BRINGUP_KEY_LOG 32U

/* Results, also for the debugger's Live Watch (in case the OLED fails). */
typedef struct {
    uint8_t  i2c_found[BRINGUP_I2C_MAX];  /* 7-bit addresses that ACKed */
    uint8_t  i2c_count;
    uint8_t  oled_addr;     /* 0 = no OLED */
    uint8_t  fram_ok;       /* 1 = pattern test passed */
    uint8_t  nfc_ok;        /* 1 = firmware version read and configured */
    pn532_fw_t nfc_fw;
    uint16_t vdd_mv;
    char     last_key;
    uint32_t key_count;
    uint8_t  uid[PN532_UID_MAX];
    uint8_t  uid_len;
    uint32_t card_reads;
    uint32_t nfc_errors;
    uint32_t oled_errors;
    char     key_log[BRINGUP_KEY_LOG];    /* labels of the first 32 key presses */
    uint8_t  key_log_len;
    uint8_t  fram_emulated; /* 1 = RAM emulator in use (Black Pill, no FRAM) */
    uint32_t nfc_attempt_ms;     /* duration of the last card-read attempt */
    uint32_t nfc_attempt_ms_max; /* longest card-read attempt since boot */
} bringup_t;

extern bringup_t g_bringup;

void bringup_run(void) __attribute__((noreturn));

#endif /* APP_BRINGUP_H */
