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

/* Target board (CLAUDE.md 3.5). 1 = WeAct Black Pill on a breadboard,
 * 0 = custom PCB. Black Pill: FRAM may be absent (RAM emulator), PC13 LED. */
#define APP_BOARD_BLACKPILL     1

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
/* I2C bus (I2C1)                                                             */
/* ------------------------------------------------------------------------- */

/* Timeout per HAL I2C transfer (ms). Longest Phase 1 transfer is a 256-byte
 * FRAM chunk: ~24 ms at 100 kHz. */
#define I2C_TIMEOUT_MS          50U

/* Timeout for one address probe during the bus scan (ms). */
#define I2C_PROBE_TIMEOUT_MS    5U

/* 7-bit device addresses (CLAUDE.md section 3.3). */
#define I2C_ADDR_PN532          0x24U   /* UM0701-02 6.2.4: 0x48 write / 0x49 read */
#define I2C_ADDR_OLED_PRIMARY   0x3CU
#define I2C_ADDR_OLED_ALT       0x3DU
#define I2C_ADDR_FRAM           0x50U   /* MB85RC256V: type code 1010, A2..A0 = 000 */

/* ------------------------------------------------------------------------- */
/* OLED (SSD1306)                                                             */
/* ------------------------------------------------------------------------- */

#define OLED_WIDTH              128U
#define OLED_HEIGHT             64U     /* 64 or 32 */

/* Contrast 0x00..0xFF (SSD1306 cmd 81h). Low = less current. */
#define OLED_CONTRAST           0x40U

/* Column offset of the panel in display RAM. 0 for SSD1306; set 2 if the
 * module turns out to be an SH1106 (text shifted / garbage at the edge). */
#define OLED_COL_OFFSET         0U

/* 1 = rotate 180 degrees (use if the text is upside down). */
#define OLED_FLIP               0

/* ------------------------------------------------------------------------- */
/* FRAM (MB85RC256V)                                                          */
/* ------------------------------------------------------------------------- */

#define FRAM_SIZE_BYTES         32768U
/* Largest single I2C transfer; longer reads/writes are split. */
#define FRAM_MAX_CHUNK          256U
/* Bring-up test scratch area (CLAUDE.md section 6.7). */
#define FRAM_SCRATCH_ADDR       0x00C0U
#define FRAM_SCRATCH_SIZE       0x40U

/* ------------------------------------------------------------------------- */
/* Black Pill LED (PC13, active low)                                          */
/* ------------------------------------------------------------------------- */

/* Heartbeat toggle period when all is well / when a check failed (ms). */
#define LED_HEARTBEAT_MS        500U
#define LED_ERROR_BLINK_MS      100U

/* ------------------------------------------------------------------------- */
/* Keypad                                                                     */
/* ------------------------------------------------------------------------- */

/* A key state must be stable this long before it is reported (ms). */
#define KEY_DEBOUNCE_MS         20U
/* Settle time after selecting a column, before reading rows (us). Rows only
 * have the ~40 kOhm internal pull-ups, so allow a few RC time constants. */
#define KEY_SETTLE_US           10U

/* ------------------------------------------------------------------------- */
/* NFC (PN532)                                                                */
/* ------------------------------------------------------------------------- */

/* Status-byte poll interval while waiting for the PN532 (ms). */
#define PN532_POLL_MS           2U
/* Max wait for the ACK frame after a command (ms). */
#define PN532_ACK_TIMEOUT_MS    30U
/* Max wait for a normal command response (ms). */
#define PN532_RESP_TIMEOUT_MS   100U
/* Max wait for an InListPassiveTarget response (ms). */
#define PN532_LIST_TIMEOUT_MS   150U
/* RFConfiguration MaxRetries, MxRtyPassiveActivation (UM0701-02 7.3.1,
 * Table 18). Default is 0xFF = retry forever, which would block. */
#define PN532_PASSIVE_RETRIES   0x01U
/* Consecutive failed card-read attempts before the PN532 is treated as
 * lost and re-initialised (every BRINGUP_VDD_PERIOD_MS in bring-up). */
#define NFC_FAIL_LIMIT          3U
/* Period between card-read attempts while scanning (ms). */
#define NFC_ATTEMPT_PERIOD_MS   300U

/* ------------------------------------------------------------------------- */
/* Battery                                                                    */
/* ------------------------------------------------------------------------- */

/* VREFINT samples averaged per measurement. */
#define BATTERY_SAMPLES         8U
/* Bring-up: re-measure VDD this often (ms). */
#define BRINGUP_VDD_PERIOD_MS   5000U

/* Thresholds (CLAUDE.md 6.8), checked on every wake before NFC is used.
 * >= OK: normal. OK..CRITICAL: "Battery low", continue. < CRITICAL:
 * "Charge battery" and back to sleep. Low-battery icon below OK (mV). */
#define BATTERY_OK_MV           3250U
#define BATTERY_CRITICAL_MV     3000U

/* ------------------------------------------------------------------------- */
/* Application (CLAUDE.md 6.3)                                                */
/* ------------------------------------------------------------------------- */

/* Admin PIN (digits only). CHANGE BEFORE DEPLOYMENT. */
#define ADMIN_PIN               "1234"
#define ADMIN_PIN_MAX_LEN       8U

/* Hold time of 'D' in sleep to open the admin PIN screen (ms). */
#define ADMIN_LONGPRESS_MS      3000U

/* Max digits in an asset ID (fits in uint32: 99 999 999). */
#define ASSET_ID_MAX_DIGITS     8U

/* State timeouts (ms). */
#define SCAN_TIMEOUT_MS         10000U  /* no card -> sleep */
#define STUDENT_SHOW_MS         1500U   /* name screen before asset entry */
#define ASSET_IDLE_MS           20000U  /* no key during asset entry -> sleep */
#define PIN_IDLE_MS             20000U  /* no key on the PIN screen -> sleep */
#define ADMIN_IDLE_MS           30000U  /* no key in any admin screen -> sleep */
#define MSG_SHORT_MS            1500U   /* "Saved #n", "Battery low" */
#define MSG_LONG_MS             3000U   /* errors, "Unregistered card" */

/* Show "Log nearly full" after a save at or above this fill level (%). */
#define LOG_WARN_PERCENT        90U

/* ------------------------------------------------------------------------- */
/* Trace (Debug builds only)                                                  */
/* ------------------------------------------------------------------------- */

/* Number of entries in the RAM trace ring buffer (12 bytes each). */
#define TRACE_DEPTH             64U

#endif /* APP_CONFIG_H */
