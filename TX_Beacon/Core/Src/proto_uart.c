#include "proto_uart.h"
#include "proto_structs.h"
#include "cmd_layer.h"
#include "svc_uart_cmd.h"
#include "drv_uart.h"
#include "drv_rf_tx.h"
#include "drv_led.h"
#include "drv_chip_temp.h"
#include "drv_batt_adc.h"
#include "drv_light_adc.h"
#include "flash_config.h"
#include "flash_log.h"
#include "hw_desc.h"
#include "svc_power.h"
#include "app_ble.h"
#include "stm32wbxx_hal.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* ── CRC32 (zlib-compatible: poly 0xEDB88320, init 0xFFFFFFFF, final XOR) ── */
uint32_t Proto_CRC32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8U; b++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)(-(int32_t)(crc & 1U)));
    }
    return ~crc;
}

/* ── Hex helpers ─────────────────────────────────────────────────────────── */
static const char s_hx[] = "0123456789ABCDEF";

static void _hex_enc(const uint8_t *src, uint32_t len, char *dst)
{
    for (uint32_t i = 0; i < len; i++) {
        *dst++ = s_hx[src[i] >> 4];
        *dst++ = s_hx[src[i] & 0xFU];
    }
    *dst = '\0';
}

static int8_t _hex_nib(char c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    return -1;
}

/* Returns 0 on success, -1 on invalid hex char. */
static int _hex_dec(const char *src, uint8_t *dst, uint32_t expected_len)
{
    for (uint32_t i = 0; i < expected_len; i++) {
        int8_t hi = _hex_nib(src[i * 2U]);
        int8_t lo = _hex_nib(src[i * 2U + 1U]);
        if (hi < 0 || lo < 0) return -1;
        dst[i] = (uint8_t)((uint8_t)(hi << 4) | (uint8_t)lo);
    }
    return 0;
}

/* ── RTC → Unix timestamp: delegate to svc_power ────────────────────────── */
static uint32_t _rtc_to_unix(void) { return Power_RTC_GetUnix(); }

/* ── Build ConfigBlob from current runtime state ─────────────────────────── */
static void _build_cfg(ConfigBlob_t *b)
{
    memset(b, 0, sizeof(*b));
    b->proto_ver = PROTO_VERSION;
    b->cfg_size  = (uint8_t)sizeof(ConfigBlob_t);

    /* rf_mode: firmware TX_MODE_PULSE=0,CONT=1,ECO=2,OFF=3
     *          blob:     off=0, pulse=1, cont=2, eco=3        */
    static const uint8_t k_fw2blob[4] = {1U, 2U, 3U, 0U};
    b->rf_mode    = (g_tx_mode <= 3U) ? k_fw2blob[g_tx_mode] : 0U;
    b->rf_channel = g_ch_idx;
    b->rf_power   = g_pwr_idx + 1U;
    b->led_mode   = (uint8_t)LED_GetMode();
    b->rf_pulse_ms  = (g_tx_duration_ms > 0xFFFFU) ? 0xFFFFU : (uint16_t)g_tx_duration_ms;
    b->rf_period_ms = g_tx_period_ms;

    b->temp_iv_s  = (g_temp_mode  == TEMP_MODE_PERIODIC)   ? (uint16_t)g_temp_period_s  : 0U;
    b->light_iv_s = (g_light_mode == LIGHT_MODE_PERIODIC)  ? (uint16_t)g_light_period_s : 0U;
    b->bat_iv_s   = (g_batt_mode  == BATT_MODE_PERIODIC)   ? (uint16_t)g_batt_period_s  : 0U;
    b->temp_offset_01c = (int16_t)g_temp_offset_x10;
    b->bat_scale_x100  = (uint16_t)g_batt_scale_x10 * 10U;

    LogConfig_t lc;
    FlashLog_GetConfig(&lc);
    b->log_mask      = lc.active_mask;
    b->log_mode      = lc.write_mode;
    b->log_overflow  = lc.overflow_mode;
    b->log_ckp_n     = lc.checkpoint_n ? lc.checkpoint_n : 1U;
    b->log_dt_01c    = lc.temp_delta_01c;
    b->log_dl_pct    = lc.light_delta_pct;
    b->log_db_pct    = lc.battery_delta_pct;
    b->log_ts_source = lc.ts_source;

    b->sched_en    = g_sched_en;
    b->sched_scope = g_sched_scope;
    b->sched_hours  = g_active_hours_mask;
    b->sched_days   = g_active_days_mask;
    b->sched_months = g_active_months_mask;
    b->uptime_save_min = g_uptime_save_min;

    b->crc32 = Proto_CRC32((const uint8_t *)b, 60U);
}

/* ── Build StatusBlob from live sensors ──────────────────────────────────── */
static void _build_stat(StatusBlob_t *b)
{
    memset(b, 0, sizeof(*b));
    b->uptime_s   = HAL_GetTick() / 1000U;
    b->temp_01c   = (g_last_temp_x10 == INT32_MIN) ? 0 : (int16_t)g_last_temp_x10;
    b->vdda_mv    = (uint16_t)g_last_vdda_mV;
    b->bat_mv     = (uint16_t)g_last_batt_mV;
    b->bat_pct    = (g_last_batt_pct < 0) ? 0U : (uint8_t)g_last_batt_pct;
    b->tx_active  = (RF_IsEnabled() && g_tx_mode != TX_MODE_OFF && !g_tx_paused) ? 1U : 0U;
    b->light_raw  = g_last_light_raw;
    b->sched_active = Schedule_IsActive();

    uint32_t used = 0U, free_cnt = 0U, used_b = 0U, free_b = 0U;
    FlashLog_GetStatus(&used, &free_cnt, &used_b, &free_b);

    LogConfig_t lc;
    FlashLog_GetConfig(&lc);
    b->flags     = (lc.active_mask ? 1U : 0U) | ((free_cnt == 0U) ? 2U : 0U);
    b->log_used  = (uint16_t)(used  > 0xFFFFU ? 0xFFFFU : used);
    /* log_total: overwrite-cycle offset (0..cap-1) when circular+full, else LOG_ENTRIES_MAX.
     * GUI uses InfoBlob.log_total_entries for the true capacity.                           */
    b->log_total = (used > (uint32_t)LOG_ENTRIES_MAX)
        ? (uint16_t)(used % (uint32_t)LOG_ENTRIES_MAX)
        : (uint16_t)LOG_ENTRIES_MAX;
    b->rtc_unix  = _rtc_to_unix();
}

/* ── Build InfoBlob ──────────────────────────────────────────────────────── */
static void _build_info(InfoBlob_t *b)
{
    memset(b, 0, sizeof(*b));
    b->proto_ver        = PROTO_VERSION;
    b->hw_rev           = g_hw_desc.hw_version;
    b->fw_major         = FW_VERSION_MAJOR;
    b->fw_minor         = FW_VERSION_MINOR;
    b->fw_patch         = FW_VERSION_PATCH;
    b->sensors_present  = (g_hw_desc.temp_type  ? 1U : 0U)
                        | (g_hw_desc.light_type  ? 2U : 0U)
                        | (g_hw_desc.batt_type   ? 4U : 0U)
                        | (g_hw_desc.accel_type  ? 8U : 0U);
    b->log_entry_size   = LOG_ENTRY_SIZE;
    b->uid               = *(volatile uint32_t *)0x1FFF7590UL; /* STM32WB UID word 0 */
    b->log_total_entries = LOG_ENTRIES_MAX;
    b->flash_page_size   = FLASH_PAGE_SIZE;
    strncpy(b->tag, g_hw_desc.tag, sizeof(b->tag) - 1U);
    b->tag[sizeof(b->tag) - 1U] = '\0';
    b->total_active_h    = HwDesc_GetTotalActiveH();
    b->total_stop1_h     = HwDesc_GetTotalStop1H();
    b->total_shutdown_h  = HwDesc_GetTotalShutdownH();
    b->flash_erase_count = g_flash_erase_count;
}

/* ── Apply validated ConfigBlob to runtime, return changed field count ────── */
static uint32_t _apply_cfg(const ConfigBlob_t *b)
{
    uint32_t n = 0U;

    /* rf_mode: blob 0=off,1=pulse,2=cont,3=eco → fw OFF=3,PULSE=0,CONT=1,ECO=2 */
    static const uint8_t k_blob2fw[4] = {
        TX_MODE_OFF, TX_MODE_PULSE, TX_MODE_CONT, TX_MODE_ECO
    };
    uint8_t new_mode = k_blob2fw[b->rf_mode];
    if (new_mode != g_tx_mode) { g_tx_mode = new_mode; n++; }

    if (b->rf_channel != g_ch_idx) {
        g_ch_idx = b->rf_channel;
        RF_SetChannel((RF_Channel_t)g_ch_idx);
        n++;
    }
    uint8_t new_pwr = b->rf_power - 1U;
    if (new_pwr != g_pwr_idx) {
        g_pwr_idx = new_pwr;
        RF_SetPower((RF_Power_t)(g_pwr_idx + 1U));
        n++;
    }
    if (b->led_mode != (uint8_t)LED_GetMode()) {
        g_led_mode = b->led_mode;
        LED_SetMode((LED_Mode_t)b->led_mode);
        n++;
    }
    uint32_t new_pulse = b->rf_pulse_ms;
    if (new_pulse != g_tx_duration_ms) { g_tx_duration_ms = new_pulse; n++; }
    if (b->rf_period_ms != g_tx_period_ms) { g_tx_period_ms = b->rf_period_ms; n++; }

    /* Sensor intervals */
    uint8_t new_temp_mode = (b->temp_iv_s > 0U) ? TEMP_MODE_PERIODIC : TEMP_MODE_OFF;
    if (new_temp_mode != g_temp_mode) { g_temp_mode = new_temp_mode; n++; }
    if (b->temp_iv_s > 0U) {
        uint8_t ps = (b->temp_iv_s > 255U) ? 255U : (uint8_t)b->temp_iv_s;
        if (ps != g_temp_period_s) { g_temp_period_s = ps; n++; }
    }

    uint8_t new_light_mode = (b->light_iv_s > 0U) ? LIGHT_MODE_PERIODIC : LIGHT_MODE_OFF;
    if (new_light_mode != g_light_mode) { g_light_mode = new_light_mode; n++; }
    if (b->light_iv_s > 0U) {
        uint8_t ps = (b->light_iv_s > 255U) ? 255U : (uint8_t)b->light_iv_s;
        if (ps != g_light_period_s) { g_light_period_s = ps; n++; }
    }

    uint8_t new_batt_mode = (b->bat_iv_s > 0U) ? BATT_MODE_PERIODIC : BATT_MODE_OFF;
    if (new_batt_mode != g_batt_mode) { g_batt_mode = new_batt_mode; n++; }
    if (b->bat_iv_s > 0U) {
        uint16_t ps = b->bat_iv_s;
        if (ps != g_batt_period_s) { g_batt_period_s = ps; n++; }
    }

    int8_t new_ofs = (int8_t)b->temp_offset_01c;
    if (new_ofs != g_temp_offset_x10) { g_temp_offset_x10 = new_ofs; n++; }

    uint8_t new_scale = (b->bat_scale_x100 < 10U) ? 10U : (uint8_t)(b->bat_scale_x100 / 10U);
    if (new_scale != g_batt_scale_x10) { g_batt_scale_x10 = new_scale; n++; }

    /* Log config */
    LogConfig_t lc;
    FlashLog_GetConfig(&lc);
    uint8_t log_changed = 0U;
    if (lc.active_mask       != b->log_mask)       { lc.active_mask       = b->log_mask;       log_changed = 1U; }
    if (lc.write_mode        != b->log_mode)       { lc.write_mode        = b->log_mode;       log_changed = 1U; }
    if (lc.overflow_mode     != b->log_overflow)   { lc.overflow_mode     = b->log_overflow;   log_changed = 1U; }
    if (lc.checkpoint_n      != b->log_ckp_n)     { lc.checkpoint_n      = b->log_ckp_n;      log_changed = 1U; }
    if (lc.temp_delta_01c    != b->log_dt_01c)    { lc.temp_delta_01c    = b->log_dt_01c;     log_changed = 1U; }
    if (lc.light_delta_pct   != b->log_dl_pct)    { lc.light_delta_pct   = b->log_dl_pct;     log_changed = 1U; }
    if (lc.battery_delta_pct != b->log_db_pct)    { lc.battery_delta_pct = b->log_db_pct;     log_changed = 1U; }
    if (lc.ts_source         != b->log_ts_source)  { lc.ts_source         = b->log_ts_source;  log_changed = 1U; }
    /* Sync log intervals to sensor periods — include in log_changed so they persist */
    if (b->temp_iv_s  > 0U && lc.temp_interval_s     != b->temp_iv_s)  { lc.temp_interval_s     = b->temp_iv_s;  log_changed = 1U; }
    if (b->light_iv_s > 0U && lc.light_interval_s    != b->light_iv_s) { lc.light_interval_s    = b->light_iv_s; log_changed = 1U; }
    if (b->bat_iv_s   > 0U && lc.battery_interval_s  != b->bat_iv_s)   { lc.battery_interval_s  = b->bat_iv_s;   log_changed = 1U; }
    if (log_changed) { FlashLog_SetConfig(&lc); n++; }

    /* Schedule */
    if (b->sched_en    != g_sched_en)            { g_sched_en            = b->sched_en;    n++; }
    if (b->sched_scope != g_sched_scope)          { g_sched_scope         = b->sched_scope; n++; }
    if (b->sched_hours  != g_active_hours_mask)   { g_active_hours_mask   = b->sched_hours;  n++; }
    if (b->sched_days   != g_active_days_mask)    { g_active_days_mask    = b->sched_days;   n++; }
    if (b->sched_months != g_active_months_mask)  { g_active_months_mask  = b->sched_months; n++; }
    if (b->uptime_save_min != g_uptime_save_min)  { g_uptime_save_min     = b->uptime_save_min; n++; }

    return n;
}

/* ── Verb handlers ────────────────────────────────────────────────────────── */

static void _do_info(uint16_t id)
{
    InfoBlob_t b;
    _build_info(&b);
    char hex[97];   /* 48 bytes × 2 + null */
    _hex_enc((const uint8_t *)&b, sizeof(b), hex);
    UART_MachPrintf("#%u INFO %s\r\n", (unsigned)id, hex);
    UART_MachPrintf("#%u OK\r\n", (unsigned)id);
}

static void _do_tag_set(uint16_t id, const char *payload)
{
    uint32_t hexlen = payload ? (uint32_t)strlen(payload) : 0U;
    if (hexlen == 0U || hexlen > 22U || hexlen % 2U != 0U) {
        UART_MachPrintf("#%u ERR len\r\n", (unsigned)id);
        return;
    }
    uint8_t raw[11];
    uint32_t nbytes = hexlen / 2U;
    if (_hex_dec(payload, raw, nbytes) != 0) {
        UART_MachPrintf("#%u ERR hex\r\n", (unsigned)id);
        return;
    }
    memset(g_hw_desc.tag, 0, sizeof(g_hw_desc.tag));
    memcpy(g_hw_desc.tag, raw, nbytes);
    g_hw_desc.tag[sizeof(g_hw_desc.tag) - 1U] = '\0';
    if (HwDesc_Save())
        UART_MachPrintf("#%u OK tag=%s\r\n", (unsigned)id, g_hw_desc.tag);
    else
        UART_MachPrintf("#%u ERR flash\r\n", (unsigned)id);
}

static void _do_uptime_clr(uint16_t id)
{
    HwDesc_ResetUptime();
    UART_MachPrintf("#%u OK\r\n", (unsigned)id);
}

static void _do_hw_save(uint16_t id)
{
    if (HwDesc_Save())
        UART_MachPrintf("#%u OK\r\n", (unsigned)id);
    else
        UART_MachPrintf("#%u ERR flash\r\n", (unsigned)id);
}

static void _do_cfg_read(uint16_t id)
{
    ConfigBlob_t b;
    _build_cfg(&b);
    /* 64 bytes = 128 hex chars + '#id CFG ' prefix → need ~145 chars */
    char hex[129];
    _hex_enc((const uint8_t *)&b, sizeof(b), hex);
    /* split into two UART_MachPrint calls to stay well within PRINTF_BUF */
    UART_MachPrintf("#%u CFG ", (unsigned)id);
    UART_MachPrint(hex);
    UART_MachPrint("\r\n");
    UART_MachPrintf("#%u OK\r\n", (unsigned)id);
}

static void _do_cfg_write(uint16_t id, const char *payload)
{
    /* Length: 64 bytes = 128 hex chars */
    uint32_t hexlen = payload ? (uint32_t)strlen(payload) : 0U;
    if (hexlen != 128U) {
        UART_MachPrintf("#%u ERR len\r\n", (unsigned)id);
        return;
    }

    ConfigBlob_t b;
    if (_hex_dec(payload, (uint8_t *)&b, 64U) != 0) {
        UART_MachPrintf("#%u ERR len\r\n", (unsigned)id);
        return;
    }

    if (b.proto_ver != PROTO_VERSION) {
        UART_MachPrintf("#%u ERR field=proto_ver val=%u\r\n", (unsigned)id, b.proto_ver);
        return;
    }

    uint32_t calc_crc = Proto_CRC32((const uint8_t *)&b, 60U);
    if (calc_crc != b.crc32) {
        UART_MachPrintf("#%u ERR crc\r\n", (unsigned)id);
        return;
    }

/* Validate field ranges — first failure sends ERR and returns */
#define FERR(fname, cond, v) \
    if (!(cond)) { UART_MachPrintf("#%u ERR field=" fname " val=%lu\r\n", (unsigned)id, (uint32_t)(v)); return; }

    FERR("rf_mode",      b.rf_mode <= 3U,                                   b.rf_mode)
    FERR("rf_channel",   b.rf_channel <= 3U,                                b.rf_channel)
    FERR("rf_power",     b.rf_power >= 1U && b.rf_power <= 4U,              b.rf_power)
    FERR("led_mode",     b.led_mode <= 3U,                                  b.led_mode)
    FERR("rf_pulse_ms",  b.rf_pulse_ms >= 5U && b.rf_pulse_ms <= 5000U,    b.rf_pulse_ms)
    FERR("rf_period_ms", b.rf_period_ms >= 100UL && b.rf_period_ms <= 600000UL, b.rf_period_ms)
    FERR("log_mode",      b.log_mode <= 2U,        b.log_mode)
    FERR("log_overflow",  b.log_overflow <= 1U,   b.log_overflow)
    FERR("log_ckp_n",     b.log_ckp_n >= 1U,      b.log_ckp_n)
    FERR("log_ts_source", b.log_ts_source <= 1U,  b.log_ts_source)
    FERR("sched_hours",  b.sched_hours < (1UL << 24U),                      b.sched_hours)
    FERR("sched_days",   b.sched_days  < (1U  << 7U),                       b.sched_days)
    FERR("sched_months", b.sched_months < (1U << 12U),                      b.sched_months)
    FERR("sched_scope",  b.sched_scope <= 1U,                               b.sched_scope)
#undef FERR

    /* All valid — apply atomically */
    uint32_t changed = _apply_cfg(&b);
    UART_MachPrintf("#%u OK changed=%lu\r\n", (unsigned)id, changed);
}

/* ── Force-read all active sensors then build fresh StatusBlob ─────────────── */
uint8_t Proto_FreshStat(uint8_t *out)
{
    /* Temp + VDDA (always available) */
    int32_t t; uint32_t vdda;
    ChipTemp_MeasureNow(&t, &vdda);

    /* Battery (if enabled) */
    if (g_batt_mode != BATT_MODE_OFF) {
        uint32_t mv; int8_t pct;
        BattAdc_MeasureNow(&mv, &pct);
    }

    /* Light (if enabled) */
    if (g_light_mode != LIGHT_MODE_OFF) {
        uint16_t raw; uint32_t lux;
        LightAdc_MeasureNow(&raw, &lux);
    }

    StatusBlob_t b;
    _build_stat(&b);
    memcpy(out, &b, sizeof(b));
    return (uint8_t)sizeof(b);  /* 24 */
}

static void _do_stat(uint16_t id)
{
    StatusBlob_t b;
    _build_stat(&b);
    char hex[49];
    _hex_enc((const uint8_t *)&b, sizeof(b), hex);
    UART_MachPrintf("#%u STAT %s\r\n", (unsigned)id, hex);
    UART_MachPrintf("#%u OK\r\n", (unsigned)id);
}

static void _do_save(uint16_t id)
{
    if (BLE_IsConnected()) {
        /* Defer flash write until after BLE disconnects — CPU2 stall kills BLE.
         * Explicit restart is via OP_REBOOT which force-saves before reset. */
        g_config_save_pending  = 1U;
        g_log_cfg_save_pending = 1U;
        UART_MachPrintf("#%u OK\r\n", (unsigned)id);
        return;
    }
    FlashLog_CommitConfig();   /* flush log config if changed by CFG! */
    if (FlashConfig_Save())
        UART_MachPrintf("#%u OK\r\n", (unsigned)id);
    else
        UART_MachPrintf("#%u ERR flash\r\n", (unsigned)id);
}

static void _do_log(uint16_t id, const char *args)
{
    /* Parse: <offset_dec> <count_dec> */
    char buf[32];
    strncpy(buf, args ? args : "0 0", sizeof(buf) - 1U);
    buf[sizeof(buf) - 1U] = '\0';

    char *p1 = strtok(buf, " \r\n");
    char *p2 = strtok(NULL, " \r\n");
    uint32_t off = p1 ? (uint32_t)strtoul(p1, NULL, 10) : 0U;
    uint32_t cnt = p2 ? (uint32_t)strtoul(p2, NULL, 10) : 0U;

    if (cnt == 0U) cnt = 1U;
    if (cnt > 8U) cnt = 8U;   /* max 8 records — keeps response ≤270 bytes (2 BLE chunks) */

    /* Hex buffer: 8 records × 16 bytes × 2 hex chars = 256 + NUL */
    static char hex[257];
    uint8_t raw[16];
    uint32_t n = 0U;
    hex[0] = '\0';

    for (uint32_t i = 0; i < cnt; i++) {
        FlashLogRecord_t rec;
        if (FlashLog_ReadRecord(off + i, &rec) != 0) break;
        memcpy(raw, &rec, 16U);
        _hex_enc(raw, 16U, hex + n * 32U);
        n++;
    }

    if (n > 0U) {
        UART_MachPrintf("#%u LOG ", (unsigned)id);
        UART_MachPrint(hex);
        UART_MachPrint("\r\n");
    }
    UART_MachPrintf("#%u OK n=%lu\r\n", (unsigned)id, n);
}

static void _do_erase(uint16_t id)
{
    int rc = FlashLog_Clear();
    if (rc == 0)
        UART_MachPrintf("#%u OK\r\n", (unsigned)id);
    else
        UART_MachPrintf("#%u ERR flash=%d\r\n", (unsigned)id, rc);
}

/* TIME! <unix_ts>  — set RTC from Unix timestamp (seconds since 1970-01-01 UTC) */
static void _do_set_time(uint16_t id, const char *payload)
{
    if (!payload || !*payload) {
        UART_MachPrintf("#%u ERR noarg\r\n", (unsigned)id);
        return;
    }
    uint32_t ts = (uint32_t)strtoul(payload, NULL, 10);
    if (ts < 946684800UL) {  /* must be >= 2000-01-01 */
        UART_MachPrintf("#%u ERR range\r\n", (unsigned)id);
        return;
    }

    /* Convert Unix timestamp to Y/M/D h:m:s (valid from 2000-01-01) */
    static const uint16_t s_yday[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    uint32_t days = ts / 86400UL;
    uint32_t sec  = ts % 86400UL;
    uint8_t  hour = (uint8_t)(sec / 3600U);
    uint8_t  min  = (uint8_t)((sec % 3600U) / 60U);
    uint8_t  sec2 = (uint8_t)(sec % 60U);

    /* Days since 1970-01-01 → year */
    uint32_t y4   = days / 1461U;   /* 4-year blocks (3*365+366) */
    uint32_t rem  = days % 1461U;
    uint16_t year = (uint16_t)(1970U + y4 * 4U);
    if (rem >= 366U) { rem -= 366U; year++; }
    if (rem >= 365U) { rem -= 365U; year++; }
    if (rem >= 365U) { rem -= 365U; year++; }
    /* rem = day-of-year (0-based) */
    uint8_t leap = ((year % 4U == 0U) && (year % 100U != 0U || year % 400U == 0U)) ? 1U : 0U;
    uint8_t month = 1U;
    for (uint8_t m = 11U; m >= 1U; m--) {
        uint16_t yd = s_yday[m] + ((m >= 2U && leap) ? 1U : 0U);
        if (rem >= (uint32_t)yd) { month = (uint8_t)(m + 1U); rem -= yd; break; }
        if (m == 1U) { month = 1U; }
    }
    uint8_t day = (uint8_t)(rem + 1U);

    /* Weekday: 1=Mon..7=Sun (HAL RTC convention) */
    /* 1970-01-01 was Thursday = 4 */
    uint8_t wday = (uint8_t)((days + 3U) % 7U) + 1U;  /* 1=Mon(Thu+3=0→1) */

    Power_RTC_SetDateTime(year, month, day, hour, min, sec2, wday);
    UART_MachPrintf("#%u OK ts=%lu\r\n", (unsigned)id, ts);
}

/* ── CMD verb: binary command layer over text protocol ───────────────────────
 * Request:  #<id> CMD <hex_opcode><hex_payload>
 * Response: #<id> RSP <hex_result>[<hex_out_payload>]
 *
 * The first hex byte after CMD is the opcode; the rest is the input payload.
 * The first hex byte of RSP is the CMD_* result code; the rest is out payload.
 */
static void _do_cmd(uint16_t id, const char *payload)
{
    if (!payload || !*payload) {
        UART_MachPrintf("#%u ERR len\r\n", (unsigned)id);
        return;
    }

    uint32_t hexlen = (uint32_t)strlen(payload);
    if (hexlen < 2U || hexlen % 2U != 0U || hexlen > (2U + CMD_MAX_IN_PAYLOAD * 2U)) {
        UART_MachPrintf("#%u ERR len\r\n", (unsigned)id);
        return;
    }

    uint32_t nbytes = hexlen / 2U;
    uint8_t raw[1U + CMD_MAX_IN_PAYLOAD];
    if (_hex_dec(payload, raw, nbytes) != 0) {
        UART_MachPrintf("#%u ERR hex\r\n", (unsigned)id);
        return;
    }

    uint8_t opcode  = raw[0];
    const uint8_t *in  = (nbytes > 1U) ? &raw[1] : NULL;
    uint8_t  in_len = (uint8_t)(nbytes - 1U);

    uint8_t out[CMD_MAX_OUT_PAYLOAD];
    uint8_t out_len = (uint8_t)sizeof(out);

    uint8_t result = CmdLayer_Dispatch(opcode, in, in_len, out, &out_len);

    /* RSP: result(1) + out payload — encode as hex */
    char rsp_hex[2U + CMD_MAX_OUT_PAYLOAD * 2U + 1U];
    uint8_t rsp_raw[1U + CMD_MAX_OUT_PAYLOAD];
    rsp_raw[0] = result;
    if (out_len > 0U) memcpy(&rsp_raw[1], out, out_len);
    _hex_enc(rsp_raw, 1U + out_len, rsp_hex);

    UART_MachPrintf("#%u RSP ", (unsigned)id);
    UART_MachPrint(rsp_hex);
    UART_MachPrint("\r\n");
    UART_MachPrintf("#%u OK\r\n", (unsigned)id);

    if (CmdLayer_IsRebootPending()) {
        HAL_Delay(200U);   /* let host receive RSP and close port cleanly */
        NVIC_SystemReset();
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void Proto_Init(void) { /* reserved */ }

void Proto_HandleLine(const char *line)
{
    /* Format: #<id> <VERB>[ <payload>] */
    if (!line || line[0] != '#') return;

    char tmp[320];
    strncpy(tmp, line + 1, sizeof(tmp) - 1U);  /* skip '#' */
    tmp[sizeof(tmp) - 1U] = '\0';
    /* Strip trailing CR/LF left by line-end detection */
    uint32_t tl = (uint32_t)strlen(tmp);
    while (tl > 0U && (tmp[tl - 1U] == '\r' || tmp[tl - 1U] == '\n'))
        tmp[--tl] = '\0';

    /* Parse id */
    char *sp = tmp;
    while (*sp && *sp != ' ') sp++;
    if (!*sp) { UART_MachPrint("#0 ERR parse\r\n"); return; }
    *sp = '\0';
    uint32_t id32 = (uint32_t)strtoul(tmp, NULL, 10);
    if (id32 < 1U || id32 > 65535U) { UART_MachPrint("#0 ERR id\r\n"); return; }
    uint16_t id = (uint16_t)id32;
    sp++;  /* point to VERB */
    UART_SilentKeepalive();  /* reset 30-s silent-mode TTL on each machine command */

    /* Split verb and optional payload */
    char *verb    = sp;
    char *payload = NULL;
    while (*sp && *sp != ' ') sp++;
    if (*sp == ' ') { *sp = '\0'; payload = sp + 1; }

    /* Dispatch */
    if (!strcmp(verb, "INFO?"))         _do_info(id);
    else if (!strcmp(verb, "CFG?"))     _do_cfg_read(id);
    else if (!strcmp(verb, "CFG!"))     _do_cfg_write(id, payload);
    else if (!strcmp(verb, "STAT?"))    _do_stat(id);
    else if (!strcmp(verb, "SAVE!"))    _do_save(id);
    else if (!strcmp(verb, "LOG?"))     _do_log(id, payload);
    else if (!strcmp(verb, "ERASE!"))   _do_erase(id);
    else if (!strcmp(verb, "TIME!"))    _do_set_time(id, payload);
    else if (!strcmp(verb, "TAG!"))     _do_tag_set(id, payload);
    else if (!strcmp(verb, "UPTIMECLR!")) _do_uptime_clr(id);
    else if (!strcmp(verb, "HWSAVE!"))   _do_hw_save(id);
    else if (!strcmp(verb, "CMD"))       _do_cmd(id, payload);
    else UART_MachPrintf("#%u ERR verb\r\n", (unsigned)id);
}
