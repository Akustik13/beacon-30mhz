#ifndef APP_BLE_H
#define APP_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── BLE operating mode bitmask ──────────────────────────────────────────── *
 * OFF=0x00 is the zero value (all bits clear).  SCHEDULE and GEKON may be   *
 * combined (0x06).  CONTINUOUS is exclusive: it conflicts with SCHED/GEKON. *
 * Stored in g_ble_op_mode (uint8_t) and ble_op_mode field of TxConfigV2_t. */
#define BLE_OP_OFF        0x00U  /* BLE never activates                          */
#define BLE_OP_CONTINUOUS 0x01U  /* Always advertising; restart after disconnect  */
#define BLE_OP_SCHEDULE   0x02U  /* Wake every ble_interval_s, adv ble_duration   */
#define BLE_OP_GEKON      0x04U  /* 2 gekon presses within 1.5 s → advertising  */

/* ── BLE LED indicator mode ──────────────────────────────────────────────── */
#define BLE_LED_NORMAL  0U  /* adv: 1 short blink/period; connect: 3 blinks then off */
#define BLE_LED_OFF     1U  /* no BLE LED indication; main LED mode always active */
#define BLE_LED_TRIPLE  2U  /* 3 quick blinks at adv start, then CPU1 can sleep */

/* ── Runtime BLE settings (Task 5) — loaded from flash, saved via FlashConfig_Save() */
extern uint8_t  g_ble_op_mode;          /* BleOpMode_t value                 */
extern uint8_t  g_ble_tx_power;         /* aci_hal_set_tx_power_level param  */
extern uint16_t g_ble_interval_s;       /* SCHEDULE: seconds between windows */
extern uint16_t g_ble_duration_sec;     /* SCHEDULE/GEKON: window duration   */
extern uint16_t g_ble_adv_interval_ms;  /* advertising interval in ms        */
extern uint8_t  g_ble_name_mode;        /* 0=auto "BCN_XXXX", 1=manual       */
extern char     g_ble_name[12];         /* manual device name (null-term)    */
extern uint8_t  g_ble_led_mode;         /* BLE_LED_NORMAL, BLE_LED_OFF, BLE_LED_TRIPLE */

/* ── Initialization ──────────────────────────────────────────────────────── */

/* Reset IPCC state before SystemClock_Config() — call at very start of main() */
void BLE_SystemPreInit(void);

/* Full BLE stack bring-up: IPCC, TL, SHCI, HCI, GAP/GATT, beacon service,
 * advertising. Blocks until CPU2 SHCI_READY (max 10 s), then returns.
 * LED management is NOT done here — use BLE_StartSession() instead. */
void BLE_AppInit(void);

/* ── Session control (Task 2) ────────────────────────────────────────────── */

/* Start a BLE advertising session.
 * timeout_ms = 0: no timeout (continuous until disconnect or BLE_StopSession).
 * timeout_ms > 0: stop advertising (and shut down CPU2) after this many ms
 *                 if no client connects.
 * On disconnect in non-continuous mode, session also ends.
 * Calls BLE_SystemPreInit() + BLE_AppInit() internally. */
void BLE_StartSession(uint32_t timeout_ms);

/* Forcefully end the current session: stop advertising, restore LED.
 * CPU2 stays alive so the next BLE_StartSession() can restart advertising. */
void BLE_StopSession(void);

/* Completely shut down CPU2: drain events, wait for CPU2 deep-sleep.
 * Must be called before device Shutdown/Sleep and when BLE mode → OFF.
 * No-op if CPU2 is not initialized.  Next BLE_StartSession() will do full init. */
void BLE_ForceShutdown(void);

/* ── State queries ───────────────────────────────────────────────────────── */
uint8_t  BLE_IsIdle(void);          /* 1 when not advertising and not connected  */
uint8_t  BLE_IsConnected(void);     /* 1 when a client is connected              */
uint8_t  BLE_IsAdvertising(void);   /* 1 when advertising (not connected)        */
uint8_t  BLE_CPU2Initialized(void); /* 1 when BLE stack is up (CPU2 in Stop1)    */
uint16_t BLE_GetConnHandle(void);   /* current connection handle, 0xFFFF if not connected */

/* Run pending BLE events — call every main loop iteration.
 * Also checks session timeout and NUS inactivity timeout. */
void BLE_ProcessEvents(void);

/* Called by NUS RX handler on every received byte — resets the NUS inactivity
 * watchdog so a legitimate client is never dropped mid-session. */
void BLE_NusMarkActivity(void);

/* Non-blocking BLE LED state machine — call from main loop every iteration.
 * NORMAL: blinks LED once per adv period; suppresses main LED driver.
 * TRIPLE: no-op (blinks already done at session start in BLE_StartSession).
 * OFF:    no-op (main LED driver runs normally; CPU1 may sleep). */
void BLE_LED_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_BLE_H */
