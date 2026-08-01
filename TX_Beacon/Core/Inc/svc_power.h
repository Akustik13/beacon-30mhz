#pragma once
#include <stdint.h>
#include "stm32wbxx_hal.h"

typedef enum {
    WAKEUP_RTC     = 0,   /* RTC wakeup timer fired */
    WAKEUP_GEKON   = 1,   /* PA11 EXTI fired        */
    WAKEUP_UNKNOWN = 2,
} WakeupReason_t;

extern WakeupReason_t    g_wakeup_reason;
extern RTC_HandleTypeDef hrtc;               /* owned here, used by stm32wbxx_it.c */
extern uint32_t          g_stop1_accumulated_s; /* total Stop1 seconds this session */

void Power_Init(void);
void Power_EnterStop2(uint32_t seconds);
void Power_EnterStop2_ms(uint32_t ms);
/* Like Power_EnterStop2_ms but without PA10 EXTI wakeup — use when RF is ON
 * (ECO TX phase) to prevent RF-coupled rising edges on PA10 from waking MCU. */
void Power_EnterStop2_ms_tx(uint32_t ms);
void Power_EnterStop1(uint32_t seconds);
void Power_EnterShutdown(void);
uint8_t Power_WokeFromShutdown(void);
void Power_PrintDiag(void);

/* Convert current RTC to Unix timestamp; returns 0 if RTC not set. */
uint32_t Power_RTC_GetUnix(void);
/* Write current RTC timestamp to BKP register before Shutdown. */
void Power_SaveShutdownTimestamp(void);
/* Return seconds elapsed since the saved shutdown timestamp (0 if unavailable). */
uint32_t Power_GetShutdownElapsedS(void);

/* ── RTC time/date ───────────────────────────────────────────────────────── */

/* Returns 1 if user has ever set the RTC (survives Shutdown via BKP reg). */
uint8_t Power_RTC_IsSet(void);

/* Set date and time. wday: 1=Mon ... 7=Sun (HAL convention). */
void Power_RTC_SetDateTime(uint16_t year, uint8_t month, uint8_t day,
                           uint8_t hour, uint8_t min, uint8_t sec, uint8_t wday);

/* Read current time and date. Must call GetTime before GetDate (RTC shadow). */
void Power_RTC_GetDateTime(RTC_TimeTypeDef *t, RTC_DateTypeDef *d);

/* Print current date/time + "SET/NOT-SET" to UART. */
void Power_RTC_PrintDateTime(void);
