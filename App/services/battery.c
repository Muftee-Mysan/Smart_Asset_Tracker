/**
 * @file    battery.c
 * @brief   VDD measurement via the internal reference (VREFINT).
 *
 * VDD_mV = 3300 * VREFINT_CAL / raw, where VREFINT_CAL was measured at
 * VDDA = 3.3 V, 30 degC (STM32F411 datasheet DocID026289 Rev 6, Table 75,
 * address 0x1FFF7A2A). ADC1 is set up by CubeMX for VREFINT, 480-cycle
 * sampling: 60 us at ADCCLK = 8 MHz, above the 10 us minimum (Table 74).
 */
#include "battery.h"
#include "adc.h"
#include "config.h"

/* VREFINT_CAL: raw ADC value at VDDA = 3.3 V, 30 degC (Table 75). */
#define VREFINT_CAL_PTR     ((const volatile uint16_t *)0x1FFF7A2AU)
#define VREFINT_CAL_VDD_MV  3300U
#define ADC_POLL_TIMEOUT_MS 5U

status_t battery_read_mv(uint16_t *vdd_mv)
{
    /* Re-enable VREFINT (cleared after every measurement) and let it
     * settle; 1 ms covers its start-up time with a wide margin. */
    ADC1_COMMON->CCR |= ADC_CCR_TSVREFE;
    HAL_Delay(1U);

    uint32_t sum = 0U;
    status_t st = STATUS_OK;
    for (uint32_t i = 0U; (i < BATTERY_SAMPLES) && (st == STATUS_OK); i++) {
        st = status_from_hal(HAL_ADC_Start(&hadc1));
        if (st == STATUS_OK) {
            st = status_from_hal(HAL_ADC_PollForConversion(&hadc1, ADC_POLL_TIMEOUT_MS));
        }
        if (st == STATUS_OK) {
            sum += HAL_ADC_GetValue(&hadc1);
        }
    }
    (void)HAL_ADC_Stop(&hadc1);                 /* ADON = 0 */
    ADC1_COMMON->CCR &= ~ADC_CCR_TSVREFE;       /* VREFINT off for sleep */

    if (st != STATUS_OK) {
        return st;
    }
    const uint32_t raw = sum / BATTERY_SAMPLES;
    if (raw == 0U) {
        return STATUS_INVALID;
    }
    const uint32_t cal = *VREFINT_CAL_PTR;
    *vdd_mv = (uint16_t)((VREFINT_CAL_VDD_MV * cal + raw / 2U) / raw);
    return STATUS_OK;
}

batt_level_t battery_check(uint16_t *vdd_mv)
{
    uint16_t mv = 0U;
    if (battery_read_mv(&mv) != STATUS_OK) {
        *vdd_mv = 0U;
        return BATT_OK;
    }
    *vdd_mv = mv;
    if (mv < BATTERY_CRITICAL_MV) {
        return BATT_CRITICAL;
    }
    return (mv < BATTERY_OK_MV) ? BATT_LOW : BATT_OK;
}
