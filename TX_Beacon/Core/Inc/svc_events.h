#pragma once
#include <stdint.h>

/*
 * svc_events — IF/THEN event rule engine.
 *
 * Flash layout: page 58 (0x0801C800), first 128 bytes.
 *   [0..3]   EVENTS_MAGIC
 *   [4..115] 4 × 28-byte event blobs
 *   [116..119] CRC32 over bytes [0..115]
 *
 * Wire format (matches PC app / Flutter):
 *   OP_EVT_GET 0x80 → 112 bytes (4 events × 28 bytes)
 *   OP_EVT_SET 0x81, 112 bytes → OK
 *   OP_EVT_CLR 0x82, idx(1): 0-3 or 0xFF=all → OK
 */

/* ── Flash page (app code ends at 110 KB = page 54; pages 55–58 free) ─ */
#define EVENTS_FLASH_PAGE  58U
#define EVENTS_FLASH_ADDR  (0x08000000UL + (uint32_t)EVENTS_FLASH_PAGE * 2048UL)
#define EVENTS_MAGIC       0xBEAC0701UL

/* ── Protocol sizes ─────────────────────────────────────────────────── */
#define EVT_MAX_EVENTS  4U
#define EVT_MAX_CONDS   3U
#define EVT_EVENT_SIZE  28U
#define EVT_BLOB_SIZE   (EVT_MAX_EVENTS * EVT_EVENT_SIZE)  /* 112 bytes */

/* ── Event flags ────────────────────────────────────────────────────── */
#define EV_FLAG_ENABLED  0x01U
#define EV_FLAG_ONESHOT  0x02U

/* ── Condition types ────────────────────────────────────────────────── */
#define COND_DISABLED       0x00U
#define COND_BATT_BELOW     0x01U  /* val1 = % threshold */
#define COND_BATT_ABOVE     0x02U
#define COND_TEMP_ABOVE     0x03U  /* val1 = °C */
#define COND_TEMP_BELOW     0x04U
#define COND_NO_MOTION      0x05U  /* val1 = N consecutive no-motion cycles */
#define COND_MOTION         0x06U
#define COND_LIGHT_BELOW    0x07U  /* val1 = % (0-100) */
#define COND_LIGHT_ABOVE    0x08U
#define COND_EVERY_N_CYCLES 0x09U  /* val1 = N TX cycles */
#define COND_ALWAYS         0x0AU
#define COND_BEFORE_BLE     0x0BU  /* treated as always-true; used as marker */
#define COND_EVERY_N_HRS    0x0CU  /* val1=total_minutes val2=extra_seconds */

/* ── Action types ───────────────────────────────────────────────────── */
#define ACT_NONE        0x00U
#define ACT_SET_POWER   0x01U  /* p1 = 0/1/2 */
#define ACT_TX_PULSES   0x02U  /* p1 = count, p2 = gap_ms */
#define ACT_TX_PATTERN  0x03U  /* p1 = on_ms, p2 = off_ms */
#define ACT_BLE_START   0x04U
#define ACT_SET_CHANNEL 0x05U  /* p1 = 1/2/3 */
#define ACT_SET_PERIOD  0x06U  /* p1 = period_s */
#define ACT_LOG_MARKER  0x07U  /* p1 = tag byte */
#define ACT_LED_ON      0x08U
#define ACT_LED_OFF     0x09U
#define ACT_LED_BLINK   0x0AU  /* p1 = count, p2 = period_ms (0 → 200 ms) */

/* ── Wire-format structs ────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t cond_type;   /* COND_* */
    int16_t val1;        /* parameter 1 (LE) */
    int16_t val2;        /* parameter 2 (LE) */
} EvCond_t;              /* 5 bytes */

typedef struct __attribute__((packed)) {
    uint8_t  flags;                /* EV_FLAG_* */
    uint8_t  n_conds;              /* active conditions, 0..EVT_MAX_CONDS */
    EvCond_t conds[EVT_MAX_CONDS]; /* 15 bytes */
    uint8_t  act_type;             /* ACT_* */
    int16_t  act_p1;               /* action param 1 (LE) */
    int16_t  act_p2;               /* action param 2 (LE) */
    uint8_t  cooldown;             /* TX cycles to skip after firing */
    uint8_t  _pad[5];              /* reserved, always 0 */
} BeaconEvent_t;                  /* 28 bytes */
_Static_assert(sizeof(BeaconEvent_t) == EVT_EVENT_SIZE, "BeaconEvent_t != 28 bytes");

/* ── Sensor snapshot passed by main loop each TX cycle ─────────────── */
typedef struct {
    int16_t  temp_01c;   /* temperature ×0.1 °C; INT16_MIN = unavailable */
    uint8_t  batt_pct;   /* 0-100 */
    uint8_t  motion;     /* 1 = motion detected this cycle */
    uint16_t light_raw;  /* ADC counts 0-4095; UINT16_MAX = unavailable */
    uint32_t rtc_unix;   /* current unix timestamp (0 = RTC not set) */
    uint32_t cycle_num;  /* total TX cycle counter since boot */
} EvSensors_t;

/* ── Public API ─────────────────────────────────────────────────────── */

/* Load events from flash; call once after FlashConfig_Load(). */
void    svc_events_init(void);

/* Evaluate all events; call once per TX cycle after reading sensors.
 * Fires matching actions in-place. */
void    svc_events_process(const EvSensors_t *s);

/* Serialise all events to buf[EVT_BLOB_SIZE]. */
void    svc_events_get_blob(uint8_t *buf);

/* Deserialise 112 bytes → RAM + save to flash.  Returns 1 on success. */
uint8_t svc_events_set_blob(const uint8_t *buf);

/* Clear event(s): idx 0-3 = single, 0xFF = all.  Returns 1 on success. */
uint8_t svc_events_clear(uint8_t idx);
