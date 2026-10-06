/**
 * @file    status.h
 * @brief   Common status codes returned by drivers and services.
 */
#ifndef APP_STATUS_H
#define APP_STATUS_H

#include "stm32f4xx_hal.h"

typedef enum {
    STATUS_OK = 0,
    STATUS_ERROR,       /* generic / HAL_ERROR */
    STATUS_BUSY,        /* peripheral busy (HAL_BUSY) */
    STATUS_TIMEOUT,     /* operation did not complete in time (HAL_TIMEOUT) */
    STATUS_NACK,        /* I2C device did not acknowledge */
    STATUS_INVALID,     /* bad argument or corrupt data */
} status_t;

static inline status_t status_from_hal(HAL_StatusTypeDef s)
{
    switch (s) {
    case HAL_OK:      return STATUS_OK;
    case HAL_BUSY:    return STATUS_BUSY;
    case HAL_TIMEOUT: return STATUS_TIMEOUT;
    default:          return STATUS_ERROR;
    }
}

#endif /* APP_STATUS_H */
