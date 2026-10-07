/**
 * @file    led.h
 * @brief   Black Pill user LED on PC13 (active low). No-ops on the custom PCB.
 */
#ifndef APP_LED_H
#define APP_LED_H

#include <stdbool.h>

/* Configures PC13 as an output with the LED off. CubeMX leaves PC13 analog,
 * so this lives in App/ code (CLAUDE.md 3.5). */
void led_init(void);
void led_set(bool on);
void led_toggle(void);

#endif /* APP_LED_H */
