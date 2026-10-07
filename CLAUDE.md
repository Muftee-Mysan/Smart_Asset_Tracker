# CLAUDE.md — Smart Asset Tracker firmware (STM32F411CEU6)

This file is the single source of truth for the firmware. Read it fully before every task.
Datasheets live in `docs/datasheets/` (STM32F411, PN532, MB85RC256V, TPS736, TPS61230A, Parallax 27899 keypad). When a register value, command byte or timing matters, check it against the datasheet or reference manual and cite it in a code comment.

---

## 1. What the product does

A battery-powered handheld that logs which student took which asset:

1. Device sleeps (STOP mode). Any key press wakes it.
2. OLED shows "Tap student card". PN532 reads an ISO14443A card UID.
3. UID is looked up in a student table compiled into the firmware. Name + student ID shown.
4. User types the asset ID on the 4x4 keypad (`#` = confirm, `*` = delete / cancel).
5. Record (UID, asset ID, timestamp, sequence no.) is saved to FRAM.
6. Device goes back to sleep.

Records are retrieved over SWD (ST-LINK) using an on-device **export mode** plus a host script. There is no UART, USB or radio link.

**Primary design goal: minimum average power consumption** on a 1000 mAh 1S LiPo, within fixed hardware.

---

## 2. Hard constraints (do not try to work around these)

- **The PCB cannot be modified.** No trace cuts, no part changes. Firmware-only solutions.
- **The 5V rail is always on.** The TPS61230A boost (U3) has EN tied to VCC. The PN532 and OLED modules are always powered. They must be put into their own low-power states by command.
- **The 3V3 LDO (U2, TPS736) draws ~400 µA ground current at all times.** EN is tied to VCC. This is the dominant sleep current and cannot be fixed in firmware. Do not spend effort shaving single µA elsewhere at the expense of complexity.
- **The FRAM has no sleep mode** (~27 µA standby, always on 3V3).
- **The PN532 has no IRQ and no RSTPD_N connection.** Use I2C status polling. Low power is via the PowerDown command only.
- **No crystals.** SYSCLK = HSI 16 MHz. RTC and IWDG run from LSI, which is 17–47 kHz per the datasheet until calibrated.
- **NRST is not on the SWD header.** "Connect under reset" is impossible. See §9 for recovery.
- **The user will not measure current.** Rely on datasheet values and disciplined design.

---

## 3. Hardware reference

### 3.1 Power tree
- Battery: 1S LiPo 1000 mAh (3.0–4.2 V) on P2 → net `VCC`.
- `VCC` → U2 TPS736 LDO → `3V3`: MCU, FRAM, I2C pull-ups R3/R4 (10k).
- `VCC` → U3 TPS61230A boost → `+5`: NFC module (J3) and OLED module (J4).
- VBAT pin of the MCU is tied to 3V3, so RTC and backup registers survive MCU resets but not battery removal.
- When the battery drops below ~3.35 V, the LDO enters dropout and VDD follows the battery. Firmware uses VREFINT to detect this (§6.8).

### 3.2 MCU pin map

| Function | Pin | Notes |
|---|---|---|
| I2C1 SCL | PB6 | AF4, open-drain, external 10k pull-up (R3/R4) plus module pull-ups |
| I2C1 SDA | PB7 | AF4, open-drain |
| Keypad col 4 (A B C D) | PB9 | J9 pin 1, output |
| Keypad col 3 (3 6 9 #) | PB8 | J9 pin 2, output |
| Keypad col 2 (2 5 8 0) | PB14 | J9 pin 3, output |
| Keypad col 1 (1 4 7 *) | PA1 | J9 pin 4, output |
| Keypad row 4 (* 0 # D) | PB5 | J9 pin 5, input pull-up, EXTI5 falling |
| Keypad row 3 (7 8 9 C) | PB13 | J9 pin 6, input pull-up, EXTI13 falling |
| Keypad row 2 (4 5 6 B) | PB12 | J9 pin 7, input pull-up, EXTI12 falling |
| Keypad row 1 (1 2 3 A) | PB10 | J9 pin 8, input pull-up, EXTI10 falling |
| SWDIO / SWCLK | PA13 / PA14 | P1 header. **Never reconfigure.** |
| BOOT0 | pin 44 | SW1 to 3V3, R5 10k pull-down |
| NRST | pin 7 | SW2 to GND (reset button) |
| BOOT1 | PB2 | R6 10k pull-down |
| J2 | PA9, PB15 | **Unused.** Leave as analog. |
| Everything else | — | Unused, analog |

The keypad mapping comes from the Parallax 27899 datasheet (pins 1–4 = columns, 5–8 = rows). It assumes the ribbon is plugged pin 1 → J9 pin 1. The bring-up test confirms this. If keys come out mirrored, flip the mapping table in `keypad.c`; do not touch wiring.

### 3.3 I2C bus (I2C1, shared)

| Device | 7-bit addr | Supply | Notes |
|---|---|---|---|
| PN532 NFC module (J3) | 0x24 | +5 (module regulator) | Module mode switches must be set to I2C. Max 400 kHz. Uses clock stretching. |
| SSD1306 OLED 128x64 (J4) | 0x3C | +5 (module regulator) | Some modules use 0x3D or are 128x32. Detect the address at boot; resolution is a `config.h` define. |
| MB85RC256V FRAM (U4) | 0x50 | 3V3 | 32 KB, 2-byte address MSB first, no write delay, no page boundary. A0–A2 = GND. WP assumed low. |

- Bus speed: 100 kHz during bring-up (Phase 1), then 400 kHz (Phase 4).
- Blocking HAL I2C with a 50 ms timeout per transfer. No I2C access from ISRs.

### 3.4 Connector pin orders (for reference)
- J3 NFC: 1 GND, 2 +5, 3 SDA, 4 SCL
- J4 OLED: 1 GND, 2 +5, 3 SCL, 4 SDA
- P1 SWD: 1 GND, 2 SWCLK (PA14), 3 SWDIO (PA13), 4 3V3

### 3.5 Development board: WeAct Black Pill (STM32F411CEU6) — used FIRST

Development starts on a WeAct Black Pill, which has the same MCU and package as the custom PCB, so the same CubeMX project, pin map and firmware are used. The custom PCB comes later. A single switch selects the board:

```c
// App/config.h
#define APP_BOARD_BLACKPILL   1   // 1 = Black Pill on breadboard, 0 = custom PCB
```

**Breadboard wiring (same MCU pins as the PCB):**

| Device | Device pin | Black Pill pin |
|---|---|---|
| OLED | VCC / GND / SCL / SDA | 5V / GND / B6 / B7 |
| PN532 (mode switches set to I2C) | VCC / GND / SDA / SCL | 5V / GND / B7 / B6 |
| I2C pull-ups | 10k from SCL and from SDA to 3V3 | B6, B7 (matches R3/R4 on the PCB) |
| Keypad pin 1 / 2 / 3 / 4 | Col 4 / 3 / 2 / 1 | B9 / B8 / B14 / A1 |
| Keypad pin 5 / 6 / 7 / 8 | Row 4 / 3 / 2 / 1 | B5 / B13 / B12 / B10 |
| ST-LINK | SWDIO / SWCLK / GND / NRST | DIO / CLK / GND / R |
| Power | USB-C cable | Supplies 5V pin and onboard 3.3V regulator |

If the ST-LINK is a clone that outputs 3.3 V, do not connect its 3.3 V pin while the board is powered from USB-C.

**Differences the firmware must handle when `APP_BOARD_BLACKPILL == 1`:**
- **FRAM may be absent.** At boot, probe address 0x50. If there is no ACK, use a RAM-backed FRAM emulator (static 32 KB array behind the same `fram_read()/fram_write()` API). Show "FRAM: RAM EMU" on the OLED at boot. Data is lost on reset, which is acceptable for testing the logic. This fallback is compiled out when `APP_BOARD_BLACKPILL == 0`; there, a missing FRAM is an error.
- **LED on PC13 (active low)**: use it as a debug heartbeat and error blinker. Configure PC13 in `App/` code (CubeMX leaves it analog). Not used on the PCB.
- **NRST is reachable** through the ST-LINK, so "connect under reset" works on this board. The PCB recovery procedure (§9) is still needed later.
- **Crystals are fitted** (25 MHz HSE, 32.768 kHz LSE) but **not used**. The clock setup stays HSI + LSI so that timing behaviour matches the PCB.
- **Battery checks**: VDD is a steady 3.3 V from USB, so the battery logic always reads "OK". Test the thresholds by temporarily lowering them in `config.h`.
- **Power numbers are not representative**: the onboard power LED and regulator dominate. Power work on the Black Pill checks behaviour (STOP entry/exit, modules sleeping), not current.

Phases 0–5 are done on the Black Pill. Moving to the PCB is its own step (Phase 6a in §8): set `APP_BOARD_BLACKPILL 0`, check the +5 V rail with a multimeter before connecting modules, rerun the Phase 1 bring-up, then the full application tests.

---

## 4. Project setup — generated with STM32CubeMX

The user generated the base project in STM32CubeMX (toolchain: CMake) for the STM32 VS Code extension. **Do not hand-edit the `.ioc` file.** If a CubeMX change is needed, describe it to the user and wait for them to regenerate.

### 4.1 What CubeMX configures

| Area | Setting |
|---|---|
| MCU | STM32F411CEUx (UFQFPN48) |
| SYS | Debug: Serial Wire (PA13/PA14). Timebase: SysTick |
| RCC | HSE and LSE disabled |
| Clocks | HSI 16 MHz → SYSCLK, no PLL. AHB, APB1, APB2 /1. RTC clock: LSI |
| I2C1 | PB6 SCL / PB7 SDA, Standard 100 kHz, clock stretching allowed, no IRQ/DMA |
| RTC | Clock source + calendar active, hour format 24, async prescaler 127, sync prescaler 249 (LSI ≈ 32 kHz → 1 Hz). Internal WakeUp: clock 1 Hz, counter 9 (every 10 s). Wake-up IRQ (EXTI 22) enabled, priority 2 |
| TIM5 | **Internal clock only, no channel configured** (so PA3 stays unused). Prescaler 0, period 0xFFFFFFFF, TIM5 global IRQ enabled, priority 2 |
| ADC1 | Vrefint only, PCLK2/2, 12-bit, right aligned, no scan, no continuous, software trigger, rank 1 = Vrefint at 480 cycles. No ADC IRQ |
| Keypad | KP_C1–C4 (PA1, PB14, PB8, PB9) output push-pull, initial Low. KP_R1–R4 (PB10, PB12, PB13, PB5) EXTI falling edge, pull-up |
| NVIC | EXTI9_5 and EXTI15_10 priority 3. RTC wake-up and TIM5 priority 2 |
| IWDG | Not enabled in CubeMX. Started from code in Phase 5 |
| Project | Name `asset_tracker`, location `C:\Academics\asset_tracker\`. Toolchain CMake, GCC. Heap 0x200, stack 0x1000. Copy only necessary library files. Peripheral init as .c/.h pairs. Keep user code. Free pins set to analog. STM32Cube FW_F4 V1.28.3 |

### 4.2 Things Claude Code must add in code
- **TIM5 CH4 input capture** for LSI calibration: configure CH4 (rising edge, direct, IC prescaler DIV8) with `HAL_TIM_IC_ConfigChannel()` and call `HAL_TIMEx_RemapConfig(&htim5, TIM_TIM5_LSI)` in `App/services/rtc_time.c`. Do not route it to a pin.
- **RTC must not be reset on every boot.** The generated `MX_RTC_Init()` sets a default time and date. Use the `USER CODE BEGIN Check_RTC_BKUP` section to return early when backup register `RTC_BKP_DR0` holds the magic value (§6.9), so the calendar survives resets.
- **RTC wake-up timer after an early return.** EXTI line 22 settings do not survive a system reset, but the RTC registers do. If `MX_RTC_Init()` returns early, the wake-up timer configuration is skipped. So `rtc_time_init()` (called from `app_init()`) must always call `HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, RTC_WAKEUP_PERIOD_S - 1, RTC_WAKEUPCLOCK_CK_SPRE_16BITS)` to re-arm the timer and EXTI 22.
- **RTC wakes the MCU every 10 s from the start.** Until IWDG is added in Phase 5, an RTC-only wake simply goes straight back to STOP (§6.10 step 9). Do not disable it.
- I2C speed change to 400 kHz in Phase 4 is done in code (re-init with a new `ClockSpeed`), not in CubeMX.

---

## 5. Repository rules for Claude Code

1. **Generated code**: edit only inside `/* USER CODE BEGIN */ ... /* USER CODE END */` blocks in `Core/` files. Everything else there is overwritten on regeneration.
2. **All application code lives in `App/`.** `main.c` calls `app_init()` and then `app_run()` (never returns) inside `USER CODE BEGIN 2`.
3. **CMake**: the root `CMakeLists.txt` is user-owned. Add `App/` sources and include paths there (in the marked user sections). Never edit `cmake/stm32cubemx/` — it is regenerated.
4. Debug/Release defines: add `$<$<CONFIG:Debug>:APP_DEBUG=1>` via `target_compile_definitions` in the root CMakeLists.
5. **Never** reconfigure PA13/PA14, never call anything that disables SWD, never write option bytes except the BOR step in §6.10.
6. One phase at a time (§8). After every change: build with zero warnings, flash, then **tell the user exactly what to check on the hardware** (what the OLED should show, which keys to press). You cannot see the device; ask the user to report results before moving on.
7. Ask before anything destructive: erasing the whole FRAM, changing option bytes, mass-erasing flash.
8. Keep a short `CHANGELOG.md` per phase. Commit to git at the end of each phase.

### Coding conventions
- C11, `-Wall -Wextra`, zero warnings. No dynamic allocation. No `printf` in Release.
- `stdint.h` types. Packed on-FRAM structs with `_Static_assert` on their sizes.
- Every tunable value (timeouts, thresholds, addresses, PIN) goes in `App/config.h` with a comment and units.
- Datasheet-derived constants are named, with a comment citing document and section/table.
- ISRs only set flags or push events. All work happens in the main loop.
- Every I2C call checks its return value. Errors propagate as `status_t` enums, never silently ignored.

### Build / flash / attach (verify CLI syntax with `STM32_Programmer_CLI --help` the first time)
```
cmake --preset Debug && cmake --build --preset Debug
STM32_Programmer_CLI -c port=SWD -w build/Debug/<project>.elf -v -rst
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG          # attach without reset
```

---

## 6. Firmware architecture

### 6.1 Layout
```
<project>.ioc, CMakeLists.txt, CMakePresets.json   (generated by CubeMX; root CMakeLists is user-owned)
cmake/stm32cubemx/         regenerated by CubeMX — never edit
Core/, Drivers/            generated — edit only inside USER CODE blocks
App/
  config.h                 all tunables
  app_main.c/.h            app_init(), app_run(): event loop + sleep decision
  app_sm.c/.h              application state machine
  ui.c/.h                  screen rendering (one function per screen)
  drivers/
    i2c_bus.c/.h           wrapper over HAL I2C: timeouts, retries, bus recovery
    pn532.c/.h             PN532 over I2C
    ssd1306.c/.h           OLED driver + 1 KB framebuffer, dirty-page flush
    font.c/.h              6x8 and 8x16 bitmap fonts (permissive/public-domain licence, noted in file)
    fram.c/.h              MB85RC256V read/write
    keypad.c/.h            matrix scan, debounce, wake config
  services/
    storage.c/.h           log in FRAM: headers, append, recovery, clear
    students.c/.h          UID → student lookup (binary search)
    power.c/.h             STOP entry/exit, debug-mode handling
    battery.c/.h           VDD via VREFINT
    rtc_time.c/.h          RTC <-> Unix time, LSI calibration, sync handling
    export.c/.h            export buffer + time-sync mailbox
    trace.c/.h             RAM ring-buffer debug log (Debug builds only)
  data/
    students_table.c       GENERATED by tools/gen_students.py — do not edit
tools/
  students.csv             uid,student_id,name (sample rows with dummy data)
  gen_students.py          CSV → App/data/students_table.c (validated, sorted by UID)
  export_log.py            SWD export + time sync + CSV output (§7)
docs/datasheets/
```

### 6.2 Main loop and events
- Events: `EV_KEY_DOWN(key)`, `EV_KEY_UP(key)`, `EV_TICK` (every 10 ms while awake), `EV_RTC_WAKE`.
- ISRs (EXTI rows, RTC wake-up) only set flags. The loop converts flags into events, runs the state machine, and then:
  - if the state is `ST_SLEEP` and no events are pending → `power_enter_stop()`
  - otherwise → `__WFI()` until the next SysTick (Sleep mode, not STOP)

### 6.3 State machine

| State | Entry action | Exit conditions |
|---|---|---|
| `ST_SLEEP` | OLED off, PN532 PowerDown, keypad wake config | Key → check battery → `ST_SCAN`. `D` held 3 s → `ST_ADMIN_PIN`. RTC wake → kick IWDG, stay asleep |
| `ST_SCAN` | OLED on, "Tap student card", start NFC duty-cycled polling | Card found → lookup. Registered → `ST_STUDENT`. Unregistered → `ST_MSG` "Unregistered card" + UID hex (3 s). `*` or 10 s timeout → `ST_SLEEP` |
| `ST_STUDENT` | Show name (truncated) + student ID, PN532 PowerDown | 1.5 s → `ST_ASSET` |
| `ST_ASSET` | "Asset ID:" with entry field | Digits append (max 8). `*` deletes, `*` when empty cancels → `ST_SLEEP`. `#` with ≥1 digit → save. 20 s idle → `ST_SLEEP` |
| (save) | `storage_append()` | OK → `ST_MSG` "Saved #<seq>" (1.5 s). Log full → "Log full – export". Error → "Save failed" |
| `ST_MSG` | Show message | Timeout → `ST_SLEEP` |
| `ST_ADMIN_PIN` | "Admin PIN" (masked) | Correct PIN → `ST_ADMIN_MENU`. Wrong → `ST_MSG`. 20 s → `ST_SLEEP` |
| `ST_ADMIN_MENU` | 1 Export, 2 Clear log, 3 Set time, 4 Info, 5 Read UID | Selection. `*` or 30 s → `ST_SLEEP` |
| `ST_EXPORT` | See §7 | `*` or 10 min → `ST_SLEEP` |
| `ST_CLEAR_CONFIRM` | "Clear N records? #=yes" | `#` twice → clear. `*` → menu |
| `ST_SET_TIME` | Enter `YYMMDDhhmm` (local time) | `#` → set RTC, log sync record. `*` → menu |
| `ST_INFO` | Records n/capacity, VDD mV, LSI Hz, time, FW version | Any key → menu |
| `ST_READ_UID` | Poll NFC, show UID of any card (for enrolling students) | `*` → menu |

- The key that wakes the device is consumed and not used as input, except for the long-press `D` admin check.
- Battery check on every wake, before NFC is powered (§6.8).
- Low-battery warning icon on every screen while VDD < 3.25 V.

### 6.4 Keypad driver
- **Sleep config**: all four columns driven low, rows input pull-up with EXTI falling enabled. Clear all four EXTI pending bits just before STOP.
- **Awake**: on the first EXTI, disable the row EXTIs and scan every 10 ms. For each column: drive only that column low, others high, wait ~5 µs, read rows.
- Debounce: a key state must be stable for 20 ms. Emit `KEY_DOWN` / `KEY_UP`. Single key at a time; ignore multi-key combinations.
- Long-press detection for `D` (3 s) lives in the state machine, based on DOWN/UP timing.
- Before re-entering sleep, wait until all keys are released.

### 6.5 PN532 driver (verify all command details against NXP UM0701-02)
- I2C address 0x24. Write frame: `00 00 FF LEN LCS D4 CMD data… DCS 00`.
- Reads start with a status byte; `0x01` = ready. Poll by reading 1 byte every 2 ms until ready or timeout. Then read the ACK frame `00 00 FF 00 FF 00`, poll again, read the response (TFI `D5`, CMD+1).
- The first transaction after PowerDown wakes the chip and may fail. Retry once after ~2 ms.
- Commands used:
  - `GetFirmwareVersion` (0x02): bring-up and health check.
  - `SAMConfiguration` (0x14): normal mode, IRQ not used.
  - `RFConfiguration` (0x32): item 0x01 to switch the RF field on/off; item 0x05 MaxRetries with a small `MxRtyPassiveActivation` so each `InListPassiveTarget` returns quickly.
  - `InListPassiveTarget` (0x4A): 1 target, 106 kbps type A. Return UID (4 or 7 bytes).
  - `PowerDown` (0x16): wake-up source I2C only (RF level detector off — it costs extra current). Verify the WakeUpEnable bit mapping in UM0701-02.
- **Duty-cycled scan** (biggest active-power lever, RF field ≈ 90 mA at 5V per datasheet): every `NFC_ATTEMPT_PERIOD_MS` (300) do one short `InListPassiveTarget`, then switch the field off and idle with `__WFI()` until the next attempt. Put the PN532 into PowerDown whenever the scan state is left.
- Boot: `GetFirmwareVersion` → `SAMConfiguration` → PowerDown. If firmware version fails, show "NFC error" and continue (the device can still export).

### 6.6 SSD1306 driver
- Standard init sequence for 128x64, with contrast set low (`CONTRAST` in `config.h`, start at 0x40) to save power.
- 1 KB framebuffer in RAM. Track dirty pages and flush only those.
- `ssd1306_sleep()`: display off (0xAE) then charge pump off (0x8D, 0x10). `ssd1306_wake()`: charge pump on (0x8D, 0x14), display on (0xAF).
- UI style: dark background, small amount of lit text. Avoid filled areas (current scales with lit pixels).

### 6.7 Storage (FRAM log)

FRAM map:

| Range | Use |
|---|---|
| 0x0000–0x003F | Header copy A |
| 0x0040–0x007F | Header copy B |
| 0x0080–0x00BF | Reserved |
| 0x00C0–0x00FF | Bring-up test scratch area |
| 0x0100–0x7FFF | Records: 1354 × 24 bytes |

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;        // 'ATRK'
    uint16_t version;      // 1
    uint16_t record_size;  // 24
    uint32_t generation;   // incremented on every header write
    uint32_t record_count;
    uint32_t next_seq;
    uint32_t reserved[8];
    uint32_t crc32;        // over all preceding bytes
} log_header_t;            // static_assert size <= 64

typedef struct __attribute__((packed)) {
    uint8_t  type;         // 0xA1 = scan, 0xA2 = time sync (never 0x00/0xFF: blank FRAM)
    uint8_t  flags;        // bit0 = time_valid (RTC had been set when recorded)
    uint8_t  uid_len;      // 4 or 7; 0 for sync records
    uint8_t  uid[7];
    uint32_t seq;
    uint32_t rtc_time;     // device RTC, Unix seconds UTC. Sync: RTC value just BEFORE it was set
    uint32_t value;        // scan: asset ID. Sync: the new time that was set
    uint16_t crc16;        // CRC-16/CCITT-FALSE over bytes 0..21
} log_record_t;            // static_assert size == 24
```

- **Append**: write the record at index `record_count`, read it back and verify, then write the header to the copy with the older generation (A/B alternate).
- **Boot recovery**: choose the header copy with a valid CRC and the higher generation. Then check whether the slot at `record_count` holds a valid record with `seq == next_seq` (record written, header not yet updated) and roll forward if so. If neither header is valid, check that the record area is unused before initialising, and ask the user before formatting a non-empty FRAM.
- **Full**: at ≥ 90% show a warning after each save. At 100% refuse new scans ("Log full – export"). Never overwrite records.
- **Clear**: write a new header with `record_count = 0`; `next_seq` keeps counting so sequence numbers stay unique.
- CRC-32 is software IEEE (zlib-compatible), so Python can verify it with `zlib.crc32`.

### 6.8 Battery (VREFINT)
- `VDD_mV = 3300 * VREFINT_CAL / raw`, with `VREFINT_CAL` read from 0x1FFF7A2A (STM32F411 datasheet Table 75; measured at VDDA = 3.3 V, 30 °C).
- Average 8 samples at 480-cycle sampling time (VREFINT needs ≥ 10 µs).
- Measure with NFC and OLED quiet. After measuring, stop the ADC and clear `TSVREFE` so the reference doesn't draw current during sleep.
- Thresholds (`config.h`): ≥ 3250 mV OK. 3000–3250 mV: show "Battery low" and continue. < 3000 mV: show "Charge battery" for 3 s and go back to sleep without scanning.
- While the LDO regulates (battery above ~3.35 V), VDD reads ~3300 mV. This only detects the last ~10% of charge, which is intended.

### 6.9 RTC and time
- The RTC stores UTC. Convert to/from Unix seconds in `rtc_time.c`. The display shows local time using `TZ_OFFSET_MIN` (+330 for Sri Lanka).
- First boot (backup register `RTC_BKP_DR0` not set to a magic value): set 2026-01-01 00:00 UTC, mark time invalid, write the magic. On later boots, do **not** re-init the calendar.
- `time_valid` is kept in a backup register and set by any time sync. Records copy it into `flags`.
- **LSI calibration**: TIM5 CH4 input capture with `HAL_TIMEx_RemapConfig(&htim5, TIM_TIM5_LSI)`. Measure the period over 8 LSI edges at 16 MHz, compute `lsi_hz`, then set the RTC prescalers (async 127, sync = `lsi_hz/128 - 1`) without resetting the calendar. Run at boot and on wake if more than 1 h has passed since the last calibration. HSI is factory-trimmed to ~1%, so expect RTC error of ~1–2%. The export script corrects the rest (§7).
- Every time sync (from the script or the admin menu) appends a type 0xA2 record.

### 6.10 Power manager

**`power_enter_stop()`** — in this order:
1. OLED sleep, PN532 PowerDown (if not already done).
2. De-initialise I2C1 (its pins become analog).
3. Keypad sleep config. Clear row EXTI pending bits.
4. Stop the ADC. Make sure TIM5 is stopped.
5. Clear the RTC wake-up flag and EXTI line 22 pending bit.
6. `HAL_SuspendTick()`.
7. `HAL_PWREx_EnableFlashPowerDown()`, `HAL_PWREx_EnableLowRegulatorLowVoltage()`, then `HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI)`.
8. On wake: SYSCLK is already HSI 16 MHz (no PLL is used). `HAL_ResumeTick()`. Re-init I2C1 only if the wake source is a key.
9. Only an RTC wake: kick IWDG and go back to sleep straight away, without touching any peripheral.

Datasheet reference (STM32F411 Table 28): STOP with LP-LV regulator and flash deep power-down ≈ 10 µA typical at 25 °C. Wake-up time ≈ 113 µs (Table 34).

**Debug vs Release:**
- `APP_DEBUG` builds: enable `DBGMCU` STOP/SLEEP/STANDBY debug bits so the debugger stays attached, and wait 3 s after reset before the first STOP.
- Release builds: clear the `DBGMCU` bits except in export mode.

**BOR (Phase 5)**: at boot, check the option bytes. If the BOR level is not Level 3, program `OB_BOR_LEVEL3` with `HAL_FLASHEx_OBProgram` and launch. This is a one-time action; the device resets. Datasheet: VBOR3 rising ≈ 2.92 V typical. It protects FRAM writes (FRAM minimum is 2.7 V). **Ask the user before running this the first time.**

**IWDG (Phase 5)**: prescaler 256, reload 4095 (≈ 32 s at 32 kHz LSI). RTC wake-up every 10 s during sleep to kick it. IWDG and the RTC wake-up timer both run from LSI, so their ratio stays the same even when the LSI is off-nominal. Freeze IWDG while the core is halted (`__HAL_DBGMCU_FREEZE_IWDG()`). Kick it in the main loop and inside any blocking wait longer than 1 s.

### 6.11 Students
- `tools/students.csv` columns: `uid` (hex, 8 or 14 chars), `student_id`, `name`.
- `gen_students.py` validates the file (unique UIDs, valid hex, name ≤ 20 chars), sorts it by UID, and writes a `const` table to `App/data/students_table.c`.
- Build integration: re-run the script automatically when the CSV changes (CMake `add_custom_command`), or document it as a manual step if that gets complicated.
- Admin menu "5 Read UID" lets the user tap cards to collect UIDs for the CSV.

### 6.12 I2C robustness
- On timeout or BUSY: de-init, drive SCL as GPIO open-drain for 9 pulses with SDA released, generate a STOP, apply `I2C_CR1_SWRST`, then re-init. This also covers the STM32F4 I2C stuck-BUSY erratum.
- Retry the failed transaction once.
- Count errors in the trace buffer.

---

## 7. Export mode and host script

### 7.1 Device side
- Entering `ST_EXPORT`:
  - enable the `DBGMCU` SLEEP/STOP bits
  - copy the header and all records from FRAM into `g_export`
  - show on the OLED: "EXPORT MODE", record count, `&g_export` address, "* to exit"
  - idle with `__WFI()`; no STOP mode while in export mode
```c
typedef struct {
    uint32_t magic;          // 'EXPT'
    uint16_t version;        // 1
    uint16_t record_size;    // 24
    uint32_t generation;     // incremented on every refresh
    uint32_t record_count;
    uint32_t capacity;
    uint32_t device_rtc_now; // Unix seconds
    uint32_t lsi_hz;
    uint16_t vdd_mv;
    uint16_t fw_version;
    uint32_t crc32;          // over records[0..record_count)
    log_record_t records[LOG_CAPACITY];
} export_buffer_t;

extern export_buffer_t g_export;                 // ~32.5 KB of RAM, static
extern volatile struct { uint32_t magic; uint32_t host_unix; } g_time_sync_mailbox;
```
- The main loop polls `g_time_sync_mailbox` in export mode. When `magic == 'TIME'`:
  1. append a sync record (`rtc_time` = current RTC, `value` = `host_unix`)
  2. set the RTC to `host_unix` and mark time valid
  3. clear `magic`
  4. refresh `g_export` (generation + 1)
  5. show "Time synced"

### 7.2 `tools/export_log.py`
1. Find the addresses of `g_export` and `g_time_sync_mailbox` from the ELF with `arm-none-eabi-nm`. Take the ELF path as an argument; default to the latest Release build.
2. Connect with `STM32_Programmer_CLI -c port=SWD mode=HOTPLUG`.
3. Write `host_unix` (current UTC), then write the magic (two 32-bit writes, in that order).
4. Poll the header (upload 64 bytes) until `generation` changes. Timeout 5 s.
5. Upload the full buffer. Check the magic and the CRC-32.
6. Decode the records. Join with `students.csv` for names.
7. **Drift correction**: sync record *i* sets the clock to H_i. The next sync record *i+1* says the device clock read D_{i+1} just before being set to H_{i+1}. For records between them: `corrected = H_i + (rtc - H_i) * (H_{i+1} - H_i) / (D_{i+1} - H_i)`. After the last sync, extrapolate with the last scale factor and mark the rows "extrapolated". Records with `time_valid == 0` are marked "unsynced".
8. Write `asset_log_<YYYYmmdd_HHMM>.csv`: `seq, uid, student_id, name, asset_id, device_time_utc, corrected_local_time, time_quality`.
9. Print a summary. The script never clears the log; clearing is done on the device.

Manual fallback for the user: in CubeIDE, attach to the running target, then open `g_export` in the Expressions view or `&g_export` in the Memory view.

---

## 8. Phases and acceptance criteria

Finish and get user confirmation for each phase before starting the next.

**Phase 0 — Skeleton**: `App/` structure, CMake integration of `App/` sources, `app_init()/app_run()` hooks in `main.c` USER CODE blocks, `config.h`, trace buffer, RTC early-return in `Check_RTC_BKUP`. Test: build passes with zero warnings, flash succeeds, the user can halt in the debugger and see `SystemCoreClock == 16000000` and a counter incrementing in Live Expressions.

**Phase 1 — Bring-up test** (`APP_MODE_BRINGUP` in `config.h`):
- OLED shows "Hello" (try 0x3C, then 0x3D).
- I2C scan lists 0x24, 0x3C, 0x50.
- Pressing each key shows its label. The user confirms all 16 match.
- FRAM test writes, reads back and verifies a pattern in the scratch area 0x00C0–0x00FF.
- PN532 firmware version is shown. Tapping a card shows its UID.
- VDD in mV is shown.

**Phase 2 — Drivers**: final APIs for i2c_bus (with recovery), pn532, ssd1306, fram, keypad. Error paths tested by unplugging the NFC module (the device must not hang).

**Phase 3 — Application**: state machine, UI, storage with header A/B and recovery, student table generation, admin menu (all items except Export). Low-power modes are not used yet (`__WFI()` only).
- Test: full transaction; unregistered card; asset entry editing; timeouts; reset during save (user presses SW2 right after `#`), then confirm no corruption and no lost earlier records.

**Phase 4 — Low power**: STOP mode, keypad wake, PN532 PowerDown, OLED sleep, NFC duty cycling, 400 kHz I2C, I2C pins analog in sleep, Release build behaviour.
- Test: 50 wake/sleep cycles with no hang; keys work right after wake.

**Phase 5 — Export, time, safety**: export mode, mailbox time sync, `export_log.py` with drift correction, LSI calibration, battery thresholds, BOR level 3 (ask first), IWDG with RTC wake kicks.
- Test: export → CSV matches the device's info screen; after a sync, timestamps are correct; IWDG doesn't fire during a long session or overnight sleep.

**Phase 6a — Move to the custom PCB**: set `APP_BOARD_BLACKPILL 0`. The user confirms +5 V reads 5.0–5.1 V with no modules connected (boost trimmer R1 adjusted). Rerun Phase 1 bring-up on the PCB, then the Phase 3–5 tests. Note any differences.

**Phase 6 — Hardening**: log-full behaviour, I2C fault injection, review all timeouts, stack usage check (`-fstack-usage`), final `README.md` for users (how to use the device, how to export, how to add students).

---

## 9. Recovery and debugging

- **Debug builds** stay attachable in STOP mode (DBGMCU bits).
- **Release firmware won't connect** (core in STOP, debug bits off):
  - Option 1: wake the device, enter export mode (admin → 1), then connect.
  - Option 2: hold SW1 (BOOT0), press and release SW2 (reset), release SW1. The ROM bootloader runs instead of the firmware. Connect normally, flash, then press SW2.
- `mode=HOTPLUG` attaches without resetting. Use it for export. A normal connect resets the core via software and works whenever the core is awake.
- There is no SWO pin (PB3 is not routed), so SWV/printf over SWO is unavailable. Use the `trace` RAM buffer and watch it in CubeIDE Live Expressions.

---

## 10. Power budget reference (datasheet typicals, not measured)

| Sleep contributor | Typical |
|---|---|
| TPS736 LDO ground current | ~400 µA |
| TPS61230A boost, enabled, light load | ~70–100 µA (estimate) |
| MB85RC256V standby | 27 µA |
| STM32F411 STOP (LP-LV regulator, flash power-down) | ~10 µA |
| PN532 soft power-down | 22 µA |
| OLED sleep + module regulator | small, unknown |
| Any power LED on the modules | unknown, possibly mA |

- Active: PN532 RF field ≈ 90 mA at 5V. OLED ≈ 10–20 mA at 5V depending on lit pixels. MCU at 16 MHz ≈ a few mA.
- Firmware priorities, in order:
  1. Minimise RF-on time.
  2. Minimise awake time (fast I2C, short timeouts).
  3. Keep the OLED off and dark.
  4. Use clean STOP entry.

---

## 11. Assumptions to confirm with the user when they come up

- The OLED is a 128x64 SSD1306. Address is detected at runtime; resolution is set in `config.h`.
- The PN532 module's mode switches are set to I2C.
- The FRAM WP pin is tied low. If FRAM writes fail but reads work, check WP.
- Keypad ribbon orientation (confirmed in Phase 1).
- `ADMIN_PIN` default is `"1234"` in `config.h`. The user must change it.
- Cards are ISO14443A (MIFARE) and only the UID is used. Nothing is read from card memory.
