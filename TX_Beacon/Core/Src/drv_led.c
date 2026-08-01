#include "drv_led.h"
#include "stm32wbxx_hal.h"

#define LED_PORT  GPIOB
#define LED_PIN   GPIO_PIN_0

static LED_Mode_t s_mode        = LED_OFF;
static uint32_t   s_tick        = 0;
static uint8_t    s_state       = 0;
static uint8_t    s_tx_active   = 0;  /* set by LED_SetTxState() */
static uint8_t    s_ble_override = 0; /* set by LED_SetBleOverride(); suppresses LED_Update */

void LED_Init(void)
{
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    s_mode      = LED_OFF;
    s_state     = 0;
    s_tick      = 0;
    s_tx_active = 0;
}

void LED_SetMode(LED_Mode_t mode)
{
    s_mode  = mode;
    s_tick  = HAL_GetTick();
    s_state = 0;
    switch (mode) {
    case LED_OFF:
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
        break;
    case LED_ON:
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
        break;
    case LED_TX:
        /* Immediately mirror current TX state */
        HAL_GPIO_WritePin(LED_PORT, LED_PIN,
            s_tx_active ? GPIO_PIN_SET : GPIO_PIN_RESET);
        break;
    case LED_HEARTBEAT:
        /* Start first beat immediately visible — GPIO set here, state machine runs in LED_Update() */
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
        break;
    default:
        break;
    }
}

LED_Mode_t LED_GetMode(void) { return s_mode; }

void LED_SetTxState(uint8_t on)
{
    s_tx_active = on;
    if (s_mode == LED_TX)
        HAL_GPIO_WritePin(LED_PORT, LED_PIN,
            on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void LED_SetBleOverride(uint8_t enable)
{
    s_ble_override = enable ? 1U : 0U;
    if (!enable) {
        /* Force GPIO off so main LED driver starts from a known-off state.
         * LED_SetMode() or LED_Update() will restore the correct level. */
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
        s_state = 0U;
        s_tick  = HAL_GetTick();
    }
}

void LED_WriteGpio(uint8_t on)
{
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void LED_Update(void)
{
    if (s_ble_override) return;  /* BLE is controlling LED directly */

    uint32_t now     = HAL_GetTick();
    uint32_t elapsed = now - s_tick;

    switch (s_mode) {
    case LED_OFF:
    case LED_ON:
    case LED_TX:        /* driven by LED_SetTxState(), nothing here */
        break;

    case LED_HEARTBEAT:
        /* Heartbeat: ON-80ms → OFF-120ms → ON-80ms → OFF-720ms (~60 BPM) */
        switch (s_state) {
        case 0U:   /* first beat ON */
            if (elapsed >= 80U) {
                s_state = 1U; s_tick = now;
                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
            }
            break;
        case 1U:   /* gap between beats */
            if (elapsed >= 120U) {
                s_state = 2U; s_tick = now;
                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
            }
            break;
        case 2U:   /* second beat ON */
            if (elapsed >= 80U) {
                s_state = 3U; s_tick = now;
                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
            }
            break;
        case 3U:   /* long pause */
            if (elapsed >= 720U) {
                s_state = 0U; s_tick = now;
                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
            }
            break;
        default:
            s_state = 0U;
            break;
        }
        break;
    }
}

void LED_Blink(uint8_t count, uint32_t on_ms, uint32_t off_ms)
{
    for (uint8_t i = 0; i < count; i++) {
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
        HAL_Delay(on_ms);
        HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
        if (i < (count - 1U))
            HAL_Delay(off_ms);
    }
}
