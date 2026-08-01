#pragma once
#include <stdint.h>

/* CRC32 (IEEE 802.3 / zlib: poly 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF).
 * Shared implementation — use this instead of private copies in flash_config.c / hw_desc.c. */
uint32_t Proto_CRC32(const uint8_t *data, uint32_t len);

/* Initialize machine protocol (no-op currently, reserved for future use). */
void Proto_Init(void);

/* Handle one complete line that starts with '#'.
 * Called by UartCmd_Poll() when the accumulated buffer begins with '#'.
 * Format: #<id> <VERB>[ <hexpayload>]
 * Sends all response lines (data + OK/ERR) synchronously via UART_Print. */
void Proto_HandleLine(const char *line);

/* Force-read all active sensors (updates g_last_* cache) then fill a fresh
 * 24-byte StatusBlob_t into *out.  Returns the number of bytes written (24). */
uint8_t Proto_FreshStat(uint8_t *out);
