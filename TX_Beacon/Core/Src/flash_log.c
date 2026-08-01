#include "flash_log.h"
#include "flash_config.h"
#include "hw_desc.h"         /* for compile-time page-overlap check only    */
#include "drv_chip_temp.h"   /* g_last_temp_x10, INT32_MIN guard            */
#include "drv_batt_adc.h"    /* g_last_batt_mV, g_last_batt_pct             */
#include "drv_light_adc.h"   /* g_last_light_raw, g_light_ever_read         */
#include "drv_uart.h"
#include "svc_power.h"       /* Power_RTC_GetDateTime, Power_RTC_IsSet      */
#include "lis2dw12.h"        /* LIS2DW12_IsPowered, ReadXYZ, GetFS          */
#include "app_ble.h"         /* BLE_IsConnected() for BLE-safe logging       */
#include <string.h>

/* Config page must not overlap log header */
#if LOG_HEADER_PAGE == FLASH_CONFIG_PAGE
#error "LOG_HEADER_PAGE == FLASH_CONFIG_PAGE — adjust page numbers!"
#endif
/* hw_desc must not overlap log pages (61-67) */
#if (HW_DESC_PAGE >= LOG_HEADER_PAGE) && (HW_DESC_PAGE <= LOG_DATA_END_PAGE)
#error "HW_DESC_PAGE overlaps flash log pages — adjust hw_desc.h!"
#endif
#if HW_DESC_PAGE == FLASH_CONFIG_PAGE
#error "HW_DESC_PAGE == FLASH_CONFIG_PAGE — adjust hw_desc.h!"
#endif

/* ── Globals ────────────────────────────────────────────────────────────────── */
static LogState_t  g_log_state    = {0};
static LogConfig_t g_log_cfg      = LOG_CFG_DEFAULT;
static uint8_t     g_log_cfg_dirty = 0U;  /* 1 = RAM differs from flash, needs CommitConfig */

/* ── BLE-session RAM queue ───────────────────────────────────────────────── */
#define BLE_LOG_QUEUE_MAX  64U
static FlashLogRecordV2_t s_ble_queue[BLE_LOG_QUEUE_MAX];
static uint8_t            s_ble_queue_len = 0U;

/* Header page layout (96 bytes total):
 *   offset  0: FlashLogHeader_t  (16 bytes)
 *   offset 16: FlashFieldDesc_t[0..3] (64 bytes)
 *   offset 80: LogConfig_t       (16 bytes)  */
#define HDR_CFG_OFFSET   80U
#define HDR_BLOCK_BYTES  96U

/* ── Static field descriptors ───────────────────────────────────────────────── */
static const FlashFieldDesc_t k_fields[4] = {
    { FIELD_ID_TEMP_INT,    FIELD_TYPE_INT16,  2, -1, "TEMP",    "C"   },
    { FIELD_ID_LIGHT,       FIELD_TYPE_UINT16, 2,  0, "LIGHT",   "raw" },
    { FIELD_ID_BATTERY_PCT, FIELD_TYPE_UINT8,  1,  0, "BAT_PCT", "%"   },
    { FIELD_ID_BATTERY_MV,  FIELD_TYPE_UINT16, 2,  0, "BAT_MV",  "mV"  },
};

/* ── Timestamp helpers ──────────────────────────────────────────────────────── */

static const uint8_t k_mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

static uint8_t _is_leap(uint16_t y)
{
    return (y % 4U == 0U && (y % 100U != 0U || y % 400U == 0U)) ? 1U : 0U;
}

/* Returns seconds since 2000-01-01 00:00:00 from RTC.
 * Covers 2000-2099 — max value ~3.15 × 10^9 < UINT32_MAX. */
static uint32_t _rtc_epoch2000(void)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    Power_RTC_GetDateTime(&t, &d);          /* d.Year = 0–99 (2000–2099) */

    uint32_t days = 0U;
    for (uint8_t y = 0U; y < d.Year; y++)
        days += _is_leap(2000U + y) ? 366U : 365U;

    for (uint8_t m = 1U; m < d.Month; m++) {
        days += k_mdays[m - 1U];
        if (m == 2U && _is_leap(2000U + d.Year)) days++;
    }
    days += (uint32_t)d.Date - 1U;

    return days * 86400UL
         + (uint32_t)t.Hours   * 3600UL
         + (uint32_t)t.Minutes * 60UL
         + (uint32_t)t.Seconds;
}

static uint32_t _get_timestamp(void)
{
    if (g_log_cfg.ts_source == LOG_TS_RTC && Power_RTC_IsSet())
        return _rtc_epoch2000();
    return HAL_GetTick() / 1000U;
}

/* ── Internal: write header page (erase + write 96 bytes) ──────────────────── */
static int _write_header_page(void)
{
    uint8_t buf[HDR_BLOCK_BYTES];
    memset(buf, 0, sizeof(buf));

    FlashLogHeader_t hdr = {
        .magic          = LOG_HEADER_MAGIC,
        .format_version = LOG_FORMAT_VERSION_V2,
        .entry_size     = LOG_ENTRY_SIZE,
        .field_count    = 4U,
        .active_mask    = g_log_cfg.active_mask,
        .overflow_mode  = g_log_cfg.overflow_mode,
        .write_mode     = g_log_cfg.write_mode,
        .checkpoint_n   = g_log_cfg.checkpoint_n,
        .reserved       = 0U,
        .total_written  = g_log_state.total_records,
    };

    memcpy(buf,                 &hdr,      sizeof(hdr));
    memcpy(buf + 16U,           k_fields,  sizeof(k_fields));
    memcpy(buf + HDR_CFG_OFFSET, &g_log_cfg, sizeof(g_log_cfg));

    if (!Flash_ErasePage(LOG_HEADER_PAGE)) return -1;
    if (!Flash_WriteBlock(LOG_HEADER_ADDR, buf, HDR_BLOCK_BYTES)) return -1;
    return 0;
}

/* ── Internal: write one 16-byte record at current write_addr ──────────────── */
static int _write_record_internal(const FlashLogRecord_t *rec)
{
    /* Page boundary: erase or wrap */
    if (((g_log_state.write_addr - LOG_DATA_START) % FLASH_PAGE_SIZE) == 0U) {

        if (g_log_state.write_addr >= LOG_DATA_END) {
            if (g_log_cfg.overflow_mode == LOG_OVERFLOW_STOP) {
                if (!g_log_state.is_full) {
                    g_log_state.is_full = 1U;
                    UART_Print("[LOG] FULL — logging stopped\r\n");
                }
                return -1;
            }
            g_log_state.write_addr = LOG_DATA_START;
        }

        uint32_t pg = (g_log_state.write_addr - FLASH_BASE_ADDR) / FLASH_PAGE_SIZE;
        if (*(volatile uint32_t *)g_log_state.write_addr != 0xFFFFFFFFUL) {
            UART_Printf("[LOG] wrap page %lu\r\n", pg);
            if (!Flash_ErasePage(pg)) return -1;
            /* After erasing old data, oldest valid data is now at the NEXT page.
             * Only update when old records were actually overwritten — not on a
             * fresh/cleared page (which is already 0xFF and needs no erase).  */
            if (g_log_cfg.overflow_mode == LOG_OVERFLOW_CIRCULAR) {
                uint32_t next_pg = g_log_state.write_addr + FLASH_PAGE_SIZE;
                if (next_pg >= LOG_DATA_END) next_pg = LOG_DATA_START;
                g_log_state.read_start_slot =
                    (next_pg - LOG_DATA_START) / LOG_ENTRY_SIZE;
            }
        }
    }

    if (!Flash_WriteBlock(g_log_state.write_addr, rec, LOG_ENTRY_SIZE)) return -1;

    g_log_state.write_addr += LOG_ENTRY_SIZE;
    g_log_state.total_records++;
    return 0;
}

/* Forward declarations — defined after FlashLog_Init in this file */
static int _write_record_v2_flash(const FlashLogRecordV2_t *rec);
static int _write_record_v2(const FlashLogRecordV2_t *rec);

/* ── Internal: write v2 typed records for each active scalar sensor ──────────
 * Writes one FlashLogRecordV2_t per sensor type (TEMP / BATT / LIGHT).
 * Sensors are pre-sampled by ChipTemp_Tick*() / BattAdc_Tick*() / LightAdc_Tick*()
 * in the main loop before FlashLog_Task() is called.                        */
static int _collect_and_write(uint8_t force_all)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t ts     = _get_timestamp();
    int      wrote  = 0;

    /* ── Temperature ─────────────────────────────────────────────────────── */
    if (g_log_cfg.active_mask & LOG_MASK_TEMP) {
        uint32_t iv_ms = (uint32_t)g_log_cfg.temp_interval_s * 1000U;
        if (force_all || (iv_ms &&
            (now_ms - g_log_state.last_temp_ts) >= iv_ms)) {

            if (g_last_temp_x10 != INT32_MIN) {
                int32_t cur = g_last_temp_x10;
                uint8_t write_it = 1U;
                if (!force_all && g_log_cfg.write_mode == LOG_WRITE_ON_CHANGE) {
                    int32_t d = cur - (int32_t)g_log_state.last_temp;
                    if (d < 0) d = -d;
                    write_it = (d >= (int32_t)g_log_cfg.temp_delta_01c);
                }
                if (write_it) {
                    FlashLogRecordV2_t rec;
                    memset(&rec, 0, sizeof(rec));
                    rec.timestamp = ts;
                    rec.type      = LOGREC_TYPE_TEMP;
                    rec.flags     = REC_FLAG_FULL;
                    int16_t  *p16 = (int16_t  *)rec.payload;
                    uint16_t *u16 = (uint16_t *)rec.payload;
                    p16[0] = (int16_t)cur;          /* temp_01c */
                    u16[1] = (uint16_t)g_last_vdda_mV; /* vdda_mv  */
                    if (_write_record_v2(&rec) == 0) {
                        g_log_state.last_temp    = (int16_t)cur;
                        g_log_state.last_temp_ts = now_ms;
                        wrote++;
                    }
                }
            }
        }
    }

    /* ── Battery ─────────────────────────────────────────────────────────── */
    if (g_log_cfg.active_mask & (LOG_MASK_BATTERY_PCT | LOG_MASK_BATTERY_MV)) {
        uint32_t iv_ms = (uint32_t)g_log_cfg.battery_interval_s * 1000U;
        if (force_all || (iv_ms &&
            (now_ms - g_log_state.last_bat_ts) >= iv_ms)) {

            if (g_last_batt_pct >= 0) {
                uint16_t cur_mv  = (uint16_t)g_last_batt_mV;
                uint8_t  cur_pct = (uint8_t)g_last_batt_pct;
                uint8_t  write_it = 1U;
                if (!force_all && g_log_cfg.write_mode == LOG_WRITE_ON_CHANGE) {
                    int32_t d = (int32_t)cur_pct - (int32_t)g_log_state.last_bat_pct;
                    if (d < 0) d = -d;
                    write_it = ((uint8_t)d >= g_log_cfg.battery_delta_pct);
                }
                if (write_it) {
                    FlashLogRecordV2_t rec;
                    memset(&rec, 0, sizeof(rec));
                    rec.timestamp = ts;
                    rec.type      = LOGREC_TYPE_BATT;
                    rec.flags     = REC_FLAG_FULL;
                    uint16_t *u16 = (uint16_t *)rec.payload;
                    u16[0]         = cur_mv;   /* bat_mv  */
                    rec.payload[2] = cur_pct;  /* bat_pct */
                    if (_write_record_v2(&rec) == 0) {
                        g_log_state.last_bat_pct = cur_pct;
                        g_log_state.last_bat_mv  = cur_mv;
                        g_log_state.last_bat_ts  = now_ms;
                        wrote++;
                    }
                }
            }
        }
    }

    /* ── Light ───────────────────────────────────────────────────────────── */
    if (g_log_cfg.active_mask & LOG_MASK_LIGHT) {
        uint32_t iv_ms = (uint32_t)g_log_cfg.light_interval_s * 1000U;
        if (force_all || (iv_ms &&
            (now_ms - g_log_state.last_light_ts) >= iv_ms)) {

            if (g_light_ever_read) {
                uint16_t cur = g_last_light_raw;
                uint8_t  write_it = 1U;
                if (!force_all && g_log_cfg.write_mode == LOG_WRITE_ON_CHANGE) {
                    uint32_t ref = g_log_state.last_light ? g_log_state.last_light : 1U;
                    int32_t d = (int32_t)cur - (int32_t)g_log_state.last_light;
                    if (d < 0) d = -d;
                    write_it = ((uint32_t)d * 100U / ref >= g_log_cfg.light_delta_pct);
                }
                if (write_it) {
                    FlashLogRecordV2_t rec;
                    memset(&rec, 0, sizeof(rec));
                    rec.timestamp = ts;
                    rec.type      = LOGREC_TYPE_LIGHT;
                    rec.flags     = REC_FLAG_FULL;
                    uint16_t *u16 = (uint16_t *)rec.payload;
                    u16[0] = cur;  /* light_raw */
                    if (_write_record_v2(&rec) == 0) {
                        g_log_state.last_light    = cur;
                        g_log_state.last_light_ts = now_ms;
                        wrote++;
                    }
                }
            }
        }
    }

    return (wrote > 0) ? 0 : 1;  /* 0=wrote something, 1=nothing due */
}

/* ── Public API ─────────────────────────────────────────────────────────────── */

int FlashLog_Init(LogConfig_t *cfg)
{
    g_log_state = (LogState_t){0};

    /* Use caller's defaults only if header is invalid */
    if (cfg) g_log_cfg = *cfg;

    const FlashLogHeader_t *hdr = (const FlashLogHeader_t *)LOG_HEADER_ADDR;

    if (hdr->magic == LOG_HEADER_MAGIC && hdr->format_version == LOG_FORMAT_VERSION_V2) {
        /* Restore config from header page */
        const LogConfig_t *saved = (const LogConfig_t *)(LOG_HEADER_ADDR + HDR_CFG_OFFSET);
        g_log_cfg = *saved;

        /* Scan for write position — forward until first EMPTY */
        uint32_t addr  = LOG_DATA_START;
        uint32_t count = 0U;
        while (addr < LOG_DATA_END) {
            const FlashLogRecord_t *r = (const FlashLogRecord_t *)addr;
            if (r->rec_flags == REC_FLAG_EMPTY) break;
            count++;
            addr += LOG_ENTRY_SIZE;
        }
        g_log_state.total_records = count;

        if (addr >= LOG_DATA_END) {
            if (g_log_cfg.overflow_mode == LOG_OVERFLOW_STOP) {
                g_log_state.is_full    = 1U;
                g_log_state.write_addr = LOG_DATA_END;
            } else {
                g_log_state.write_addr = LOG_DATA_START;
            }
        } else {
            g_log_state.write_addr = addr;
        }

        /* In circular mode the linear scan stops at the first EMPTY slot,
         * which may be in the middle of the ring because the write page was
         * erased on the last wrap.  Old valid records survive on pages AFTER
         * the write page and must be included in the total and in reads.
         *
         * Condition: count < LOG_ENTRIES_MAX means we found an EMPTY before
         * reaching the end → a wrap has happened (or flash is empty).       */
        if (g_log_cfg.overflow_mode == LOG_OVERFLOW_CIRCULAR
            && count < LOG_ENTRIES_MAX) {

            uint32_t wa      = g_log_state.write_addr;
            uint32_t pg_off  = (wa - LOG_DATA_START) % FLASH_PAGE_SIZE;
            uint32_t pg_base = wa - pg_off;           /* start of write page  */
            uint32_t next    = pg_base + FLASH_PAGE_SIZE; /* first of next page */

            if (next < LOG_DATA_END) {
                const FlashLogRecord_t *rn = (const FlashLogRecord_t *)next;
                if (rn->rec_flags != REC_FLAG_EMPTY) {
                    /* Old records exist on pages after the write page.
                     * Count them; they occupy slots [old_slot, LOG_ENTRIES_MAX). */
                    uint32_t old_slot  = (next - LOG_DATA_START) / LOG_ENTRY_SIZE;
                    uint32_t old_count = 0U;
                    uint32_t oa        = next;
                    while (oa < LOG_DATA_END) {
                        const FlashLogRecord_t *ro = (const FlashLogRecord_t *)oa;
                        if (ro->rec_flags == REC_FLAG_EMPTY) break;
                        old_count++;
                        oa += LOG_ENTRY_SIZE;
                    }
                    if (old_count > 0U) {
                        g_log_state.read_start_slot = old_slot;
                        g_log_state.total_records   = old_count + count;
                        UART_Printf("[LOG] wrap: old_slot=%lu old=%lu new=%lu total=%lu\r\n",
                                    old_slot, old_count, count,
                                    g_log_state.total_records);
                    }
                }
            }
        }

        UART_Printf("[LOG] Init OK: %lu records write@0x%08lX rss=%lu\r\n",
                    g_log_state.total_records, g_log_state.write_addr,
                    g_log_state.read_start_slot);
    } else {
        /* First boot or cleared: write default header */
        g_log_state.write_addr    = LOG_DATA_START;
        g_log_state.total_records = 0U;
        if (_write_header_page() < 0) {
            UART_Print("[LOG] Init FAILED: header write error\r\n");
            return -1;
        }
        UART_Print("[LOG] Init: new header written\r\n");
    }

    g_log_state.initialized = 1U;

    uint32_t used  = g_log_state.total_records < LOG_ENTRIES_MAX ?
                     g_log_state.total_records : LOG_ENTRIES_MAX;
    uint32_t free_ = LOG_ENTRIES_MAX - used;
    UART_Printf("[LOG] hdr=pg%u data=pg%u-%u  %lu/%u entries\r\n",
                LOG_HEADER_PAGE, LOG_DATA_START_PAGE, LOG_DATA_END_PAGE,
                used, LOG_ENTRIES_MAX);
    UART_Printf("[LOG] free=%lu (%lu bytes)\r\n",
                free_, free_ * LOG_ENTRY_SIZE);
    return 0;
}

void FlashLog_Task(void)
{
    if (!g_log_state.initialized) return;
    if (g_log_state.is_full && g_log_cfg.overflow_mode == LOG_OVERFLOW_STOP) return;
    if (g_log_cfg.active_mask == 0U) return;
    _collect_and_write(0U);

    /* Periodic accel logging — separate from the scalar record path */
    if ((g_log_cfg.active_mask & LOG_MASK_ACCEL) &&
        g_log_cfg.accel_interval_s > 0U &&
        LIS2DW12_IsPowered()) {
        uint32_t now_ms = HAL_GetTick();
        uint32_t iv_ms  = (uint32_t)g_log_cfg.accel_interval_s * 1000U;
        if ((now_ms - g_log_state.last_accel_ts) >= iv_ms) {
            int16_t x, y, z;
            if (LIS2DW12_ReadXYZ(&x, &y, &z) == HAL_OK) {
                FlashLog_WriteAccelV2(x, y, z, LIS2DW12_GetFS());
            }
        }
    }
}

int FlashLog_WriteNow(void)
{
    if (!g_log_state.initialized) return -1;
    return _collect_and_write(1U);
}

int FlashLog_ReadRecord(uint32_t idx, FlashLogRecord_t *out)
{
    if (!out || idx >= LOG_ENTRIES_MAX) return -1;

    /* In circular mode after a wrap, logical idx 0 = oldest record whose
     * physical slot is read_start_slot.  Map via modulo so the read order
     * spans old tail → new head across the ring boundary.               */
    uint32_t phys;
    if (g_log_cfg.overflow_mode == LOG_OVERFLOW_CIRCULAR
        && g_log_state.read_start_slot > 0U) {
        phys = (g_log_state.read_start_slot + idx) % LOG_ENTRIES_MAX;
    } else {
        phys = idx;
    }

    uint32_t addr = LOG_DATA_START + phys * LOG_ENTRY_SIZE;
    if (addr + LOG_ENTRY_SIZE > LOG_DATA_END) return -1;
    const FlashLogRecord_t *p = (const FlashLogRecord_t *)addr;
    if (p->rec_flags == REC_FLAG_EMPTY) return -1;
    memcpy(out, p, sizeof(*out));
    return 0;
}

int FlashLog_DecodeAbsolute(uint32_t idx, FlashLogRecord_t *out_abs)
{
    if (!out_abs) return -1;

    /* Walk backward from idx to find checkpoint or FULL record */
    int32_t ck = -1;
    for (int32_t i = (int32_t)idx; i >= 0; i--) {
        FlashLogRecord_t r;
        if (FlashLog_ReadRecord((uint32_t)i, &r) < 0) break;
        if (r.rec_flags == REC_FLAG_FULL || r.rec_flags == REC_FLAG_CHECKPOINT) {
            ck = i;
            break;
        }
    }
    if (ck < 0) return -1;

    FlashLogRecord_t abs_rec;
    FlashLog_ReadRecord((uint32_t)ck, &abs_rec);
    int16_t  t   = abs_rec.temp_01c;
    uint16_t li  = abs_rec.light_raw;
    uint8_t  bp  = abs_rec.battery_pct;
    uint16_t bmv = REC_BAT_MV(abs_rec);

    for (uint32_t i = (uint32_t)ck + 1U; i <= idx; i++) {
        FlashLogRecord_t d;
        if (FlashLog_ReadRecord(i, &d) < 0) break;
        if (d.rec_flags != REC_FLAG_DELTA) {
            t   = d.temp_01c;
            li  = d.light_raw;
            bp  = d.battery_pct;
            bmv = REC_BAT_MV(d);
            continue;
        }
        if (d.mask & LOG_MASK_TEMP)       t   = (int16_t)(t + d.temp_01c);
        if (d.mask & LOG_MASK_LIGHT)      li  = (uint16_t)((int32_t)li + (int16_t)d.light_raw);
        if (d.mask & LOG_MASK_BATTERY_PCT) {
            bp  = (uint8_t)((int32_t)bp + (int8_t)d.battery_pct);
            int16_t mv_d = (int16_t)(((uint16_t)d.bat_mv_hi << 8) | d.bat_mv_lo);
            bmv = (uint16_t)((int32_t)bmv + mv_d);
        }
    }

    FlashLog_ReadRecord(idx, out_abs);
    out_abs->temp_01c    = t;
    out_abs->light_raw   = li;
    out_abs->battery_pct = bp;
    out_abs->bat_mv_hi   = (uint8_t)(bmv >> 8);
    out_abs->bat_mv_lo   = (uint8_t)(bmv & 0xFFU);
    return 0;
}

void FlashLog_GetStatus(uint32_t *total, uint32_t *free_count,
                        uint32_t *used_bytes, uint32_t *free_bytes)
{
    uint32_t used = g_log_state.total_records < LOG_ENTRIES_MAX ?
                    g_log_state.total_records : LOG_ENTRIES_MAX;
    uint32_t fr   = LOG_ENTRIES_MAX - used;
    if (total)      *total      = g_log_state.total_records;
    if (free_count) *free_count = fr;
    if (used_bytes) *used_bytes = used * LOG_ENTRY_SIZE;
    if (free_bytes) *free_bytes = fr   * LOG_ENTRY_SIZE;
}

int FlashLog_ReadHeader(FlashLogHeader_t *out)
{
    if (!out) return -1;
    memcpy(out, (const void *)LOG_HEADER_ADDR, sizeof(*out));
    return (out->magic == LOG_HEADER_MAGIC) ? 0 : -1;
}

int FlashLog_ReadFieldDesc(uint8_t idx, FlashFieldDesc_t *out)
{
    if (!out || idx >= 4U) return -1;
    uint32_t addr = LOG_HEADER_ADDR + 16U + (uint32_t)idx * sizeof(FlashFieldDesc_t);
    memcpy(out, (const void *)addr, sizeof(*out));
    return 0;
}

int FlashLog_Clear(void)
{
    UART_Print("[LOG] clearing data pages...\r\n");
    for (uint32_t pg = LOG_DATA_START_PAGE; pg <= LOG_DATA_END_PAGE; pg++) {
        if (!Flash_ErasePage(pg)) {
            UART_Printf("[LOG] clear: erase page %lu FAILED\r\n", pg);
            return -1;
        }
    }
    g_log_state.total_records            = 0U;
    g_log_state.write_addr               = LOG_DATA_START;
    g_log_state.read_start_slot          = 0U;
    g_log_state.is_full                  = 0U;
    g_log_state.records_since_checkpoint = 0U;
    _write_header_page();
    UART_Print("[LOG] cleared OK\r\n");
    return 0;
}

int FlashLog_SetConfig(LogConfig_t *cfg)
{
    if (!cfg) return -1;
    g_log_cfg      = *cfg;
    g_log_cfg_dirty = 1U;   /* mark as needing commit — no flash write here */
    return 0;
}

int FlashLog_CommitConfig(void)
{
    if (!g_log_cfg_dirty) return 0;
    int r = _write_header_page();
    if (r == 0) {
        g_log_cfg_dirty = 0U;
        UART_Print("[LOG] config committed to flash\r\n");
    }
    return r;
}

void FlashLog_GetConfig(LogConfig_t *out)
{
    if (out) *out = g_log_cfg;
}

/* ── v2 typed-record write path ─────────────────────────────────────────────
 * _write_record_v2_flash: direct flash write — must NOT be called with BLE connected.
 * _write_record_v2:        BLE-aware router — queues to RAM during BLE session.  */
static int _write_record_v2_flash(const FlashLogRecordV2_t *rec)
{
    /* Page boundary: erase or wrap */
    if (((g_log_state.write_addr - LOG_DATA_START) % FLASH_PAGE_SIZE) == 0U) {
        if (g_log_state.write_addr >= LOG_DATA_END) {
            if (g_log_cfg.overflow_mode == LOG_OVERFLOW_STOP) {
                if (!g_log_state.is_full) {
                    g_log_state.is_full = 1U;
                    UART_Print("[LOG] FULL — logging stopped\r\n");
                }
                return -1;
            }
            g_log_state.write_addr = LOG_DATA_START;
        }
        uint32_t pg = (g_log_state.write_addr - FLASH_BASE_ADDR) / FLASH_PAGE_SIZE;
        if (*(volatile uint32_t *)g_log_state.write_addr != 0xFFFFFFFFUL) {
            if (!Flash_ErasePage(pg)) return -1;
            if (g_log_cfg.overflow_mode == LOG_OVERFLOW_CIRCULAR) {
                uint32_t next_pg = g_log_state.write_addr + FLASH_PAGE_SIZE;
                if (next_pg >= LOG_DATA_END) next_pg = LOG_DATA_START;
                g_log_state.read_start_slot =
                    (next_pg - LOG_DATA_START) / LOG_ENTRY_SIZE;
            }
        }
    }

    if (!Flash_WriteBlock(g_log_state.write_addr, rec, LOG_ENTRY_SIZE)) return -1;
    g_log_state.write_addr += LOG_ENTRY_SIZE;
    g_log_state.total_records++;
    return 0;
}

static int _write_record_v2(const FlashLogRecordV2_t *rec)
{
    if (BLE_IsConnected()) {
        /* BLE active: queue to RAM — flushed after disconnect by FlashLog_FlushBleQueue() */
        if (s_ble_queue_len < BLE_LOG_QUEUE_MAX) {
            s_ble_queue[s_ble_queue_len++] = *rec;
            return 0;
        }
        return -1;  /* queue full — skip entry */
    }
    return _write_record_v2_flash(rec);
}

void FlashLog_FlushBleQueue(void)
{
    if (s_ble_queue_len == 0U) return;
    UART_Printf("[LOG] flush %u BLE-queued entries to flash\r\n", (unsigned)s_ble_queue_len);
    uint8_t len = s_ble_queue_len;
    s_ble_queue_len = 0U;
    for (uint8_t i = 0U; i < len; i++) {
        _write_record_v2_flash(&s_ble_queue[i]);
    }
}

int FlashLog_WriteMark(uint8_t tag)
{
    if (!g_log_state.initialized) return -1;
    FlashLogRecordV2_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp  = _get_timestamp();
    rec.type       = LOGREC_TYPE_MARKER;
    rec.flags      = REC_FLAG_FULL;
    rec.payload[0] = tag;
    return _write_record_v2(&rec);
}

int FlashLog_WriteAccelV2(int16_t x, int16_t y, int16_t z, uint8_t fs)
{
    if (!g_log_state.initialized) return -1;
    FlashLogRecordV2_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp = _get_timestamp();
    rec.type      = LOGREC_TYPE_ACCEL;
    rec.flags     = REC_FLAG_FULL;
    AccelPayload_t *p = (AccelPayload_t *)rec.payload;
    p->x  = x;
    p->y  = y;
    p->z  = z;
    p->fs = fs;
    g_log_state.last_accel_ts = HAL_GetTick();
    return _write_record_v2(&rec);
}

/* ── Print helpers ──────────────────────────────────────────────────────────── */

static const char *_flag_name(uint8_t f) {
    if (f == REC_FLAG_CHECKPOINT) return "CKP";
    if (f == REC_FLAG_DELTA)      return "DELTA";
    if (f == REC_FLAG_EMPTY)      return "EMPTY";
    return "FULL";
}

static const char *_mode_str(uint8_t m) {
    if (m == LOG_WRITE_ON_CHANGE) return "ON_CHANGE";
    if (m == LOG_WRITE_ADAPTIVE)  return "ADAPTIVE";
    return "ALWAYS";
}

void FlashLog_PrintStatus(void)
{
    uint32_t used = g_log_state.total_records < LOG_ENTRIES_MAX ?
                    g_log_state.total_records : LOG_ENTRIES_MAX;
    uint32_t fr   = LOG_ENTRIES_MAX - used;
    uint32_t used_pct = (used * 100U) / LOG_ENTRIES_MAX;

    UART_Print("[LOG] ================================\r\n");
    UART_Print("[LOG] Flash Log Status\r\n");
    UART_Print("[LOG] ================================\r\n");
    UART_Printf("[LOG] Header: pg%u @ 0x%08lX\r\n",
                LOG_HEADER_PAGE, (uint32_t)LOG_HEADER_ADDR);
    UART_Printf("[LOG] Data:   pg%u-%u  (%u pages, %uKB)\r\n",
                LOG_DATA_START_PAGE, LOG_DATA_END_PAGE,
                LOG_DATA_PAGES, (unsigned)(LOG_DATA_BYTES / 1024U));
    UART_Printf("[LOG] Total:  %u entries max\r\n", LOG_ENTRIES_MAX);
    UART_Printf("[LOG] Used:   %lu entries (%lu%%)\r\n", used, used_pct);
    UART_Printf("[LOG] Free:   %lu entries (%lu bytes)\r\n", fr, fr * LOG_ENTRY_SIZE);
    if (g_log_state.is_full) UART_Print("[LOG] Status: FULL\r\n");
    UART_Print("[LOG] --------------------------------\r\n");
    UART_Printf("[LOG] Write:  %s\r\n", _mode_str(g_log_cfg.write_mode));
    UART_Printf("[LOG] Oflow:  %s\r\n",
                g_log_cfg.overflow_mode == LOG_OVERFLOW_CIRCULAR ? "CIRCULAR" : "STOP");
    UART_Printf("[LOG] CKP/N:  %u\r\n", g_log_cfg.checkpoint_n);
    UART_Printf("[LOG] Mask:   0x%02X\r\n", g_log_cfg.active_mask);
    UART_Printf("[LOG] TS:     %s\r\n",
                g_log_cfg.ts_source == LOG_TS_RTC ? "rtc (epoch2000)" : "boot (s since reset)");
    UART_Print("[LOG] --------------------------------\r\n");
    UART_Print("[LOG] Intervals:\r\n");
    if (g_log_cfg.active_mask & LOG_MASK_TEMP)
        UART_Printf("[LOG]  TEMP    %us  (delta %u.%01uC)\r\n",
                    g_log_cfg.temp_interval_s,
                    g_log_cfg.temp_delta_01c / 10U,
                    g_log_cfg.temp_delta_01c % 10U);
    if (g_log_cfg.active_mask & LOG_MASK_LIGHT)
        UART_Printf("[LOG]  LIGHT   %us  (delta %u%%)\r\n",
                    g_log_cfg.light_interval_s, g_log_cfg.light_delta_pct);
    if (g_log_cfg.active_mask & (LOG_MASK_BATTERY_PCT | LOG_MASK_BATTERY_MV))
        UART_Printf("[LOG]  BAT     %us  (delta %u%%)\r\n",
                    g_log_cfg.battery_interval_s, g_log_cfg.battery_delta_pct);
    UART_Print("[LOG] ================================\r\n");
}

void FlashLog_PrintCalc(void)
{
    uint32_t fr = LOG_ENTRIES_MAX -
                  (g_log_state.total_records < LOG_ENTRIES_MAX ?
                   g_log_state.total_records : LOG_ENTRIES_MAX);

    UART_Print("[LOG] ================================\r\n");
    UART_Print("[LOG] Capacity Calculator\r\n");
    UART_Print("[LOG] ================================\r\n");
    UART_Printf("[LOG] Free entries: %lu\r\n", fr);
    UART_Print("[LOG] Writes/hour:\r\n");

    uint32_t total_per_hr = 0U;
    if ((g_log_cfg.active_mask & LOG_MASK_TEMP) && g_log_cfg.temp_interval_s) {
        uint32_t n = 3600U / g_log_cfg.temp_interval_s;
        UART_Printf("[LOG]  TEMP  (%us):  %lu/hr\r\n", g_log_cfg.temp_interval_s, n);
        total_per_hr += n;
    }
    if ((g_log_cfg.active_mask & LOG_MASK_LIGHT) && g_log_cfg.light_interval_s) {
        uint32_t n = 3600U / g_log_cfg.light_interval_s;
        UART_Printf("[LOG]  LIGHT (%us):  %lu/hr\r\n", g_log_cfg.light_interval_s, n);
        total_per_hr += n;
    }
    if ((g_log_cfg.active_mask & (LOG_MASK_BATTERY_PCT | LOG_MASK_BATTERY_MV)) &&
        g_log_cfg.battery_interval_s) {
        uint32_t n = 3600U / g_log_cfg.battery_interval_s;
        UART_Printf("[LOG]  BAT   (%us):  %lu/hr\r\n", g_log_cfg.battery_interval_s, n);
        total_per_hr += n;
    }
    UART_Printf("[LOG]  Total:         %lu/hr\r\n", total_per_hr);
    UART_Print("[LOG] --------------------------------\r\n");
    if (total_per_hr > 0U) {
        uint32_t hrs = fr / total_per_hr;
        UART_Printf("[LOG] Always mode:  ~%lu hours\r\n", hrs);
        UART_Printf("[LOG] Adaptive est: ~%lu hours (3x)\r\n", hrs * 3U);
    }
    UART_Printf("[LOG] Overflow: %s\r\n",
                g_log_cfg.overflow_mode == LOG_OVERFLOW_CIRCULAR ? "CIRCULAR" : "STOP");
    UART_Print("[LOG] ================================\r\n");
}

void FlashLog_PrintRecord(uint32_t idx)
{
    FlashLogRecord_t rec, abs_rec;
    if (FlashLog_ReadRecord(idx, &rec) < 0) {
        UART_Printf("[LOG] record %lu: not found\r\n", idx);
        return;
    }

    uint32_t addr = LOG_DATA_START + idx * LOG_ENTRY_SIZE;
    UART_Printf("[LOG] Record [%lu] @ 0x%08lX\r\n", idx, addr);
    UART_Printf("[LOG]  ts=%lu s  flags=%s  mask=0x%02X\r\n",
                rec.timestamp, _flag_name(rec.rec_flags), rec.mask);

    /* For DELTA records, decode to absolute */
    int is_delta = (rec.rec_flags == REC_FLAG_DELTA);
    if (is_delta) {
        if (FlashLog_DecodeAbsolute(idx, &abs_rec) == 0) {
            UART_Print("[LOG]  (decoded from delta)\r\n");
        } else {
            memcpy(&abs_rec, &rec, sizeof(rec));
            is_delta = 0;
        }
    } else {
        memcpy(&abs_rec, &rec, sizeof(rec));
    }

    if (abs_rec.mask & LOG_MASK_TEMP) {
        int32_t tw = abs_rec.temp_01c / 10;
        int32_t tf = abs_rec.temp_01c >= 0 ? abs_rec.temp_01c % 10 : -(abs_rec.temp_01c % 10);
        UART_Printf("[LOG]  TEMP:    %ld.%01ldC\r\n", (long)tw, (long)tf);
    }
    if (abs_rec.mask & LOG_MASK_LIGHT)
        UART_Printf("[LOG]  LIGHT:   %u raw\r\n", abs_rec.light_raw);
    if (abs_rec.mask & LOG_MASK_BATTERY_PCT)
        UART_Printf("[LOG]  BAT_PCT: %u%%\r\n", abs_rec.battery_pct);
    if (abs_rec.mask & LOG_MASK_BATTERY_MV)
        UART_Printf("[LOG]  BAT_MV:  %umV\r\n", (unsigned)REC_BAT_MV(abs_rec));
}

void FlashLog_DumpCSV(uint32_t start, uint32_t count)
{
    uint32_t end = (count == 0U) ? g_log_state.total_records : start + count;
    if (end > g_log_state.total_records) end = g_log_state.total_records;
    if (end > LOG_ENTRIES_MAX) end = LOG_ENTRIES_MAX;

    UART_Print("idx,ts,flags,temp_c,light_raw,bat_pct,bat_mv\r\n");

    for (uint32_t i = start; i < end; i++) {
        FlashLogRecord_t rec;
        if (FlashLog_ReadRecord(i, &rec) < 0) break;

        FlashLogRecord_t abs_rec;
        if (rec.rec_flags == REC_FLAG_DELTA) {
            if (FlashLog_DecodeAbsolute(i, &abs_rec) < 0)
                memcpy(&abs_rec, &rec, sizeof(rec));
        } else {
            memcpy(&abs_rec, &rec, sizeof(rec));
        }

        /* Print index, ts, flags */
        UART_Printf("%lu,%lu,%s,", i, rec.timestamp, _flag_name(rec.rec_flags));

        /* temp_c */
        if (abs_rec.mask & LOG_MASK_TEMP) {
            int32_t tw = abs_rec.temp_01c / 10;
            int32_t tf = abs_rec.temp_01c >= 0 ?
                         abs_rec.temp_01c % 10 : -(abs_rec.temp_01c % 10);
            UART_Printf("%ld.%01ld,", (long)tw, (long)tf);
        } else UART_Print(",");

        /* light_raw */
        if (abs_rec.mask & LOG_MASK_LIGHT)
            UART_Printf("%u,", abs_rec.light_raw);
        else UART_Print(",");

        /* bat_pct */
        if (abs_rec.mask & LOG_MASK_BATTERY_PCT)
            UART_Printf("%u,", abs_rec.battery_pct);
        else UART_Print(",");

        /* bat_mv */
        if (abs_rec.mask & LOG_MASK_BATTERY_MV)
            UART_Printf("%u\r\n", (unsigned)REC_BAT_MV(abs_rec));
        else UART_Print("\r\n");
    }
}
