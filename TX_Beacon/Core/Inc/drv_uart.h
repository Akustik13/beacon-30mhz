#pragma once
#include <stdint.h>

void    UART_Init(void);
void    UART_DeInit(void);
uint8_t UART_IsConnected(void);    /* boot probe: PA10 HIGH = PC connected */
void    UART_ActivityUpdate(void);
void    UART_CheckIdle(void);      /* disable UART when PA10 goes LOW (disconnected) */
void    UART_TryReconnect(void);   /* re-init UART if PA10 goes HIGH again (free IDR read) */
uint8_t UART_IsActive(void);

/* Telemetry / AT output — suppressed in silent mode */
void    UART_Print(const char *str);
void    UART_Printf(const char *fmt, ...);

/* Machine-protocol output — always transmitted, bypasses silent mode */
void    UART_MachPrint(const char *str);
void    UART_MachPrintf(const char *fmt, ...);

/* Silent mode: suppress all AT/telemetry output; auto-reverts after 30 s
 * of inactivity (no machine-protocol commands received).               */
extern uint8_t g_uart_silent;
void    UART_SilentSet(uint8_t on);       /* 0=verbose, 1=silent; resets TTL  */
void    UART_SilentKeepalive(void);       /* call on each machine cmd received */
void    UART_SilentTick(void);            /* call from main loop (any rate)    */

void    UART_RxEnable(void);
uint8_t UART_RxGetChar(uint8_t *ch);  /* 1 = got char, 0 = empty */
void    UART_RxIRQ(void);             /* call from USART1_IRQHandler */

/* NUS output hook — set by ble_nus_service when client subscribes, cleared on disconnect.
 * Both UART_Print and UART_MachPrint call the hook so all firmware output reaches the GUI. */
typedef void (*UartNusHook_t)(const char *str);
void UART_SetNusHook(UartNusHook_t hook);
