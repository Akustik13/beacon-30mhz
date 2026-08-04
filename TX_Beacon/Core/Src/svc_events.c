#include "svc_events.h"
#include "flash_config.h"
#include "flash_log.h"
#include "drv_led.h"
#include "app_ble.h"
#include "proto_uart.h"
#include "drv_uart.h"
#include "svc_uart_cmd.h"
#include "stm32wbxx_hal.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>

/* ── RAM state ─────────────────────────────────────────────────────── */
static BeaconEvent_t s_ev[EVT_MAX_EVENTS];
static uint8_t       s_cooldown[EVT_MAX_EVENTS];    /* cycles left */
static uint32_t      s_last_fire_ts[EVT_MAX_EVENTS]; /* unix of last fire */
static uint32_t      s_no_motion_cnt;               /* consecutive no-motion cycles */
static uint8_t       s_initialized   = 0U;
static uint8_t       s_ts_seeded     = 0U;  /* 1 once rtc_unix > 0 seen */

/* ── Flash storage helpers ──────────────────────────────────────────── */

/* Storage block: magic(4) + blob(112) + crc32(4) = 120 bytes → padded to 128 */
#define _EV_OFF_MAGIC    0U
#define _EV_OFF_BLOB     4U
#define _EV_OFF_CRC      (4U + EVT_BLOB_SIZE)   /* 116 */
#define _EV_BLOCK_SIZE   (_EV_OFF_CRC + 4U)     /* 120 */
#define _EV_BUF_SIZE     128U                   /* padded to double-word multiple */

static uint32_t _get_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void _put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint8_t _events_save(void)
{
    static uint8_t buf[_EV_BUF_SIZE] __attribute__((aligned(8)));
    memset(buf, 0xFF, sizeof(buf));

    _put_u32le(&buf[_EV_OFF_MAGIC], EVENTS_MAGIC);
    memcpy(&buf[_EV_OFF_BLOB], s_ev, EVT_BLOB_SIZE);
    uint32_t crc = Proto_CRC32(buf, _EV_OFF_CRC);
    _put_u32le(&buf[_EV_OFF_CRC], crc);

    if (!Flash_ErasePage(EVENTS_FLASH_PAGE)) {
        UART_Print("[EVT] save: erase failed\r\n");
        return 0U;
    }
    if (!Flash_WriteBlock(EVENTS_FLASH_ADDR, buf, _EV_BUF_SIZE)) {
        UART_Print("[EVT] save: write failed\r\n");
        return 0U;
    }
    UART_Print("[EVT] saved to flash\r\n");
    return 1U;
}

static uint8_t _events_load(void)
{
    const uint8_t *p = (const uint8_t *)EVENTS_FLASH_ADDR;

    if (_get_u32le(&p[_EV_OFF_MAGIC]) != EVENTS_MAGIC) return 0U;

    uint32_t stored = _get_u32le(&p[_EV_OFF_CRC]);
    uint32_t calc   = Proto_CRC32(p, _EV_OFF_CRC);
    if (stored != calc) {
        UART_Printf("[EVT] CRC mismatch stored=0x%08lX calc=0x%08lX\r\n",
                    stored, calc);
        return 0U;
    }
    memcpy(s_ev, &p[_EV_OFF_BLOB], EVT_BLOB_SIZE);
    return 1U;
}

/* ── Condition evaluator ────────────────────────────────────────────── */
static uint8_t _eval_cond(const EvCond_t *c, const EvSensors_t *s, uint8_t ei)
{
    switch (c->cond_type) {
    case COND_DISABLED:       return 0U;
    case COND_ALWAYS:         return 1U;
    case COND_BEFORE_BLE:     return 1U;  /* treated as unconditional trigger */

    case COND_BATT_BELOW:
        return (s->batt_pct < (uint8_t)c->val1) ? 1U : 0U;
    case COND_BATT_ABOVE:
        return (s->batt_pct > (uint8_t)c->val1) ? 1U : 0U;

    case COND_TEMP_ABOVE:
        return (s->temp_01c != INT16_MIN &&
                s->temp_01c > (int16_t)(c->val1 * 10)) ? 1U : 0U;
    case COND_TEMP_BELOW:
        return (s->temp_01c != INT16_MIN &&
                s->temp_01c < (int16_t)(c->val1 * 10)) ? 1U : 0U;

    case COND_MOTION:
        return s->motion ? 1U : 0U;
    case COND_NO_MOTION: {
        uint32_t n = (c->val1 > 0) ? (uint32_t)c->val1 : 1U;
        return (s_no_motion_cnt >= n) ? 1U : 0U;
    }

    case COND_LIGHT_BELOW:
        return (s->light_raw != UINT16_MAX &&
                s->light_raw < (uint16_t)c->val1) ? 1U : 0U;
    case COND_LIGHT_ABOVE:
        return (s->light_raw != UINT16_MAX &&
                s->light_raw > (uint16_t)c->val1) ? 1U : 0U;

    case COND_EVERY_N_CYCLES: {
        uint32_t n = (c->val1 > 0) ? (uint32_t)c->val1 : 1U;
        return ((s->cycle_num % n) == 0U) ? 1U : 0U;
    }

    case COND_EVERY_N_HRS: {
        /* val1 = total_minutes (hours×60 + min), val2 = extra seconds */
        uint32_t period_s = (uint32_t)(c->val1 > 0 ? c->val1 : 0) * 60UL
                          + (uint32_t)(c->val2 > 0 ? c->val2 : 0);
        if (period_s == 0U) period_s = 1U;  /* minimum 1 s */
        if (!s_ts_seeded || s->rtc_unix == 0U) return 0U;
        uint32_t elapsed = s->rtc_unix - s_last_fire_ts[ei];
        return (elapsed >= period_s) ? 1U : 0U;
    }

    default: return 0U;
    }
}

/* ── Action executor ────────────────────────────────────────────────── */
static void _exec_action(const BeaconEvent_t *ev)
{
    int16_t p1 = ev->act_p1;
    int16_t p2 = ev->act_p2;

    switch (ev->act_type) {
    case ACT_NONE:
        break;

    case ACT_SET_POWER:
        if (p1 >= 0 && p1 <= 2) {
            g_pwr_idx = (uint8_t)p1;
            g_config_save_pending = 1U;
            UART_Printf("[EVT] pwr→%d\r\n", p1);
        }
        break;

    case ACT_SET_CHANNEL:
        if (p1 >= 1 && p1 <= 3) {
            g_ch_idx = (uint8_t)(p1 - 1U);
            g_config_save_pending = 1U;
            UART_Printf("[EVT] ch→%d\r\n", p1);
        }
        break;

    case ACT_SET_PERIOD:
        if (p1 > 0) {
            g_tx_period_ms = (uint32_t)p1 * 1000UL;
            g_config_save_pending = 1U;
            g_cfg_changed = 1U;
            UART_Printf("[EVT] period→%ds\r\n", p1);
        }
        break;

    case ACT_BLE_START:
        BLE_StartSession(g_ble_duration_sec ? (uint32_t)g_ble_duration_sec * 1000UL
                                            : 30000UL);
        UART_Print("[EVT] ble_start\r\n");
        break;

    case ACT_LOG_MARKER:
        FlashLog_WriteMark((uint8_t)(p1 & 0xFFU));
        UART_Printf("[EVT] mark tag=%d\r\n", p1);
        break;

    case ACT_TX_PULSES:
        UART_Printf("[EVT] tx_pulses count=%d gap=%d (NYI)\r\n", p1, p2);
        break;

    case ACT_TX_PATTERN:
        UART_Printf("[EVT] tx_pattern on=%d off=%d (NYI)\r\n", p1, p2);
        break;

    case ACT_LED_ON:
        LED_SetMode(LED_ON);
        UART_Print("[EVT] led on\r\n");
        break;

    case ACT_LED_OFF:
        LED_SetMode(LED_OFF);
        UART_Print("[EVT] led off\r\n");
        break;

    case ACT_LED_BLINK: {
        uint8_t  cnt    = (p1 > 0 && p1 <= 20) ? (uint8_t)p1 : 3U;
        uint32_t period = (p2 > 0) ? (uint32_t)p2 : 200UL;
        uint32_t half   = period / 2U;
        LED_Blink(cnt, half ? half : 50UL, half ? half : 50UL);
        UART_Printf("[EVT] led blink ×%d %lums\r\n", cnt, period);
        break;
    }

    default:
        UART_Printf("[EVT] unknown act=0x%02X\r\n", ev->act_type);
        break;
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

void svc_events_init(void)
{
    memset(s_ev, 0, sizeof(s_ev));
    memset(s_cooldown, 0, sizeof(s_cooldown));
    memset(s_last_fire_ts, 0, sizeof(s_last_fire_ts));
    s_no_motion_cnt = 0U;
    s_ts_seeded     = 0U;
    s_initialized   = 1U;

    if (_events_load()) {
        UART_Print("[EVT] loaded ok\r\n");
    } else {
        UART_Print("[EVT] flash empty — defaults\r\n");
    }
}

void svc_events_process(const EvSensors_t *s)
{
    if (!s_initialized) return;

    /* Seed last_fire_ts once RTC is available so the first period starts NOW */
    if (!s_ts_seeded && s->rtc_unix > 0U) {
        s_ts_seeded = 1U;
        for (uint8_t i = 0U; i < EVT_MAX_EVENTS; i++) {
            s_last_fire_ts[i] = s->rtc_unix;
        }
    }

    /* Update no-motion streak */
    if (s->motion) {
        s_no_motion_cnt = 0U;
    } else {
        s_no_motion_cnt++;
    }

    for (uint8_t i = 0U; i < EVT_MAX_EVENTS; i++) {
        BeaconEvent_t *ev = &s_ev[i];
        if (!(ev->flags & EV_FLAG_ENABLED)) continue;
        if (ev->n_conds == 0U)              continue;

        /* Cooldown gate */
        if (s_cooldown[i] > 0U) {
            s_cooldown[i]--;
            continue;
        }

        /* Evaluate all conditions (AND logic) */
        uint8_t n = (ev->n_conds <= EVT_MAX_CONDS) ? ev->n_conds : EVT_MAX_CONDS;
        uint8_t pass = 1U;
        for (uint8_t j = 0U; j < n; j++) {
            if (!_eval_cond(&ev->conds[j], s, i)) { pass = 0U; break; }
        }
        if (!pass) continue;

        /* FIRE */
        UART_Printf("[EVT] #%d fired act=0x%02X\r\n", i, ev->act_type);
        _exec_action(ev);

        s_last_fire_ts[i] = s->rtc_unix;
        s_cooldown[i]     = ev->cooldown;

        if (ev->flags & EV_FLAG_ONESHOT) {
            ev->flags &= (uint8_t)(~EV_FLAG_ENABLED);
            _events_save();  /* persist disabled state */
        }
    }
}

void svc_events_get_blob(uint8_t *buf)
{
    memcpy(buf, s_ev, EVT_BLOB_SIZE);
}

uint8_t svc_events_set_blob(const uint8_t *buf)
{
    memcpy(s_ev, buf, EVT_BLOB_SIZE);
    return _events_save();
}

uint8_t svc_events_clear(uint8_t idx)
{
    if (idx == 0xFFU) {
        memset(s_ev, 0, sizeof(s_ev));
    } else if (idx < EVT_MAX_EVENTS) {
        memset(&s_ev[idx], 0, sizeof(BeaconEvent_t));
        s_cooldown[idx]     = 0U;
        s_last_fire_ts[idx] = 0U;
    } else {
        return 0U;
    }
    return _events_save();
}
