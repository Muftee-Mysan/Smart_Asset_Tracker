/**
 * @file    i2c_bus.c
 * @brief   Blocking wrapper over HAL I2C1: timeouts, status_t errors, bus
 *          recovery and one retry (CLAUDE.md 6.12).
 *
 * Recovery: de-init I2C1, drive SCL as open-drain GPIO for up to 9 pulses
 * with SDA released (lets a slave that is holding SDA low finish its byte),
 * generate a STOP, pulse I2C_CR1_SWRST, then re-init. The software reset also
 * clears a BUSY flag that stays set while both lines are high (the STM32F4
 * stuck-BUSY condition named in CLAUDE.md 6.12).
 */
#include "i2c_bus.h"
#include "config.h"
#include "delay.h"
#include "i2c.h"
#include "trace.h"

/* I2C1 pins (CLAUDE.md 3.2 / CubeMX i2c.c). */
#define SCL_PORT        GPIOB
#define SCL_PIN         GPIO_PIN_6
#define SDA_PORT        GPIOB
#define SDA_PIN         GPIO_PIN_7

/* Half period of the recovery clock (us): 5 us -> 100 kHz. */
#define RECOVER_HALF_US         5U
/* Max wait for a slave to release a stretched SCL during recovery (us). */
#define RECOVER_STRETCH_US      1000U
#define RECOVER_PULSES          9U

typedef enum { OP_WRITE, OP_READ, OP_MEM_WRITE, OP_MEM_READ } op_t;

static uint32_t s_errors;
static uint32_t s_nacks;
static uint32_t s_recoveries;

static bool line_high(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
}

static void bus_recover(void)
{
    s_recoveries++;
    TRACE(TR_I2C_RECOVER, ((uint32_t)line_high(SCL_PORT, SCL_PIN) << 1) |
                          (uint32_t)line_high(SDA_PORT, SDA_PIN));

    (void)HAL_I2C_DeInit(&hi2c1);   /* I2C1 clock off, pins back to input */

    /* Both lines as open-drain GPIO, released (external pull-ups). */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_SET);
    GPIO_InitTypeDef gi = {0};
    gi.Mode = GPIO_MODE_OUTPUT_OD;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    gi.Pin = SCL_PIN;
    HAL_GPIO_Init(SCL_PORT, &gi);
    gi.Pin = SDA_PIN;
    HAL_GPIO_Init(SDA_PORT, &gi);
    delay_us(RECOVER_HALF_US);

    /* Up to 9 SCL pulses until the slave releases SDA. */
    for (uint32_t i = 0U; (i < RECOVER_PULSES) && !line_high(SDA_PORT, SDA_PIN); i++) {
        HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_RESET);
        delay_us(RECOVER_HALF_US);
        HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
        for (uint32_t t = 0U; (t < RECOVER_STRETCH_US) && !line_high(SCL_PORT, SCL_PIN); t++) {
            delay_us(1U);           /* clock stretching */
        }
        delay_us(RECOVER_HALF_US);
    }

    /* STOP: SDA rises while SCL is high. */
    HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_RESET);
    delay_us(RECOVER_HALF_US);
    HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_RESET);
    delay_us(RECOVER_HALF_US);
    HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET);
    delay_us(RECOVER_HALF_US);
    HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_SET);
    delay_us(RECOVER_HALF_US);

    /* Peripheral software reset, then re-init (MspInit restores AF pins). */
    __HAL_RCC_I2C1_CLK_ENABLE();
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;
    hi2c1.State = HAL_I2C_STATE_RESET;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        s_errors++;
        TRACE(TR_I2C_ERROR, 0xFFFFU);
    }
}

static bool bus_stuck(void)
{
    return !line_high(SCL_PORT, SCL_PIN) || !line_high(SDA_PORT, SDA_PIN) ||
           (__HAL_I2C_GET_FLAG(&hi2c1, I2C_FLAG_BUSY) != RESET);
}

void i2c_bus_init(void)
{
    delay_init();
    if (bus_stuck()) {
        bus_recover();
    }
}

status_t i2c_bus_set_speed(uint32_t hz)
{
    (void)HAL_I2C_DeInit(&hi2c1);
    hi2c1.Init.ClockSpeed = hz;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    return status_from_hal(HAL_I2C_Init(&hi2c1));
}

void i2c_bus_suspend(void)
{
    (void)HAL_I2C_DeInit(&hi2c1);
    GPIO_InitTypeDef gi = {0};
    gi.Pin = SCL_PIN | SDA_PIN;
    gi.Mode = GPIO_MODE_ANALOG;
    gi.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(SCL_PORT, &gi);
}

void i2c_bus_resume(void)
{
    /* hi2c1.Init keeps the speed set by i2c_bus_set_speed(). */
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        s_errors++;
    }
    if (bus_stuck()) {
        bus_recover();
    }
}

static HAL_StatusTypeDef hal_xfer(op_t op, uint8_t addr, uint16_t mem, uint8_t mem_size,
                                  uint8_t *data, uint16_t len)
{
    const uint16_t a = (uint16_t)(addr << 1);
    const uint16_t ms = (mem_size == 2U) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
    switch (op) {
    case OP_WRITE:     return HAL_I2C_Master_Transmit(&hi2c1, a, data, len, I2C_TIMEOUT_MS);
    case OP_READ:      return HAL_I2C_Master_Receive(&hi2c1, a, data, len, I2C_TIMEOUT_MS);
    case OP_MEM_WRITE: return HAL_I2C_Mem_Write(&hi2c1, a, mem, ms, data, len, I2C_TIMEOUT_MS);
    case OP_MEM_READ:  return HAL_I2C_Mem_Read(&hi2c1, a, mem, ms, data, len, I2C_TIMEOUT_MS);
    default:           return HAL_ERROR;
    }
}

/* Classifies a HAL result. NACKs are counted apart from bus errors: the
 * PN532 NACKs its address while busy (UM0701-02 6.2.4). */
static status_t classify(HAL_StatusTypeDef hs)
{
    if (hs == HAL_OK) {
        return STATUS_OK;
    }
    const uint32_t err = HAL_I2C_GetError(&hi2c1);
    if ((hs == HAL_ERROR) && (err == HAL_I2C_ERROR_AF)) {
        s_nacks++;
        return STATUS_NACK;
    }
    s_errors++;
    status_t st = status_from_hal(hs);
    if ((err & HAL_I2C_ERROR_TIMEOUT) != 0U) {
        st = STATUS_TIMEOUT;
    }
    TRACE(TR_I2C_ERROR, ((uint32_t)st << 16) | err);
    return st;
}

static status_t xfer(op_t op, uint8_t addr, uint16_t mem, uint8_t mem_size,
                     uint8_t *data, uint16_t len)
{
    status_t st = STATUS_ERROR;
    for (uint32_t attempt = 0U; attempt < 2U; attempt++) {
        st = classify(hal_xfer(op, addr, mem, mem_size, data, len));
        if ((st == STATUS_OK) || (st == STATUS_NACK)) {
            break;
        }
        bus_recover();              /* then retry once */
    }
    return st;
}

status_t i2c_bus_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    /* HAL takes a non-const buffer but only reads it when transmitting. */
    return xfer(OP_WRITE, addr, 0U, 0U, (uint8_t *)data, len);
}

status_t i2c_bus_read(uint8_t addr, uint8_t *data, uint16_t len)
{
    return xfer(OP_READ, addr, 0U, 0U, data, len);
}

status_t i2c_bus_mem_write(uint8_t addr, uint16_t mem, uint8_t mem_size,
                           const uint8_t *data, uint16_t len)
{
    return xfer(OP_MEM_WRITE, addr, mem, mem_size, (uint8_t *)data, len);
}

status_t i2c_bus_mem_read(uint8_t addr, uint16_t mem, uint8_t mem_size,
                          uint8_t *data, uint16_t len)
{
    return xfer(OP_MEM_READ, addr, mem, mem_size, data, len);
}

bool i2c_bus_probe(uint8_t addr)
{
    HAL_StatusTypeDef hs = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1U,
                                                 I2C_PROBE_TIMEOUT_MS);
    if (hs == HAL_BUSY) {
        bus_recover();
        hs = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1U, I2C_PROBE_TIMEOUT_MS);
    }
    return hs == HAL_OK;
}

uint32_t i2c_bus_error_count(void)
{
    return s_errors;
}

uint32_t i2c_bus_nack_count(void)
{
    return s_nacks;
}

uint32_t i2c_bus_recovery_count(void)
{
    return s_recoveries;
}
