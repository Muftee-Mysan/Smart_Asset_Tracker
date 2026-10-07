# Changelog

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
