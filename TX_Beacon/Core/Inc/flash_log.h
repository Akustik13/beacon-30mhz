#pragma once
#include "flash_log_types.h"

int  FlashLog_Init(LogConfig_t *cfg);
void FlashLog_Task(void);
int  FlashLog_WriteNow(void);

int  FlashLog_ReadRecord(uint32_t idx, FlashLogRecord_t *out);
int  FlashLog_DecodeAbsolute(uint32_t idx, FlashLogRecord_t *out_abs);

void FlashLog_GetStatus(uint32_t *total, uint32_t *free_count,
                        uint32_t *used_bytes, uint32_t *free_bytes);
int  FlashLog_ReadHeader(FlashLogHeader_t *out);
int  FlashLog_ReadFieldDesc(uint8_t idx, FlashFieldDesc_t *out);

int  FlashLog_Clear(void);
/* Update LogConfig in RAM only — does NOT write flash.
 * Call FlashLog_CommitConfig() to persist (or it is flushed by SAVE! command). */
int  FlashLog_SetConfig(LogConfig_t *cfg);
/* Flush RAM LogConfig to flash page 61. Returns 0 on success, -1 on error. */
int  FlashLog_CommitConfig(void);
void FlashLog_GetConfig(LogConfig_t *out);

void FlashLog_PrintStatus(void);
void FlashLog_PrintCalc(void);
void FlashLog_PrintRecord(uint32_t idx);
void FlashLog_DumpCSV(uint32_t start, uint32_t count);

/* ── v2 typed-record writers ─────────────────────────────────────────────── */
/* Write a LOGREC_TYPE_MARKER record. tag=0x01 for WoM event, 0x02 for manual. */
int FlashLog_WriteMark(uint8_t tag);
/* Write a LOGREC_TYPE_ACCEL record (raw 16-bit XYZ + FS value). */
int FlashLog_WriteAccelV2(int16_t x, int16_t y, int16_t z, uint8_t fs);

/* ── BLE-session RAM queue ───────────────────────────────────────────────── */
/* Flush entries buffered in RAM during a BLE session to flash.
 * Call after BLE disconnect, guarded by Flash_CPU2IsIdle(). */
void FlashLog_FlushBleQueue(void);
