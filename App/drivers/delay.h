/**
 * @file    delay.h
 * @brief   Busy-wait microsecond delay on the DWT cycle counter.
 */
#ifndef APP_DELAY_H
#define APP_DELAY_H

#include <stdint.h>

/* Enables the DWT cycle counter. Safe to call more than once. */
void delay_init(void);

/* Busy-waits at least `us` microseconds. */
void delay_us(uint32_t us);

#endif /* APP_DELAY_H */
