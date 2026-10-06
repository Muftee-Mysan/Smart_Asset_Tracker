/**
 * @file    rtc_time.c
 * @brief   RTC calendar <-> Unix time, first-boot handling, wake-up timer.
 */
#include "rtc_time.h"
#include "config.h"
#include "rtc.h"

/* Days between 0000-03-01 and 1970-01-01 in the proleptic Gregorian calendar
 * (H. Hinnant, "chrono-compatible low-level date algorithms"). */
#define DAYS_0000_03_01_TO_EPOCH    719468UL
#define DAYS_PER_400_YEARS          146097UL
#define SECONDS_PER_DAY             86400UL

static bool s_first_boot;
static volatile bool s_wake_flag;

bool rtc_time_is_initialised(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_IDX_MAGIC) == RTC_BKP_MAGIC;
}

void rtc_time_first_boot(void)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) {
        Error_Handler();
    }

    d.Year = RTC_FIRST_BOOT_YEAR;
    d.Month = RTC_FIRST_BOOT_MONTH;
    d.Date = RTC_FIRST_BOOT_DAY;
    d.WeekDay = RTC_FIRST_BOOT_WEEKDAY;
    if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) {
        Error_Handler();
    }

    /* Backup-domain write access was enabled by HAL_RCCEx_PeriphCLKConfig()
     * in HAL_RTC_MspInit(). Write the magic last, so an interrupted first
     * boot is simply repeated. */
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_IDX_TIME_VALID, 0U);
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_IDX_MAGIC, RTC_BKP_MAGIC);
    s_first_boot = true;
}

bool rtc_time_was_first_boot(void)
{
    return s_first_boot;
}

status_t rtc_time_init(void)
{
    /* WUCKSEL = ck_spre (1 Hz), so the counter is the period in seconds - 1.
     * Also enables the wake-up interrupt and EXTI line 22 (rising edge). */
    return status_from_hal(HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, RTC_WAKEUP_PERIOD_S - 1U,
                                                       RTC_WAKEUPCLOCK_CK_SPRE_16BITS));
}

bool rtc_time_take_wake_flag(void)
{
    if (!s_wake_flag) {
        return false;
    }
    s_wake_flag = false;
    return true;
}

/* Days since 1970-01-01 for a date in 2000..2099 (Hinnant's days_from_civil,
 * reduced to unsigned arithmetic because the year is never before 2000). */
static uint32_t days_from_civil(uint32_t y, uint32_t m, uint32_t d)
{
    if (m <= 2U) {
        y -= 1U;
    }
    const uint32_t era = y / 400U;
    const uint32_t yoe = y - era * 400U;                          /* [0, 399] */
    const uint32_t mp = (m > 2U) ? (m - 3U) : (m + 9U);           /* March = 0 */
    const uint32_t doy = (153U * mp + 2U) / 5U + d - 1U;          /* [0, 365] */
    const uint32_t doe = yoe * 365U + yoe / 4U - yoe / 100U + doy; /* [0, 146096] */
    return era * DAYS_PER_400_YEARS + doe - DAYS_0000_03_01_TO_EPOCH;
}

status_t rtc_time_now_unix(uint32_t *unix_out)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    /* GetTime must be followed by GetDate: reading TR locks the shadow
     * registers until DR is read. */
    HAL_StatusTypeDef hs = HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    if (hs == HAL_OK) {
        hs = HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
    }
    if (hs != HAL_OK) {
        return status_from_hal(hs);
    }

    const uint32_t days = days_from_civil(2000U + d.Year, d.Month, d.Date);
    *unix_out = days * SECONDS_PER_DAY + (uint32_t)t.Hours * 3600U
              + (uint32_t)t.Minutes * 60U + t.Seconds;
    return STATUS_OK;
}

/* HAL callback from RTC_WKUP_IRQHandler(). ISR: set a flag only. */
void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *h)
{
    (void)h;
    s_wake_flag = true;
}
