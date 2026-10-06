# Changelog

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
