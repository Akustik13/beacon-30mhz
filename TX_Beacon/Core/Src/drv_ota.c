#include "drv_ota.h"
#include "ota_flash_map.h"
#include "flash_config.h"   /* Flash_ErasePage, Flash_WriteBlock, Flash_CPU2IsIdle */
#include "proto_uart.h"     /* Proto_CRC32                                         */
#include "app_ble.h"        /* BLE_ProcessEvents                                   */
#include "drv_uart.h"       /* UART_Printf (diagnostics)                           */
#include <string.h>

/* ── Module state ─────────────────────────────────────────────────────────── */
static OtaState_t s_state       = OTA_STATE_IDLE;
static uint32_t   s_total_size  = 0U;   /* declared image size (bytes)         */
static uint32_t   s_version     = 0U;   /* declared version word               */
static uint32_t   s_begin_crc   = 0U;   /* CRC32 declared in OTA_Begin         */
static uint32_t   s_rx_offset   = 0U;   /* bytes written to Slot B so far      */

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static uint8_t _meta_read(OtaMetaPage_t *m)
{
    memcpy(m, (const void *)OTA_META_ADDR, sizeof(OtaMetaPage_t));
    return 1U;
}

/* Erase + rewrite the metadata page.  Preserves whichever headers are NOT
 * being replaced by using the freshly-read in-memory copy. */
static uint8_t _meta_write(const OtaMetaPage_t *m)
{
    if (!Flash_CPU2IsIdle()) return 0U;
    if (!Flash_ErasePage(OTA_META_PAGE)) return 0U;
    /* Write 72 bytes (must be multiple of 8; pad to 80 bytes = 10 dwords) */
    uint8_t buf[80U];
    memcpy(buf, m, sizeof(OtaMetaPage_t));
    memset(buf + sizeof(OtaMetaPage_t), 0xFFU, sizeof(buf) - sizeof(OtaMetaPage_t));
    return Flash_WriteBlock(OTA_META_ADDR, buf, sizeof(buf));
}

/* ── Public: boot-state I/O ─────────────────────────────────────────────── */

uint8_t OTA_ReadBootState(BootState_t *bs)
{
    OtaMetaPage_t m;
    _meta_read(&m);
    *bs = m.boot_state;
    uint32_t calc = Proto_CRC32((const uint8_t *)bs, offsetof(BootState_t, crc32));
    return (calc == bs->crc32) ? 1U : 0U;
}

uint8_t OTA_WriteBootState(const BootState_t *bs_in)
{
    OtaMetaPage_t m;
    _meta_read(&m);

    BootState_t bs = *bs_in;
    bs.crc32 = Proto_CRC32((const uint8_t *)&bs, offsetof(BootState_t, crc32));
    m.boot_state = bs;

    return _meta_write(&m);
}

uint8_t OTA_ReadSlotHeader(uint8_t slot, ImageHeader_t *hdr)
{
    OtaMetaPage_t m;
    _meta_read(&m);
    *hdr = (slot == BOOT_SLOT_A) ? m.slot_a_header : m.slot_b_header;
    return (hdr->magic == OTA_IMAGE_MAGIC) ? 1U : 0U;
}

/* ── Public: self-confirm ─────────────────────────────────────────────────── */

uint8_t OTA_SelfConfirm(void)
{
    BootState_t bs;
    if (!OTA_ReadBootState(&bs)) {
        /* No valid boot state — nothing to confirm */
        return 0U;
    }
    if (bs.pending_slot == BOOT_NO_PENDING) {
        return 1U;  /* already confirmed / no update in flight */
    }
    /* The bootloader completed a copy from Slot B → Slot A and booted.
     * Confirm: clear pending so the next boot doesn't retry the copy. */
    bs.active_slot    = BOOT_SLOT_A;
    bs.pending_slot   = BOOT_NO_PENDING;
    bs.boot_attempts  = 0U;
    bs.reserved       = 0U;
    uint8_t ok = OTA_WriteBootState(&bs);
    UART_Printf("[OTA] self-confirm %s\r\n", ok ? "OK" : "FAIL");
    return ok;
}

/* ── Public: engine init ─────────────────────────────────────────────────── */

void OTA_Init(void)
{
    s_state      = OTA_STATE_IDLE;
    s_total_size = 0U;
    s_rx_offset  = 0U;
}

OtaState_t OTA_GetState(void) { return s_state; }

/* ── Public: OTA_Begin ───────────────────────────────────────────────────── */

uint8_t OTA_Begin(uint32_t total_size, uint32_t version, uint32_t crc32)
{
    if (s_state != OTA_STATE_IDLE && s_state != OTA_STATE_DONE) {
        UART_Print("[OTA] BEGIN: already active — abort first\r\n");
        return OTA_ERR_STATE;
    }
    if (total_size == 0U || total_size > OTA_SLOT_SIZE) {
        UART_Printf("[OTA] BEGIN: size %lu > slot %lu\r\n", total_size, OTA_SLOT_SIZE);
        return OTA_ERR_SIZE;
    }
    if (!Flash_CPU2IsIdle()) {
        UART_Print("[OTA] BEGIN: CPU2 holding flash\r\n");
        return OTA_ERR_BUSY;
    }

    s_state      = OTA_STATE_ERASING;
    s_total_size = total_size;
    s_version    = version;
    s_begin_crc  = crc32;
    s_rx_offset  = 0U;

    /* Erase Slot B page by page; yield to BLE between pages so connection stays. */
    uint32_t pages = (total_size + OTA_PAGE_SIZE - 1U) / OTA_PAGE_SIZE;
    if (pages > OTA_SLOT_PAGE_COUNT) pages = OTA_SLOT_PAGE_COUNT;

    UART_Printf("[OTA] BEGIN: size=%lu ver=0x%08lX pages=%lu\r\n",
                total_size, version, pages);

    for (uint32_t i = 0U; i < pages; i++) {
        if (!Flash_ErasePage(OTA_SLOT_B_PAGE + i)) {
            s_state = OTA_STATE_IDLE;
            return OTA_ERR_FLASH;
        }
        BLE_ProcessEvents();    /* keep BLE alive during ~22 ms erase           */
    }

    s_state = OTA_STATE_RECEIVING;
    return OTA_OK;
}

/* ── Public: OTA_Chunk ───────────────────────────────────────────────────── */

uint8_t OTA_Chunk(uint32_t offset, const uint8_t *data, uint8_t len)
{
    if (s_state != OTA_STATE_RECEIVING) return OTA_ERR_STATE;
    if (len == 0U) return OTA_ERR_SIZE;
    if (offset != s_rx_offset) return OTA_ERR_OFFSET;
    if ((uint32_t)offset + len > s_total_size) return OTA_ERR_SIZE;

    uint32_t dest = OTA_SLOT_B_ADDR + offset;

    /* Flash_WriteBlock requires 8-byte multiples.  Pad final chunk with 0xFF
     * (erased flash default) so we always write a complete set of dwords. */
    if ((len & 7U) != 0U) {
        uint8_t aligned[OTA_CHUNK_MAX + 8U];
        uint8_t padded = (uint8_t)((len + 7U) & ~7U);
        memcpy(aligned, data, len);
        memset(aligned + len, 0xFFU, padded - len);
        if (!Flash_WriteBlock(dest, aligned, padded)) {
            s_state = OTA_STATE_IDLE;
            return OTA_ERR_FLASH;
        }
    } else {
        if (!Flash_WriteBlock(dest, data, len)) {
            s_state = OTA_STATE_IDLE;
            return OTA_ERR_FLASH;
        }
    }

    /* Verify readback */
    if (memcmp((const void *)dest, data, len) != 0) {
        s_state = OTA_STATE_IDLE;
        return OTA_ERR_VERIFY;
    }

    s_rx_offset += len;
    return OTA_OK;
}

/* ── Public: OTA_Finish ─────────────────────────────────────────────────── */

uint8_t OTA_Finish(void)
{
    if (s_state != OTA_STATE_RECEIVING) return OTA_ERR_STATE;
    if (s_rx_offset != s_total_size) {
        UART_Printf("[OTA] FINISH: incomplete: got %lu / %lu\r\n",
                    s_rx_offset, s_total_size);
        return OTA_ERR_STATE;
    }

    /* Verify full image CRC from flash */
    uint32_t calc = Proto_CRC32((const uint8_t *)OTA_SLOT_B_ADDR, s_total_size);
    if (calc != s_begin_crc) {
        UART_Printf("[OTA] FINISH: CRC MISMATCH calc=0x%08lX expected=0x%08lX\r\n",
                    calc, s_begin_crc);
        s_state = OTA_STATE_IDLE;
        return OTA_ERR_CRC;
    }

    /* CRC ok — write ImageHeader for Slot B and update BootState */
    OtaMetaPage_t m;
    _meta_read(&m);

    ImageHeader_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic      = OTA_IMAGE_MAGIC;
    hdr.version    = s_version;
    hdr.image_size = s_total_size;
    hdr.crc32      = s_begin_crc;
    m.slot_b_header = hdr;

    /* Update BootState: request Slot B copy on next boot */
    m.boot_state.pending_slot  = BOOT_SLOT_B;
    m.boot_state.boot_attempts = 0U;
    m.boot_state.reserved      = 0U;
    m.boot_state.crc32 = Proto_CRC32((const uint8_t *)&m.boot_state,
                                     offsetof(BootState_t, crc32));

    if (!_meta_write(&m)) {
        s_state = OTA_STATE_IDLE;
        return OTA_ERR_FLASH;
    }

    s_state = OTA_STATE_DONE;
    UART_Printf("[OTA] FINISH OK — ver=0x%08lX pending reboot\r\n", s_version);
    return OTA_OK;
}

/* ── Public: OTA_Abort ───────────────────────────────────────────────────── */

void OTA_Abort(void)
{
    s_state     = OTA_STATE_IDLE;
    s_rx_offset = 0U;
    UART_Print("[OTA] ABORT\r\n");
}

/* ── Public: OTA_GetStatus ─────────────────────────────────────────────────
 * Response: active(1) pending(1) attempts(1) a_ver(4) b_ver(4) a_crc(4) b_crc(4)
 * = 18 bytes */
void OTA_GetStatus(uint8_t *out, uint8_t *out_len)
{
    OtaMetaPage_t m;
    _meta_read(&m);

    BootState_t *bs = &m.boot_state;
    uint32_t calc = Proto_CRC32((const uint8_t *)bs, offsetof(BootState_t, crc32));
    uint8_t  bs_ok = (calc == bs->crc32) ? 1U : 0U;

    out[0] = bs_ok ? bs->active_slot  : BOOT_SLOT_A;
    out[1] = bs_ok ? bs->pending_slot : BOOT_NO_PENDING;
    out[2] = bs_ok ? bs->boot_attempts : 0U;

    /* Slot A header version + crc */
    uint32_t a_ver = 0U, a_crc = 0U;
    if (m.slot_a_header.magic == OTA_IMAGE_MAGIC) {
        a_ver = m.slot_a_header.version;
        a_crc = m.slot_a_header.crc32;
    }
    out[3]  = (uint8_t)(a_ver);
    out[4]  = (uint8_t)(a_ver >> 8U);
    out[5]  = (uint8_t)(a_ver >> 16U);
    out[6]  = (uint8_t)(a_ver >> 24U);

    /* Slot B header version + crc */
    uint32_t b_ver = 0U, b_crc = 0U;
    if (m.slot_b_header.magic == OTA_IMAGE_MAGIC) {
        b_ver = m.slot_b_header.version;
        b_crc = m.slot_b_header.crc32;
    }
    out[7]  = (uint8_t)(b_ver);
    out[8]  = (uint8_t)(b_ver >> 8U);
    out[9]  = (uint8_t)(b_ver >> 16U);
    out[10] = (uint8_t)(b_ver >> 24U);

    out[11] = (uint8_t)(a_crc);
    out[12] = (uint8_t)(a_crc >> 8U);
    out[13] = (uint8_t)(a_crc >> 16U);
    out[14] = (uint8_t)(a_crc >> 24U);

    out[15] = (uint8_t)(b_crc);
    out[16] = (uint8_t)(b_crc >> 8U);
    out[17] = (uint8_t)(b_crc >> 16U);
    out[18] = (uint8_t)(b_crc >> 24U);

    *out_len = 19U;
}
