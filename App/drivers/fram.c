/**
 * @file    fram.c
 * @brief   MB85RC256V FRAM (32 KB) over I2C, with a RAM emulator on the
 *          Black Pill.
 */
#include "fram.h"
#include <string.h>
#include "config.h"
#include "i2c_bus.h"

#if APP_BOARD_BLACKPILL
static uint8_t s_emu[FRAM_SIZE_BYTES];
static bool s_emulated;
#endif

static status_t check_range(uint16_t addr, uint16_t len)
{
    if ((uint32_t)addr + len > FRAM_SIZE_BYTES) {
        return STATUS_INVALID;
    }
    return STATUS_OK;
}

status_t fram_init(void)
{
    if (i2c_bus_probe(I2C_ADDR_FRAM)) {
#if APP_BOARD_BLACKPILL
        s_emulated = false;
#endif
        return STATUS_OK;
    }
#if APP_BOARD_BLACKPILL
    memset(s_emu, 0xFF, sizeof(s_emu));     /* like blank FRAM */
    s_emulated = true;
    return STATUS_OK;
#else
    return STATUS_NACK;
#endif
}

bool fram_is_emulated(void)
{
#if APP_BOARD_BLACKPILL
    return s_emulated;
#else
    return false;
#endif
}

status_t fram_read(uint16_t addr, uint8_t *buf, uint16_t len)
{
    status_t st = check_range(addr, len);
#if APP_BOARD_BLACKPILL
    if ((st == STATUS_OK) && s_emulated) {
        memcpy(buf, &s_emu[addr], len);
        return STATUS_OK;
    }
#endif
    while ((st == STATUS_OK) && (len > 0U)) {
        const uint16_t n = (len > FRAM_MAX_CHUNK) ? FRAM_MAX_CHUNK : len;
        st = i2c_bus_mem_read(I2C_ADDR_FRAM, addr, 2U, buf, n);
        addr = (uint16_t)(addr + n);
        buf += n;
        len = (uint16_t)(len - n);
    }
    return st;
}

status_t fram_write(uint16_t addr, const uint8_t *buf, uint16_t len)
{
    status_t st = check_range(addr, len);
#if APP_BOARD_BLACKPILL
    if ((st == STATUS_OK) && s_emulated) {
        memcpy(&s_emu[addr], buf, len);
        return STATUS_OK;
    }
#endif
    while ((st == STATUS_OK) && (len > 0U)) {
        const uint16_t n = (len > FRAM_MAX_CHUNK) ? FRAM_MAX_CHUNK : len;
        st = i2c_bus_mem_write(I2C_ADDR_FRAM, addr, 2U, buf, n);
        addr = (uint16_t)(addr + n);
        buf += n;
        len = (uint16_t)(len - n);
    }
    return st;
}
