/**
 * @file    i2c_bus.h
 * @brief   Blocking wrapper over HAL I2C1: timeouts, status_t errors, bus
 *          recovery and one retry (CLAUDE.md 6.12).
 *
 * Addresses are 7-bit. Never call from an ISR.
 * A NACK means "device absent or busy" and is returned as STATUS_NACK
 * without recovery. A timeout, bus error, arbitration loss or stuck BUSY
 * flag triggers bus recovery and one retry.
 */
#ifndef APP_I2C_BUS_H
#define APP_I2C_BUS_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"

/* Call once after MX_I2C1_Init(): recovers the bus if a line is stuck low
 * or the BUSY flag is set (e.g. a reset in the middle of a transfer). */
void i2c_bus_init(void);

/* Re-initialises I2C1 at a new SCL frequency (Hz), e.g. 400000. */
status_t i2c_bus_set_speed(uint32_t hz);

/* Before STOP: I2C1 off, SCL/SDA to analog (CLAUDE.md 6.10 step 2). */
void i2c_bus_suspend(void);

/* After a key wake: I2C1 back on at the last speed; recovers a stuck bus. */
void i2c_bus_resume(void);

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

/* Bus recoveries since boot. */
uint32_t i2c_bus_recovery_count(void);

#endif /* APP_I2C_BUS_H */
