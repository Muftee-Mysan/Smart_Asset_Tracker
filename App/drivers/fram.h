/**
 * @file    fram.h
 * @brief   MB85RC256V FRAM (32 KB) over I2C.
 *
 * 2-byte memory address, MSB first. No write delay and no page boundary
 * (MB85RC256V datasheet, "Page write" / "Sequential read").
 *
 * Black Pill builds (APP_BOARD_BLACKPILL == 1): if no FRAM answers at boot,
 * a 32 KB RAM array stands in behind the same API (CLAUDE.md 3.5). Its
 * contents are lost on reset.
 */
#ifndef APP_FRAM_H
#define APP_FRAM_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"

/* Probes the FRAM. On the custom PCB a missing FRAM returns STATUS_NACK.
 * On the Black Pill it switches to the RAM emulator and returns STATUS_OK. */
status_t fram_init(void);

/* True when the RAM emulator is in use. */
bool fram_is_emulated(void);

status_t fram_read(uint16_t addr, uint8_t *buf, uint16_t len);
status_t fram_write(uint16_t addr, const uint8_t *buf, uint16_t len);

#endif /* APP_FRAM_H */
