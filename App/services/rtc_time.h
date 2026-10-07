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

/* Sets the RTC calendar to `unix` (UTC, 2000..2099). Does not touch the
 * time_valid flag. */
status_t rtc_time_set_unix(uint32_t unix);

/* time_valid flag (backup register): set by any time sync, survives resets. */
bool rtc_time_is_valid(void);
void rtc_time_mark_valid(void);

typedef struct {
    uint16_t year;      /* 2000..2099 */
    uint8_t  month;     /* 1..12 */
    uint8_t  day;       /* 1..31 */
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} datetime_t;

/* Unix seconds -> calendar fields, shifted by offset_min (e.g. TZ_OFFSET_MIN
 * for local time, 0 for UTC). */
void rtc_time_to_datetime(uint32_t unix, int32_t offset_min, datetime_t *dt);

/* Calendar fields given in a zone offset_min east of UTC -> Unix seconds UTC.
 * STATUS_INVALID if a field is out of range. */
status_t rtc_time_from_datetime(const datetime_t *dt, int32_t offset_min, uint32_t *unix);

#endif /* APP_RTC_TIME_H */
