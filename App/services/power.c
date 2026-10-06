/**
 * @file    power.c
 * @brief   Power manager. Phase 0: debug-mode handling only.
 */
#include "power.h"
#include "config.h"
#include "stm32f4xx_hal.h"

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
