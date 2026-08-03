#pragma once
#include <stdint.h>

/* ── Page map (WB1M: 2KB pages) ─────────────────────────────────────────── */
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE           2048U
#endif
#define FLASH_BASE_ADDR           0x08000000UL

/* OTA_LAYOUT (production, BLE light stack reflashed):
 *   Config = page 90, Log = pages 91-98, BLE light @ page 99+
 * Default (development, BLE full stack):
 *   Config = page 60, Log = pages 61-67, BLE full @ page 68+
 */
#ifdef OTA_LAYOUT
  #define BLE_STACK_FIRST_PAGE    99U   /* update after verifying SFSA option byte */
  #define LOG_HEADER_PAGE         91U
  #define LOG_DATA_START_PAGE     92U
  #define LOG_DATA_END_PAGE       97U   /* 6 pages = 768 entries (same as original) */
#else
  #define BLE_STACK_FIRST_PAGE    68U
  #define LOG_HEADER_PAGE         61U
  #define LOG_DATA_START_PAGE     62U
  #define LOG_DATA_END_PAGE       67U   /* 6 pages = 768 entries                    */
#endif

#define LOG_HEADER_ADDR  (FLASH_BASE_ADDR + (uint32_t)LOG_HEADER_PAGE * FLASH_PAGE_SIZE)
#define LOG_DATA_START   (FLASH_BASE_ADDR + (uint32_t)LOG_DATA_START_PAGE * FLASH_PAGE_SIZE)
#define LOG_DATA_END     (FLASH_BASE_ADDR + ((uint32_t)LOG_DATA_END_PAGE + 1U) * FLASH_PAGE_SIZE)
#define LOG_DATA_PAGES   (LOG_DATA_END_PAGE - LOG_DATA_START_PAGE + 1U)
#define LOG_DATA_BYTES   ((uint32_t)LOG_DATA_PAGES * FLASH_PAGE_SIZE)
#define LOG_ENTRY_SIZE   16U
#define LOG_ENTRIES_MAX  (LOG_DATA_BYTES / LOG_ENTRY_SIZE)
#define LOG_ENTRIES_PER_PAGE (FLASH_PAGE_SIZE / LOG_ENTRY_SIZE)

#if (LOG_DATA_END_PAGE >= BLE_STACK_FIRST_PAGE)
#error "LOG_DATA_END_PAGE overlaps BLE stack!"
#endif
#if (LOG_HEADER_PAGE >= BLE_STACK_FIRST_PAGE)
#error "LOG_HEADER_PAGE overlaps BLE stack!"
#endif

/* ── Magic ────────────────────────────────────────────────────────────────── */
#define LOG_HEADER_MAGIC      0xBEAC0601UL
#define LOG_FORMAT_VERSION    1U   /* v1 — combined-sensor record (legacy)     */
#define LOG_FORMAT_VERSION_V2 2U   /* v2 — typed single-sensor record          */

/* ── Field IDs — FIXED FOREVER ────────────────────────────────────────────── */
#define FIELD_ID_TEMP_INT       0x10U
#define FIELD_ID_TEMP_EXT       0x11U
#define FIELD_ID_LIGHT          0x20U
#define FIELD_ID_BATTERY_PCT    0x30U
#define FIELD_ID_BATTERY_MV     0x31U
#define FIELD_ID_ACCEL_X        0x40U
#define FIELD_ID_ACCEL_Y        0x41U
#define FIELD_ID_ACCEL_Z        0x42U

#define FIELD_TYPE_INT8         0x01U
#define FIELD_TYPE_UINT8        0x02U
#define FIELD_TYPE_INT16        0x03U
#define FIELD_TYPE_UINT16       0x04U

/* ── Mask bits in LogRecord.mask ──────────────────────────────────────────── */
#define LOG_MASK_TEMP           (1U << 0)
#define LOG_MASK_LIGHT          (1U << 1)
#define LOG_MASK_BATTERY_PCT    (1U << 2)
#define LOG_MASK_BATTERY_MV     (1U << 3)
#define LOG_MASK_ACCEL          (1U << 4)

/* ── Record flags ─────────────────────────────────────────────────────────── */
#define REC_FLAG_FULL           0x00U
#define REC_FLAG_DELTA          0x01U
#define REC_FLAG_CHECKPOINT     0x02U
#define REC_FLAG_EMPTY          0xFFU

/* ── Overflow / write modes ───────────────────────────────────────────────── */
#define LOG_OVERFLOW_STOP       0U
#define LOG_OVERFLOW_CIRCULAR   1U

#define LOG_WRITE_ALWAYS        0U
#define LOG_WRITE_ON_CHANGE     1U
#define LOG_WRITE_ADAPTIVE      2U

/* ── Timestamp source ─────────────────────────────────────────────────────── */
#define LOG_TS_BOOT             0U   /* HAL_GetTick()/1000 — seconds since boot  */
#define LOG_TS_RTC              1U   /* seconds since 2000-01-01 00:00:00 (RTC)  */

/* ── Structures ───────────────────────────────────────────────────────────── */

/* Flash Header — 16 bytes at LOG_HEADER_ADDR */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  format_version;
    uint8_t  entry_size;
    uint8_t  field_count;
    uint8_t  active_mask;
    uint8_t  overflow_mode;
    uint8_t  write_mode;
    uint8_t  checkpoint_n;
    uint8_t  reserved;
    uint32_t total_written;
} FlashLogHeader_t;

/* Field Descriptor — 16 bytes, follows header */
typedef struct __attribute__((packed)) {
    uint8_t field_id;
    uint8_t data_type;
    uint8_t size_bytes;
    int8_t  scale_exp;
    char    name[8];
    char    unit[4];
} FlashFieldDesc_t;

/* ── v1 record (legacy — read-only after format migration) ───────────────── */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;     /* seconds since boot                */
    uint8_t  mask;          /* LOG_MASK_* present fields (bitmask) */
    uint8_t  rec_flags;     /* REC_FLAG_*                        */
    int16_t  temp_01c;
    uint16_t light_raw;
    uint8_t  battery_pct;
    uint8_t  bat_mv_hi;
    uint8_t  bat_mv_lo;
    uint8_t  pad[3];
} FlashLogRecord_t;        /* keep name for existing callers */
#define REC_BAT_MV(r) ((uint16_t)((uint16_t)(r).bat_mv_hi << 8 | (r).bat_mv_lo))

/* ── v2 typed record — one sensor per 16-byte entry ─────────────────────── */
/*
 * Migration path from v1:
 *   1. Upload all v1 log data via GUI before upgrading firmware.
 *   2. Issue LOG_ERASE after flashing new firmware.
 *      The new header is written with format_version=2.
 *   3. All subsequent records use the v2 typed layout.
 *   Old v1 records are still readable via FlashLog_ReadRecord_V1() for
 *   diagnostic purposes until the log is erased.
 *
 * Capacity note: v1 wrote ≤1 record per logging cycle (all sensors combined).
 * v2 writes one record per sensor, so a cycle with 3 active sensors uses
 * 3 × 16 = 48 bytes instead of 16 bytes.  Capacity drops from ~768 combined
 * readings to ~256 per-sensor readings (same 12 KB data region, same page map).
 */
#define LOGREC_TYPE_TEMP    0x00U   /* payload: temp_01c(i16) vdda_mv(u16) [6 rsv] */
#define LOGREC_TYPE_BATT    0x01U   /* payload: bat_mv(u16) bat_pct(u8)    [7 rsv] */
#define LOGREC_TYPE_LIGHT   0x02U   /* payload: light_raw(u16)             [8 rsv] */
#define LOGREC_TYPE_ACCEL   0x03U   /* payload: x(i16) y(i16) z(i16) fs(u8) [3 rsv] */
#define LOGREC_TYPE_MARKER  0xFEU   /* payload: tag(u8)                    [9 rsv] */
#define LOGREC_TYPE_EMPTY   0xFFU   /* erased / unused slot                        */

typedef struct __attribute__((packed)) {
    uint32_t timestamp;     /* seconds (boot-relative or RTC epoch-2000)       */
    uint8_t  type;          /* LOGREC_TYPE_*                                   */
    uint8_t  flags;         /* REC_FLAG_*                                      */
    uint8_t  payload[10];   /* type-specific, see LOGREC_TYPE_* above          */
} FlashLogRecordV2_t;
_Static_assert(sizeof(FlashLogRecordV2_t) == 16U, "FlashLogRecordV2_t must be 16 bytes");

/* Accel payload overlay — cast payload[0..9] for LOGREC_TYPE_ACCEL */
typedef struct __attribute__((packed)) {
    int16_t  x;             /* raw 16-bit output, left-justified in LP1 mode  */
    int16_t  y;
    int16_t  z;
    uint8_t  fs;            /* LIS2DW12_FS_t — needed to decode sensitivity   */
    uint8_t  _rsv[3];
} AccelPayload_t;
_Static_assert(sizeof(AccelPayload_t) == 10U, "AccelPayload_t must be 10 bytes");

/* Log config — stored in header page at offset 80 */
typedef struct __attribute__((packed)) {
    uint8_t  active_mask;
    uint8_t  overflow_mode;
    uint8_t  write_mode;
    uint8_t  checkpoint_n;
    uint16_t temp_interval_s;
    uint16_t light_interval_s;
    uint16_t battery_interval_s;
    uint16_t accel_interval_s;
    uint8_t  temp_delta_01c;
    uint8_t  light_delta_pct;
    uint8_t  battery_delta_pct;
    uint8_t  ts_source;          /* LOG_TS_BOOT or LOG_TS_RTC              */
} LogConfig_t;

#define LOG_CFG_DEFAULT { \
    .active_mask        = LOG_MASK_TEMP | LOG_MASK_BATTERY_PCT, \
    .overflow_mode      = LOG_OVERFLOW_CIRCULAR, \
    .write_mode         = LOG_WRITE_ALWAYS, \
    .checkpoint_n       = 16, \
    .temp_interval_s    = 60, \
    .light_interval_s   = 300, \
    .battery_interval_s = 3600, \
    .accel_interval_s   = 0, \
    .temp_delta_01c     = 5, \
    .light_delta_pct    = 10, \
    .battery_delta_pct  = 2, \
    .ts_source          = LOG_TS_BOOT, \
}

/* Runtime state — RAM only */
typedef struct {
    uint32_t write_addr;
    uint32_t total_records;
    /* Circular-wrap read ordering: physical slot index of the OLDEST record.
     * 0 means linear order (no wrap yet).  Set by FlashLog_Init() when it
     * detects valid old data on pages after the current write page.        */
    uint32_t read_start_slot;
    uint8_t  is_full;
    uint8_t  initialized;
    uint8_t  format_version;         /* LOG_FORMAT_VERSION or LOG_FORMAT_VERSION_V2 */
    uint8_t  _pad;
    uint16_t records_since_checkpoint;
    int16_t  last_temp;
    uint16_t last_light;
    uint8_t  last_bat_pct;
    uint16_t last_bat_mv;
    uint32_t last_temp_ts;
    uint32_t last_light_ts;
    uint32_t last_bat_ts;
    uint32_t last_accel_ts;          /* NEW — timestamp of last accel log write */
} LogState_t;

_Static_assert(sizeof(FlashLogHeader_t) == 16U, "FlashLogHeader_t must be 16 bytes");
_Static_assert(sizeof(FlashFieldDesc_t) == 16U, "FlashFieldDesc_t must be 16 bytes");
_Static_assert(sizeof(FlashLogRecord_t) == 16U, "FlashLogRecord_t must be 16 bytes");
_Static_assert(sizeof(LogConfig_t)      == 16U, "LogConfig_t must be 16 bytes");
