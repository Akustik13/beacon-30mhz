#pragma once
#include <stdint.h>

typedef enum {
    GEKON_NONE  = 0,
    GEKON_SHORT = 1,   /* press < GEKON_SHORT_MAX_MS */
    GEKON_LONG  = 2,   /* press >= GEKON_LONG_MS     */
} GekonEvent_t;

#define GEKON_DEBOUNCE_MS    20U   /* < 20ms: bounce, ignored           */
#define GEKON_SHORT_MAX_MS  500U   /* 20–500ms: short press             */
#define GEKON_LONG_MS      3000U   /* >= 3000ms: long press → Shutdown  */

void         GEKON_Init(void);
GekonEvent_t GEKON_Poll(void);
uint8_t      GEKON_IsPressed(void);
uint8_t      GEKON_IsPending(void);     /* 1 if ISR set flag, not yet consumed by Poll */
void         GEKON_ClearPending(void);  /* discard pending event without processing    */
void         GEKON_EXTI_IRQHandler(void);
