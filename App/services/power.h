/**
 * @file    power.h
 * @brief   Power manager: debug-mode handling and STOP mode (CLAUDE.md 6.10).
 */
#ifndef APP_POWER_H
#define APP_POWER_H

#include <stdbool.h>
#include <stdint.h>

/* Debug builds: keep the debugger attached in SLEEP/STOP/STANDBY.
 * Release builds: clear those bits (export mode sets them again, Phase 5). */
void power_debug_config(void);

/* Export mode: DBGMCU SLEEP/STOP/STANDBY bits on in every build so the host
 * can attach (CLAUDE.md 6.10). power_debug_config() restores the build
 * default. */
void power_debug_enable(void);

/* BOR (CLAUDE.md 6.10): reads the option bytes and, when
 * APP_SET_BOR_LEVEL3 is 1 and the level is not 3 yet, programs
 * OB_BOR_LEVEL3 and resets (one time). Returns the current BOR level as
 * read (OB_BOR_LEVEL1..3 or OB_BOR_OFF). */
uint8_t power_check_bor(void);

/* True once STOP is allowed: always in Release, after DEBUG_STOP_DELAY_MS
 * from reset in Debug builds. */
bool power_stop_allowed(void);

/* Enters STOP and stays there until a key wakes the device. RTC wake-ups
 * (every RTC_WAKEUP_PERIOD_S) go straight back to STOP without touching any
 * peripheral (step 9). Before returning, the keypad is back in awake mode
 * and I2C1 is re-initialised.
 * Caller: the OLED is off and the PN532 is in PowerDown (step 1), and no
 * key is held. */
void power_sleep_until_key(void);

/* Counters for the debugger and the Phase 4 wake/sleep test. */
uint32_t power_stop_count(void);        /* STOP entries */
uint32_t power_key_wake_count(void);    /* returns caused by a key */
uint32_t power_rtc_wake_count(void);    /* RTC-only wakes, back to STOP */

#endif /* APP_POWER_H */
