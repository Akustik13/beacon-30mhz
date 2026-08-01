#include "ble_beacon_service.h"
#include "ble_common.h"
#include "ble_gatt_aci.h"
#include "ble_gap_aci.h"
#include "ble_vs_codes.h"
#include "svc_ctl.h"
#include "drv_uart.h"
#include <string.h>

#define BEACON_SVC_UUID     0xBEACU
#define BEACON_CHAR_UUID    0xBEA1U

static uint16_t s_svc_handle  = 0U;
static uint16_t s_char_handle = 0U;
static uint8_t  s_notify_en   = 0U;   /* 1 when client wrote CCCD 0x0001 */

static SVCCTL_EvtAckStatus_t _EventHandler(void *pckt);

void BLE_Beacon_OnDisconnect(void)
{
    s_notify_en = 0U;
}

void BLE_Beacon_Init(void)
{
    Service_UUID_t   svc_uuid;
    Char_UUID_t      chr_uuid;
    tBleStatus       ret;

    SVCCTL_RegisterSvcHandler(_EventHandler);

    /* Add primary service */
    svc_uuid.Service_UUID_16 = BEACON_SVC_UUID;
    ret = aci_gatt_add_service(UUID_TYPE_16,
                               &svc_uuid,
                               PRIMARY_SERVICE,
                               4U,             /* max attribute records: svc + char decl + char val + CCCD */
                               &s_svc_handle);
    if (ret != BLE_STATUS_SUCCESS)
        UART_Printf("[BLE] add_service err 0x%02X\r\n", ret);

    /* Add characteristic: READ + NOTIFY, 24 bytes fixed */
    chr_uuid.Char_UUID_16 = BEACON_CHAR_UUID;
    ret = aci_gatt_add_char(s_svc_handle,
                            UUID_TYPE_16,
                            &chr_uuid,
                            sizeof(StatusBlob_t),
                            CHAR_PROP_READ | CHAR_PROP_NOTIFY,
                            ATTR_PERMISSION_NONE,
                            GATT_NOTIFY_ATTRIBUTE_WRITE, /* get notified when CCCD written */
                            16U,
                            CHAR_VALUE_LEN_CONSTANT,
                            &s_char_handle);
    if (ret != BLE_STATUS_SUCCESS)
        UART_Printf("[BLE] add_char err 0x%02X\r\n", ret);
}

void BLE_Beacon_NotifyStatus(const StatusBlob_t *blob)
{
    if (!s_notify_en || s_char_handle == 0U) return;

    tBleStatus ret = aci_gatt_update_char_value(s_svc_handle,
                                                s_char_handle,
                                                0U,
                                                sizeof(StatusBlob_t),
                                                (const uint8_t*)blob);
    if (ret != BLE_STATUS_SUCCESS)
        UART_Printf("[BLE] notify err 0x%02X\r\n", ret);
}

static SVCCTL_EvtAckStatus_t _EventHandler(void *pckt)
{
    hci_event_pckt            *event_pckt = (hci_event_pckt*)((hci_uart_pckt*)pckt)->data;
    evt_blecore_aci           *blecore_evt;
    aci_gatt_attribute_modified_event_rp0 *attr_mod;

    if (event_pckt->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE)
        return SVCCTL_EvtNotAck;

    blecore_evt = (evt_blecore_aci*)event_pckt->data;

    if (blecore_evt->ecode == ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) {
        attr_mod = (aci_gatt_attribute_modified_event_rp0*)blecore_evt->data;
        /* CCCD is char_handle + 2 */
        if (attr_mod->Attr_Handle == (s_char_handle + 2U)) {
            if (attr_mod->Attr_Data[0] == 0x01U) {
                s_notify_en = 1U;
                UART_Print("[BLE] StatusBlob notifications ENABLED\r\n");
            } else {
                s_notify_en = 0U;
                UART_Print("[BLE] StatusBlob notifications disabled\r\n");
            }
            return SVCCTL_EvtAckFlowEnable;
        }
    }

    return SVCCTL_EvtNotAck;
}
