/*
 * TX_Beacon — Phase 1, Step 6: UART commands
 *
 * Modes:
 *   pulse — TX ON briefly, sleep with TX off (default)
 *   cont  — TX ON continuously, MCU stays awake
 *   eco   — TX ON, MCU sleeps Stop1 during TX (GPIO holds), wakes, TX off, pause
 *
 * UART commands: help, status, mode, tx, ch, pwr, pulse, period, sleep, regs
 * PA0: 20-500ms = short press | 3s+ = Shutdown
 */

#include "main.h"
#include "svc_power.h"
#include "svc_uart_cmd.h"
#include "flash_config.h"
#include "hw_desc.h"
#include "drv_rf_tx.h"
#include "drv_led.h"
#include "drv_uart.h"
#include "drv_gekon.h"
#include "drv_chip_temp.h"
#include "drv_batt_adc.h"
#include "drv_light_adc.h"
#include "flash_log.h"
#include "cmd_layer.h"
#include "svc_wake.h"
#include "lis2dw12.h"
#include "app_ble.h"
#include "ble_beacon_service.h"
#include "proto_structs.h"
#include "otp.h"
#include "stm32wbxx_ll_rcc.h"
#include <limits.h>
#include <string.h>

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);

/* I2C1 handle — used by lis2dw12.c (declared extern there) */
I2C_HandleTypeDef hi2c1;

/* Redirect printf to USART1 (needed by flash_test.c, kept for diagnostics) */
int _write(int fd, const char *buf, int len)
{
    (void)fd;
    if (!UART_IsActive()) return len;
    for (int i = 0; i < len; i++) {
        while (!(USART1->ISR & USART_ISR_TXE_TXFNF));
        USART1->TDR = (uint8_t)buf[i];
    }
    return len;
}

/* ── Runtime state (read/written by svc_uart_cmd) ────────────────────────── */
uint32_t g_tx_duration_ms = 20U;    /* TX on time (ms)                  */
uint32_t g_tx_period_ms   = 2000U;  /* pause between TX sessions (ms)   */
uint32_t g_cont_on_s      = 5U;     /* unused placeholder               */
uint8_t  g_ch_idx         = 0U;     /* channel index 0-3                */
uint8_t  g_pwr_idx        = 3U;     /* power index 0-3  (default PWR4)  */
uint8_t  g_tx_mode        = TX_MODE_PULSE;

static const RF_Channel_t k_channels[] = { RF_CH0, RF_CH1, RF_CH2, RF_CH3 };
static const RF_Power_t   k_powers[]   = { RF_PWR1, RF_PWR2, RF_PWR3, RF_PWR4 };
static const char        *k_ch_name[]  = { "CH0", "CH1", "CH2", "CH3" };
static const char        *k_pwr_name[] = { "PWR1", "PWR2", "PWR3", "PWR4" };

/* Sleep duration for TX_MODE_OFF: limited by the shortest active sensor period.
 * Returns 3600 s if no periodic sensors are enabled. */
static uint32_t _off_sleep_secs(void)
{
    uint32_t s = 3600U;
    if (g_temp_mode  == TEMP_MODE_PERIODIC  && (uint32_t)g_temp_period_s  < s) s = g_temp_period_s;
    if (g_batt_mode  == BATT_MODE_PERIODIC  && (uint32_t)g_batt_period_s  < s) s = g_batt_period_s;
    if (g_light_mode == LIGHT_MODE_PERIODIC && (uint32_t)g_light_period_s < s) s = g_light_period_s;
    if (s < 1U) s = 1U;
    return s;
}

int main(void)
{
    HAL_Init();

    /* ST BLE projects clear OPTVERR and reset stale IPCC state before HSE.
     * BLE_SystemPreInit() clears all IPCC channels so CPU2 starts fresh.
     * OTP HSE tuning must be applied before SystemClock_Config() enables HSE. */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);
    BLE_SystemPreInit();
    {
        OTP_ID0_t *p_otp = (OTP_ID0_t *)OTP_Read(0);
        if (p_otp != NULL) {
            LL_RCC_HSE_SetCapacitorTuning(p_otp->hse_tuning);
        }
    }

    SystemClock_Config();
    MX_GPIO_Init();
    MX_I2C1_Init();
    Power_Init();

    RF_Init();
    LED_Init();
    GEKON_Init();
    BattAdc_Init();
    LightAdc_Init();

    uint8_t from_shutdown = Power_WokeFromShutdown();

    /* Load flash config BEFORE UART banner so saved mode/ch/pwr are active */
    uint8_t cfg_ok  = FlashConfig_Load();
    uint8_t hw_ok   = HwDesc_Load();

    /* ── Auto-detect accelerometer on I2C bus ──────────────────────────────── *
     * Probe both LIS2DW12 addresses (0x18 / 0x19 via SA0 pin).               *
     * Write result to HwDesc flash only if it differs from stored value.      *
     * If found — power on with default config to enable full functionality.   */
    LIS2DW12_ProbeResult_t accel_probe = {0};
    LIS2DW12_Probe(&accel_probe);
    {
        uint8_t hw_changed = 0U;
        if (accel_probe.ok) {
            if (g_hw_desc.accel_type != HW_ACCEL_LIS2DW12) {
                g_hw_desc.accel_type = HW_ACCEL_LIS2DW12;
                hw_changed = 1U;
            }
            if (strncmp(g_hw_desc.accel_model, "LIS2DW12TR",
                        sizeof(g_hw_desc.accel_model)) != 0) {
                strncpy(g_hw_desc.accel_model, "LIS2DW12TR",
                        sizeof(g_hw_desc.accel_model) - 1U);
                g_hw_desc.accel_model[sizeof(g_hw_desc.accel_model) - 1U] = '\0';
                hw_changed = 1U;
            }
            static const LIS2DW12_Config_t k_accel_cfg = {
                .odr     = LIS2DW12_ODR_12HZ5,
                .mode    = LIS2DW12_MODE_LP,
                .lp_mode = LIS2DW12_LP1,
                .fs      = LIS2DW12_FS_4G,
                .bw      = LIS2DW12_BW_ODR_4,
            };
            LIS2DW12_PowerOn(&k_accel_cfg);
        } else {
            if (g_hw_desc.accel_type != HW_ACCEL_NONE) {
                g_hw_desc.accel_type     = HW_ACCEL_NONE;
                g_hw_desc.accel_model[0] = '\0';
                hw_changed = 1U;
            }
        }
        if (hw_changed) {
            HwDesc_Save();
            hw_ok = 1U;
        }
    }

    SvcWake_Init();
    CmdLayer_Init();

    /* Account for time spent in Shutdown between sessions (RTC-based) */
    if (from_shutdown) {
        uint32_t shutdown_s = Power_GetShutdownElapsedS();
        if (shutdown_s > 0U) {
            g_hw_desc.total_shutdown_h += shutdown_s / 3600U;
            HwDesc_Save();   /* commit to flash immediately — protects against
                              * unexpected reset before the next Shutdown entry */
        }
    }
    LogConfig_t log_cfg_def = LOG_CFG_DEFAULT;
    FlashLog_Init(&log_cfg_def);
    if (!cfg_ok) {
        LED_SetMode(LED_OFF);
    }

    LED_Blink(3, 100, 200);

    if (UART_IsConnected()) {
        UART_Init();
        UartCmd_Init();
        UART_Print("\r\n========================================\r\n");
        UART_Print("  TX_Beacon v1.0 — Step 6: UART cmd\r\n");
        UART_Print("  PA0: 20-500ms=short  3s+=Shutdown\r\n");
        UART_Print("  Type 'help' for commands\r\n");
        UART_Print("========================================\r\n\r\n");
        if (from_shutdown) UART_Print("[BOOT] woke from Shutdown\r\n");
        if (cfg_ok) UART_Print("[CFG] loaded from flash\r\n");
        else        UART_Print("[CFG] no saved config — defaults\r\n");
        if (hw_ok)  { UART_Print("[HW]  descriptor loaded:\r\n"); HwDesc_Print(); }
        else        UART_Print("[HW]  no hw descriptor  (use 'hwdesc' to configure)\r\n");
        if (accel_probe.ok)
            UART_Printf("[ACCEL] LIS2DW12 found @ 0x%02X  boot %u ms — enabled\r\n",
                        (unsigned)accel_probe.i2c_addr, (unsigned)accel_probe.boot_ms);
        else
            UART_Print("[ACCEL] not found — disabled\r\n");
        UART_Print("\r\n");
    }

    /* HSEM clock + IRQ — CPU2 BLE stack uses HSEM for internal synchronization */
    __HAL_RCC_HSEM_CLK_ENABLE();
    HAL_NVIC_SetPriority(HSEM_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(HSEM_IRQn);

    /* BLE init depends on operating mode loaded from flash (Task 2):
     *   CONTINUOUS → start advertising immediately at boot
     *   SCHEDULE   → session starts on timer (main loop handles it)
     *   GEKON      → session starts on double-press within 1.5 s (main loop)
     *   OFF        → BLE disabled */
    if (g_ble_op_mode & BLE_OP_CONTINUOUS) {
        BLE_StartSession(0U);   /* 0 = no timeout */
    }

    /* 30-second wake window: stays in main loop after button wakeup.
     * Allows time to connect UART before MCU returns to schedule sleep. */
    uint32_t gekon_wake_until = 0U;

    if (from_shutdown) {
        HAL_Delay(500);
        GEKON_ClearPending();   /* clear bounce — event counts as press #1 */
        gekon_wake_until = HAL_GetTick() + 5000U;
    }

    Power_PrintDiag();

/* When UART active or BLE active: busy-wait so events are processed.
     * When deployed without UART/BLE: Stop1 sleep to save power.
     * Validate GEKON before skipping sleep: RF coupling at max power can cause
     * spurious falling edges on PA0, setting s_flag without a real press.
     * If flag is set but PA0 is HIGH (glitch), clear it and sleep normally.
     * When collecting gekon double-press (s_gcnt>0): cap wait to the BLE window
     * so the timeout check at the top of the loop fires within 1.5 s, not
     * after the full g_tx_period_ms which can be much longer. */
#define SLEEP_OR_WAIT_MS(ms) \
    do { \
        if (GEKON_IsPending() && !GEKON_IsPressed()) GEKON_ClearPending(); \
        if (!GEKON_IsPending()) { \
            if (s_gcnt > 0U) { \
                uint32_t _sw = (ms); \
                if (_sw > GEKON_BLE_WIN_MAX_MS) _sw = GEKON_BLE_WIN_MAX_MS; \
                UartCmd_Wait(_sw); \
            } else if (UART_IsActive()) { \
                UartCmd_Wait(ms); \
            } else if (!BLE_IsIdle()) { \
                if (g_ble_led_mode == BLE_LED_NORMAL) { \
                    UartCmd_Wait(ms); \
                } else { \
                    /* OFF/TRIPLE: WFI sleep while BLE active. \
                     * Stop1 overcounts uwTick on early IPCC wakeup (adds full ms \
                     * even when woken at 50ms) → session timeout fires 20× too fast. \
                     * WFI keeps SysTick running: HAL_GetTick() stays accurate, \
                     * IPCC wakes CPU1 within 1 IRQ latency to service events. */ \
                    uint32_t _ble_t0 = HAL_GetTick(); \
                    uint32_t _ble_ms = (ms) < 1000U ? (ms) : 1000U; \
                    while ((HAL_GetTick() - _ble_t0) < _ble_ms) { \
                        BLE_ProcessEvents(); \
                        LED_Update(); \
                        if (BLE_IsIdle()) break; \
                        __WFI(); \
                    } \
                } \
            } else { \
                Power_EnterStop2_ms(ms); \
            } \
        } \
    } while (0)

    /* ── BLE gekon double-press state ──────────────────────────────────────
     * Always available regardless of g_ble_op_mode (even BLE_OP_OFF).
     * 2nd press valid only in [GEKON_BLE_WIN_MIN_MS .. GEKON_BLE_WIN_MAX_MS]
     * after press #1. MCU busy-waits during this window (no Stop1 sleep) so
     * uwTick advances in real time and the check is accurate. */
#define GEKON_BLE_WIN_MIN_MS  350U    /* min gap: avoids single-press bounce    */
#define GEKON_BLE_WIN_MAX_MS  1500U   /* max gap: window closes after this      */
    static uint8_t  s_gcnt    = 0U;
    static uint32_t s_gcnt_t0 = 0U;   /* HAL_GetTick() of press #1 */

    /* Gekon wakeup from Shutdown counts as press #1 regardless of BLE op mode.
     * WUF1 unreliable on WB15 — assume all Shutdown wakeups are gekon-triggered
     * (no RTC alarm is armed before Shutdown in this firmware). */
    if (from_shutdown) {
        s_gcnt    = 1U;
        s_gcnt_t0 = HAL_GetTick();
        g_wakeup_reason = WAKEUP_GEKON;
        UART_Print("[GEKON] cnt=1 (from shutdown)\r\n");
    }

    while (1)
    {
        UART_TryReconnect();
        UartCmd_Poll();
        LED_Update();
        BLE_ProcessEvents();
        BLE_LED_Update();

        /* Gekon double-press timeout: window expired → reset so TX resumes. */
        if (s_gcnt > 0U &&
            (HAL_GetTick() - s_gcnt_t0) > GEKON_BLE_WIN_MAX_MS) {
            UART_Print("[GEKON] cnt reset (timeout)\r\n");
            s_gcnt = 0U;
        }

        /* ── BLE SCHEDULE timer (Task 2) ────────────────────────────────────── *
         * Interval measured from END of last session.                            *
         * Time source: RTC unix time when available, HAL_GetTick()/1000 otherwise*
         * (uwTick is advanced through Stop1 sleep, so tick-based timing is safe).*
         * s_ble_sched_armed=0 at boot → fires immediately on first check.        */
        {
            static uint32_t s_ble_sched_rtc   = 0U;   /* time at last session end */
            static uint8_t  s_ble_prev_idle    = 1U;   /* was BLE idle last loop?  */
            static uint8_t  s_ble_sched_armed  = 0U;   /* 0 = fire on first check  */
            uint8_t ble_idle_now = BLE_IsIdle();

            if (g_ble_op_mode & BLE_OP_SCHEDULE) {
                /* Use RTC when available, else tick-based (survives Stop1 sleep) */
                uint32_t now_time = Power_RTC_IsSet() ? Power_RTC_GetUnix()
                                                      : (HAL_GetTick() / 1000U);

                /* Detect session-end transition (non-IDLE → IDLE): restart interval */
                if (!ble_idle_now) {
                    s_ble_prev_idle = 0U;
                } else if (!s_ble_prev_idle) {
                    /* Just became idle — record end time, pause starts now */
                    s_ble_prev_idle     = 1U;
                    s_ble_sched_rtc     = now_time;
                    s_ble_sched_armed   = 1U;
                    UART_Printf("[BLE] Session ended — pause %lu s\r\n",
                                (unsigned long)(g_ble_interval_s ? g_ble_interval_s : 1800U));
                }

                if (ble_idle_now) {
                    uint32_t iv_s = (g_ble_interval_s > 0U)
                                  ? (uint32_t)g_ble_interval_s
                                  : 1800UL;
                    /* !armed = first boot, always fire; armed = wait iv_s */
                    int fire = (!s_ble_sched_armed) ||
                               ((now_time - s_ble_sched_rtc) >= iv_s);
                    if (fire) {
                        s_ble_sched_rtc   = now_time;
                        s_ble_sched_armed = 1U;
                        s_ble_prev_idle   = 0U;
                        uint32_t dur_ms = (g_ble_duration_sec > 0U)
                                        ? (uint32_t)g_ble_duration_sec * 1000UL
                                        : 60000UL;
                        UART_Printf("[BLE] Schedule trigger — %lu s window\r\n",
                                    dur_ms / 1000UL);
                        BLE_StartSession(dur_ms);
                    }
                }
            } else {
                /* BLE_OP_SCHEDULE cleared — keep state fresh for next enable */
                s_ble_prev_idle = (uint8_t)ble_idle_now;
            }
        }

        /* 1 s StatusBlob notify when BLE client is connected */
        {
            static uint32_t s_ble_notify_tick = 0U;
            if (BLE_IsConnected() && (HAL_GetTick() - s_ble_notify_tick >= 1000U)) {
                s_ble_notify_tick = HAL_GetTick();
                StatusBlob_t blob = {0};
                blob.uptime_s     = HAL_GetTick() / 1000U;
                blob.temp_01c     = (int16_t)g_last_temp_x10;
                blob.vdda_mv      = (uint16_t)g_last_vdda_mV;
                blob.bat_mv       = (uint16_t)g_last_batt_mV;
                blob.bat_pct      = (uint8_t)g_last_batt_pct;
                blob.tx_active    = (uint8_t)RF_IsEnabled();
                blob.light_raw    = (uint16_t)g_last_light_raw;
                blob.sched_active = (uint8_t)Schedule_IsActive();
                blob.rtc_unix     = Power_RTC_GetUnix();
                BLE_Beacon_NotifyStatus(&blob);
                UART_Print("[BLE] Notify StatusBlob (24 bytes)\r\n");
            }
        }

        /* All deferred flash writes — guarded: never erase/config-write while BLE connected
         * (CPU2 busy → _flash_take_access forces CPU2 shutdown → BLE disconnect) */
        if (!BLE_IsConnected() && Flash_CPU2IsIdle()) {
            /* Flush log entries buffered in RAM during the last BLE session */
            FlashLog_FlushBleQueue();
            uint8_t did_save = 0U;
            if (g_config_save_pending) {
                g_config_save_pending = 0U;
                if (FlashConfig_Save()) { UART_Print("[CFG] auto-saved\r\n");  did_save = 1U; }
                else                      UART_Print("[CFG] auto-save FAILED\r\n");
            }
            if (g_log_cfg_save_pending) {
                g_log_cfg_save_pending = 0U;
                FlashLog_CommitConfig();
                UART_Print("[LOG] log-cfg saved\r\n");
            }
            if (g_hwdesc_save_pending) {
                g_hwdesc_save_pending = 0U;
                if (HwDesc_Save()) UART_Print("[HW] hwdesc saved\r\n");
                else               UART_Print("[HW] hwdesc save FAILED\r\n");
            }
            if (g_log_erase_pending) {
                g_log_erase_pending = 0U;
                int _er = FlashLog_Clear();
                UART_Printf("[LOG] deferred erase %s\r\n", _er == 0 ? "OK" : "FAILED");
            }
            if (g_reboot_after_save && did_save) {
                g_reboot_after_save = 0U;
                UART_Print("[CFG] reboot after apply\r\n");
                HAL_Delay(50U);
                NVIC_SystemReset();
            }
        }

        /* RTC live: print date/time once per second while UART connected */
        if (g_rtc_live && UART_IsActive()) {
            static uint32_t s_rtc_live_tick = 0U;
            uint32_t _rl = HAL_GetTick();
            if ((_rl - s_rtc_live_tick) >= 1000U) {
                s_rtc_live_tick = _rl;
                Power_RTC_PrintDateTime();
            }
        }

        /* Periodic sensors: temp first, then battery, then light */
        if (ChipTemp_TickPeriodic()) {
            int32_t tw = g_last_temp_x10 / 10;
            int32_t tf = g_last_temp_x10 >= 0 ? g_last_temp_x10 % 10 : -(g_last_temp_x10 % 10);
            UART_Printf("[TEMP] chip=%ld.%01ldC VDDA=%lumV\r\n",
                        (long)tw, (long)tf, (unsigned long)g_last_vdda_mV);
        }
        if (BattAdc_TickPeriodic())
            UART_Printf("[BATT] Battery: %lumV %d%% raw=%lu vref=%lu\r\n",
                        (unsigned long)g_last_batt_mV, (int)g_last_batt_pct,
                        (unsigned long)g_last_batt_raw, (unsigned long)g_last_vref_raw);
        if (LightAdc_TickPeriodic())
            UART_Printf("[LIGHT] Light: %u (raw) ~%lu lux\r\n",
                        (unsigned)g_last_light_raw, (unsigned long)g_last_light_lux);

        /* Live sensor refresh: keep STATUS fresh when UART or BLE connected,
         * regardless of configured logging intervals (feeds STAT? / BLE notify). */
        if (UART_IsActive() || BLE_IsConnected()) {
            static uint32_t s_live_tick = 0U;
            uint32_t _lt = HAL_GetTick();
            if ((_lt - s_live_tick) >= 2000U) {
                s_live_tick = _lt;
                int32_t t; uint32_t v;
                if (ChipTemp_MeasureNow(&t, &v)) {
                    g_last_temp_x10 = t + (int32_t)g_temp_offset_x10;
                    g_last_vdda_mV  = v;
                }
                BattAdc_MeasureNow(&g_last_batt_mV, &g_last_batt_pct);
                LightAdc_MeasureNow(&g_last_light_raw, &g_last_light_lux);
            }
        }

        /* Flash log: always run task (queues to RAM during BLE; flushed after disconnect) */
        FlashLog_Task();

        /* Periodic HwDesc save — interval set via GUI (uptime_save_min); 0 = 24 h default.
         * Deferred while BLE connected to avoid flash-stall disconnect. */
        {
            static uint32_t s_hwdesc_tick = 0U;
            uint32_t _ht   = HAL_GetTick();
            uint32_t iv_ms = (g_uptime_save_min > 0U)
                           ? (uint32_t)g_uptime_save_min * 60000UL
                           : 86400000UL;
            if (!BLE_IsConnected() && (_ht - s_hwdesc_tick >= iv_ms)) {
                s_hwdesc_tick = _ht;
                HwDesc_Save();
                UART_Printf("[HW] periodic uptime save (iv=%u min)\r\n",
                            g_uptime_save_min ? g_uptime_save_min : 1440U);
            }
        }

        if (g_tx_mode == TX_MODE_PULSE) {
            /* ── Schedule gate ──────────────────────────────────────────── */
            if (!g_tx_paused && !Schedule_IsActive() &&
                (int32_t)(gekon_wake_until - HAL_GetTick()) <= 0) {
                uint32_t sl = Schedule_SecsToNextSlot();
                if (sl < 60U)   sl = 60U;
                if (sl > 3600U) sl = 3600U;
                if ((g_ble_op_mode & BLE_OP_SCHEDULE) && g_ble_interval_s > 0U &&
                    (uint32_t)g_ble_interval_s < sl) {
                    sl = (uint32_t)g_ble_interval_s;
                }
                UART_CheckIdle(); LED_Update();
                if (UART_IsActive() || !BLE_IsIdle()) {
                    UartCmd_Wait(sl * 1000U); /* returns immediately if UART/BLE inactive */
                } else {
                    UART_Printf("[SCHED] inactive — sleep %lu min\r\n", sl / 60U);
                    Power_EnterStop2(sl);
                }
                UartCmd_Poll();
                GekonEvent_t gs = GEKON_Poll();
                if (gs == GEKON_SHORT) {
                    gekon_wake_until = HAL_GetTick() + 5000U;
                    /* BLE double-press — always available regardless of BLE op mode */
                    uint32_t _now = HAL_GetTick();
                    uint32_t _el  = _now - s_gcnt_t0;
                    if (s_gcnt == 0U) {
                        s_gcnt    = 1U;
                        s_gcnt_t0 = _now;
                        UART_Print("[GEKON] cnt=1 (waiting 2nd)\r\n");
                    } else if (_el >= GEKON_BLE_WIN_MIN_MS &&
                               _el <= GEKON_BLE_WIN_MAX_MS && BLE_IsIdle()) {
                        s_gcnt = 0U;
                        uint32_t _dur = g_ble_duration_sec > 0U ? (uint32_t)g_ble_duration_sec * 1000UL : 30000UL;
                        UART_Print("[BLE] Gekon 2x -> BLE start\r\n");
                        BLE_StartSession(_dur);
                    } else if (_el > GEKON_BLE_WIN_MAX_MS) {
                        s_gcnt    = 1U;
                        s_gcnt_t0 = _now;
                        UART_Print("[GEKON] cnt=1 (restart)\r\n");
                    }
                } else if (gs == GEKON_LONG) {
                    UART_Print("[GEKON] long press -> Shutdown\r\n");
                    LED_Blink(5, 50, 50); HAL_Delay(1000);
                    GEKON_ClearPending(); UART_CheckIdle(); BLE_ForceShutdown(); Power_EnterShutdown();
                }
                continue;
            }
            /* ── Pulse: fixed ch/pwr, brief TX then sleep ──────────────── */
            if (!g_tx_paused) {
                /* Skip TX while collecting gekon press sequence (any BLE mode) */
                uint8_t skip_tx = s_gcnt > 0U;
                if (!skip_tx) {
                    if (ChipTemp_TickBeforeTX()) {
                        int32_t tw = g_last_temp_x10 / 10;
                        int32_t tf = g_last_temp_x10 >= 0 ? g_last_temp_x10 % 10 : -(g_last_temp_x10 % 10);
                        UART_Printf("[TEMP] chip=%ld.%01ldC VDDA=%lumV\r\n",
                                    (long)tw, (long)tf, (unsigned long)g_last_vdda_mV);
                    }
                    RF_Start(k_channels[g_ch_idx], k_powers[g_pwr_idx]);
                    UART_Printf("[TX ON ] %s %s\r\n", k_ch_name[g_ch_idx], k_pwr_name[g_pwr_idx]);
                    HAL_Delay(g_tx_duration_ms);
                    RF_Stop();
                    UART_Printf("[TX OFF] %lums  pause %lums...\r\n", g_tx_duration_ms, g_tx_period_ms);
                }
            }
            UART_CheckIdle();
            LED_Update();
            SLEEP_OR_WAIT_MS(g_tx_period_ms);

            UartCmd_Poll();
            GekonEvent_t gev = GEKON_Poll();
            if (gev == GEKON_SHORT) {
                /* BLE double-press — always available regardless of BLE op mode.
                 * LED blink suppressed during sequence: 300 ms breaks timing window. */
                uint32_t _now = HAL_GetTick();
                uint32_t _el  = _now - s_gcnt_t0;
                if (s_gcnt == 0U) {
                    s_gcnt    = 1U;
                    s_gcnt_t0 = _now;
                    UART_Print("[GEKON] cnt=1 (waiting 2nd)\r\n");
                } else if (_el >= GEKON_BLE_WIN_MIN_MS &&
                           _el <= GEKON_BLE_WIN_MAX_MS && BLE_IsIdle()) {
                    s_gcnt = 0U;
                    uint32_t _dur = g_ble_duration_sec > 0U ? (uint32_t)g_ble_duration_sec * 1000UL : 30000UL;
                    UART_Print("[BLE] Gekon 2x -> BLE start\r\n");
                    BLE_StartSession(_dur);
                } else if (_el > GEKON_BLE_WIN_MAX_MS) {
                    s_gcnt    = 1U;
                    s_gcnt_t0 = _now;
                    UART_Print("[GEKON] cnt=1 (restart)\r\n");
                }
            } else if (gev == GEKON_LONG) {
                UART_Print("[GEKON] long press -> Shutdown\r\n");
                LED_Blink(5, 50, 50);
                HAL_Delay(1000);
                GEKON_ClearPending();
                UART_CheckIdle();
                BLE_ForceShutdown(); Power_EnterShutdown();
            }

        } else if (g_tx_mode == TX_MODE_ECO) {
            /* ── Schedule gate ──────────────────────────────────────────── */
            if (!g_tx_paused && !Schedule_IsActive() &&
                (int32_t)(gekon_wake_until - HAL_GetTick()) <= 0) {
                uint32_t sl = Schedule_SecsToNextSlot();
                if (sl < 60U)   sl = 60U;
                if (sl > 3600U) sl = 3600U;
                if ((g_ble_op_mode & BLE_OP_SCHEDULE) && g_ble_interval_s > 0U &&
                    (uint32_t)g_ble_interval_s < sl) {
                    sl = (uint32_t)g_ble_interval_s;
                }
                UART_CheckIdle(); LED_Update();
                if (UART_IsActive() || !BLE_IsIdle()) {
                    UartCmd_Wait(sl * 1000U); /* returns immediately if UART/BLE inactive */
                } else {
                    UART_Printf("[SCHED] inactive — sleep %lu min\r\n", sl / 60U);
                    Power_EnterStop2(sl);
                }
                UartCmd_Poll();
                GekonEvent_t gs = GEKON_Poll();
                if (gs == GEKON_SHORT) {
                    gekon_wake_until = HAL_GetTick() + 5000U;
                    /* BLE double-press — always available regardless of BLE op mode */
                    uint32_t _now = HAL_GetTick();
                    uint32_t _el  = _now - s_gcnt_t0;
                    if (s_gcnt == 0U) {
                        s_gcnt    = 1U;
                        s_gcnt_t0 = _now;
                        UART_Print("[GEKON] cnt=1 (waiting 2nd)\r\n");
                    } else if (_el >= GEKON_BLE_WIN_MIN_MS &&
                               _el <= GEKON_BLE_WIN_MAX_MS && BLE_IsIdle()) {
                        s_gcnt = 0U;
                        uint32_t _dur = g_ble_duration_sec > 0U ? (uint32_t)g_ble_duration_sec * 1000UL : 30000UL;
                        UART_Print("[BLE] Gekon 2x -> BLE start\r\n");
                        BLE_StartSession(_dur);
                    } else if (_el > GEKON_BLE_WIN_MAX_MS) {
                        s_gcnt    = 1U;
                        s_gcnt_t0 = _now;
                        UART_Print("[GEKON] cnt=1 (restart)\r\n");
                    }
                } else if (gs == GEKON_LONG) {
                    UART_Print("[GEKON] long press -> Shutdown\r\n");
                    LED_Blink(5, 50, 50); HAL_Delay(1000);
                    GEKON_ClearPending(); UART_CheckIdle(); BLE_ForceShutdown(); Power_EnterShutdown();
                }
                continue;
            }
            /* ── Eco: MCU sleeps Stop1 during TX; GPIO holds RF state ─────
             * TX on → MCU sleeps tx_duration_ms → TX off → pause           */
            if (!g_tx_paused) {
                /* Skip TX while collecting gekon press sequence (any BLE mode) */
                uint8_t skip_tx = s_gcnt > 0U;
                if (!skip_tx) {
                    if (ChipTemp_TickBeforeTX()) {
                        int32_t tw = g_last_temp_x10 / 10;
                        int32_t tf = g_last_temp_x10 >= 0 ? g_last_temp_x10 % 10 : -(g_last_temp_x10 % 10);
                        UART_Printf("[TEMP] chip=%ld.%01ldC VDDA=%lumV\r\n",
                                    (long)tw, (long)tf, (unsigned long)g_last_vdda_mV);
                    }
                    RF_Start(k_channels[g_ch_idx], k_powers[g_pwr_idx]);
                    UART_Printf("[ECO ON ] %s %s  %lums\r\n",
                                k_ch_name[g_ch_idx], k_pwr_name[g_pwr_idx], g_tx_duration_ms);
                    UART_CheckIdle();
                    LED_Update();
                    Power_EnterStop2_ms_tx(g_tx_duration_ms);
                    RF_Stop();
                    UART_Printf("[ECO OFF] pause %lums...\r\n", g_tx_period_ms);
                }
            }
            UART_CheckIdle();
            LED_Update();
            SLEEP_OR_WAIT_MS(g_tx_period_ms);

            UartCmd_Poll();
            GekonEvent_t gev = GEKON_Poll();
            if (gev == GEKON_SHORT) {
                /* BLE double-press — always available regardless of BLE op mode.
                 * LED blink suppressed during sequence: 300 ms breaks timing window. */
                uint32_t _now = HAL_GetTick();
                uint32_t _el  = _now - s_gcnt_t0;
                if (s_gcnt == 0U) {
                    s_gcnt    = 1U;
                    s_gcnt_t0 = _now;
                    UART_Print("[GEKON] cnt=1 (waiting 2nd)\r\n");
                } else if (_el >= GEKON_BLE_WIN_MIN_MS &&
                           _el <= GEKON_BLE_WIN_MAX_MS && BLE_IsIdle()) {
                    s_gcnt = 0U;
                    uint32_t _dur = g_ble_duration_sec > 0U ? (uint32_t)g_ble_duration_sec * 1000UL : 30000UL;
                    UART_Print("[BLE] Gekon 2x -> BLE start\r\n");
                    BLE_StartSession(_dur);
                } else if (_el > GEKON_BLE_WIN_MAX_MS) {
                    s_gcnt    = 1U;
                    s_gcnt_t0 = _now;
                    UART_Print("[GEKON] cnt=1 (restart)\r\n");
                }
            } else if (gev == GEKON_LONG) {
                UART_Print("[GEKON] long press -> Shutdown\r\n");
                LED_Blink(5, 50, 50);
                HAL_Delay(1000);
                GEKON_ClearPending();
                UART_CheckIdle();
                BLE_ForceShutdown(); Power_EnterShutdown();
            }

        } else if (g_tx_mode == TX_MODE_CONT) {
            /* ── Cont: stays in this block while mode == CONT ──────────── *
             * tx on/off toggle TX inside cont mode.                         *
             * 'mode pulse' → RF_Stop + mode change → exits loop.            */
            if (!g_tx_paused) {
                RF_Start(k_channels[g_ch_idx], k_powers[g_pwr_idx]);
                UART_Printf("[TX ON ] cont %s %s  ('tx off' to stop)\r\n",
                            k_ch_name[g_ch_idx], k_pwr_name[g_pwr_idx]);
            } else {
                UART_Print("[CONT] paused — 'tx on' to start\r\n");
            }

            while (g_tx_mode == TX_MODE_CONT) {
                UART_TryReconnect();
                UartCmd_Poll();   /* tx on/off, mode pulse — all handled here */
                LED_Update();
                BLE_ProcessEvents();  /* process BLE writes — mode change via GUI */
                BLE_LED_Update();
                if (g_config_save_pending && !BLE_IsConnected() && Flash_CPU2IsIdle()) {
                    g_config_save_pending = 0U;
                    if (FlashConfig_Save()) UART_Print("[CFG] auto-saved\r\n");
                    else                    UART_Print("[CFG] auto-save FAILED\r\n");
                }
                GekonEvent_t ev = GEKON_Poll();
                if (ev == GEKON_SHORT) {
                    if (RF_IsEnabled()) {
                        RF_Stop();
                        UART_Print("[CONT] TX off (short press) — 'tx on' to resume\r\n");
                        LED_Blink(2, 150, 150);
                        LED_SetMode(LED_HEARTBEAT);
                    }
                } else if (ev == GEKON_LONG) {
                    RF_Stop();
                    UART_Print("[GEKON] long press -> Shutdown\r\n");
                    LED_Blink(5, 50, 50);
                    HAL_Delay(1000);
                    GEKON_ClearPending();
                    UART_CheckIdle();
                    BLE_ForceShutdown(); Power_EnterShutdown();
                }
                HAL_Delay(5);
            }
            /* mode changed to pulse — make sure TX is stopped */
            if (RF_IsEnabled()) RF_Stop();
            UART_Print("[CONT] exited — pulse mode\r\n");
            UART_CheckIdle();
        } else if (g_tx_mode == TX_MODE_OFF) {
            /* ── Off: TX permanently disabled ─────────────────────────────────
             * Sleep duration = shortest active sensor period (default 3600 s).
             * With UART active: busy-wait period_ms — commands remain responsive.
             * Without UART: Stop1 sleep — PA10 EXTI wakes immediately on reconnect. */
            if (RF_IsEnabled()) RF_Stop();
            UART_CheckIdle();
            LED_Update();
            if (s_gcnt > 0U) {
                uint32_t _w = g_tx_period_ms;
                if (_w > GEKON_BLE_WIN_MAX_MS) _w = GEKON_BLE_WIN_MAX_MS;
                UartCmd_Wait(_w);
            } else if (UART_IsActive()) {
                UartCmd_Wait(g_tx_period_ms);
            } else if (!BLE_IsIdle()) {
                if (g_ble_led_mode == BLE_LED_NORMAL) {
                    UartCmd_Wait(g_tx_period_ms);
                } else {
                    uint32_t _ble_t0 = HAL_GetTick();
                    uint32_t _ble_ms = g_tx_period_ms < 1000U ? g_tx_period_ms : 1000U;
                    while ((HAL_GetTick() - _ble_t0) < _ble_ms) {
                        BLE_ProcessEvents();
                        LED_Update();
                        if (BLE_IsIdle()) break;
                        __WFI();
                    }
                }
            } else {
                uint32_t sleep_s = _off_sleep_secs();
                if ((g_ble_op_mode & BLE_OP_SCHEDULE) && g_ble_interval_s > 0U &&
                    (uint32_t)g_ble_interval_s < sleep_s) {
                    sleep_s = (uint32_t)g_ble_interval_s;
                }
                Power_EnterStop2(sleep_s);
            }

            UartCmd_Poll();
            GekonEvent_t gev = GEKON_Poll();
            if (gev == GEKON_SHORT) {
                /* BLE double-press — always available regardless of BLE op mode.
                 * LED blink suppressed during sequence: 300 ms breaks timing window. */
                uint32_t _now = HAL_GetTick();
                uint32_t _el  = _now - s_gcnt_t0;
                if (s_gcnt == 0U) {
                    s_gcnt    = 1U;
                    s_gcnt_t0 = _now;
                    UART_Print("[GEKON] cnt=1 (waiting 2nd)\r\n");
                } else if (_el >= GEKON_BLE_WIN_MIN_MS &&
                           _el <= GEKON_BLE_WIN_MAX_MS && BLE_IsIdle()) {
                    s_gcnt = 0U;
                    uint32_t _dur = g_ble_duration_sec > 0U ? (uint32_t)g_ble_duration_sec * 1000UL : 30000UL;
                    UART_Print("[BLE] Gekon 2x -> BLE start\r\n");
                    BLE_StartSession(_dur);
                } else if (_el > GEKON_BLE_WIN_MAX_MS) {
                    s_gcnt    = 1U;
                    s_gcnt_t0 = _now;
                    UART_Print("[GEKON] cnt=1 (restart)\r\n");
                }
            } else if (gev == GEKON_LONG) {
                UART_Print("[GEKON] long press -> Shutdown\r\n");
                LED_Blink(5, 50, 50);
                HAL_Delay(1000);
                GEKON_ClearPending();
                UART_CheckIdle();
                BLE_ForceShutdown(); Power_EnterShutdown();
            }

        } else {
            /* Unknown tx_mode: reset to PULSE to avoid silent misbehavior */
            if (RF_IsEnabled()) RF_Stop();
            UART_Printf("[ERR] unknown tx_mode=%u — reset to pulse\r\n", g_tx_mode);
            g_tx_mode = TX_MODE_PULSE;
        }
    }
}

/* ── System clock: HSE 32 MHz, LSE 32.768 kHz ────────────────────────────── */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef       osc    = {0};
    RCC_ClkInitTypeDef       clk    = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};

    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.LSEState       = RCC_LSE_ON;
    osc.PLL.PLLState   = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_HCLK4 | RCC_CLOCKTYPE_HCLK2 |
                         RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_HSE;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    clk.AHBCLK2Divider = RCC_SYSCLK_DIV1;
    clk.AHBCLK4Divider = RCC_SYSCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1);

    /* RF wakeup clock (LSE) and SMPS clock (HSE) — required by CPU2 BLE stack */
    periph.PeriphClockSelection    = RCC_PERIPHCLK_SMPS | RCC_PERIPHCLK_RFWAKEUP;
    periph.RFWakeUpClockSelection  = RCC_RFWKPCLKSOURCE_LSE;
    periph.SmpsClockSelection      = RCC_SMPSCLKSOURCE_HSE;
    periph.SmpsDivSelection        = RCC_SMPSCLKDIV_RANGE1;
    HAL_RCCEx_PeriphCLKConfig(&periph);
}

/* ── GPIO ─────────────────────────────────────────────────────────────────── */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin = GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_8;
    HAL_GPIO_Init(GPIOA, &g);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin = GPIO_PIN_0 | GPIO_PIN_5 | GPIO_PIN_8;
    HAL_GPIO_Init(GPIOB, &g);
}

/* ── I2C1: PB6=SCL, PB7=SDA, PCLK1=32 MHz (HSE, no PLL), SM 100 kHz ─────── */
static void MX_I2C1_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_NOPULL;  /* hardware pull-ups from sensor board */
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    __HAL_RCC_I2C1_CLK_ENABLE();

    hi2c1.Instance              = I2C1;
    /* PRESC=3 SCLDEL=2 SDADEL=0 SCLH=0x1F SCLL=0x2D → ~100 kHz @ 32 MHz PCLK1 */
    hi2c1.Init.Timing           = 0x30201F2DU;
    hi2c1.Init.OwnAddress1      = 0U;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0U;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
        Error_Handler();

    HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);
    HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0U);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
        for (volatile uint32_t i = 0; i < 200000UL; i++);
    }
}
