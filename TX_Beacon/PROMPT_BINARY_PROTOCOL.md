# Task: Binary config protocol over UART (BLE-ready)

## Context

Project: C:\Users\prymv\STM32CubeIDE\workspace_1.19.0\Beacon_30MHz\TX_Beacon
STM32WB1M beacon. Working: TX, sleep, gekon, UART text commands,
flash config (page 60), flash log (hdr page 61, data 62-67).
Flash page size = 2048 bytes (WB1M!).

## Goal

Add a MACHINE protocol layer on top of existing UART, using BINARY
structures that will later be reused 1:1 as BLE GATT characteristics.
Existing human text commands stay untouched.

## Design principles

1. One set of packed C structs = single source of truth
   (UART now, BLE GATT later — same bytes, same validation code).
2. Request/response tagging: client sends `#<id> CMD`, MCU replies
   `#<id> ...`. Telemetry lines have NO tag — clients filter easily.
3. Atomic config apply: full config arrives as one message,
   validated entirely (CRC32 + field ranges), applied all-or-nothing.
4. Hex encoding for binary payloads over UART (simple, debuggable).

---

## New files

```
Core/Inc/proto_structs.h   ← ALL wire structures (shared UART/BLE)
Core/Inc/proto_uart.h      ← machine protocol API
Core/Src/proto_uart.c      ← implementation
```

Modify: svc_uart_cmd.c (route `#` lines to proto_uart),
        main.c (init).

---

## proto_structs.h — wire structures

ALL structs packed, little-endian (native Cortex-M). These exact
layouts will become BLE characteristics later — never reorder fields,
only append.

```c
#pragma once
#include <stdint.h>

#define PROTO_VERSION      1U

/* ── ConfigBlob: full beacon configuration (64 bytes) ── */
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
    int16_t  temp_offset_01c;  /* calibration, ×0.1°C            */
    uint16_t bat_scale_x100;   /* divider scale ×100 (200=2.00)  */
    /* flash log */
    uint8_t  log_mask;         /* bit0=temp 1=light 2=bat% 3=batmv */
    uint8_t  log_mode;         /* 0=always 1=on_change 2=adaptive */
    uint8_t  log_overflow;     /* 0=stop 1=circular              */
    uint8_t  log_ckp_n;        /* checkpoint every N (adaptive)  */
    uint8_t  log_dt_01c;       /* change thresholds              */
    uint8_t  log_dl_pct;
    uint8_t  log_db_pct;
    uint8_t  reserved1;
    /* schedule */
    uint8_t  sched_en;         /* 0/1                            */
    uint8_t  reserved2;
    uint32_t sched_hours;      /* bit N = hour N active          */
    uint8_t  sched_days;       /* bit0=Mon..bit6=Sun             */
    uint8_t  reserved3;
    uint16_t sched_months;     /* bit0=Jan..bit11=Dec            */
    /* padding to 56 + crc */
    uint8_t  reserved4[14];
    uint32_t crc32;            /* CRC32 of bytes [0..59]         */
} ConfigBlob_t;
_Static_assert(sizeof(ConfigBlob_t) == 64, "ConfigBlob must be 64");

/* ── StatusBlob: live sensors snapshot (24 bytes) ── */
typedef struct __attribute__((packed)) {
    uint32_t uptime_s;
    int16_t  temp_01c;         /* 364 = 36.4°C                   */
    uint16_t vdda_mv;
    uint16_t bat_mv;
    uint8_t  bat_pct;
    uint8_t  tx_active;        /* 0/1 current TX state           */
    uint16_t light_raw;
    uint8_t  sched_active;     /* 0/1 schedule window state      */
    uint8_t  flags;            /* bit0=logging bit1=full         */
    uint16_t log_used;
    uint16_t log_total;
    uint32_t rtc_unix;         /* 0 if RTC not set               */
} StatusBlob_t;
_Static_assert(sizeof(StatusBlob_t) == 24, "StatusBlob must be 24");

/* ── InfoBlob: static board info (32 bytes) ── */
typedef struct __attribute__((packed)) {
    uint8_t  proto_ver;
    uint8_t  hw_rev;
    uint8_t  fw_major, fw_minor, fw_patch;
    uint8_t  sensors_present;  /* bit0=temp 1=light 2=bat 3=accel */
    uint16_t log_entry_size;   /* 16 */
    uint32_t uid;              /* from MCU UID                   */
    uint32_t log_total_entries;
    uint32_t flash_page_size;  /* 2048                           */
    char     tag[12];          /* animal tag, null-padded        */
} InfoBlob_t;
_Static_assert(sizeof(InfoBlob_t) == 32, "InfoBlob must be 32");

uint32_t Proto_CRC32(const uint8_t *data, uint32_t len);
```

CRC32: standard polynomial 0xEDB88320, init 0xFFFFFFFF, final XOR
0xFFFFFFFF (zlib-compatible — Python binascii.crc32 must match!).
Software table-less bit implementation is fine (64 bytes, rare calls).

---

## proto_uart protocol

Machine lines start with `#`. Format:
```
#<id> <VERB>[ <hexpayload>]
```
id: decimal 1..65535, client-chosen. Reply carries the same id.
hexpayload: uppercase hex, 2 chars per byte, no spaces.

### Verbs

```
#5 INFO?
→ #5 INFO <64 hex chars = 32 bytes InfoBlob>
→ #5 OK

#6 CFG?
→ #6 CFG <128 hex chars = 64 bytes ConfigBlob, crc filled>
→ #6 OK

#7 CFG! <128 hex chars>
→ validate: size, proto_ver, CRC32, every field range
→ on success: apply ALL to runtime + reply
  #7 OK changed=<n>
→ on failure (nothing applied):
  #7 ERR crc                          (CRC mismatch)
  #7 ERR field=<name> val=<v>         (range violation, first one)
  #7 ERR len                          (wrong payload size)

#8 STAT?
→ #8 STAT <48 hex chars = 24 bytes StatusBlob>
→ #8 OK

#9 SAVE!
→ write current config to flash page 60
→ #9 OK   /  #9 ERR flash

#10 LOG? <offset_dec> <count_dec>
→ #10 LOG <hex of count×16 bytes of FlashLogRecord_t>
   (max 8 records = 128 bytes per response line)
→ #10 OK n=<returned>
```

### Rules

- Lines NOT starting with `#` → existing text command handler
  (human protocol untouched).
- Telemetry ([TEMP], [DATA], [LOG] prints) NEVER carries `#`.
- Response always ends with `#<id> OK` or `#<id> ERR ...` line.
- Field validation table (apply BEFORE touching runtime):
  rf_mode≤3, rf_channel≤3, 1≤rf_power≤4, led_mode≤3,
  5≤rf_pulse_ms≤5000, 100≤rf_period_ms≤600000,
  log_mode≤2, log_overflow≤1, log_ckp_n≥1,
  sched_hours < (1<<24), sched_days < (1<<7), sched_months < (1<<12).
- CFG! maps blob → existing g_config fields, then calls the SAME
  apply functions the text commands use (RF_SetChannel, etc).
  Do not duplicate apply logic.
- Buffer: machine lines can be up to 160 chars — check existing
  uart_rx_buf size (64!) and ENLARGE to 192 bytes. Verify the
  2-slot pending buffer still works with the new size.

---

## Testing (via PuTTY/terminal, by hand)

1. `#1 INFO?` → hex line + OK; decode: fw version, uid visible
2. `#2 CFG?` → 128 hex chars; last 8 chars = CRC32
3. Copy CFG hex, change one byte, send `#3 CFG! <bad>` → ERR crc
4. Send valid CFG! with rf_power nibble = 09 → ERR field=rf_power
5. Valid CFG! with changed channel → OK changed=1, verify `status`
   text command shows new channel
6. `#4 SAVE!` → OK, power cycle, `#5 CFG?` → same config
7. `#6 LOG? 0 4` → 4 records hex
8. Mixed mode: text `temp` command still works between # commands

---

## ADDENDUM: sched_scope field

Use reserved2 byte in ConfigBlob_t (rename it):

```c
    uint8_t  sched_en;         /* 0/1                              */
    uint8_t  sched_scope;      /* 0 = TX only (default)            */
                               /* 1 = TX + sensor logging          */
```

Validation: sched_scope <= 1.

Firmware behavior:
- sched_scope=0: schedule gates ONLY the transmitter task.
  Sensor reads + FlashLog_Task() run on their intervals 24/7.
- sched_scope=1: outside the active window the scheduler ALSO
  skips sensor reads and flash log writes (deeper sleep between
  RTC wakeups; wake only to check schedule state).
- In both modes gekon, UART detect and BLE window behave unchanged.

Text command for human protocol: `sched scope 0|1`, shown in
`sched show` output as `scope: tx-only | tx+log`.
