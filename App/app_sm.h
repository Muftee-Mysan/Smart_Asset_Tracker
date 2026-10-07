/**
 * @file    app_sm.h
 * @brief   Application state machine (CLAUDE.md 6.3).
 */
#ifndef APP_SM_H
#define APP_SM_H

#include <stdbool.h>

typedef enum {
    EV_KEY_DOWN,    /* debounced key press; key = label */
    EV_KEY_UP,      /* debounced key release */
    EV_TICK,        /* every TICK_PERIOD_MS while awake */
    EV_RTC_WAKE,    /* RTC wake-up timer (every RTC_WAKEUP_PERIOD_S) */
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    char key;
} app_event_t;

/* Brings up the peripherals, shows the boot summary and enters ST_SLEEP
 * (or asks to format an unreadable log). */
void app_sm_init(void);

/* Runs one event through the state machine and flushes the display. */
void app_sm_handle(const app_event_t *ev);

/* True in ST_EXPORT: the main loop must not use WFI or STOP, so the host
 * can read RAM over SWD (deviation from CLAUDE.md 7.1, see CHANGELOG). */
bool app_sm_in_export(void);

/* True in ST_SLEEP (Phase 4 enters STOP mode there). */
bool app_sm_is_sleeping(void);

#endif /* APP_SM_H */
