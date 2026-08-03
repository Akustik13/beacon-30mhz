#include "drv_batt_adc.h"
#include "hw_desc.h"
#include "main.h"
#include <limits.h>

/* PA7 = ADC1_IN12 */
#define BATT_ADC_CH     ADC_CHANNEL_12
/* PA12: LOW=divider on, HIGH=divider off */
#define BATT_EN_PIN     GPIO_PIN_12
#define BATT_EN_PORT    GPIOA

#define BATT_SAMPLES    4U

uint8_t  g_batt_mode       = BATT_MODE_PERIODIC;
uint16_t g_batt_period_s   = 3600U;
uint8_t  g_batt_scale_x10  = 20U;   /* default 2.0× */
uint32_t g_last_batt_mV     = 0U;
int8_t   g_last_batt_pct    = -1;
uint32_t g_last_batt_raw    = 0U;
uint32_t g_last_vref_raw    = 0U;

static uint32_t s_last_tick = 0xFFFFFFFFUL;

void BattAdc_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};

    /* PA7 — analog input, no pull */
    g.Pin  = GPIO_PIN_7;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    /* PA12 — output, start HIGH (divider off) */
    g.Pin   = BATT_EN_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BATT_EN_PORT, &g);
    HAL_GPIO_WritePin(BATT_EN_PORT, BATT_EN_PIN, GPIO_PIN_SET);
}

/* ── Read VREFINT + PA7 (no blocking loops, single-shot HAL polling) ─────── */
static uint8_t _read_raw(uint32_t *vref_out, uint32_t *batt_out)
{
    ADC_HandleTypeDef h = {0};
    h.Instance = ADC1;
    h.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    h.Init.Resolution            = ADC_RESOLUTION_12B;
    h.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    h.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    h.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    h.Init.LowPowerAutoWait      = DISABLE;
    h.Init.ContinuousConvMode    = DISABLE;
    h.Init.NbrOfConversion       = 1;
    h.Init.DiscontinuousConvMode = DISABLE;
    h.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    h.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    h.Init.DMAContinuousRequests = DISABLE;
    h.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    h.Init.SamplingTimeCommon1   = ADC_SAMPLETIME_160CYCLES_5;

    if (HAL_ADC_Init(&h) != HAL_OK) return 0U;
    if (HAL_ADCEx_Calibration_Start(&h, ADC_SINGLE_ENDED) != HAL_OK) {
        HAL_ADC_DeInit(&h); return 0U;
    }

    ADC_ChannelConfTypeDef ch = {0};
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;

    /* VREFINT → compute actual VDDA */
    ch.Channel = ADC_CHANNEL_VREFINT;
    if (HAL_ADC_ConfigChannel(&h, &ch) != HAL_OK) { HAL_ADC_DeInit(&h); return 0U; }
    uint32_t vsum = 0U;
    for (uint8_t i = 0U; i < 4U; i++) {
        if (HAL_ADC_Start(&h) != HAL_OK) { HAL_ADC_DeInit(&h); return 0U; }
        if (HAL_ADC_PollForConversion(&h, 10U) != HAL_OK) {
            HAL_ADC_Stop(&h); HAL_ADC_DeInit(&h); return 0U;
        }
        vsum += HAL_ADC_GetValue(&h);
        HAL_ADC_Stop(&h);
    }
    *vref_out = vsum / 4U;

    /* PA7 = ADC1_IN12 */
    ch.Channel = BATT_ADC_CH;
    if (HAL_ADC_ConfigChannel(&h, &ch) != HAL_OK) { HAL_ADC_DeInit(&h); return 0U; }
    uint32_t bsum = 0U;
    for (uint8_t i = 0U; i < BATT_SAMPLES; i++) {
        if (HAL_ADC_Start(&h) != HAL_OK) { HAL_ADC_DeInit(&h); return 0U; }
        if (HAL_ADC_PollForConversion(&h, 10U) != HAL_OK) {
            HAL_ADC_Stop(&h); HAL_ADC_DeInit(&h); return 0U;
        }
        bsum += HAL_ADC_GetValue(&h);
        HAL_ADC_Stop(&h);
    }
    *batt_out = bsum / BATT_SAMPLES;

    HAL_ADC_DeInit(&h);
    return 1U;
}

uint8_t BattAdc_MeasureNow(uint32_t *batt_mV, int8_t *pct)
{
    HAL_GPIO_WritePin(BATT_EN_PORT, BATT_EN_PIN, GPIO_PIN_RESET); /* divider ON */
    HAL_Delay(2U);

    uint32_t vref_raw, batt_raw;
    uint8_t ok = _read_raw(&vref_raw, &batt_raw);

    HAL_GPIO_WritePin(BATT_EN_PORT, BATT_EN_PIN, GPIO_PIN_SET);   /* divider OFF */

    if (!ok || vref_raw == 0U) return 0U;

    g_last_batt_raw = batt_raw;
    g_last_vref_raw = vref_raw;

    /* VDDA from VREFINT calibration (AN4800) */
    uint32_t vdda = ((uint32_t)VREFINT_CAL_VREF * (uint32_t)(*VREFINT_CAL_ADDR)) / vref_raw;
    uint32_t raw_mV = (batt_raw * vdda) / 4095U;
    uint32_t actual = (raw_mV * (uint32_t)g_batt_scale_x10) / 10U;

    g_last_batt_mV = actual;
    if (batt_mV) *batt_mV = actual;

    uint16_t full  = g_hw_desc.batt_full_mv  ? g_hw_desc.batt_full_mv  : 4200U;
    uint16_t empty = g_hw_desc.batt_empty_mv ? g_hw_desc.batt_empty_mv : 3000U;
    int32_t p = 50;
    if (full > empty) {
        p = ((int32_t)actual - (int32_t)empty) * 100L / (int32_t)(full - empty);
        if (p < 0) p = 0; else if (p > 100) p = 100;
    }
    g_last_batt_pct = (int8_t)p;
    if (pct) *pct = (int8_t)p;
    return 1U;
}

uint8_t BattAdc_TickPeriodic(void)
{
    if (g_batt_mode != BATT_MODE_PERIODIC) return 0U;
    uint32_t now = HAL_GetTick();
    uint32_t ms  = (g_batt_period_s >= 1U) ? (uint32_t)g_batt_period_s * 1000U : 5000U;
    if ((now - s_last_tick) < ms) return 0U;
    s_last_tick = now;

    uint32_t mv; int8_t pct;
    return BattAdc_MeasureNow(&mv, &pct);
}
