/**
 * @file    keypad.h
 * @brief   4x4 matrix keypad (Parallax 27899): scan, debounce, events.
 *
 * Keys are reported as their label characters: '0'..'9', 'A'..'D', '*', '#'.
 */
#ifndef APP_KEYPAD_H
#define APP_KEYPAD_H

#include <stdbool.h>
#include <stdint.h>

#define KEY_NONE    '\0'

typedef enum {
    KEY_EV_DOWN,
    KEY_EV_UP,
} key_event_type_t;

typedef struct {
    key_event_type_t type;
    char key;
} key_event_t;

/* Awake mode: row EXTIs off, columns open-drain released, scanning via
 * keypad_tick(). */
void keypad_awake_config(void);

/* Sleep mode (CLAUDE.md 6.4): all columns driven low, row EXTIs (falling
 * edge, set up by CubeMX) enabled with pending bits cleared, so any key
 * press wakes the MCU. */
void keypad_sleep_config(void);

/* True when no key is pressed (checked with all columns low). */
bool keypad_all_released(void);

/* Call every TICK_PERIOD_MS. Returns true and fills *ev when a debounced
 * key goes down or up. Multi-key presses are ignored. */
bool keypad_tick(key_event_t *ev);

/* Immediate (not debounced) scan: the single pressed key, or KEY_NONE. */
char keypad_scan_raw(void);

#endif /* APP_KEYPAD_H */
