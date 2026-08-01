#ifndef BLE_NUS_SERVICE_H
#define BLE_NUS_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Nordic UART Service (NUS) — 128-bit UUID BLE transport.
 *   RX char 6e400002-... (WRITE): client → firmware (command lines)
 *   TX char 6e400003-... (NOTIFY): firmware → client (responses + telemetry)
 *
 * Wire-up:
 *   Call NUS_Init() once inside BLE_AppInit() after SVCCTL_Init().
 *   Call NUS_OnDisconnect() on every BLE disconnect event.
 *
 * Incoming bytes are assembled into lines; each complete line is dispatched
 * to Proto_HandleLine().  Responses written via UART_Print / UART_MachPrint
 * are routed to NUS TX via the hook set in drv_uart.c. */

void NUS_Init(void);
void NUS_OnDisconnect(void);
void NUS_TX_Send(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* BLE_NUS_SERVICE_H */
