/**
 * @file    battery.h
 * @brief   VDD measurement via the internal reference (VREFINT).
 */
#ifndef APP_BATTERY_H
#define APP_BATTERY_H

#include <stdint.h>
#include "status.h"

/* Measures VDD in mV. Takes ~1 ms. Leaves the ADC and VREFINT off. */
status_t battery_read_mv(uint16_t *vdd_mv);

typedef enum {
    BATT_OK,        /* >= BATTERY_OK_MV */
    BATT_LOW,       /* BATTERY_CRITICAL_MV .. BATTERY_OK_MV: warn, continue */
    BATT_CRITICAL,  /* < BATTERY_CRITICAL_MV: refuse to scan */
} batt_level_t;

/* Measures VDD and classifies it (CLAUDE.md 6.8). A failed measurement is
 * reported as BATT_OK so the device stays usable; *vdd_mv is then 0. */
batt_level_t battery_check(uint16_t *vdd_mv);

#endif /* APP_BATTERY_H */
