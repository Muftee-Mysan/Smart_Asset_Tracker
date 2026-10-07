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

#endif /* APP_BATTERY_H */
