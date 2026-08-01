#pragma once
#include <stdint.h>

typedef enum {
    RF_CH0 = 0,   /* PA6=0, PB8=0 */
    RF_CH1 = 1,   /* PA6=1, PB8=0 */
    RF_CH2 = 2,   /* PA6=0, PB8=1 */
    RF_CH3 = 3,   /* PA6=1, PB8=1 */
} RF_Channel_t;

typedef enum {
    RF_PWR1 = 1,  /* PA1=0, PA8=0 — lowest  */
    RF_PWR2 = 2,  /* PA1=1, PA8=0           */
    RF_PWR3 = 3,  /* PA1=0, PA8=1           */
    RF_PWR4 = 4,  /* PA1=1, PA8=1 — highest */
} RF_Power_t;

void    RF_Init(void);
void    RF_Enable(void);
void    RF_Disable(void);
void    RF_SetChannel(RF_Channel_t ch);
void    RF_SetPower(RF_Power_t pwr);
void    RF_Start(RF_Channel_t ch, RF_Power_t pwr);
void    RF_Stop(void);
uint8_t RF_IsEnabled(void);
