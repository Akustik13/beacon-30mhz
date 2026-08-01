#ifndef BLE_BEACON_SERVICE_H
#define BLE_BEACON_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "proto_structs.h"

/* Service UUID  : 0x0000BEAC-0000-1000-8000-00805F9B34FB (16-bit: 0xBEAC)
 * Characteristic: 0x0000BEA1-0000-1000-8000-00805F9B34FB (16-bit: 0xBEA1)
 *   - Properties: READ + NOTIFY
 *   - Value:      StatusBlob_t (24 bytes, fixed length) */

void BLE_Beacon_Init(void);
void BLE_Beacon_NotifyStatus(const StatusBlob_t *blob);
void BLE_Beacon_OnDisconnect(void);   /* reset CCCD subscription state on disconnect */

#ifdef __cplusplus
}
#endif

#endif /* BLE_BEACON_SERVICE_H */
