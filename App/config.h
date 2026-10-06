/**
 * @file    config.h
 * @brief   All tunable values for the asset tracker firmware.
 *
 * Every timeout, threshold, address and magic number lives here, with units.
 * See CLAUDE.md for the meaning of each subsystem.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Build                                                                      */
/* ------------------------------------------------------------------------- */

/* APP_DEBUG is set to 1 by CMake for Debug builds; Release leaves it at 0. */
#ifndef APP_DEBUG
#define APP_DEBUG 0
#endif

/* Firmware version, packed as (major << 8) | minor. Shown on the info screen
 * and exported in export_buffer_t.fw_version. */
#define FW_VERSION_MAJOR        0U
#define FW_VERSION_MINOR        0U
#define FW_VERSION              ((uint16_t)((FW_VERSION_MAJOR << 8) | FW_VERSION_MINOR))

/* Application mode. Phase 1 adds the bring-up test mode. */
#define APP_MODE_NORMAL         0
#define APP_MODE_BRINGUP        1
#define APP_MODE                APP_MODE_NORMAL

/* ------------------------------------------------------------------------- */
/* Main loop                                                                  */
/* ------------------------------------------------------------------------- */

/* Period of EV_TICK while awake (ms). Keypad scan and timeouts run on it. */
#define TICK_PERIOD_MS          10U

/* 1 = idle in __WFI() between interrupts while awake. 0 = busy-wait.
 * On this board, SWD memory reads (Live Watch, CLI hot-plug) return zeros
 * while the core sleeps in __WFI(), even with DBGMCU DBG_SLEEP set (seen in
 * Phase 0). Debug builds therefore busy-wait so the debugger can read RAM;
 * Release builds sleep. */
#define APP_IDLE_USE_WFI        (!APP_DEBUG)

/* ------------------------------------------------------------------------- */
/* RTC                                                                        */
/* ------------------------------------------------------------------------- */

/* RTC wake-up period (s). Must match the CubeMX WakeUp counter (9 -> 10 s),
 * and stay well below the IWDG timeout (~32 s, Phase 5). */
#define RTC_WAKEUP_PERIOD_S     10U

/* Backup register holding RTC_BKP_MAGIC once the calendar has been set.
 * Backup registers survive MCU resets (VBAT is tied to 3V3), not battery removal. */
#define RTC_BKP_IDX_MAGIC       RTC_BKP_DR0
#define RTC_BKP_MAGIC           0x52544331UL    /* 'RTC1' */

/* Backup register holding the time_valid flag (1 = set by a time sync). */
#define RTC_BKP_IDX_TIME_VALID  RTC_BKP_DR1

/* Calendar set on first boot: 2026-01-01 00:00:00 UTC (a Thursday).
 * Unix time 1767225600. */
#define RTC_FIRST_BOOT_YEAR     26U     /* years since 2000 */
#define RTC_FIRST_BOOT_MONTH    1U
#define RTC_FIRST_BOOT_DAY      1U
#define RTC_FIRST_BOOT_WEEKDAY  4U      /* RTC_WEEKDAY_THURSDAY */

/* Local time offset for display only (minutes). +330 = Sri Lanka (UTC+5:30). */
#define TZ_OFFSET_MIN           330

/* ------------------------------------------------------------------------- */
/* Trace (Debug builds only)                                                  */
/* ------------------------------------------------------------------------- */

/* Number of entries in the RAM trace ring buffer (12 bytes each). */
#define TRACE_DEPTH             64U

#endif /* APP_CONFIG_H */
