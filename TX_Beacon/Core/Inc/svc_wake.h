#pragma once
#include <stdint.h>
#include "lis2dw12.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Wake-on-motion service
 *
 * Sits above the LIS2DW12 driver.  Owns the wake config in RAM; bridges
 * OP_WAKE_CFG_GET/SET and OP_WAKE_STATUS/CLEAR to driver calls.
 *
 * Conflict rule (mirrors the driver): wake-on-motion cannot be armed while
 * the sensor supply (PA3) is off.  SvcWake_SetConfig() enforces this and
 * returns CMD_ERR_STATE on conflict.
 *
 * OP_ACCEL_POWER (on=0) also refuses if WoM is armed (returns CMD_ERR_STATE).
 * The two states are mutually exclusive by design — keeping the sensor
 * continuously powered when WoM is armed is intentional (it cannot detect
 * motion if unpowered).
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  armed;           /* 1 = wake-on-motion active                     */
    uint32_t trigger_count;   /* total events since last WAKE_CLEAR             */
    uint32_t last_trigger_ts; /* HAL_GetTick() at last event (0 = never)       */
} WakeStatus_t;

/* Call once at startup, after LIS2DW12 driver is initialised. */
void SvcWake_Init(void);

/*
 * Apply a new wake configuration.
 * Returns CMD_OK, CMD_ERR_STATE (sensor off + enable=1), or CMD_ERR_PARAM.
 */
uint8_t SvcWake_SetConfig(const LIS2DW12_WakeCfg_t *cfg);

/* Read back the current wake configuration. */
void SvcWake_GetConfig(LIS2DW12_WakeCfg_t *out);

/* Read status counters. */
void SvcWake_GetStatus(WakeStatus_t *out);

/*
 * Clear wake interrupt latch and reset trigger counters.
 * Must be called from the EXTI2 ISR (via LIS2DW12_ClearWakeIRQ) and can
 * also be called by OP_WAKE_CLEAR to reset counters without ISR context.
 */
void SvcWake_Clear(void);

/*
 * Called by the EXTI2_IRQHandler after LIS2DW12_ClearWakeIRQ().
 * Updates trigger_count and last_trigger_ts, then executes the action bitmask:
 *   WAKE_ACTION_WAKE_MCU   — handled automatically by EXTI exit from Stop1
 *   WAKE_ACTION_TX_BURST   — sets g_wake_tx_pending flag (checked in main loop)
 *   WAKE_ACTION_LOG_MARKER — writes a marker record to the flash log
 */
void SvcWake_OnEvent(void);

/* Set to 1 by SvcWake_OnEvent() when WAKE_ACTION_TX_BURST is armed.
 * Main loop checks and clears this. */
extern volatile uint8_t g_wake_tx_pending;
