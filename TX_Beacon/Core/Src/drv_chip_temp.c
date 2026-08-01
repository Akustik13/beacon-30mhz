#include "drv_chip_temp.h"
#include "main.h"
#include <limits.h>

/*
 * STM32WB1M ADC (ADC_SUPPORT_2_5_MSPS variant):
 *   - Sampling time is global (SamplingTimeCommon1 in Init), channels select it
 *     via ADC_SAMPLINGTIME_COMMON_1 / ADC_SAMPLINGTIME_COMMON_2.
 *   - No differential mode, no per-channel offset, no oversampling.
 *   - Max sample time: ADC_SAMPLETIME_160CYCLES_5.
 *
 * Calibration data (from STM32WBxx_LL_ADC header, system memory):
 *   VREFINT_CAL_ADDR   @ 0x1FFF75AA — VREFINT ADC code at VDDA=3.6V, 12-bit
 *   VREFINT_CAL_VREF   = 3600 mV   — calibration VDDA for VREFINT
 *   TEMPSENSOR_CAL1_ADDR @ 0x1FFF75A8 — TEMPSENSOR at 30 °C, VDDA=3.0V, 12-bit
 *   TEMPSENSOR_CAL2_ADDR @ 0x1FFF75CA — TEMPSENSOR at 130 °C, VDDA=3.0V, 12-bit
 *   TEMPSENSOR_CAL_VREFANALOG = 3000 mV — calibration VDDA for TEMPSENSOR
 *
 * VDDA computation (AN4800):
 *   VDDA_mV = VREFINT_CAL_VREF * (*VREFINT_CAL_ADDR) / raw_vref
 *
 * Temperature with 0.1 °C resolution:
 *   raw_at_3v = raw_temp * VDDA_mV / TEMPSENSOR_CAL_VREFANALOG
 *   T×10 = TEMPSENSOR_CAL1_TEMP×10 + (raw_at_3v - CAL1)
 *          * (CAL2_T×10 - CAL1_T×10) / (CAL2 - CAL1)
 *
 * All calibration symbols (VREFINT_CAL_ADDR, VREFINT_CAL_VREF,
 * TEMPSENSOR_CAL*) come from stm32wbxx_ll_adc.h included via main.h.
 */

/* ── Public globals ──────────────────────────────────────────────────────── */
uint8_t  g_temp_mode       = TEMP_MODE_OFF;
uint8_t  g_temp_period_s   = 1U;    /* interval for PERIODIC mode (1-255 s)  */
int8_t   g_temp_offset_x10 = 0;     /* user calibration offset ×0.1°C        */
int32_t  g_last_temp_x10   = INT32_MIN;
uint32_t g_last_vdda_mV    = 0U;

/* Periodic mode: track HAL_GetTick() of last reading.
 * Initialized to UINT32_MAX so first call always fires (uint32 wrap trick). */
static uint32_t s_last_periodic_tick = 0xFFFFFFFFUL;

/* ── ADC MSP: internal channels need no GPIO, only clock ─────────────────── */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
        __HAL_RCC_ADC_CLK_ENABLE();
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        /* VREFEN and TSEN in ADC_CCR are NOT cleared by HAL_ADC_DeInit().
         * If left set they draw ~6 µA (VREFINT) + ~5 µA (TEMPSENSOR) in Stop1.
         * Clear them here so every HAL_ADC_DeInit() call fully powers down ADC. */
        CLEAR_BIT(ADC1_COMMON->CCR, ADC_CCR_VREFEN | ADC_CCR_TSEN);
        __HAL_RCC_ADC_CLK_DISABLE();
    }
}

/* ── Init ADC1 for single-shot polling on internal channels ─────────────── */
static uint8_t _adc_init(ADC_HandleTypeDef *h)
{
    h->Instance = ADC1;
    h->Init.ClockPrescaler          = ADC_CLOCK_SYNC_PCLK_DIV4;
    h->Init.Resolution              = ADC_RESOLUTION_12B;
    h->Init.DataAlign               = ADC_DATAALIGN_RIGHT;
    h->Init.ScanConvMode            = ADC_SCAN_DISABLE;
    h->Init.EOCSelection            = ADC_EOC_SINGLE_CONV;
    h->Init.LowPowerAutoWait        = DISABLE;
    h->Init.ContinuousConvMode      = DISABLE;
    h->Init.NbrOfConversion         = 1;
    h->Init.DiscontinuousConvMode   = DISABLE;
    h->Init.ExternalTrigConv        = ADC_SOFTWARE_START;
    h->Init.ExternalTrigConvEdge    = ADC_EXTERNALTRIGCONVEDGE_NONE;
    h->Init.DMAContinuousRequests   = DISABLE;
    h->Init.Overrun                 = ADC_OVR_DATA_OVERWRITTEN;
    /* WB1M (ADC_SUPPORT_2_5_MSPS): sampling time is global in Init */
    h->Init.SamplingTimeCommon1     = ADC_SAMPLETIME_160CYCLES_5;
    /* SamplingTimeCommon2 stays 0 (not used) */

    if (HAL_ADC_Init(h) != HAL_OK) return 0U;

    if (HAL_ADCEx_Calibration_Start(h, ADC_SINGLE_ENDED) != HAL_OK) {
        HAL_ADC_DeInit(h);
        return 0U;
    }
    return 1U;
}

/* ── Configure channel (uses Common1 timing) and average n conversions ───── */
static uint8_t _avg(ADC_HandleTypeDef *h, uint32_t channel, uint8_t n, uint16_t *out)
{
    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel      = channel;
    ch.Rank         = ADC_REGULAR_RANK_1;
    /* For ADC_SUPPORT_2_5_MSPS, SamplingTime selects Common1 or Common2 */
    ch.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    /* HAL_ADC_ConfigChannel enables internal sensor + waits startup delay */
    if (HAL_ADC_ConfigChannel(h, &ch) != HAL_OK) return 0U;

    uint32_t sum = 0U;
    for (uint8_t i = 0U; i < n; i++) {
        if (HAL_ADC_Start(h) != HAL_OK) return 0U;
        if (HAL_ADC_PollForConversion(h, 10U) != HAL_OK) {
            HAL_ADC_Stop(h);
            return 0U;
        }
        sum += HAL_ADC_GetValue(h);
        HAL_ADC_Stop(h);
    }
    *out = (uint16_t)(sum / (uint32_t)n);
    return 1U;
}

/* ── Temperature interpolation, 0.1 °C resolution ───────────────────────── */
static int32_t _calc_temp_x10(uint16_t raw_temp, uint32_t vdda_mV)
{
    const int32_t cal1 = (int32_t)(*TEMPSENSOR_CAL1_ADDR);
    const int32_t cal2 = (int32_t)(*TEMPSENSOR_CAL2_ADDR);
    if (cal2 == cal1) return INT32_MIN;

    /* Scale to calibration VDDA (3.0 V) before comparing with CAL values */
    int32_t raw_at_3v = ((int32_t)raw_temp * (int32_t)vdda_mV) /
                        (int32_t)TEMPSENSOR_CAL_VREFANALOG;

    return ((int32_t)TEMPSENSOR_CAL1_TEMP * 10L) +
           ((raw_at_3v - cal1) *
            ((int32_t)TEMPSENSOR_CAL2_TEMP - (int32_t)TEMPSENSOR_CAL1_TEMP) * 10L) /
           (cal2 - cal1);
}

/* ── Public: one-shot measurement ─────────────────────────────────────────── */
uint8_t ChipTemp_MeasureNow(int32_t *temp_x10, uint32_t *vdda_mV)
{
    if (!temp_x10 || !vdda_mV) return 0U;

    ADC_HandleTypeDef hadc = {0};
    if (!_adc_init(&hadc)) return 0U;

    /* VREFINT → actual VDDA (calibrated at VREFINT_CAL_VREF = 3600 mV) */
    uint16_t raw_vref;
    if (!_avg(&hadc, ADC_CHANNEL_VREFINT, 4U, &raw_vref) || raw_vref == 0U) {
        HAL_ADC_DeInit(&hadc);
        return 0U;
    }
    uint32_t vdda = ((uint32_t)VREFINT_CAL_VREF * (uint32_t)(*VREFINT_CAL_ADDR)) / raw_vref;

    /* TEMPSENSOR: CHIP_TEMP_SAMPLES averages */
    uint16_t raw_temp;
    if (!_avg(&hadc, ADC_CHANNEL_TEMPSENSOR, CHIP_TEMP_SAMPLES, &raw_temp)) {
        HAL_ADC_DeInit(&hadc);
        return 0U;
    }

    HAL_ADC_DeInit(&hadc);

    int32_t t = _calc_temp_x10(raw_temp, vdda);
    if (t == INT32_MIN) return 0U;

    *temp_x10 = t;
    *vdda_mV  = vdda;
    return 1U;
}

/* Take 3 independent full measurements (each with fresh VREF) and return median.
 * Robust against single-sample spikes from USB/power-supply noise. */
static int32_t _measure_median3(uint32_t *vdda_out)
{
    int32_t  t[3];
    uint32_t v[3];

    for (uint8_t i = 0U; i < 3U; i++) {
        if (!ChipTemp_MeasureNow(&t[i], &v[i])) return INT32_MIN;
    }

    /* Sort 3 values (simple insertion) */
    for (uint8_t i = 1U; i < 3U; i++) {
        int32_t  kt = t[i];
        uint32_t kv = v[i];
        int8_t   j  = (int8_t)i - 1;
        while (j >= 0 && t[j] > kt) {
            t[j + 1] = t[j];
            v[j + 1] = v[j];
            j--;
        }
        t[j + 1] = kt;
        v[j + 1] = kv;
    }

    /* Return median (index 1) */
    *vdda_out = v[1];
    return t[1];
}

/* ── Public: call at top of main loop (every wakeup cycle) ─────────────────
 * Reads if TEMP_MODE_PERIODIC and ≥g_temp_period_s elapsed since last read.
 * uint32 wrap trick: UINT32_MAX initial value fires immediately on first call. */
uint8_t ChipTemp_TickPeriodic(void)
{
    if (g_temp_mode != TEMP_MODE_PERIODIC) return 0U;

    uint32_t now = HAL_GetTick();
    uint32_t period_ms = (g_temp_period_s >= 1U) ? (uint32_t)g_temp_period_s * 1000U : 1000U;
    if ((now - s_last_periodic_tick) < period_ms) return 0U;
    s_last_periodic_tick = now;

    uint32_t v;
    int32_t  t = _measure_median3(&v);
    if (t == INT32_MIN) return 0U;

    g_last_temp_x10 = t + (int32_t)g_temp_offset_x10;
    g_last_vdda_mV  = v;
    return 1U;
}

/* ── Public: call just before RF_Start() in TX blocks ──────────────────────
 * Reads if TEMP_MODE_BEFORE_TX; applies calibration offset; returns 1 on read. */
uint8_t ChipTemp_TickBeforeTX(void)
{
    if (g_temp_mode != TEMP_MODE_BEFORE_TX) return 0U;

    uint32_t v;
    int32_t  t = _measure_median3(&v);
    if (t == INT32_MIN) return 0U;

    g_last_temp_x10 = t + (int32_t)g_temp_offset_x10;
    g_last_vdda_mV  = v;
    return 1U;
}
