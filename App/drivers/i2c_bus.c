/**
 * @file    i2c_bus.c
 * @brief   Blocking wrapper over HAL I2C1 with timeouts and status_t errors.
 */
#include "i2c_bus.h"
#include "config.h"
#include "i2c.h"
#include "trace.h"

static uint32_t s_errors;
static uint32_t s_nacks;

static status_t finish(HAL_StatusTypeDef hs)
{
    if (hs == HAL_OK) {
        return STATUS_OK;
    }
    if ((hs == HAL_ERROR) && ((HAL_I2C_GetError(&hi2c1) & HAL_I2C_ERROR_AF) != 0U)) {
        /* Acknowledge failure: device absent, or busy (the PN532 NACKs its
         * address while processing, UM0701-02 6.2.4). Counted separately. */
        s_nacks++;
        return STATUS_NACK;
    }
    s_errors++;
    const status_t st = status_from_hal(hs);
    TRACE(TR_I2C_ERROR, ((uint32_t)st << 16) | HAL_I2C_GetError(&hi2c1));
    return st;
}

status_t i2c_bus_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    /* HAL takes a non-const buffer but only reads it when transmitting. */
    return finish(HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(addr << 1), (uint8_t *)data,
                                          len, I2C_TIMEOUT_MS));
}

status_t i2c_bus_read(uint8_t addr, uint8_t *data, uint16_t len)
{
    return finish(HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(addr << 1), data, len,
                                         I2C_TIMEOUT_MS));
}

status_t i2c_bus_mem_write(uint8_t addr, uint16_t mem, uint8_t mem_size,
                           const uint8_t *data, uint16_t len)
{
    const uint16_t ms = (mem_size == 2U) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
    return finish(HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(addr << 1), mem, ms, (uint8_t *)data,
                                    len, I2C_TIMEOUT_MS));
}

status_t i2c_bus_mem_read(uint8_t addr, uint16_t mem, uint8_t mem_size,
                          uint8_t *data, uint16_t len)
{
    const uint16_t ms = (mem_size == 2U) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
    return finish(HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(addr << 1), mem, ms, data, len,
                                   I2C_TIMEOUT_MS));
}

bool i2c_bus_probe(uint8_t addr)
{
    return HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1U,
                                 I2C_PROBE_TIMEOUT_MS) == HAL_OK;
}

uint32_t i2c_bus_error_count(void)
{
    return s_errors;
}

uint32_t i2c_bus_nack_count(void)
{
    return s_nacks;
}
