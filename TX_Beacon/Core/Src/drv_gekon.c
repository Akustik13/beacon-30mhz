#include "drv_gekon.h"
#include "stm32wbxx_hal.h"

/*
 * Gekon = magnetic reed switch on PA0 (WKUP1), active-LOW (closes to GND).
 * Stop1 wakeup: EXTI0 falling edge.
 * Shutdown wakeup: WKUP1=PA0, falling edge (CR4_WP1=1) = button press brings PA0 LOW.
 */

#define GEKON_PORT   GPIOA
#define GEKON_PIN    GPIO_PIN_0

static volatile uint8_t s_flag = 0;   /* set by EXTI ISR, cleared by GEKON_Poll */

void GEKON_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin   = GEKON_PIN;
    g.Mode  = GPIO_MODE_IT_FALLING;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GEKON_PORT, &g);

    HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

uint8_t GEKON_IsPressed(void)
{
    return (HAL_GPIO_ReadPin(GEKON_PORT, GEKON_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
}

uint8_t GEKON_IsPending(void)    { return s_flag; }
void    GEKON_ClearPending(void) { s_flag = 0; }

void GEKON_EXTI_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GEKON_PIN)) {
        __HAL_GPIO_EXTI_CLEAR_IT(GEKON_PIN);
        s_flag = 1;
    }
}

GekonEvent_t GEKON_Poll(void)
{
    if (!s_flag)
        return GEKON_NONE;

    /* Measure total hold time from now (≈ EXTI fire time).
     *  < 20ms  : bounce → NONE
     *  20–500ms: SHORT
     *  500ms–3s: medium → NONE
     *  >= 3s   : LONG (returned immediately, button may still be held) */
    uint32_t t0 = HAL_GetTick();

    while (GEKON_IsPressed()) {
        if ((HAL_GetTick() - t0) >= GEKON_LONG_MS) {
            s_flag = 0;
            return GEKON_LONG;
        }
    }

    uint32_t held_ms = HAL_GetTick() - t0;
    s_flag = 0;

    if (held_ms < GEKON_DEBOUNCE_MS)   return GEKON_NONE;
    if (held_ms < GEKON_SHORT_MAX_MS)  return GEKON_SHORT;
    return GEKON_NONE;
}
