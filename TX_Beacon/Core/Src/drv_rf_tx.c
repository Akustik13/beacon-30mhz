#include "drv_rf_tx.h"
#include "drv_led.h"
#include "stm32wbxx_hal.h"

/* ── Pin mapping ──────────────────────────────────────────────────────────── */
#define TX_EN_PORT   GPIOB
#define TX_EN_PIN    GPIO_PIN_5

#define CH_A_PORT    GPIOA      /* CH bit 0 */
#define CH_A_PIN     GPIO_PIN_6
#define CH_B_PORT    GPIOB      /* CH bit 1 */
#define CH_B_PIN     GPIO_PIN_8

#define PWR_A_PORT   GPIOA      /* PWR bit 0 */
#define PWR_A_PIN    GPIO_PIN_1
#define PWR_B_PORT   GPIOA      /* PWR bit 1 */
#define PWR_B_PIN    GPIO_PIN_8

/* ── State ────────────────────────────────────────────────────────────────── */
static RF_Channel_t s_channel = RF_CH0;
static RF_Power_t   s_power   = RF_PWR1;
static uint8_t      s_enabled = 0;

/* ── Public ───────────────────────────────────────────────────────────────── */

void RF_Init(void)
{
    /* Clocks enabled by MX_GPIO_Init before RF_Init() is called */
    HAL_GPIO_WritePin(TX_EN_PORT, TX_EN_PIN,  GPIO_PIN_RESET); /* TX off */
    HAL_GPIO_WritePin(CH_A_PORT,  CH_A_PIN,   GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CH_B_PORT,  CH_B_PIN,   GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWR_A_PORT, PWR_A_PIN,  GPIO_PIN_SET);   /* PWR active-LOW: HIGH = off */
    HAL_GPIO_WritePin(PWR_B_PORT, PWR_B_PIN,  GPIO_PIN_SET);
    s_channel = RF_CH0;
    s_power   = RF_PWR1;
    s_enabled = 0;
}

void RF_Enable(void)
{
    HAL_GPIO_WritePin(TX_EN_PORT, TX_EN_PIN, GPIO_PIN_SET);
    s_enabled = 1;
    LED_SetTxState(1);
}

void RF_Disable(void)
{
    HAL_GPIO_WritePin(TX_EN_PORT, TX_EN_PIN, GPIO_PIN_RESET);
    s_enabled = 0;
    LED_SetTxState(0);
}

void RF_SetChannel(RF_Channel_t ch)
{
    s_channel = ch;
    /* bit 0 → CH_A, bit 1 → CH_B */
    HAL_GPIO_WritePin(CH_A_PORT, CH_A_PIN,
        ((uint8_t)ch & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CH_B_PORT, CH_B_PIN,
        ((uint8_t)ch & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void RF_SetPower(RF_Power_t pwr)
{
    s_power = pwr;
    /* PWR1→0, PWR2→1, PWR3→2, PWR4→3  (subtract 1 to get 0-based bits) */
    uint8_t v = (uint8_t)pwr - 1U;
    HAL_GPIO_WritePin(PWR_A_PORT, PWR_A_PIN,
        (v & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWR_B_PORT, PWR_B_PIN,
        (v & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void RF_Start(RF_Channel_t ch, RF_Power_t pwr)
{
    RF_SetChannel(ch);
    RF_SetPower(pwr);
    RF_Enable();
}

void RF_Stop(void)
{
    HAL_GPIO_WritePin(TX_EN_PORT, TX_EN_PIN,  GPIO_PIN_RESET); /* TX off */
    HAL_GPIO_WritePin(CH_A_PORT,  CH_A_PIN,   GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CH_B_PORT,  CH_B_PIN,   GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PWR_A_PORT, PWR_A_PIN,  GPIO_PIN_SET);   /* PWR active-LOW: HIGH = off */
    HAL_GPIO_WritePin(PWR_B_PORT, PWR_B_PIN,  GPIO_PIN_SET);
    s_enabled = 0;
    LED_SetTxState(0);
}

uint8_t RF_IsEnabled(void) { return s_enabled; }
