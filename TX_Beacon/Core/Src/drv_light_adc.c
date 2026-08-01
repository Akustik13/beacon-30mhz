#include "drv_light_adc.h"
#include "main.h"

/* PA5 = ADC1_IN10 */
#define LIGHT_ADC_CH    ADC_CHANNEL_10
/* PB1: HIGH=sensor on, LOW=sensor off */
#define LIGHT_EN_PIN    GPIO_PIN_1
#define LIGHT_EN_PORT   GPIOB

#define LIGHT_SAMPLES   4U

uint8_t  g_light_mode      = LIGHT_MODE_OFF;
uint8_t  g_light_period_s  = 5U;
uint16_t g_last_light_raw  = 0U;
uint32_t g_last_light_lux  = 0U;
uint8_t  g_light_ever_read = 0U;

static uint32_t s_last_tick = 0xFFFFFFFFUL;

void LightAdc_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};

    /* PA5 — analog input, no pull */
    g.Pin  = GPIO_PIN_5;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    /* PB1 — output, start LOW (sensor off) */
    g.Pin   = LIGHT_EN_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LIGHT_EN_PORT, &g);
    HAL_GPIO_WritePin(LIGHT_EN_PORT, LIGHT_EN_PIN, GPIO_PIN_RESET);
}

static uint8_t _read_raw(uint16_t *raw_out)
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
    ch.Channel      = LIGHT_ADC_CH;
    ch.Rank         = ADC_REGULAR_RANK_1;
    ch.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    if (HAL_ADC_ConfigChannel(&h, &ch) != HAL_OK) { HAL_ADC_DeInit(&h); return 0U; }

    uint32_t sum = 0U;
    for (uint8_t i = 0U; i < LIGHT_SAMPLES; i++) {
        if (HAL_ADC_Start(&h) != HAL_OK) { HAL_ADC_DeInit(&h); return 0U; }
        if (HAL_ADC_PollForConversion(&h, 10U) != HAL_OK) {
            HAL_ADC_Stop(&h); HAL_ADC_DeInit(&h); return 0U;
        }
        sum += HAL_ADC_GetValue(&h);
        HAL_ADC_Stop(&h);
    }
    *raw_out = (uint16_t)(sum / LIGHT_SAMPLES);

    HAL_ADC_DeInit(&h);
    return 1U;
}

uint8_t LightAdc_MeasureNow(uint16_t *raw, uint32_t *lux)
{
    HAL_GPIO_WritePin(LIGHT_EN_PORT, LIGHT_EN_PIN, GPIO_PIN_SET);  /* sensor ON */
    HAL_Delay(5U);

    uint16_t rv = 0U;
    uint8_t ok = _read_raw(&rv);

    HAL_GPIO_WritePin(LIGHT_EN_PORT, LIGHT_EN_PIN, GPIO_PIN_RESET); /* sensor OFF */

    if (!ok) return 0U;

    uint32_t lv = ((uint32_t)rv * 10000UL) / 4095UL;

    g_last_light_raw  = rv;
    g_last_light_lux  = lv;
    g_light_ever_read = 1U;
    if (raw) *raw = rv;
    if (lux) *lux = lv;
    return 1U;
}

uint8_t LightAdc_TickPeriodic(void)
{
    if (g_light_mode != LIGHT_MODE_PERIODIC) return 0U;
    uint32_t now = HAL_GetTick();
    uint32_t ms  = (g_light_period_s >= 1U) ? (uint32_t)g_light_period_s * 1000U : 5000U;
    if ((now - s_last_tick) < ms) return 0U;
    s_last_tick = now;

    uint16_t raw; uint32_t lux;
    return LightAdc_MeasureNow(&raw, &lux);
}
