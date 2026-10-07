/**
 * @file    app_main.h
 * @brief   Application entry points, called from main.c (USER CODE 2).
 */
#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdbool.h>
#include <stdint.h>

/* Live debug counters, for the debugger's Live Expressions view. */
typedef struct {
    uint32_t heartbeat;       /* +1 per main-loop tick (TICK_PERIOD_MS) */
    uint32_t rtc_wakes;       /* +1 per RTC wake-up (RTC_WAKEUP_PERIOD_S) */
    uint32_t key_irqs;        /* keypad row EXTI events seen by the main loop */
    uint32_t unix_time;       /* RTC time, Unix seconds UTC (refreshed 10x/s) */
    uint32_t reset_flags;     /* RCC_CSR reset flags captured at boot */
    uint32_t error_count;     /* errors reported by app code since boot */
    uint8_t  boot_from_bkup;  /* 1 = calendar kept from before the reset */
    uint32_t stop_count;      /* STOP entries */
    uint32_t key_wakes;       /* wakes from STOP by a key */
    uint32_t rtc_only_wakes;  /* RTC wakes that went straight back to STOP */
} app_dbg_t;

extern volatile app_dbg_t g_app_dbg;

void app_init(void);
void app_run(void) __attribute__((noreturn));

#endif /* APP_MAIN_H */
