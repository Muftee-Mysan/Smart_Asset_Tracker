/**
 * @file    app_main.c
 * @brief   app_init() and the main event loop.
 *
 * ISRs only set flags. The loop turns them into events (CLAUDE.md 6.2):
 * EV_RTC_WAKE, then every TICK_PERIOD_MS the debounced key events followed
 * by EV_TICK, each run through the state machine in order. Between ticks
 * the core idles in __WFI() (Release). STOP mode in ST_SLEEP is Phase 4.
 */
#include "app_main.h"
#include "app_sm.h"
#include "bringup.h"
#include "keypad.h"
#include "config.h"
#include "status.h"
#include "main.h"
#include "power.h"
#include "rtc_time.h"
#include "trace.h"

/* Refresh g_app_dbg.unix_time every this many ticks (100 ms). */
#define UNIX_TIME_REFRESH_TICKS  10U

volatile app_dbg_t g_app_dbg;

static volatile bool s_key_irq;

static void report_error(status_t st)
{
    g_app_dbg.error_count++;
    TRACE(TR_ERROR, st);
}

void app_init(void)
{
    trace_init();

    /* Capture and clear the reset cause (RCC_CSR bits 24..31). */
    g_app_dbg.reset_flags = RCC->CSR & 0xFF000000UL;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    TRACE(TR_BOOT, g_app_dbg.reset_flags);

    power_debug_config();

    g_app_dbg.boot_from_bkup = rtc_time_was_first_boot() ? 0U : 1U;
    if (rtc_time_was_first_boot()) {
        TRACE(TR_RTC_FIRST_BOOT, 0U);
    }

    status_t st = rtc_time_init();
    if (st != STATUS_OK) {
        report_error(st);
    }
}

void app_run(void)
{
#if APP_MODE == APP_MODE_BRINGUP
    bringup_run();
#endif

    app_sm_init();
    uint32_t last_tick = HAL_GetTick();

    for (;;) {
        if (rtc_time_take_wake_flag()) {
            g_app_dbg.rtc_wakes++;
            TRACE(TR_RTC_WAKE, g_app_dbg.rtc_wakes);
            const app_event_t ev = { EV_RTC_WAKE, KEY_NONE };
            app_sm_handle(&ev);
        }

        if (s_key_irq) {
            s_key_irq = false;
            g_app_dbg.key_irqs++;
            TRACE(TR_KEY_IRQ, g_app_dbg.key_irqs);
        }

        if ((HAL_GetTick() - last_tick) >= TICK_PERIOD_MS) {
            /* No catch-up: an NFC attempt can block for tens of ms. */
            last_tick = HAL_GetTick();
            g_app_dbg.heartbeat++;

            key_event_t kev;
            if (keypad_tick(&kev)) {
                const app_event_t ev = {
                    (kev.type == KEY_EV_DOWN) ? EV_KEY_DOWN : EV_KEY_UP, kev.key
                };
                app_sm_handle(&ev);
            }
            const app_event_t tick = { EV_TICK, KEY_NONE };
            app_sm_handle(&tick);

            if ((g_app_dbg.heartbeat % UNIX_TIME_REFRESH_TICKS) == 0U) {
                uint32_t now;
                status_t st = rtc_time_now_unix(&now);
                if (st == STATUS_OK) {
                    g_app_dbg.unix_time = now;
                } else {
                    report_error(st);
                }
            }
        }

#if APP_IDLE_USE_WFI
        /* Sleep until the next interrupt (SysTick at the latest, 1 ms). */
        __WFI();
#endif
    }
}

/* HAL callback from the keypad row EXTI handlers. ISR: set a flag only. */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    (void)GPIO_Pin;
    s_key_irq = true;
}
