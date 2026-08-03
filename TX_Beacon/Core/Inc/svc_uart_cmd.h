#pragma once
#include <stdint.h>

/* TX modes -- stored in g_tx_mode */
#define TX_MODE_PULSE  0U   /* brief pulse, sleep with TX off             */
#define TX_MODE_CONT   1U   /* TX on continuously; MCU stays awake        */
#define TX_MODE_ECO    2U   /* pulse: MCU sleeps during TX (GPIO holds)   */
#define TX_MODE_OFF    3U   /* TX permanently disabled; saved to flash     */

/* Runtime state -- defined in main.c, read/written by UART commands */
extern uint32_t g_tx_duration_ms;  /* TX on time (ms)                   */
extern uint32_t g_tx_period_ms;    /* pause between TX sessions (ms)    */
extern uint32_t g_cont_on_s;       /* unused placeholder                */
extern uint8_t  g_ch_idx;          /* channel index 0-3                 */
extern uint8_t  g_pwr_idx;         /* power index 0-3                   */
extern uint8_t  g_tx_mode;         /* TX_MODE_PULSE / CONT / ECO / OFF  */

/* TX pause flag -- set by 'tx off', cleared by 'tx on' or any 'mode' cmd   */
extern uint8_t  g_tx_paused;

/* 1 = send RTC date/time every 1s while UART is connected                   */
extern uint8_t  g_rtc_live;

/* Set to 1 by any parameter-changing command; main loop calls FlashConfig_Save() */
extern volatile uint8_t g_config_save_pending;

/* Set to 1 by _apply_cfg(); UartCmd_Wait/WFI loops break early so new period takes
 * effect immediately instead of after the current wait cycle completes. */
extern volatile uint8_t g_cfg_changed;

/* Deferred flash writes — set when BLE is connected; main loop executes after disconnect */
extern volatile uint8_t g_log_cfg_save_pending;   /* FlashLog_CommitConfig() deferred  */
extern volatile uint8_t g_hwdesc_save_pending;    /* HwDesc_Save() deferred            */
extern volatile uint8_t g_reboot_after_save;      /* 1 = NVIC_SystemReset after config saved */
extern volatile uint8_t g_log_erase_pending;      /* FlashLog_Clear() deferred         */

/* Schedule masks (0 = dimension not filtered = always active)               */
extern uint32_t g_active_hours_mask;   /* bit N = hour N active (0-23)       */
extern uint8_t  g_active_days_mask;    /* bit N = weekday N active (0=Mon..6=Sun) */
extern uint16_t g_active_months_mask;  /* bit N = month N active (0=Jan..11=Dec) */

/* Schedule enable + scope (added by binary protocol) */
extern uint8_t g_sched_en;      /* 0 = disabled (TX always); 1 = mask-gated */
extern uint8_t g_sched_scope;   /* 0 = TX only;  1 = TX + sensor logging    */

void UartCmd_Init(void);
void UartCmd_Poll(void);            /* call every main loop iteration        */
void UartCmd_Wait(uint32_t ms);     /* busy-wait ms milliseconds, poll UART  */

/* Schedule check -- returns 1 if TX is allowed now (RTC + masks).
 * Returns 1 if no schedule is configured or RTC is not set.        */
uint8_t  Schedule_IsActive(void);

/* Seconds until the next active hour starts. Returns 3600 if unknown. */
uint32_t Schedule_SecsToNextSlot(void);
