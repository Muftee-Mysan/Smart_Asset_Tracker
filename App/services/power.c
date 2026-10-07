/**
 * @file    power.c
 * @brief   Power manager: debug-mode handling and STOP mode (CLAUDE.md 6.10).
 *
 * STOP entry order (6.10): 1 OLED sleep + PN532 PowerDown (done by the state
 * machine on entering ST_SLEEP), 2 I2C1 off with pins analog, 3 keypad sleep
 * config with row EXTI pending bits cleared, 4 ADC and TIM5 stopped (both
 * are only ever run briefly and left off), 5 RTC wake-up flag and EXTI 22
 * cleared, 6 SysTick suspended, 7 flash power-down + low-power regulator in
 * low-voltage mode, then STOP via WFI.
 * Typical STOP current with LP-LV regulator and flash power-down: ~10 uA at
 * 25 degC (STM32F411 datasheet, Table 28, per CLAUDE.md 6.10).
 * Exit: SYSCLK is HSI 16 MHz again (no PLL is used), so only the tick needs
 * resuming.
 */
#include "power.h"
#include "config.h"
#include "i2c_bus.h"
#include "keypad.h"
#include "rtc.h"
#include "rtc_time.h"
#include "stm32f4xx_hal.h"
#include "watchdog.h"

static uint32_t s_stops;
static uint32_t s_key_wakes;
static uint32_t s_rtc_wakes;

void power_debug_config(void)
{
#if APP_DEBUG
    /* DBGMCU_CR DBG_SLEEP/DBG_STOP/DBG_STANDBY keep the debug clocks running
     * in low-power modes (RM0383, "Debug MCU configuration register"
     * DBGMCU_CR). */
    HAL_DBGMCU_EnableDBGSleepMode();
    HAL_DBGMCU_EnableDBGStopMode();
    HAL_DBGMCU_EnableDBGStandbyMode();
#else
    HAL_DBGMCU_DisableDBGSleepMode();
    HAL_DBGMCU_DisableDBGStopMode();
    HAL_DBGMCU_DisableDBGStandbyMode();
#endif
}

void power_debug_enable(void)
{
    HAL_DBGMCU_EnableDBGSleepMode();
    HAL_DBGMCU_EnableDBGStopMode();
    HAL_DBGMCU_EnableDBGStandbyMode();
}

uint8_t power_check_bor(void)
{
    FLASH_OBProgramInitTypeDef ob = {0};
    HAL_FLASHEx_OBGetConfig(&ob);
#if APP_SET_BOR_LEVEL3
    /* VBOR3 ~2.92 V rising protects FRAM writes (MB85RC256V min 2.7 V). */
    if (ob.BORLevel != OB_BOR_LEVEL3) {
        FLASH_OBProgramInitTypeDef prog = {0};
        prog.OptionType = OPTIONBYTE_BOR;
        prog.BORLevel = OB_BOR_LEVEL3;
        if ((HAL_FLASH_Unlock() == HAL_OK) && (HAL_FLASH_OB_Unlock() == HAL_OK) &&
            (HAL_FLASHEx_OBProgram(&prog) == HAL_OK)) {
            (void)HAL_FLASH_OB_Launch();
            NVIC_SystemReset();             /* boot again with the new level */
        }
        (void)HAL_FLASH_OB_Lock();
        (void)HAL_FLASH_Lock();
    }
#endif
    return (uint8_t)ob.BORLevel;
}

bool power_stop_allowed(void)
{
#if APP_DEBUG
    return HAL_GetTick() >= DEBUG_STOP_DELAY_MS;
#else
    return true;
#endif
}

void power_sleep_until_key(void)
{
    i2c_bus_suspend();                          /* step 2 */
    keypad_sleep_config();                      /* step 3 (pending cleared) */
    (void)keypad_take_irq_flag();

    for (;;) {
        /* A key already held cannot produce a new falling edge: stay awake. */
        if (keypad_row_active()) {
            break;
        }

        /* Step 5: clear the RTC wake-up flag and EXTI line 22. */
        __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
        __HAL_RTC_WAKEUPTIMER_EXTI_CLEAR_FLAG();

        HAL_SuspendTick();                      /* step 6 */
        HAL_PWREx_EnableFlashPowerDown();       /* step 7 */
        HAL_PWREx_EnableLowRegulatorLowVoltage();
        s_stops++;
        HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
        HAL_ResumeTick();                       /* step 8 */

        if (keypad_take_irq_flag()) {
            break;                              /* key wake */
        }
        watchdog_kick();                        /* step 9: every wake kicks */
        if (rtc_time_take_wake_flag()) {
            s_rtc_wakes++;
        }
    }

    s_key_wakes++;
    keypad_awake_config();
    i2c_bus_resume();                           /* step 8: only on key wake */
}

uint32_t power_stop_count(void)
{
    return s_stops;
}

uint32_t power_key_wake_count(void)
{
    return s_key_wakes;
}

uint32_t power_rtc_wake_count(void)
{
    return s_rtc_wakes;
}
