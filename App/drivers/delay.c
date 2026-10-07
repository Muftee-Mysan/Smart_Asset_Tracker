/**
 * @file    delay.c
 * @brief   Busy-wait microsecond delay on the DWT cycle counter.
 */
#include "delay.h"
#include "stm32f4xx_hal.h"

void delay_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void delay_us(uint32_t us)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t cycles = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < cycles) {
    }
}
