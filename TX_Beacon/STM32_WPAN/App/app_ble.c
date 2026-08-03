#include "app_ble.h"
#include "app_common.h"
#include "ble_common.h"
#include "ble_hal_aci.h"
#include "ble_gap_aci.h"
#include "ble_gatt_aci.h"
#include "ble_hci_le.h"
#include "ble_legacy.h"
#include "hci_tl.h"
#include "shci_tl.h"
#include "shci.h"
#include "tl.h"
#include "svc_ctl.h"
#include "stm32_seq.h"
#include "otp.h"
#include "hw.h"
#include "ble_beacon_service.h"
#include "ble_nus_service.h"
#include "drv_led.h"
#include "drv_uart.h"
#include "stm32wbxx_hal.h"
#include "stm32wbxx_ll_system.h"
#include "stm32wbxx_ll_ipcc.h"
#include "stm32wbxx_ll_exti.h"
#include <string.h>

/* ── BLE state ───────────────────────────────────────────────────────────── */
typedef enum {
    BLE_ST_IDLE    = 0,
    BLE_ST_ADV     = 1,
    BLE_ST_CONN    = 2,
} BleState_t;

static volatile BleState_t s_ble_state          = BLE_ST_IDLE;
static volatile uint8_t    s_cpu2_ready         = 0U;
static volatile uint8_t    s_waiting_cpu2_ready  = 0U;
static volatile uint8_t    s_shci_evt_pending    = 0U;
static volatile uint8_t    s_hci_evt_pending     = 0U;
static volatile uint32_t   s_hci_notify_count    = 0U;
static volatile uint32_t   s_hci_process_count   = 0U;
static uint16_t            s_conn_handle = 0xFFFFU;
static uint32_t            s_uid_low     = 0U;
/* ACI advertising local-name field: first byte is the AD type, followed by
 * the visible device name bytes. */
static uint8_t             s_adv_local_name[1U + CFG_GAP_DEVICE_NAME_LENGTH];

/* ── Session management (Task 2) ─────────────────────────────────────────── */
static uint8_t  s_session_active      = 0U;
static uint8_t  s_session_is_timed    = 0U;
static uint32_t s_session_timeout_ms  = 0U;
static uint32_t s_session_start_tick  = 0U;
/* Set after first successful BLE_AppInit(); cleared only by BLE_ForceShutdown().
 * When 1, BLE_StartSession() skips full re-init and just restarts advertising.
 * Needed because once C2BOOT is set, a clear/set has no effect (hw_ipcc.c:210),
 * so TL_Enable() cannot reboot CPU2 for a second session. */
static uint8_t  s_cpu2_initialized   = 0U;

/* ── BLE LED state machine (independent from main LED driver) ────────────── */
static uint32_t s_adv_blink_tick  = 0U;
static uint8_t  s_adv_blink_phase = 0U;  /* 0=waiting, 1=pulse on */

/* ── NUS inactivity timeout (Bug 1: stale session guard) ─────────────────── */
/* Reset on connection + each NUS RX write. If connected and no NUS data for
 * BLE_NUS_IDLE_TIMEOUT_MS, we force-disconnect — PC app failed to subscribe. */
#define BLE_NUS_IDLE_TIMEOUT_MS  60000U   /* 60 s — covers any init latency */
static volatile uint32_t s_nus_last_rx_tick = 0U;

void BLE_NusMarkActivity(void) { s_nus_last_rx_tick = HAL_GetTick(); }

/* ── BLE runtime settings (Task 5) — loaded from flash via flash_config.c ── */
uint8_t  g_ble_op_mode          = BLE_OP_GEKON;
uint8_t  g_ble_tx_power         = CFG_TX_POWER;
uint16_t g_ble_interval_s       = 1800U; /* schedule mode: seconds between windows */
uint16_t g_ble_duration_sec     = 10U;   /* session window (s) for schedule/gekon  */
uint16_t g_ble_adv_interval_ms  = 100U;  /* advertising interval (ms)              */
uint8_t  g_ble_name_mode        = 0U;    /* 0=auto "BCN_XXXX", 1=manual g_ble_name */
char     g_ble_name[12]         = {0};   /* manual device name (null-terminated)   */
uint8_t  g_ble_led_mode         = BLE_LED_NORMAL;

/* ── Shared-memory buffers (placed in MB_MEM sections in RAM_SHARED) ─────── */
#define POOL_SIZE  (CFG_TLBLE_EVT_QUEUE_LENGTH * 4U * \
                    DIVC((sizeof(TL_PacketHeader_t) + TL_BLE_EVENT_FRAME_SIZE), 4U))

PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static TL_CmdPacket_t s_BleCmdBuffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t        s_SysSpareEvtBuf[sizeof(TL_PacketHeader_t)
                                                                              + TL_EVT_HDR_SIZE + 255U];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t s_ShciCmdBuffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t        s_BleSpareEvtBuf[sizeof(TL_PacketHeader_t)
                                                                              + TL_EVT_HDR_SIZE + 255U];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t        s_EvtPool[POOL_SIZE];

/* ── Forward declarations ────────────────────────────────────────────────── */
static void _SysEvtHandler(void *pPayload);
static void _SysStatusNot(SHCI_TL_CmdStatus_t status);
static void _BleUserEvtRx(void *pPayload);
static void _BleStatusNot(HCI_TL_CmdStatus_t status);
static void _HandleConnectionEvent(void *pPayload);
static void _GapGattInit(void);
static void _StartAdvertising(void);
static void _BleGetBdAddress(uint8_t *pbd_addr);
static void _StopSession_internal(void);

/* ── Public API ──────────────────────────────────────────────────────────── */

uint8_t  BLE_IsIdle(void)        { return (s_ble_state == BLE_ST_IDLE) ? 1U : 0U; }
uint8_t  BLE_IsConnected(void)   { return (s_ble_state == BLE_ST_CONN) ? 1U : 0U; }
uint8_t  BLE_CPU2Initialized(void) { return s_cpu2_initialized; }
uint8_t  BLE_IsAdvertising(void) { return (s_ble_state == BLE_ST_ADV)  ? 1U : 0U; }
uint16_t BLE_GetConnHandle(void) { return s_conn_handle; }

void BLE_ProcessEvents(void)
{
    /* Only drain IPCC when CPU2's BLE stack is running.
     * Clearing C2→C1 flags while CPU2 is in Shutdown sends it a "TX free"
     * interrupt via the IPCC peripheral, waking CPU2 from Shutdown into Stop1
     * (~12 µA extra). Guard with s_cpu2_initialized to prevent this. */
    if (s_cpu2_initialized) {
        uint32_t rx_pending = IPCC->C2TOC1SR & ~IPCC->C1MR & 0x3FU;
        if (rx_pending != 0U) {
            HW_IPCC_Rx_Handler();
        }

        while (s_shci_evt_pending != 0U) {
            s_shci_evt_pending = 0U;
            shci_user_evt_proc();
        }
        while (s_hci_evt_pending != 0U) {
            s_hci_evt_pending = 0U;
            s_hci_process_count++;
            hci_user_evt_proc();
        }
    }

    /* Session timeout: stop advertising if window expired (schedule/gekon modes) */
    if (s_session_is_timed && s_session_active && (s_ble_state == BLE_ST_ADV)) {
        if ((HAL_GetTick() - s_session_start_tick) >= s_session_timeout_ms) {
            UART_Print("[BLE] Session timeout — stopping.\r\n");
            _StopSession_internal();
        }
    }

    /* NUS inactivity guard: PC connected but never subscribed NUS or crashed.
     * Without this the beacon hangs in BLE_ST_CONN with no data flowing. */
    if (s_ble_state == BLE_ST_CONN) {
        if ((HAL_GetTick() - s_nus_last_rx_tick) >= BLE_NUS_IDLE_TIMEOUT_MS) {
            UART_Print("[BLE] NUS idle timeout — dropping stale client\r\n");
            aci_gap_terminate(s_conn_handle, 0x13U); /* 0x13=RemoteUserTerminatedConn */
            s_nus_last_rx_tick = HAL_GetTick();   /* prevent rapid re-fires */
        }
    }
}

/* Clear all IPCC flags that survive NRST. Without this, CPU2 sees a stale
 * command from the previous run (C1TOC2SR != 0) and hangs before SHCI_READY. */
void BLE_SystemPreInit(void)
{
    LL_AHB3_GRP1_EnableClock(LL_AHB3_GRP1_PERIPH_IPCC);

    LL_C1_IPCC_ClearFlag_CHx(IPCC,
        LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 |
        LL_IPCC_CHANNEL_4 | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);
    LL_C2_IPCC_ClearFlag_CHx(IPCC,
        LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 |
        LL_IPCC_CHANNEL_4 | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);
    LL_C1_IPCC_DisableTransmitChannel(IPCC,
        LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 |
        LL_IPCC_CHANNEL_4 | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);
    LL_C2_IPCC_DisableTransmitChannel(IPCC,
        LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 |
        LL_IPCC_CHANNEL_4 | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);
    LL_C1_IPCC_DisableReceiveChannel(IPCC,
        LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 |
        LL_IPCC_CHANNEL_4 | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);
    LL_C2_IPCC_DisableReceiveChannel(IPCC,
        LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 |
        LL_IPCC_CHANNEL_4 | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);
}

/* ── BLE_AppInit: full stack bring-up, blocks until CPU2 ready (≤10 s) ───── */
void BLE_AppInit(void)
{
    /* Enable IPCC (EXTI36) and HSEM (EXTI38) wakeup lines so Stop mode is
     * exited when CPU2 sends events or acquires HSEM semaphores */
    LL_EXTI_EnableIT_32_63(LL_EXTI_LINE_36 | LL_EXTI_LINE_38);

    UART_Print("[BLE] AppInit: registering tasks...\r\n");

    /* ── Register sequencer tasks ─────────────────────────────────────────── */
    UTIL_SEQ_RegTask(1U << CFG_TASK_HCI_ASYNCH_EVT_ID,
                     UTIL_SEQ_RFU, hci_user_evt_proc);
    UTIL_SEQ_RegTask(1U << CFG_TASK_SYSTEM_HCI_ASYNCH_EVT_ID,
                     UTIL_SEQ_RFU, shci_user_evt_proc);

    /* ── Init IPCC hardware ───────────────────────────────────────────────── */
    UART_Print("[BLE] IPCC init...\r\n");
    HW_IPCC_Init();

    /* ── Init transport layer reference table ─────────────────────────────── */
    UART_Print("[BLE] TL init...\r\n");
    TL_Init();

    /* ── Init SHCI (system control channel to CPU2) ───────────────────────── */
    UART_Print("[BLE] SHCI init...\r\n");
    SHCI_TL_HciInitConf_t shci_cfg;
    shci_cfg.p_cmdbuffer       = (uint8_t*)&s_ShciCmdBuffer;
    shci_cfg.StatusNotCallBack = _SysStatusNot;
    shci_init(_SysEvtHandler, (void*)&shci_cfg);

    /* ── Init memory manager ──────────────────────────────────────────────── */
    UART_Print("[BLE] MM init...\r\n");
    TL_MM_Config_t mm_cfg;
    mm_cfg.p_BleSpareEvtBuffer    = s_BleSpareEvtBuf;
    mm_cfg.p_SystemSpareEvtBuffer = s_SysSpareEvtBuf;
    mm_cfg.p_AsynchEvtPool        = s_EvtPool;
    mm_cfg.AsynchEvtPoolSize      = sizeof(s_EvtPool);
    TL_MM_Init(&mm_cfg);

    /* ── Enable TL (boots CPU2) ───────────────────────────────────────────── */
    s_cpu2_ready = 0U;
    s_waiting_cpu2_ready = 1U;
    UART_Print("[BLE] TL_Enable (booting CPU2)...\r\n");
    TL_Enable();
    UART_Printf("[BLE] After TL_Enable: CR4=%08lX C2BOOT=%lu SR2=%08lX "
                "C1TOC2SR=%08lX C2TOC1SR=%08lX C1MR=%08lX\r\n",
                (unsigned long)PWR->CR4,
                (PWR->CR4 & PWR_CR4_C2BOOT) ? 1UL : 0UL,
                (unsigned long)PWR->SR2,
                (unsigned long)IPCC->C1TOC2SR,
                (unsigned long)IPCC->C2TOC1SR,
                (unsigned long)IPCC->C1MR);

    /* ── Wait for SHCI_READY from CPU2 (≤10 s) ──────────────────────────── */
    UART_Print("[BLE] Waiting for CPU2 READY...\r\n");
    extern volatile uint32_t g_ipcc_rx_irq_cnt;
    uint32_t t0 = HAL_GetTick();
    uint32_t last_diag = t0;
    while (!s_cpu2_ready) {
        /* Drain SHCI event directly — UTIL_SEQ_Run() blocks here in this
         * integration; direct flag polling avoids the deadlock. */
        if (s_shci_evt_pending != 0U) {
            s_shci_evt_pending = 0U;
            shci_user_evt_proc();
        }

        uint32_t now = HAL_GetTick();
        if ((now - last_diag) >= 500U) {
            last_diag = now;
            UART_Printf("[BLE] t=%lums CR4=%08lX C2BOOT=%lu SR2=%08lX "
                        "C1TOC2SR=%08lX C2TOC1SR=%08lX C1MR=%08lX IRQcnt=%lu\r\n",
                        (unsigned long)(now - t0),
                        (unsigned long)PWR->CR4,
                        (PWR->CR4 & PWR_CR4_C2BOOT) ? 1UL : 0UL,
                        (unsigned long)PWR->SR2,
                        (unsigned long)IPCC->C1TOC2SR,
                        (unsigned long)IPCC->C2TOC1SR,
                        (unsigned long)IPCC->C1MR,
                        (unsigned long)g_ipcc_rx_irq_cnt);
        }
        if ((now - t0) > 10000U) {
            UART_Printf("[BLE] ERROR: CPU2 not ready in 10s. IRQcnt=%lu\r\n",
                        (unsigned long)g_ipcc_rx_irq_cnt);
            s_waiting_cpu2_ready = 0U;
            return;
        }
    }
    s_waiting_cpu2_ready = 0U;
    UART_Print("[BLE] CPU2 READY received.\r\n");

    /* ── Init HCI transport BEFORE asking CPU2 to init BLE stack ────────────
     * ST's APP_BLE_Init() does Ble_Tl_Init() before SHCI_C2_BLE_Init().
     * The BLE event/command channels must be ready first. */
    UART_Print("[BLE] HCI transport init...\r\n");
    HCI_TL_HciInitConf_t hci_cfg;
    hci_cfg.p_cmdbuffer       = (uint8_t*)&s_BleCmdBuffer;
    hci_cfg.StatusNotCallBack = _BleStatusNot;
    hci_init(_BleUserEvtRx, (void*)&hci_cfg);
    UART_Printf("[BLE] HCI ready: C1MR=%08lX\r\n", (unsigned long)IPCC->C1MR);

    /* ── Init BLE stack on CPU2 ───────────────────────────────────────────── */
    SHCI_C2_Ble_Init_Cmd_Packet_t ble_init = {
        {{0, 0, 0}},
        {
            0U,                                 /* BleBufferAddress (auto) */
            0U,                                 /* BleBufferSize    (auto) */
            CFG_BLE_NUM_GATT_ATTRIBUTES,
            CFG_BLE_NUM_GATT_SERVICES,
            CFG_BLE_ATT_VALUE_ARRAY_SIZE,
            CFG_BLE_NUM_LINK,
            CFG_BLE_DATA_LENGTH_EXTENSION,
            CFG_BLE_PREPARE_WRITE_LIST_SIZE,
            CFG_BLE_MBLOCK_COUNT,
            CFG_BLE_MAX_ATT_MTU,
            CFG_BLE_PERIPHERAL_SCA,
            CFG_BLE_CENTRAL_SCA,
            CFG_BLE_LS_SOURCE,
            CFG_BLE_MAX_CONN_EVENT_LENGTH,
            CFG_BLE_HSE_STARTUP_TIME,
            CFG_BLE_VITERBI_MODE,
            CFG_BLE_OPTIONS,
            0U,                                 /* hw_version */
            CFG_BLE_MAX_COC_INITIATOR_NBR,
            CFG_BLE_MIN_TX_POWER,
            CFG_BLE_MAX_TX_POWER,
            CFG_BLE_RX_MODEL_CONFIG,
            CFG_BLE_MAX_ADV_SET_NBR,
            CFG_BLE_MAX_ADV_DATA_LEN,
            CFG_BLE_TX_PATH_COMPENS,
            CFG_BLE_RX_PATH_COMPENS,
            CFG_BLE_CORE_VERSION,
            CFG_BLE_OPTIONS_EXT,
            CFG_BLE_MAX_ADD_EATT_BEARERS,
        }
    };
    UART_Print("[BLE] SHCI_C2_BLE_Init...\r\n");
    SHCI_CmdStatus_t shci_status = SHCI_C2_BLE_Init(&ble_init);
    UART_Printf("[BLE] SHCI_C2_BLE_Init returned 0x%02X\r\n", (unsigned)shci_status);
    if (shci_status != SHCI_Success) {
        UART_Print("[BLE] ERROR: SHCI_C2_BLE_Init failed!\r\n");
        return;
    }

    /* ── Init GAP / GATT layers ───────────────────────────────────────────── */
    UART_Print("[BLE] GAP/GATT init...\r\n");
    _GapGattInit();

    /* ── Init service controller ──────────────────────────────────────────── */
    SVCCTL_Init();

    /* ── Register beacon GATT service ────────────────────────────────────── */
    UART_Print("[BLE] Beacon service init...\r\n");
    BLE_Beacon_Init();
    NUS_Init();

    /* ── Start advertising ────────────────────────────────────────────────── */
    UART_Print("[BLE] Starting advertising...\r\n");
    s_cpu2_initialized = 1U;
    _StartAdvertising();
    /* LED is managed by BLE_StartSession() — do NOT call LED_SetMode here */
}

/* ── BLE session management (Task 2) ─────────────────────────────────────── */

void BLE_StartSession(uint32_t timeout_ms)
{
    if (!BLE_IsIdle()) return;

    s_session_active     = 1U;
    s_session_is_timed   = (timeout_ms > 0U) ? 1U : 0U;
    s_session_timeout_ms = timeout_ms;
    s_session_start_tick = HAL_GetTick();

    UART_Printf("[BLE] StartSession timeout=%lu ms\r\n", timeout_ms);

    if (s_cpu2_initialized) {
        /* CPU2 BLE stack already running — restart advertising directly.
         * We cannot re-run BLE_AppInit() because once C2BOOT is set once,
         * TL_Enable()/LL_PWR_EnableBootC2() has no effect (hw_ipcc.c:210). */
        UART_Print("[BLE] CPU2 up — restart adv\r\n");
        _StartAdvertising();
    } else {
        BLE_SystemPreInit();
        BLE_AppInit();
    }

    /* BLE LED: if NORMAL mode, take over GPIO so main driver doesn't interfere */
    if (g_ble_led_mode == BLE_LED_NORMAL) {
        LED_SetBleOverride(1U);
        LED_WriteGpio(0U);
        s_adv_blink_tick  = HAL_GetTick();
        s_adv_blink_phase = 0U;
    } else if (g_ble_led_mode == BLE_LED_TRIPLE) {
        /* 3 quick blinks at session start — CPU1 can sleep after that */
        LED_Blink(3U, 80U, 80U);
        /* No LED override: main driver continues; CPU1 free to Stop2 */
    }
    /* BLE_LED_OFF: override stays off — main LED driver runs normally throughout */
}

void BLE_StopSession(void)
{
    if (BLE_IsIdle()) return;
    UART_Print("[BLE] StopSession requested.\r\n");
    _StopSession_internal();
}

/* Shut down CPU2 completely — call before device Sleep/Shutdown or when BLE
 * mode changes to OFF.  Drains events, waits for CPU2 deep-sleep (max 500 ms),
 * then clears s_cpu2_initialized so the next BLE_StartSession() does full init. */
void BLE_ForceShutdown(void)
{
    if (!s_cpu2_initialized) return;

    /* Stop advertising if still active */
    if (s_ble_state != BLE_ST_IDLE) {
        aci_gap_set_non_discoverable();
        HAL_Delay(50U);
        s_ble_state        = BLE_ST_IDLE;
        s_session_active   = 0U;
        s_session_is_timed = 0U;
        LED_SetBleOverride(0U);
        BLE_Beacon_OnDisconnect();
        NUS_OnDisconnect();
    }

    LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);
    {
        uint32_t _t0 = HAL_GetTick();
        for (uint8_t _p = 0U; _p < 5U; _p++) {
            uint32_t _rx = IPCC->C2TOC1SR & ~IPCC->C1MR & 0x3FU;
            if (_rx) HW_IPCC_Rx_Handler();
            if (s_hci_evt_pending)  { s_hci_evt_pending  = 0U; hci_user_evt_proc(); }
            if (s_shci_evt_pending) { s_shci_evt_pending = 0U; shci_user_evt_proc(); }
            HAL_Delay(10U);
        }
        while (!(READ_BIT(PWR->EXTSCR, PWR_EXTSCR_C2DS))) {
            if ((HAL_GetTick() - _t0) >= 500U) {
                UART_Print("[BLE] CPU2 slow to sleep\r\n");
                break;
            }
        }
    }
    s_cpu2_initialized = 0U;
    s_cpu2_ready       = 0U;
    s_shci_evt_pending = 0U;
    s_hci_evt_pending  = 0U;
    UART_Print("[BLE] CPU2 shut down.\r\n");
}

static void _StopSession_internal(void)
{
    /* Set Stop1 so CPU2 enters low-power after the last BLE LL timer.
     * We cannot use Shutdown here: C2BOOT can only be set once per hardware
     * reset — software cannot clear it (RM0471 / hw_ipcc.c:~210).  If Shutdown
     * were used, TL_Enable() on the next BLE_StartSession() would be a 1→1
     * no-op and CPU2 would never receive SHCI_READY.
     * svc_power.c guards the C2CR1=Shutdown write with !BLE_CPU2Initialized(),
     * so this Stop1 request is preserved across CPU1 sleep cycles. */
    LL_C2_PWR_SetPowerMode(LL_PWR_MODE_STOP1);

    if (s_ble_state != BLE_ST_IDLE) {
        aci_gap_set_non_discoverable();
        /* Drain IPCC for up to 200 ms so CPU2 can process the advertising-stop
         * cleanup event and transition to Shutdown before CPU1 sleeps. */
        uint32_t _t0 = HAL_GetTick();
        while ((HAL_GetTick() - _t0) < 200U) {
            uint32_t _rx = IPCC->C2TOC1SR & ~IPCC->C1MR & 0x3FU;
            if (_rx)               HW_IPCC_Rx_Handler();
            if (s_hci_evt_pending)  { s_hci_evt_pending  = 0U; hci_user_evt_proc();  }
            if (s_shci_evt_pending) { s_shci_evt_pending = 0U; shci_user_evt_proc(); }
            HAL_Delay(10U);
        }
    }

    s_ble_state        = BLE_ST_IDLE;
    s_shci_evt_pending = 0U;
    s_hci_evt_pending  = 0U;
    s_session_active   = 0U;
    s_session_is_timed = 0U;
    /* s_cpu2_initialized stays 1: BLE stack SRAM preserved in Stop1.
     * Next BLE_StartSession() takes the fast _StartAdvertising() path. */
    s_cpu2_ready       = 0U;

    LED_SetBleOverride(0U);
    BLE_Beacon_OnDisconnect();
    NUS_OnDisconnect();
    UART_Print("[BLE] Session ended (CPU2 → Stop1, fast restart on next session).\r\n");
}

/* ── GAP / GATT init sequence ─────────────────────────────────────────────── */
static void _GapGattInit(void)
{
    uint8_t  bd_addr[6];
    uint16_t gap_svc_handle, gap_dev_name_handle, gap_appearance_handle;
    const uint8_t ir[16] = CFG_BLE_IR;
    const uint8_t er[16] = CFG_BLE_ER;
    char     name[CFG_GAP_DEVICE_NAME_LENGTH + 1U];

    hci_reset();
    HAL_Delay(300U);

    _BleGetBdAddress(bd_addr);
    s_uid_low = ((uint32_t)bd_addr[1] << 8U) | bd_addr[0];

    aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET,
                              CONFIG_DATA_PUBADDR_LEN, bd_addr);
    aci_hal_write_config_data(CONFIG_DATA_IR_OFFSET,
                              CONFIG_DATA_IR_LEN, ir);
    aci_hal_write_config_data(CONFIG_DATA_ER_OFFSET,
                              CONFIG_DATA_ER_LEN, er);

    /* TX power: use configurable g_ble_tx_power (Task 5) */
    aci_hal_set_tx_power_level(1, g_ble_tx_power);

    aci_gatt_init();

    /* Device name: manual or auto "BCN_XXXX" (Task 5) */
    if (g_ble_name_mode != 0U && g_ble_name[0] != '\0') {
        strncpy(name, g_ble_name, CFG_GAP_DEVICE_NAME_LENGTH);
        name[CFG_GAP_DEVICE_NAME_LENGTH] = '\0';
    } else {
        (void)snprintf(name, sizeof(name), "BCN_%04lX",
                       (unsigned long)(s_uid_low & 0xFFFFUL));
    }

    s_adv_local_name[0] = AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&s_adv_local_name[1], name, CFG_GAP_DEVICE_NAME_LENGTH);

    aci_gap_init(GAP_PERIPHERAL_ROLE, PRIVACY_DISABLED,
                 (uint8_t)strlen(name),
                 &gap_svc_handle, &gap_dev_name_handle, &gap_appearance_handle);

    aci_gatt_update_char_value(gap_svc_handle, gap_dev_name_handle,
                               0U, (uint8_t)strlen(name), (uint8_t*)name);

    aci_gap_set_io_capability(CFG_IO_CAPABILITY);
    aci_gap_set_authentication_requirement(CFG_BONDING_MODE,
                                           CFG_MITM_PROTECTION,
                                           CFG_SC_SUPPORT,
                                           CFG_KEYPRESS_NOTIFICATION_SUPPORT,
                                           CFG_ENCRYPTION_KEY_SIZE_MIN,
                                           CFG_ENCRYPTION_KEY_SIZE_MAX,
                                           CFG_USED_FIXED_PIN,
                                           CFG_FIXED_PIN,
                                           GAP_PUBLIC_ADDR);

    UART_Printf("[BLE] Stack ready. Name=\"%s\" UID=0x%08lX\r\n",
                name, (unsigned long)LL_FLASH_GetUDN());
}

/* ── Advertising ──────────────────────────────────────────────────────────── */
static void _StartAdvertising(void)
{
    /* Advertising interval: configurable via g_ble_adv_interval_ms (Task 5).
     * BLE unit = 0.625 ms → units = ms * 8 / 5.  Clamp to [100ms, 10s]. */
    uint32_t adv_ms = g_ble_adv_interval_ms;
    if (adv_ms < 100U)   adv_ms = 100U;
    if (adv_ms > 10000U) adv_ms = 10000U;
    uint16_t adv_units = (uint16_t)(adv_ms * 8U / 5U);

    tBleStatus ret = aci_gap_set_discoverable(ADV_IND,
                                              adv_units, adv_units,
                                              GAP_PUBLIC_ADDR,
                                              NO_WHITE_LIST_USE,
                                              0U, NULL,
                                              0U, NULL,
                                              0U, 0U);
    if (ret != BLE_STATUS_SUCCESS) {
        UART_Printf("[BLE] set_discoverable err 0x%02X\r\n", ret);
        s_ble_state = BLE_ST_IDLE;
        return;
    }

    aci_gap_delete_ad_type(AD_TYPE_TX_POWER_LEVEL);

    /* Write device name into ADV payload explicitly (unreliable via param on WB1M) */
    uint8_t adv_data[2U + CFG_GAP_DEVICE_NAME_LENGTH];
    adv_data[0] = (uint8_t)(1U + CFG_GAP_DEVICE_NAME_LENGTH);
    memcpy(&adv_data[1], s_adv_local_name, sizeof(s_adv_local_name));
    tBleStatus adv_ret = aci_gap_update_adv_data(sizeof(adv_data), adv_data);
    if (adv_ret != BLE_STATUS_SUCCESS) {
        UART_Printf("[BLE] update_adv_data err 0x%02X\r\n", adv_ret);
        s_ble_state = BLE_ST_IDLE;
        return;
    }

    UART_Printf("[BLE] Advertising started (interval=%lu ms)\r\n", adv_ms);
    s_ble_state = BLE_ST_ADV;
}

/* Handle link-state events directly at HCI ingress — avoids SVCCTL routing
 * dependency for reconnect advertising. */
static void _HandleConnectionEvent(void *pPayload)
{
    hci_event_pckt *event_pckt =
        (hci_event_pckt*)((hci_uart_pckt*)pPayload)->data;

    switch (event_pckt->evt) {
    case HCI_DISCONNECTION_COMPLETE_EVT_CODE: {
        s_conn_handle = 0xFFFFU;
        s_ble_state   = BLE_ST_IDLE;
        BLE_Beacon_OnDisconnect();
        NUS_OnDisconnect();
        UART_Print("[BLE] Client disconnected.\r\n");

        if (g_ble_op_mode & BLE_OP_CONTINUOUS) {
            /* Continuous mode: always restart advertising on disconnect */
            _StartAdvertising();
            if (g_ble_led_mode == BLE_LED_NORMAL) {
                LED_SetBleOverride(1U);     /* re-take LED for adv blink */
                s_adv_blink_tick  = HAL_GetTick();
                s_adv_blink_phase = 0U;
            }
        } else {
            /* Schedule / Gekon: end session after disconnect */
            _StopSession_internal();
        }
        break;
    }
    case HCI_LE_META_EVT_CODE: {
        evt_le_meta_event *meta = (evt_le_meta_event*)event_pckt->data;
        if (meta->subevent == HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE) {
            hci_le_connection_complete_event_rp0 *cc =
                (hci_le_connection_complete_event_rp0*)meta->data;
            s_conn_handle       = cc->Connection_Handle;
            s_ble_state         = BLE_ST_CONN;
            s_nus_last_rx_tick  = HAL_GetTick();  /* start inactivity watchdog */
            UART_Print("[BLE] Client connected.\r\n");
            if (g_ble_led_mode == BLE_LED_NORMAL) {
                LED_Blink(3U, 80U, 80U);
                LED_SetBleOverride(0U);     /* release — let main LED run during connection */
                LED_SetMode(LED_GetMode()); /* re-init mode state machine */
            }
        }
        break;
    }
    default:
        break;
    }
}

/* Retained for SVCCTL compatibility; link-state handling occurs in
 * _HandleConnectionEvent() called from _BleUserEvtRx(). */
SVCCTL_UserEvtFlowStatus_t SVCCTL_App_Notification(void *pckt)
{
    (void)pckt;
    return SVCCTL_UserEvtFlowEnable;
}

/* ── HCI / SHCI user callbacks ────────────────────────────────────────────── */

static void _BleUserEvtRx(void *pPayload)
{
    tHCI_UserEvtRxParam *param = (tHCI_UserEvtRxParam*)pPayload;
    void *evtserial = (void*)&param->pckt->evtserial;

    /* Handle connection events before routing to SVCCTL */
    _HandleConnectionEvent(evtserial);
    SVCCTL_UserEvtFlowStatus_t flow = SVCCTL_UserEvtRx(evtserial);
    param->status = (flow == SVCCTL_UserEvtFlowDisable)
                  ? HCI_TL_UserEventFlow_Disable
                  : HCI_TL_UserEventFlow_Enable;
}

static void _BleStatusNot(HCI_TL_CmdStatus_t status)
{
    (void)status;
}

static void _SysEvtHandler(void *pPayload)
{
    TL_AsynchEvt_t *p_evt =
        (TL_AsynchEvt_t*)(((tSHCI_UserEvtRxParam*)pPayload)->pckt->evtserial.evt.payload);

    if (p_evt->subevtcode == SHCI_SUB_EVT_CODE_READY)
        s_cpu2_ready = 1U;
}

static void _SysStatusNot(SHCI_TL_CmdStatus_t status)
{
    (void)status;
}

/* Called from IRQ (IPCC Rx) when a BLE async event is ready */
void hci_notify_asynch_evt(void *pdata)
{
    (void)pdata;
    s_hci_notify_count++;
    s_hci_evt_pending = 1U;
}

/* Called from IRQ when a SHCI async event is ready */
void shci_notify_asynch_evt(void *pdata)
{
    (void)pdata;
    s_shci_evt_pending = 1U;
}

/* HCI command response sync primitives */
void hci_cmd_resp_wait(uint32_t timeout)
{
    UTIL_SEQ_WaitEvt(1U << CFG_IDLEEVT_HCI_CMD_EVT_RSP_ID);
    (void)timeout;
}

void hci_cmd_resp_release(uint32_t flag)
{
    (void)flag;
    UTIL_SEQ_SetEvt(1U << CFG_IDLEEVT_HCI_CMD_EVT_RSP_ID);
}

/* SHCI command response sync primitives */
void shci_cmd_resp_wait(uint32_t timeout)
{
    UTIL_SEQ_WaitEvt(1U << CFG_IDLEEVT_SYSTEM_HCI_CMD_EVT_RSP_ID);
    (void)timeout;
}

void shci_cmd_resp_release(uint32_t flag)
{
    (void)flag;
    UTIL_SEQ_SetEvt(1U << CFG_IDLEEVT_SYSTEM_HCI_CMD_EVT_RSP_ID);
}

/* UTIL_SEQ idle — no-op: TX_Beacon manages its own power in svc_power.c */
void UTIL_SEQ_Idle(void) {}

/* ── BD address derivation from MCU UID ───────────────────────────────────── */
static void _BleGetBdAddress(uint8_t *pbd_addr)
{
    const uint32_t udn = LL_FLASH_GetUDN();

    if (udn != 0xFFFFFFFFUL) {
        const uint32_t company = LL_FLASH_GetSTCompanyID();
        const uint32_t devid   = LL_FLASH_GetDeviceID();

        pbd_addr[0] = (uint8_t)(udn          & 0xFFU);
        pbd_addr[1] = (uint8_t)((udn >> 8U)  & 0xFFU);
        pbd_addr[2] = (uint8_t)((udn >> 16U) & 0xFFU);
        pbd_addr[3] = (uint8_t)(company       & 0xFFU);
        pbd_addr[4] = (uint8_t)((devid >> 8U) & 0xFFU);
        pbd_addr[5] = (uint8_t)((devid        & 0xFFU) | 0xC0U);
    } else {
        pbd_addr[0] = 0x00U;
        pbd_addr[1] = 0x00U;
        pbd_addr[2] = 0x00U;
        pbd_addr[3] = 0xAAU;
        pbd_addr[4] = 0xBBU;
        pbd_addr[5] = 0xCCU;
    }
}

/* ── BLE LED non-blocking state machine ──────────────────────────────────── */
void BLE_LED_Update(void)
{
    /* TRIPLE: only 3 blinks at session start (handled in BLE_StartSession).
     * OFF/TRIPLE: no periodic blink — main driver runs; CPU1 may sleep. */
    if (g_ble_led_mode != BLE_LED_NORMAL) return;
    if (s_ble_state != BLE_ST_ADV) return;          /* only during advertising */

    uint32_t now    = HAL_GetTick();
    uint32_t period = (g_ble_adv_interval_ms >= 100U) ? g_ble_adv_interval_ms : 1000U;

    if (s_adv_blink_phase == 0U) {
        /* waiting for next blink */
        if ((now - s_adv_blink_tick) >= period) {
            LED_WriteGpio(1U);
            s_adv_blink_tick  = now;
            s_adv_blink_phase = 1U;
        }
    } else {
        /* pulse is on — turn off after 50 ms */
        if ((now - s_adv_blink_tick) >= 50U) {
            LED_WriteGpio(0U);
            s_adv_blink_tick  = now;
            s_adv_blink_phase = 0U;
        }
    }
}
