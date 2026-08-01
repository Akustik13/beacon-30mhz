#pragma once
#include <stdint.h>
#include <limits.h>

/*
 * drv_chip_temp — internal temperature sensor + VDDA via VREFINT (ADC1).
 *
 * Algorithm (per ST AN4800):
 *   1. ADC hardware calibration (HAL_ADCEx_Calibration_Start)
 *   2. Read VREFINT ADC code
 *   3. Compute actual VDDA = VREFINT_CAL_VREF(3600mV) * CAL / raw_vref
 *   4. Read TEMPSENSOR ADC code (CHIP_TEMP_SAMPLES averages)
 *   5. Normalise to VDDA=3.0V:  raw_3v = raw * VDDA / 3000
 *   6. Two-point interpolation via TS_CAL1(30°C) and TS_CAL2(130°C)
 *   7. 0.1 °C resolution (result ×10)
 *   8. User calibration offset (g_temp_offset_x10, applied last)
 */

#ifndef CHIP_TEMP_SAMPLES
#define CHIP_TEMP_SAMPLES   8U
#endif

#define TEMP_MODE_OFF        0U
#define TEMP_MODE_PERIODIC   1U   /* read every g_temp_period_s seconds     */
#define TEMP_MODE_BEFORE_TX  2U   /* read just before RF_Start()            */

extern uint8_t  g_temp_mode;        /* TEMP_MODE_* constant                 */
extern uint8_t  g_temp_period_s;    /* 1-255 s between periodic reads       */
extern int8_t   g_temp_offset_x10;  /* user calibration offset ×0.1 °C     */

extern int32_t  g_last_temp_x10;    /* last reading ×10; INT32_MIN = never  */
extern uint32_t g_last_vdda_mV;

/* Call at top of main loop — reads if PERIODIC and ≥g_temp_period_s elapsed */
uint8_t ChipTemp_TickPeriodic(void);

/* Call just before RF_Start() — reads if BEFORE_TX                          */
uint8_t ChipTemp_TickBeforeTX(void);

/* One-shot read regardless of mode. Returns 1 on success.                   */
uint8_t ChipTemp_MeasureNow(int32_t *temp_x10, uint32_t *vdda_mV);
