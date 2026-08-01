/* BLE stack configuration for TX_Beacon (Phase 1 minimal) */
#ifndef APP_CONF_H
#define APP_CONF_H

#include "ble_bufsize.h"

/* LL drivers needed by hw_ipcc.c and BLE middleware */
#include "stm32wbxx.h"
#include "stm32wbxx_ll_ipcc.h"
#include "stm32wbxx_ll_hsem.h"
#include "stm32wbxx_ll_pwr.h"
#include "stm32wbxx_ll_bus.h"

/* ── Advertising ─────────────────────────────────────────────────────────── */
#define CFG_ADV_BD_ADDRESS               (0x7257acd87a6c)
#define CFG_BLE_ADDRESS_TYPE             GAP_PUBLIC_ADDR
#define CFG_IDENTITY_ADDRESS             GAP_PUBLIC_ADDR
#define CFG_PRIVACY                      PRIVACY_DISABLED

/* 1000 ms advertising interval (0x0640 × 0.625 ms = 1000 ms) */
#define CFG_FAST_CONN_ADV_INTERVAL_MIN   (0x0640)
#define CFG_FAST_CONN_ADV_INTERVAL_MAX   (0x0640)
#define ADV_TYPE                         ADV_IND
#define ADV_FILTER                       NO_WHITE_LIST_USE

/* ── Security (open, no bonding for bring-up) ────────────────────────────── */
#define CFG_BONDING_MODE                 (0)
#define CFG_FIXED_PIN                    (111111)
#define CFG_USED_FIXED_PIN               (0)
#define CFG_ENCRYPTION_KEY_SIZE_MAX      (16)
#define CFG_ENCRYPTION_KEY_SIZE_MIN      (8)
#define CFG_IO_CAPABILITY                (0x03)   /* no input/no output */
#define CFG_MITM_PROTECTION              (0x00)   /* not required */
#define CFG_SC_SUPPORT                   (0x01)   /* optional */
#define CFG_KEYPRESS_NOTIFICATION_SUPPORT (0x00)

#define YES (0x01)
#define NO  (0x00)

/* Device name — real suffix set at runtime from UID */
#define CFG_GAP_DEVICE_NAME              "BCN_XXXX"
#define CFG_GAP_DEVICE_NAME_LENGTH       (8)

/* TX power */
#define CFG_TX_POWER                     (0x18)   /* -0.15 dBm */

/* ── PHY ─────────────────────────────────────────────────────────────────── */
#define ALL_PHYS_PREFERENCE              0x00
#define RX_2M_PREFERRED                  0x02
#define TX_2M_PREFERRED                  0x02
#define TX_1M                            0x01
#define TX_2M                            0x02
#define RX_1M                            0x01
#define RX_2M                            0x02

/* ── Key roots ───────────────────────────────────────────────────────────── */
#define CFG_BLE_IR  {0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0,\
                     0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0}
#define CFG_BLE_ER  {0xFE,0xDC,0xBA,0x09,0x87,0x65,0x43,0x21,\
                     0xFE,0xDC,0xBA,0x09,0x87,0x65,0x43,0x21}

#define CFG_USE_SMPS    0

/* ── IPCC IRQ redirect ───────────────────────────────────────────────────── */
/* We call HW_IPCC_Rx/Tx_Handler directly from stm32wbxx_it.c, so no macros
 * needed here.  Do NOT define HAL_RTCEx_WakeUpTimerIRQHandler — it would
 * redirect to HW_TS which we are not using and would break svc_power.c. */

/* ── BLE stack parameters ────────────────────────────────────────────────── */
#define CFG_BLE_NUM_LINK                 1
#define CFG_BLE_NUM_GATT_SERVICES        6    /* GAP + GATT + beacon + NUS + margin */
#define CFG_BLE_NUM_GATT_ATTRIBUTES      27   /* ~11 built-in + 4 beacon + 7 NUS + 5 margin */
#define CFG_BLE_MAX_ATT_MTU              (156)
#define CFG_BLE_ATT_VALUE_ARRAY_SIZE     (1290)
#define CFG_BLE_PREPARE_WRITE_LIST_SIZE  BLE_PREP_WRITE_X_ATT(CFG_BLE_MAX_ATT_MTU)
#define CFG_BLE_MBLOCK_COUNT             (BLE_MBLOCKS_CALC(CFG_BLE_PREPARE_WRITE_LIST_SIZE, \
                                          CFG_BLE_MAX_ATT_MTU, CFG_BLE_NUM_LINK))
#define CFG_BLE_DATA_LENGTH_EXTENSION    1
#define CFG_BLE_PERIPHERAL_SCA           500
#define CFG_BLE_CENTRAL_SCA              0
#define CFG_BLE_LS_SOURCE                (SHCI_C2_BLE_INIT_CFG_BLE_LS_NOCALIB | \
                                          SHCI_C2_BLE_INIT_CFG_BLE_LS_OTHER_DEV | \
                                          SHCI_C2_BLE_INIT_CFG_BLE_LS_CLK_LSE)
#define CFG_BLE_HSE_STARTUP_TIME         0x148
#define CFG_BLE_MAX_CONN_EVENT_LENGTH    (0xFFFFFFFF)
#define CFG_BLE_VITERBI_MODE             1
#define CFG_BLE_OPTIONS                  (SHCI_C2_BLE_INIT_OPTIONS_LL_HOST | \
                                          SHCI_C2_BLE_INIT_OPTIONS_WITH_SVC_CHANGE_DESC | \
                                          SHCI_C2_BLE_INIT_OPTIONS_DEVICE_NAME_RW | \
                                          SHCI_C2_BLE_INIT_OPTIONS_NO_EXT_ADV | \
                                          SHCI_C2_BLE_INIT_OPTIONS_NO_CS_ALGO2 | \
                                          SHCI_C2_BLE_INIT_OPTIONS_FULL_GATTDB_NVM | \
                                          SHCI_C2_BLE_INIT_OPTIONS_GATT_CACHING_NOTUSED | \
                                          SHCI_C2_BLE_INIT_OPTIONS_POWER_CLASS_2_3)
#define CFG_BLE_OPTIONS_EXT              (SHCI_C2_BLE_INIT_OPTIONS_APPEARANCE_READONLY | \
                                          SHCI_C2_BLE_INIT_OPTIONS_ENHANCED_ATT_NOTSUPPORTED)
#define CFG_BLE_MAX_COC_INITIATOR_NBR    (32)
#define CFG_BLE_MIN_TX_POWER             (-40)
#define CFG_BLE_MAX_TX_POWER             (6)
#define CFG_BLE_MAX_ADD_EATT_BEARERS     (0)
#define CFG_BLE_RX_MODEL_CONFIG          (SHCI_C2_BLE_INIT_RX_MODEL_AGC_RSSI_LEGACY)
#define CFG_BLE_MAX_ADV_SET_NBR          (1)
#define CFG_BLE_MAX_ADV_DATA_LEN         (31)
#define CFG_BLE_TX_PATH_COMPENS          (0)
#define CFG_BLE_RX_PATH_COMPENS          (0)
#define CFG_BLE_CORE_VERSION             (SHCI_C2_BLE_INIT_BLE_CORE_5_4)

/* ── Transport layer ─────────────────────────────────────────────────────── */
#define CFG_TLBLE_EVT_QUEUE_LENGTH       5
#define CFG_TLBLE_MOST_EVENT_PAYLOAD_SIZE 255
#define TL_BLE_EVENT_FRAME_SIZE          (TL_EVT_HDR_SIZE + CFG_TLBLE_MOST_EVENT_PAYLOAD_SIZE)

/* ── Reset on power-on for clean re-init ────────────────────────────────── */
#define CFG_HW_RESET_BY_FW               1

/* ── Sequencer tasks ─────────────────────────────────────────────────────── */
typedef enum {
    CFG_TASK_ADV_CANCEL_ID,
    CFG_TASK_HCI_ASYNCH_EVT_ID,
    CFG_LAST_TASK_ID_WITH_HCICMD,
} CFG_Task_Id_With_HCI_Cmd_t;

typedef enum {
    CFG_FIRST_TASK_ID_WITH_NO_HCICMD = CFG_LAST_TASK_ID_WITH_HCICMD - 1,
    CFG_TASK_SYSTEM_HCI_ASYNCH_EVT_ID,
    CFG_LAST_TASK_ID_WITH_NO_HCICMD,
} CFG_Task_Id_With_NO_HCI_Cmd_t;

#define CFG_TASK_NBR    CFG_LAST_TASK_ID_WITH_NO_HCICMD

typedef enum { CFG_SCH_PRIO_0, CFG_SCH_PRIO_NBR } CFG_SCH_Prio_Id_t;

typedef enum {
    CFG_IDLEEVT_HCI_CMD_EVT_RSP_ID,
    CFG_IDLEEVT_SYSTEM_HCI_CMD_EVT_RSP_ID,
} CFG_IdleEvt_Id_t;

/* ── OTP (WB1M: 1 kB OTP at 0x1FFF7000–0x1FFF73FF) ─────────────────────── */
#define CFG_OTP_BASE_ADDRESS    (0x1FFF7000UL)
#define CFG_OTP_END_ADRESS      (0x1FFF73FFUL)

/* ── Debug (suppress — TX_Beacon uses its own UART) ─────────────────────── */
#define CFG_DEBUG_APP_TRACE        0
#define CFG_DEBUG_BLE_TRACE        0
#define CFG_DEBUG_TRACE_FULL       0
#define CFG_DEBUG_TRACE_LIGHT      0
#define APP_DBG_MSG(...)        do {} while (0)
#define BLE_DBG_SVCCTL_MSG(...) do {} while (0)

/* L2CAP not used */
#define L2CAP_REQUEST_NEW_CONN_PARAM     0

#endif /* APP_CONF_H */
