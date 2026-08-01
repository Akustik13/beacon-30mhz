#pragma once
#include <stdint.h>

/*
 * drv_light_adc — photodiode + resistor on PA5, enable on PB1.
 *
 * PB1 HIGH → sensor powered, ADC measures light level
 * PB1 LOW  → sensor off (saves power)
 * PA5      → ADC1_IN10, reads photodiode voltage
 *
 * lux estimate (rough): lux ≈ raw_adc * 10000 / 4095
 * Calibrate with real sensor via scale coefficient if needed.
 */

#define LIGHT_MODE_OFF       0U
#define LIGHT_MODE_PERIODIC  1U

extern uint8_t  g_light_mode;
extern uint8_t  g_light_period_s;
extern uint16_t g_last_light_raw;
extern uint32_t g_last_light_lux;
extern uint8_t  g_light_ever_read;   /* 1 after first successful measurement */

/* Call once at startup — PB1 LOW (sensor off), PA5 as analog */
void LightAdc_Init(void);

/* One-shot measurement: enable sensor, read ADC, disable sensor.
 * Returns 1 on success; updates g_last_light_raw / g_last_light_lux. */
uint8_t LightAdc_MeasureNow(uint16_t *raw, uint32_t *lux);

/* Call each main-loop iteration: fires when mode=PERIODIC and period elapsed.
 * Returns 1 when a new reading was taken. */
uint8_t LightAdc_TickPeriodic(void);
