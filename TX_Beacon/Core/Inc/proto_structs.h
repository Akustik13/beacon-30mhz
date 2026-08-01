#pragma once
#include <stdint.h>

#define PROTO_VERSION      1U
#define FW_VERSION_MAJOR   1U
#define FW_VERSION_MINOR   2U
#define FW_VERSION_PATCH   0U

/* ── ConfigBlob: full beacon configuration (64 bytes) ─────────────────────── */
typedef struct __attribute__((packed)) {
    /* header */
    uint8_t  proto_ver;        /* = PROTO_VERSION                */
    uint8_t  cfg_size;         /* = 64                           */
    /* RF */
    uint8_t  rf_mode;          /* 0=off 1=pulse 2=cont 3=eco     */
    uint8_t  rf_channel;       /* 0..3                           */
    uint8_t  rf_power;         /* 1..4                           */
    uint8_t  led_mode;         /* 0=off 1=on 2=heartbeat 3=tx    */
    uint16_t rf_pulse_ms;      /* 5..5000                        */
    uint32_t rf_period_ms;     /* 100..600000                    */
    /* sensors: interval seconds, 0 = sensor off */
    uint16_t temp_iv_s;
    uint16_t light_iv_s;
    uint16_t bat_iv_s;
    int16_t  temp_offset_01c;  /* calibration, x0.1 C            */
    uint16_t bat_scale_x100;   /* divider scale x100 (200=2.00)  */
    /* flash log */
    uint8_t  log_mask;         /* bit0=temp 1=light 2=bat% 3=batmv */
    uint8_t  log_mode;         /* 0=always 1=on_change 2=adaptive */
    uint8_t  log_overflow;     /* 0=stop 1=circular              */
    uint8_t  log_ckp_n;        /* checkpoint every N             */
    uint8_t  log_dt_01c;       /* change thresholds              */
    uint8_t  log_dl_pct;
    uint8_t  log_db_pct;
    uint8_t  log_ts_source;    /* 0=boot (s since reset) 1=RTC   */
    /* schedule */
    uint8_t  sched_en;         /* 0/1                            */
    uint8_t  sched_scope;      /* 0=TX only  1=TX+logging        */
    uint32_t sched_hours;      /* bit N = hour N active (0-23)   */
    uint8_t  sched_days;       /* bit0=Mon..bit6=Sun             */
    uint8_t  reserved3;
    uint16_t sched_months;     /* bit0=Jan..bit11=Dec            */
    /* [40..41] uptime auto-save interval (minutes); 0 = firmware default (24 h) */
    uint16_t uptime_save_min;
    /* padding so data = 60 bytes, crc at offset 60 */
    uint8_t  reserved4[18];
    uint32_t crc32;            /* CRC32 of bytes [0..59]         */
} ConfigBlob_t;
_Static_assert(sizeof(ConfigBlob_t) == 64, "ConfigBlob must be 64");

/* ── StatusBlob: live sensors snapshot (24 bytes) ─────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t uptime_s;
    int16_t  temp_01c;         /* 364 = 36.4 C                   */
    uint16_t vdda_mv;
    uint16_t bat_mv;
    uint8_t  bat_pct;
    uint8_t  tx_active;        /* 0/1 current TX enabled state   */
    uint16_t light_raw;
    uint8_t  sched_active;     /* 0/1 schedule window state      */
    uint8_t  flags;            /* bit0=logging bit1=full         */
    uint16_t log_used;
    uint16_t log_total;
    uint32_t rtc_unix;         /* 0 if RTC not set               */
} StatusBlob_t;
_Static_assert(sizeof(StatusBlob_t) == 24, "StatusBlob must be 24");

/* ── InfoBlob: static board info (48 bytes) ───────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  proto_ver;
    uint8_t  hw_rev;
    uint8_t  fw_major, fw_minor, fw_patch;
    uint8_t  sensors_present;  /* bit0=temp 1=light 2=bat 3=accel */
    uint16_t log_entry_size;   /* 16 */
    uint32_t uid;              /* from MCU UID registers         */
    uint32_t log_total_entries;
    uint32_t flash_page_size;  /* 2048                           */
    char     tag[12];          /* animal tag, null-padded        */
    uint32_t total_active_h;   /* CPU active hours (excl. Stop1) */
    uint32_t total_stop1_h;    /* hours in Stop1 sleep           */
    uint32_t total_shutdown_h; /* hours in Shutdown between sessions */
    uint32_t flash_erase_count;/* lifetime flash page erase count    */
} InfoBlob_t;
_Static_assert(sizeof(InfoBlob_t) == 48, "InfoBlob must be 48");

/* CRC32: polynomial 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF
 * (zlib-compatible — Python binascii.crc32 matches) */
uint32_t Proto_CRC32(const uint8_t *data, uint32_t len);
