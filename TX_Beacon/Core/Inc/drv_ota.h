#pragma once
#include <stdint.h>
#include "ota_types.h"

/*
 * OTA engine — app-side.
 *
 * Protocol (opcodes in cmd_layer.h):
 *   OP_OTA_STATUS → OTA_GetStatus()
 *   OP_OTA_BEGIN  → OTA_Begin(total_size, version, crc32)
 *   OP_OTA_CHUNK  → OTA_Chunk(offset, data, len)
 *   OP_OTA_FINISH → OTA_Finish()
 *   OP_OTA_ABORT  → OTA_Abort()
 *   OP_OTA_REBOOT → handled in cmd_layer (same path as OP_REBOOT)
 *
 * A transfer writes raw app binary bytes into the INACTIVE slot (Slot B if
 * Slot A is active, always — we boot from Slot A exclusively).
 * OTA_Finish() verifies the full CRC, writes BootState pending_slot = B and
 * the ImageHeader for Slot B.  The bootloader then copies B → A on next boot.
 *
 * Max chunk payload: OTA_CHUNK_MAX bytes (limited by CMD_MAX_IN_PAYLOAD).
 */

#define OTA_CHUNK_MAX    116U   /* 128 (CMD_MAX) - 4 (offset u32) - 8 overhead */

/* Engine result codes (returned in CMD response byte) */
#define OTA_OK           0x00U
#define OTA_ERR_STATE    0x01U  /* not in expected OTA state                   */
#define OTA_ERR_SIZE     0x02U  /* image too large for slot                    */
#define OTA_ERR_OFFSET   0x03U  /* unexpected chunk offset (sequence error)    */
#define OTA_ERR_FLASH    0x04U  /* flash write/erase failed                    */
#define OTA_ERR_CRC      0x05U  /* image CRC mismatch at FINISH                */
#define OTA_ERR_BUSY     0x06U  /* flash locked (CPU2 active)                  */
#define OTA_ERR_VERIFY   0x07U  /* post-write readback mismatch                */

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_ERASING,      /* Slot B erase in progress (within OTA_Begin)     */
    OTA_STATE_RECEIVING,    /* accepting OTA_CHUNK calls                       */
    OTA_STATE_DONE,         /* OTA_Finish succeeded; pending reboot            */
} OtaState_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

void       OTA_Init(void);
OtaState_t OTA_GetState(void);

/* OP_OTA_BEGIN: declare transfer start; erases Slot B pages.
 * total_size = app binary bytes (must be ≤ OTA_SLOT_SIZE).
 * version    = (major<<16)|(minor<<8)|patch.
 * crc32      = expected CRC32 of the full image (verified at FINISH).
 * Returns OTA_OK or OTA_ERR_*. */
uint8_t OTA_Begin(uint32_t total_size, uint32_t version, uint32_t crc32);

/* OP_OTA_CHUNK: write <len> bytes at <offset> within the inactive slot.
 * offset must equal the cumulative number of bytes received so far.
 * Returns OTA_OK or OTA_ERR_*. */
uint8_t OTA_Chunk(uint32_t offset, const uint8_t *data, uint8_t len);

/* OP_OTA_FINISH: verify full image CRC; if good, commit BootState.
 * Returns OTA_OK or OTA_ERR_CRC / OTA_ERR_STATE. */
uint8_t OTA_Finish(void);

/* OP_OTA_ABORT: cancel transfer in any state; leaves Slot B in erased state.
 * BootState is NOT modified. */
void    OTA_Abort(void);

/* OP_OTA_STATUS: fill out[] with status payload; sets *out_len.
 * Response: active(1) pending(1) attempts(1) a_ver(4) b_ver(4) a_crc(4) b_crc(4)
 * Total: 18 bytes */
void    OTA_GetStatus(uint8_t *out, uint8_t *out_len);

/* App self-confirm: call after post-boot sanity checks pass.
 * Clears pending_slot → 0xFF, resets boot_attempts.
 * Safe to call on every boot; is a no-op if pending_slot == 0xFF. */
uint8_t OTA_SelfConfirm(void);

/* Read current BootState from flash. Returns 1 if CRC valid. */
uint8_t OTA_ReadBootState(BootState_t *bs);

/* Write BootState to flash (computes CRC, erases + rewrites meta page).
 * Returns 1 on success. */
uint8_t OTA_WriteBootState(const BootState_t *bs);

/* Read ImageHeader for a slot (BOOT_SLOT_A or BOOT_SLOT_B) from meta page.
 * Returns 1 if magic == OTA_IMAGE_MAGIC. */
uint8_t OTA_ReadSlotHeader(uint8_t slot, ImageHeader_t *hdr);
