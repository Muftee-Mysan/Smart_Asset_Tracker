/**
 * @file    crc.h
 * @brief   CRC-32 (IEEE, zlib-compatible) and CRC-16/CCITT-FALSE.
 */
#ifndef APP_CRC_H
#define APP_CRC_H

#include <stddef.h>
#include <stdint.h>

/* CRC-32 as zlib.crc32(): reflected poly 0xEDB88320, init and xorout
 * 0xFFFFFFFF. Check value for "123456789": 0xCBF43926. */
uint32_t crc32_ieee(const void *data, size_t len);

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
 * Check value for "123456789": 0x29B1. */
uint16_t crc16_ccitt_false(const void *data, size_t len);

#endif /* APP_CRC_H */
