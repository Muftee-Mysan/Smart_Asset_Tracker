/**
 * @file    led.c
 * @brief   Black Pill user LED on PC13 (active low). No-ops on the custom PCB.
 */
#include "led.h"
#include "config.h"
#include "stm32f4xx_hal.h"

#if APP_BOARD_BLACKPILL

#define LED_PORT    GPIOC
#define LED_PIN     GPIO_PIN_13

void led_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);     /* off */
    GPIO_InitTypeDef gi = {0};
    gi.Pin = LED_PIN;
    gi.Mode = GPIO_MODE_OUTPUT_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gi);
}

void led_set(bool on)
{
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void led_toggle(void)
{
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
}

#else

void led_init(void) {}
void led_set(bool on) { (void)on; }
void led_toggle(void) {}

#endif
