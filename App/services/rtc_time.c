/**
 * @file    rtc_time.c
 * @brief   RTC calendar <-> Unix time, first-boot handling, wake-up timer.
 */
#include "rtc_time.h"
#include "config.h"
#include "rtc.h"
#include "tim.h"

/* Days between 0000-03-01 and 1970-01-01 in the proleptic Gregorian calendar
 * (H. Hinnant, "chrono-compatible low-level date algorithms"). */
#define DAYS_0000_03_01_TO_EPOCH    719468UL
#define DAYS_PER_400_YEARS          146097UL
#define SECONDS_PER_DAY             86400UL

/* LSI calibration: TIM5 counts at 16 MHz (APB1 = HSI / 1, timer clock x1). */
#define TIM5_CLOCK_HZ               16000000UL
#define LSI_EDGES_PER_CAPTURE       8U      /* IC prescaler DIV8 */
#define RTC_ASYNC_PREDIV            127U
#define LSI_CAPTURE_TIMEOUT_MS      5U

static bool s_first_boot;
static uint32_t s_lsi_hz;
static uint32_t s_last_cal_unix;
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

static status_t wait_capture(uint32_t *ccr)
{
    const uint32_t start = HAL_GetTick();
    while (__HAL_TIM_GET_FLAG(&htim5, TIM_FLAG_CC4) == RESET) {
        if ((HAL_GetTick() - start) > LSI_CAPTURE_TIMEOUT_MS) {
            return STATUS_TIMEOUT;
        }
    }
    *ccr = HAL_TIM_ReadCapturedValue(&htim5, TIM_CHANNEL_4);   /* clears CC4IF */
    return STATUS_OK;
}

status_t rtc_time_calibrate_lsi(void)
{
    /* TIM5 CH4 input = LSI (internal remap, no pin), rising edge, capture
     * every 8th edge (CLAUDE.md 4.2). */
    TIM_IC_InitTypeDef ic = {0};
    ic.ICPolarity = TIM_ICPOLARITY_RISING;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV8;
    ic.ICFilter = 0U;
    status_t st = status_from_hal(HAL_TIMEx_RemapConfig(&htim5, TIM_TIM5_LSI));
    if (st == STATUS_OK) {
        st = status_from_hal(HAL_TIM_IC_ConfigChannel(&htim5, &ic, TIM_CHANNEL_4));
    }
    if (st == STATUS_OK) {
        st = status_from_hal(HAL_TIM_IC_Start(&htim5, TIM_CHANNEL_4));
    }

    uint32_t first = 0U;
    uint32_t last = 0U;
    if (st == STATUS_OK) {
        __HAL_TIM_CLEAR_FLAG(&htim5, TIM_FLAG_CC4);
        st = wait_capture(&first);
    }
    for (uint32_t i = 0U; (st == STATUS_OK) && (i < LSI_CAL_CAPTURES); i++) {
        st = wait_capture(&last);
    }
    (void)HAL_TIM_IC_Stop(&htim5, TIM_CHANNEL_4);   /* TIM5 off before sleep */
    if (st != STATUS_OK) {
        return st;
    }

    const uint32_t ticks = last - first;            /* 32-bit counter wraps fine */
    if (ticks == 0U) {
        return STATUS_INVALID;
    }
    const uint64_t edges = (uint64_t)LSI_EDGES_PER_CAPTURE * LSI_CAL_CAPTURES;
    const uint32_t hz = (uint32_t)((edges * TIM5_CLOCK_HZ + ticks / 2U) / ticks);
    if ((hz < LSI_MIN_HZ) || (hz > LSI_MAX_HZ)) {
        return STATUS_INVALID;
    }
    s_lsi_hz = hz;

    /* 1 Hz calendar: (async + 1) * (sync + 1) = LSI. Rewriting PRER enters
     * init mode, which restarts the sub-second counter, so only do it when
     * the value changes. */
    const uint32_t sync = (hz + (RTC_ASYNC_PREDIV + 1U) / 2U) / (RTC_ASYNC_PREDIV + 1U) - 1U;
    if (sync != hrtc.Init.SynchPrediv) {
        hrtc.Init.AsynchPrediv = RTC_ASYNC_PREDIV;
        hrtc.Init.SynchPrediv = sync;
        st = status_from_hal(HAL_RTC_Init(&hrtc));  /* keeps calendar and WUT */
    }
    if (st == STATUS_OK) {
        (void)rtc_time_now_unix(&s_last_cal_unix);
    }
    return st;
}

void rtc_time_calibrate_if_due(void)
{
    uint32_t now = 0U;
    if ((s_lsi_hz == 0U) ||
        ((rtc_time_now_unix(&now) == STATUS_OK) && ((now - s_last_cal_unix) >= LSI_CAL_PERIOD_S))) {
        (void)rtc_time_calibrate_lsi();
    }
}

uint32_t rtc_time_lsi_hz(void)
{
    return s_lsi_hz;
}

bool rtc_time_take_wake_flag(void)
{
    if (!s_wake_flag) {
        return false;
    }
    s_wake_flag = false;
    return true;
}

/* Calendar date from days since 1970-01-01 (Hinnant's civil_from_days,
 * unsigned because dates are never before 1970). */
static void civil_from_days(uint32_t days, uint32_t *y, uint32_t *m, uint32_t *d)
{
    const uint32_t z = days + DAYS_0000_03_01_TO_EPOCH;
    const uint32_t era = z / DAYS_PER_400_YEARS;
    const uint32_t doe = z - era * DAYS_PER_400_YEARS;                    /* [0, 146096] */
    const uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    const uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);      /* [0, 365] */
    const uint32_t mp = (5U * doy + 2U) / 153U;                           /* March = 0 */
    *d = doy - (153U * mp + 2U) / 5U + 1U;
    *m = (mp < 10U) ? (mp + 3U) : (mp - 9U);
    *y = yoe + era * 400U + ((*m <= 2U) ? 1U : 0U);
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

status_t rtc_time_set_unix(uint32_t unix)
{
    datetime_t dt;
    rtc_time_to_datetime(unix, 0, &dt);
    if ((dt.year < 2000U) || (dt.year > 2099U)) {
        return STATUS_INVALID;
    }
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    t.Hours = dt.hour;
    t.Minutes = dt.minute;
    t.Seconds = dt.second;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    d.Year = (uint8_t)(dt.year - 2000U);
    d.Month = dt.month;
    d.Date = dt.day;
    /* HAL weekday: Monday = 1 .. Sunday = 7; 1970-01-01 was a Thursday. */
    d.WeekDay = (uint8_t)(((unix / SECONDS_PER_DAY) + 3U) % 7U + 1U);
    HAL_StatusTypeDef hs = HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
    if (hs == HAL_OK) {
        hs = HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);
    }
    return status_from_hal(hs);
}

bool rtc_time_is_valid(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_IDX_TIME_VALID) == 1U;
}

void rtc_time_mark_valid(void)
{
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_IDX_TIME_VALID, 1U);
}

void rtc_time_to_datetime(uint32_t unix, int32_t offset_min, datetime_t *dt)
{
    const uint32_t t = (uint32_t)((int64_t)unix + (int64_t)offset_min * 60);
    const uint32_t days = t / SECONDS_PER_DAY;
    const uint32_t sod = t % SECONDS_PER_DAY;
    uint32_t y;
    uint32_t m;
    uint32_t d;
    civil_from_days(days, &y, &m, &d);
    dt->year = (uint16_t)y;
    dt->month = (uint8_t)m;
    dt->day = (uint8_t)d;
    dt->hour = (uint8_t)(sod / 3600U);
    dt->minute = (uint8_t)((sod / 60U) % 60U);
    dt->second = (uint8_t)(sod % 60U);
}

status_t rtc_time_from_datetime(const datetime_t *dt, int32_t offset_min, uint32_t *unix)
{
    static const uint8_t mdays[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if ((dt->year < 2000U) || (dt->year > 2099U) || (dt->month < 1U) || (dt->month > 12U) ||
        (dt->day < 1U) || (dt->day > mdays[dt->month - 1U]) || (dt->hour > 23U) ||
        (dt->minute > 59U) || (dt->second > 59U)) {
        return STATUS_INVALID;
    }
    /* 29 February only in leap years (every 4th year in 2000..2099). */
    if ((dt->month == 2U) && (dt->day == 29U) && ((dt->year % 4U) != 0U)) {
        return STATUS_INVALID;
    }
    const uint32_t local = days_from_civil(dt->year, dt->month, dt->day) * SECONDS_PER_DAY +
                           (uint32_t)dt->hour * 3600U + (uint32_t)dt->minute * 60U +
                           dt->second;
    *unix = (uint32_t)((int64_t)local - (int64_t)offset_min * 60);
    return STATUS_OK;
}

/* HAL callback from RTC_WKUP_IRQHandler(). ISR: set a flag only. */
void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *h)
{
    (void)h;
    s_wake_flag = true;
}
