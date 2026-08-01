#include "ble_nus_service.h"
#include "ble_common.h"
#include "ble_gatt_aci.h"
#include "ble_vs_codes.h"
#include "svc_ctl.h"
#include "proto_uart.h"
#include "drv_uart.h"
#include "app_ble.h"
#include "stm32wbxx_hal.h"
#include <string.h>

/* NUS 128-bit UUIDs, little-endian (STM32 BLE stack byte order):
 *   Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
 *   RX char: 6e400002-b5a3-f393-e0a9-e50e24dcca9e  (Write W/O Resp: PC → MCU)
 *   TX char: 6e400003-b5a3-f393-e0a9-e50e24dcca9e  (Notify: MCU → PC)       */
#define NUS_SVC_UUID_128 \
    {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e}
#define NUS_RX_UUID_128 \
    {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e}
#define NUS_TX_UUID_128 \
    {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e}

/* Max data per write/notify.  Default ATT_MTU=23 → 20-byte payload safe minimum. */
#define NUS_MAX_ATTR_LEN  244U

/* Line accumulation buffer for incoming command bytes */
#define NUS_LINE_BUF_SZ   256U

static uint16_t s_svc_handle      = 0U;
static uint16_t s_rx_char_handle  = 0U;  /* NUS RX declaration handle */
static uint16_t s_tx_char_handle  = 0U;  /* NUS TX declaration handle */
static uint8_t  s_tx_notify_en    = 0U;  /* 1 = CCCD subscribed       */
static uint16_t s_att_mtu         = 20U; /* max notify payload bytes; updated on MTU exchange */

static uint8_t  s_rx_line[NUS_LINE_BUF_SZ];
static uint16_t s_rx_line_len = 0U;

static SVCCTL_EvtAckStatus_t _EventHandler(void *pckt);
static void _nus_tx_hook(const char *str);

/* ── Public API ───────────────────────────────────────────────────────────── */

void NUS_OnDisconnect(void)
{
    s_tx_notify_en = 0U;
    s_rx_line_len  = 0U;
    s_att_mtu      = 20U;
    UART_SetNusHook(NULL);
}

void NUS_Init(void)
{
    static const uint8_t svc_uuid_128[16] = NUS_SVC_UUID_128;
    static const uint8_t rx_uuid_128[16]  = NUS_RX_UUID_128;
    static const uint8_t tx_uuid_128[16]  = NUS_TX_UUID_128;
    Service_UUID_t svc_uuid;
    Char_UUID_t    chr_uuid;
    tBleStatus     ret;

    SVCCTL_RegisterSvcHandler(_EventHandler);

    /* Primary service — 6 attribute records:
     *   svc(1) + rx_decl(1)+rx_val(1) + tx_decl(1)+tx_val(1)+tx_cccd(1) */
    memcpy(svc_uuid.Service_UUID_128, svc_uuid_128, 16U);
    ret = aci_gatt_add_service(UUID_TYPE_128, &svc_uuid, PRIMARY_SERVICE,
                               7U, &s_svc_handle);
    if (ret != BLE_STATUS_SUCCESS)
        UART_Printf("[NUS] add_service err 0x%02X\r\n", ret);

    /* RX: Write Without Response (PC → MCU command lines) */
    memcpy(chr_uuid.Char_UUID_128, rx_uuid_128, 16U);
    ret = aci_gatt_add_char(s_svc_handle,
                            UUID_TYPE_128, &chr_uuid,
                            NUS_MAX_ATTR_LEN,
                            CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP,
                            ATTR_PERMISSION_NONE,
                            GATT_NOTIFY_ATTRIBUTE_WRITE,
                            16U, CHAR_VALUE_LEN_VARIABLE,
                            &s_rx_char_handle);
    if (ret != BLE_STATUS_SUCCESS)
        UART_Printf("[NUS] add_rx_char err 0x%02X\r\n", ret);

    /* TX: Notify (MCU → PC responses + telemetry) */
    memcpy(chr_uuid.Char_UUID_128, tx_uuid_128, 16U);
    ret = aci_gatt_add_char(s_svc_handle,
                            UUID_TYPE_128, &chr_uuid,
                            NUS_MAX_ATTR_LEN,
                            CHAR_PROP_NOTIFY,
                            ATTR_PERMISSION_NONE,
                            GATT_NOTIFY_ATTRIBUTE_WRITE,
                            16U, CHAR_VALUE_LEN_VARIABLE,
                            &s_tx_char_handle);
    if (ret != BLE_STATUS_SUCCESS)
        UART_Printf("[NUS] add_tx_char err 0x%02X\r\n", ret);
}

void NUS_TX_Send(const uint8_t *data, uint16_t len)
{
    if (!s_tx_notify_en || s_tx_char_handle == 0U || !data || len == 0U) return;

    uint16_t offset = 0U;
    while (offset < len) {
        uint16_t chunk = len - offset;
        if (chunk > s_att_mtu) chunk = s_att_mtu;

        tBleStatus ret;
        uint8_t tries = 0U;
        do {
            ret = aci_gatt_update_char_value(s_svc_handle, s_tx_char_handle,
                                             0U, (uint8_t)chunk, data + offset);
            if (ret == BLE_STATUS_SUCCESS) break;
            /* BLE_STATUS_INSUFFICIENT_RESOURCES: TX pool full — wait for CPU2
             * to drain a connection event (~connection interval, up to 100 ms) */
            HAL_Delay(20U);
        } while (++tries < 20U);  /* up to 400 ms total per chunk */

        offset += chunk;
    }
}

/* ── Internal ─────────────────────────────────────────────────────────────── */

static void _nus_tx_hook(const char *str)
{
    if (str)
        NUS_TX_Send((const uint8_t *)str, (uint16_t)strlen(str));
}

static SVCCTL_EvtAckStatus_t _EventHandler(void *pckt)
{
    hci_event_pckt *event_pckt =
        (hci_event_pckt*)((hci_uart_pckt*)pckt)->data;
    evt_blecore_aci *blecore_evt;
    aci_gatt_attribute_modified_event_rp0 *attr_mod;

    if (event_pckt->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE)
        return SVCCTL_EvtNotAck;

    blecore_evt = (evt_blecore_aci*)event_pckt->data;

    /* MTU exchange: update chunk size so large responses use full MTU */
    if (blecore_evt->ecode == ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE) {
        aci_att_exchange_mtu_resp_event_rp0 *mtu_evt =
            (aci_att_exchange_mtu_resp_event_rp0*)blecore_evt->data;
        uint16_t payload = (mtu_evt->Server_RX_MTU > 3U)
                         ? (mtu_evt->Server_RX_MTU - 3U) : 20U;
        if (payload > NUS_MAX_ATTR_LEN) payload = NUS_MAX_ATTR_LEN;
        s_att_mtu = payload;
        UART_Printf("[NUS] MTU=%u chunk=%u\r\n", mtu_evt->Server_RX_MTU, s_att_mtu);
        return SVCCTL_EvtAckFlowEnable;
    }

    if (blecore_evt->ecode != ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE)
        return SVCCTL_EvtNotAck;

    attr_mod = (aci_gatt_attribute_modified_event_rp0*)blecore_evt->data;

    /* TX CCCD written: client subscribes or unsubscribes notifications */
    if (attr_mod->Attr_Handle == (s_tx_char_handle + 2U)) {
        if (attr_mod->Attr_Data_Length >= 1U && attr_mod->Attr_Data[0] == 0x01U) {
            s_tx_notify_en = 1U;
            UART_SetNusHook(_nus_tx_hook);
            UART_Print("[NUS] connected\r\n");
        } else {
            s_tx_notify_en = 0U;
            UART_SetNusHook(NULL);
        }
        return SVCCTL_EvtAckFlowEnable;
    }

    /* RX value written: client sent command bytes */
    if (attr_mod->Attr_Handle == (s_rx_char_handle + 1U)) {
        BLE_NusMarkActivity();   /* reset inactivity watchdog */
        for (uint16_t i = 0U; i < attr_mod->Attr_Data_Length; i++) {
            uint8_t c = attr_mod->Attr_Data[i];
            if (c == '\n') {
                if (s_rx_line_len > 0U && s_rx_line[s_rx_line_len - 1U] == '\r')
                    s_rx_line_len--;
                s_rx_line[s_rx_line_len] = '\0';
                if (s_rx_line_len > 0U)
                    Proto_HandleLine((const char *)s_rx_line);
                s_rx_line_len = 0U;
            } else if (s_rx_line_len < NUS_LINE_BUF_SZ - 1U) {
                s_rx_line[s_rx_line_len++] = c;
            }
        }
        return SVCCTL_EvtAckFlowEnable;
    }

    return SVCCTL_EvtNotAck;
}
