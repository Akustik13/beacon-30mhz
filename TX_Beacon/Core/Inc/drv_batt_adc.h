#pragma once
#include <stdint.h>

/*
 * drv_batt_adc — battery voltage via resistor divider on PA7, enable on PA12.
 *
 * PA12 LOW  → divider powered, ADC measures battery voltage
 * PA12 HIGH → divider off (saves power between readings)
 * PA7       → ADC1_IN12, reads divider midpoint
 *
 * Conversion:
 *   raw_mV  = raw_adc * vdda_mV / 4095
 *   batt_mV = raw_mV  * g_batt_scale_x10 / 10
 *   pct     = clamp((batt_mV - empty_mV)*100 / (full_mV - empty_mV), 0, 100)
 *   full_mV / empty_mV from g_hw_desc (hwdesc batt adc <full> <empty>).
 */

#define BATT_MODE_OFF       0U
#define BATT_MODE_PERIODIC  1U

extern uint8_t  g_batt_mode;
extern uint16_t g_batt_period_s;
extern uint8_t g_batt_scale_x10;    /* divider multiplier ×10 (20 = 2.0×) */

extern uint32_t g_last_batt_mV;
extern int8_t   g_last_batt_pct;     /* -1 = never read */
extern uint32_t g_last_batt_raw;     /* raw ADC (0-4095) */
extern uint32_t g_last_vref_raw;     /* VREFINT raw ADC (0-4095) */

/* Call once at startup — configure PA12 HIGH (divider off), PA7 as analog */
void BattAdc_Init(void);

/* One-shot measurement: enable divider, read ADC, disable divider.
 * Returns 1 on success; updates g_last_batt_mV / g_last_batt_pct. */
uint8_t BattAdc_MeasureNow(uint32_t *batt_mV, int8_t *pct);

/* Call each main-loop iteration: fires when mode=PERIODIC and period elapsed.
 * Returns 1 when a new reading was taken. */
uint8_t BattAdc_TickPeriodic(void);
