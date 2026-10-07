/**
 * @file    i2c_bus.h
 * @brief   Blocking wrapper over HAL I2C1 with timeouts and status_t errors.
 *
 * Addresses are 7-bit. Never call from an ISR.
 * Phase 2 adds bus recovery (9 SCL pulses + SWRST) and one retry.
 */
#ifndef APP_I2C_BUS_H
#define APP_I2C_BUS_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"

/* Plain write: START, addr+W, data..., STOP. */
status_t i2c_bus_write(uint8_t addr, const uint8_t *data, uint16_t len);

/* Plain read: START, addr+R, data..., STOP. */
status_t i2c_bus_read(uint8_t addr, uint8_t *data, uint16_t len);

/* Register/memory access with a 1- or 2-byte address sent MSB first. */
status_t i2c_bus_mem_write(uint8_t addr, uint16_t mem, uint8_t mem_size,
                           const uint8_t *data, uint16_t len);
status_t i2c_bus_mem_read(uint8_t addr, uint16_t mem, uint8_t mem_size,
                          uint8_t *data, uint16_t len);

/* True if a device acknowledges its address. */
bool i2c_bus_probe(uint8_t addr);

/* Failed transfers since boot: bus errors and timeouts (not NACKs). */
uint32_t i2c_bus_error_count(void);

/* Transfers since boot that ended with a NACK (probes excluded). */
uint32_t i2c_bus_nack_count(void);

#endif /* APP_I2C_BUS_H */
