# Changelog

## Phase 3 — Application (unreleased, FW 0.0)

- `app_sm.c`: state machine per CLAUDE.md 6.3 (SLEEP, SCAN, STUDENT, ASSET, MSG,
  ADMIN_PIN, ADMIN_MENU, CLEAR_CONFIRM, SET_TIME, INFO, READ_UID, plus FORMAT_CONFIRM
  for an unreadable log). Wake key consumed; 'D' held 3 s opens the admin PIN.
  Battery check on every wake before NFC. Log-full check refuses scans. PN532
  PowerDown whenever scanning ends. All timeouts in `config.h`.
- `ui.c`: one function per screen, status bar with title, low-battery mark and local
  time ('?' while the clock is unset). Large text = 6x8 font at 2x (12x16) instead of
  a separate 8x16 font (deviation from CLAUDE.md 6.1, same height).
- `storage.c`: FRAM log per 6.7 — A/B headers with generation and CRC-32 (zlib),
  24-byte records with CRC-16/CCITT-FALSE, append = write record, read back, then
  header to the older copy; boot picks the newer valid header and rolls forward a
  record written before a reset; blank FRAM is formatted automatically, a non-empty
  one only after the user confirms; clear keeps next_seq.
- `students.c` + `tools/gen_students.py` + `tools/students.csv`: CSV validated (8/14 hex
  UID, unique, ID <= 11, name <= 20 ASCII), sorted, generated into
  `App/data/students_table.c` by CMake whenever the CSV changes; binary search.
- `rtc_time.c`: Unix <-> calendar (checked against Python datetime for 2000-2099),
  set time, time_valid flag. `battery.c`: thresholds 3250 / 3000 mV.
- Admin: 1 Export (placeholder until Phase 5), 2 Clear (double #), 3 Set time
  (local YYMMDDhhmm, logs an 0xA2 sync record first), 4 Info, 5 Read UID.
- Verified on the Black Pill (FRAM RAM emulator, records read back over SWD): full
  transaction, delete key, cancel, 10 s / 20 s timeouts, wrong / right PIN, info,
  set time (UTC conversion correct, sync record logged), read UID, clear (next_seq
  kept: next save was #4), header A/B alternation, CRC-32 matches zlib.crc32.
- Not tested yet: unregistered card (only one card available); reset during save
  (needs real FRAM, moved to Phase 6a).

## Phase 2 — Drivers and error paths (unreleased, FW 0.0)

- `i2c_bus`: bus recovery per CLAUDE.md 6.12 (de-init, up to 9 SCL pulses with SDA
  released, STOP, `I2C_CR1_SWRST`, re-init) and one retry on timeout, bus error,
  arbitration loss or stuck BUSY. NACKs are returned without recovery. Recovery also
  runs at boot if a line is stuck low. `i2c_bus_set_speed()` added for Phase 4.
  Recovery count exposed and traced.
- `pn532`: `pn532_init()` boot sequence (GetFirmwareVersion retried once,
  SAMConfiguration, MaxRetries, RF off, PowerDown); `pn532_power_down()` with I2C as
  the only wake source (WakeUpEnable 0x80, UM0701-02 7.2.11) and a 2 ms settle;
  status byte checked on bits 0-5 (7.1). Command + ACK retried once (wake from
  PowerDown).
- `keypad`: `keypad_sleep_config()` and `keypad_all_released()` for Phase 4.
- `ssd1306`: refuses to send when no display was found (address 0 would be a general
  call); `ssd1306_is_present()`.
- `delay`: shared DWT microsecond delay.
- Bring-up: NFC marked lost after 3 failed reads and re-initialised every 5 s; OLED
  re-detected every 5 s after it stops answering; stats line shows card reads, NFC
  errors, I2C errors, recoveries.
- Verified on the Black Pill: card reads after PowerDown wake; PN532 unplugged while
  running -> NFC:ERROR, fast LED, keypad still responsive, no hang; plugged back ->
  re-initialised automatically and reads again. 0 I2C bus errors (absent device gives
  NACKs). Longest card-read attempt 44 ms.
- Not yet tested on hardware: forced bus recovery (SDA shorted to GND).

## Phase 1 — Bring-up test (unreleased, FW 0.0)

- Board switch `APP_BOARD_BLACKPILL` (CLAUDE.md 3.5); development now on a WeAct Black Pill.
- Drivers: `i2c_bus` (timeouts, status_t, NACKs counted apart from bus errors), `ssd1306`
  (address detect 0x3C/0x3D, dirty-page flush, 6x8 text, 2x scale), `font` (6x8, CC0),
  `fram` (2-byte address, 256-byte chunks; RAM emulator on the Black Pill when no FRAM
  answers), `keypad` (open-drain column scan, 20 ms debounce), `pn532` (I2C status
  polling, ACK, checksums, GetFirmwareVersion, SAMConfiguration, RFConfiguration,
  InListPassiveTarget), `led` (PC13, Black Pill only).
- Services: `battery` (VDD from VREFINT_CAL, 8 samples, VREFINT off afterwards).
- `bringup.c` (`APP_MODE_BRINGUP`): I2C scan, OLED "Hello", FRAM pattern test in the
  scratch area, NFC firmware + config, VDD, live key and card UID display, LED
  heartbeat (slow = all OK, fast = a check failed). Results also in `g_bringup`.
- PN532 MaxRetries (MxRtyPassiveActivation) set to 1: the default 0xFF never returns
  without a card (UM0701-02 Table 18).
- Verified on the Black Pill: I2C 0x24 + 0x3C, OLED OK, FRAM RAM emulator OK,
  PN532 IC 0x32 v1.6, VDD 3331 mV, all 16 keys correct, card UID DAE9351D read
  (21 reads, 0 errors). One NFC attempt takes 40 ms without a card.
- Custom PCB (for Phase 6a): open joints on PB5, PB6, PB10 and later PA1 were found
  and partly reworked; the board stopped answering SWD after the last rework
  (suspect VCAP1/pin 22 or NRST). FRAM on the PCB passed the pattern test once SCL
  was fixed.

## Phase 0 — Skeleton (unreleased, FW 0.0)

- `App/` structure: `config.h`, `status.h`, `app_main.c/.h`, `services/{power,rtc_time,trace}.c/.h`;
  empty `drivers/` and `data/`.
- `main.c` calls `app_init()` / `app_run()` from `USER CODE 2`.
- RTC: `Check_RTC_BKUP` returns early when `RTC_BKP_DR0` holds the magic, so the
  calendar survives resets. First boot sets 2026-01-01 00:00 UTC, clears time_valid
  (DR1) and writes the magic. `rtc_time_init()` re-arms the 10 s wake-up timer on
  every boot.
- Main loop: ISR flags -> counters in `g_app_dbg`, 10 ms tick, `__WFI()` in between.
- Trace ring buffer `g_trace` (Debug builds only).
- Debug builds set DBGMCU SLEEP/STOP/STANDBY; Release clears them.
- CMake: App sources/includes, `APP_DEBUG=1` for Debug, `-Wextra` on Core/ and App/
  (not on the ST HAL, which has 3 unused-parameter warnings).
- VS Code: `.vscode/launch.json` (explicit ELF path, Live Watch on, SWD 1 MHz),
  `.vscode/tasks.json` (CMake build tasks), `.settings/ide.store.json` (device/core
  for the ST-Link debug adapter, which fails without it).
- Known: SWD memory reads return zeros while the core sleeps in `__WFI()`, even
  with DBGMCU DBG_SLEEP set (confirmed with a busy-wait build: Live Watch is then
  steady). Debug builds therefore busy-wait while awake (`APP_IDLE_USE_WFI`);
  Release builds use `__WFI()`. Export mode (Phase 5) must not idle in `__WFI()`.
  Also, SWD hot-plug at 4 MHz fails on this ST-LINK; 1 MHz works.
- Verified on hardware: SystemCoreClock 16 MHz, heartbeat, 10 s RTC wake-ups,
  calendar and wake-up timer survive reset (boot_from_bkup = 1), keypad EXTI.
