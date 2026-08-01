#pragma once
#include <stdint.h>

typedef enum {
    LED_OFF       = 0,
    LED_ON        = 1,
    LED_HEARTBEAT = 2,   /* 100ms pulse every 800ms                */
    LED_TX        = 3,   /* mirrors RF state: on while TX, off idle */
} LED_Mode_t;

void       LED_Init(void);
void       LED_SetMode(LED_Mode_t mode);
LED_Mode_t LED_GetMode(void);
void       LED_SetTxState(uint8_t on);   /* called by RF_Start/RF_Stop */
void       LED_Update(void);
void       LED_Blink(uint8_t count, uint32_t on_ms, uint32_t off_ms); /* blocking */

/* BLE overlay: suppresses LED_Update() so BLE can control the LED directly.
 * enable=1: LED_Update is a no-op (BLE controls GPIO via LED_WriteGpio).
 * enable=0: LED state machine resumes from current s_mode. */
void LED_SetBleOverride(uint8_t enable);
void LED_WriteGpio(uint8_t on);   /* direct GPIO write — for BLE blink signals */
