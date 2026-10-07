/**
 * @file    crc.c
 * @brief   CRC-32 (IEEE, zlib-compatible) and CRC-16/CCITT-FALSE.
 *
 * Bitwise implementations: the inputs are at most 64 bytes per call outside
 * export, so a lookup table is not worth its 1 KB.
 */
#include "crc.h"

uint32_t crc32_ieee(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;
    while (len-- > 0U) {
        crc ^= *p++;
        for (uint32_t b = 0U; b < 8U; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

uint16_t crc16_ccitt_false(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFFU;
    while (len-- > 0U) {
        crc ^= (uint16_t)((uint16_t)*p++ << 8);
        for (uint32_t b = 0U; b < 8U; b++) {
            crc = ((crc & 0x8000U) != 0U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                                          : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
