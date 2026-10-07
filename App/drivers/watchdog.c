/**
 * @file    watchdog.c
 * @brief   Independent watchdog (IWDG), CLAUDE.md 6.10.
 *
 * Direct register access: the HAL IWDG module is not enabled in CubeMX.
 * Key values from RM0383 (IWDG_KR): 0xCCCC start, 0x5555 unlock PR/RLR,
 * 0xAAAA reload.
 */
#include "watchdog.h"
#include "config.h"
#include "stm32f4xx_hal.h"

#define IWDG_KEY_START      0xCCCCU
#define IWDG_KEY_UNLOCK     0x5555U
#define IWDG_KEY_RELOAD     0xAAAAU
#define IWDG_PR_DIV256      6U          /* PR[2:0] = 110: /256 */
#define IWDG_UPDATE_WAIT    100000U     /* loop bound while PVU/RVU are set */

static bool s_iwdg_reset;

void watchdog_start(void)
{
    s_iwdg_reset = (RCC->CSR & RCC_CSR_IWDGRSTF) != 0U;

    __HAL_DBGMCU_FREEZE_IWDG();
    IWDG->KR = IWDG_KEY_START;
    IWDG->KR = IWDG_KEY_UNLOCK;
    IWDG->PR = IWDG_PR_DIV256;
    IWDG->RLR = IWDG_RELOAD;
    for (uint32_t i = 0U; (i < IWDG_UPDATE_WAIT) && (IWDG->SR != 0U); i++) {
    }
    IWDG->KR = IWDG_KEY_RELOAD;
}

void watchdog_kick(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

bool watchdog_caused_reset(void)
{
    return s_iwdg_reset;
}
