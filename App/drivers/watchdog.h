/**
 * @file    watchdog.h
 * @brief   Independent watchdog (IWDG), CLAUDE.md 6.10.
 *
 * Clocked by LSI: prescaler 256, reload 4095 -> ~32 s at 32 kHz (22 s at the
 * 47 kHz datasheet maximum). The RTC wake-up timer also runs from LSI, so
 * the 10 s wake-ups that kick it during sleep keep the same ratio.
 * Once started the IWDG cannot be stopped (only a reset stops it).
 */
#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include <stdbool.h>

/* Starts the IWDG. In all builds the counter is frozen while the core is
 * halted by a debugger (DBGMCU_APB1_FZ DBG_IWDG_STOP). */
void watchdog_start(void);

/* Reloads the counter. Cheap; call from the main loop. */
void watchdog_kick(void);

/* True if the last reset was caused by the IWDG (read once at boot, before
 * the reset flags are cleared). */
bool watchdog_caused_reset(void);

#endif /* APP_WATCHDOG_H */
