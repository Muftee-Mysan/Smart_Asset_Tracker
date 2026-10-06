/**
 * @file    power.h
 * @brief   Power manager. Phase 0: debug-mode handling only.
 *          STOP entry/exit is added in Phase 4.
 */
#ifndef APP_POWER_H
#define APP_POWER_H

/* Debug builds: keep the debugger attached in SLEEP/STOP/STANDBY.
 * Release builds: clear those bits (export mode sets them again, Phase 5). */
void power_debug_config(void);

#endif /* APP_POWER_H */
