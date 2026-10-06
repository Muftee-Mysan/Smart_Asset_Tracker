/**
 * @file    rtc_time.h
 * @brief   RTC calendar <-> Unix time, first-boot handling, wake-up timer.
 *
 * The RTC holds UTC. LSI calibration (TIM5 CH4 capture) is added in Phase 5.
 */
#ifndef APP_RTC_TIME_H
#define APP_RTC_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"

/* Called from MX_RTC_Init() (USER CODE Check_RTC_BKUP). True when the backup
 * register holds the magic, i.e. the calendar was set on an earlier boot and
 * must not be re-initialised. */
bool rtc_time_is_initialised(void);

/* Called from MX_RTC_Init() (USER CODE RTC_Init 2), only on first boot:
 * sets the first-boot date, clears time_valid and writes the magic.
 * Errors go to Error_Handler(), like the surrounding generated code. */
void rtc_time_first_boot(void);

/* True if this boot ran rtc_time_first_boot(). */
bool rtc_time_was_first_boot(void);

/* Re-arms the RTC wake-up timer and EXTI line 22. Must run on every boot,
 * because MX_RTC_Init() returns before configuring them when the calendar
 * was already set (CLAUDE.md section 4.2). */
status_t rtc_time_init(void);

/* Returns and clears the "RTC wake-up fired" flag set by the ISR callback. */
bool rtc_time_take_wake_flag(void);

/* Current RTC time as Unix seconds (UTC). */
status_t rtc_time_now_unix(uint32_t *unix_out);

#endif /* APP_RTC_TIME_H */
