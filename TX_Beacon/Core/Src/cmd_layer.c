#include "cmd_layer.h"
#include "app_ble.h"
#include "ble_hci_le.h"
#include "flash_config.h"
#include "flash_log.h"
#include "hw_desc.h"
#include "svc_uart_cmd.h"
#include "svc_events.h"
#include "svc_power.h"
#include "svc_wake.h"
#include "lis2dw12.h"
#include "drv_chip_temp.h"
#include "drv_batt_adc.h"
#include "drv_light_adc.h"
#include "drv_led.h"
#include "drv_rf_tx.h"
#include "drv_uart.h"
#include "proto_uart.h"
#include "proto_structs.h"
#include "stm32wbxx_hal.h"
#include <string.h>
#include <limits.h>

/* ── Module state ─────────────────────────────────────────────────────────── */
static uint8_t s_reboot_pending = 0U;
uint8_t CmdLayer_IsRebootPending(void) { return s_reboot_pending; }

/* ── Staging helpers ─────────────────────────────────────────────────────── */

void CmdLayer_Init(void)
{
    s_reboot_pending = 0U;
    FlashConfig_StagingLoad();
}

void CmdLayer_StagingLoad(void)
{
    FlashConfig_StagingLoad();
}

const uint8_t *CmdLayer_StagingPtr(void)
{
    return (const uint8_t *)&g_cfg_staging;
}

uint16_t CmdLayer_StagingSize(void)
{
    return (uint16_t)sizeof(TxConfigV2_t);
}

/* ── Unix → RTC helper (same algorithm as proto_uart.c _do_set_time) ─────── */
static void _unix_to_rtc(uint32_t ts)
{
    static const uint16_t s_yday[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    uint32_t days = ts / 86400UL;
    uint32_t sec  = ts % 86400UL;
    uint8_t  hour = (uint8_t)(sec / 3600U);
    uint8_t  min  = (uint8_t)((sec % 3600U) / 60U);
    uint8_t  sec2 = (uint8_t)(sec % 60U);

    uint32_t y4   = days / 1461U;
    uint32_t rem  = days % 1461U;
    uint16_t year = (uint16_t)(1970U + y4 * 4U);
    if (rem >= 366U) { rem -= 366U; year++; }
    if (rem >= 365U) { rem -= 365U; year++; }
    if (rem >= 365U) { rem -= 365U; year++; }
    uint8_t leap = ((year % 4U == 0U) && (year % 100U != 0U || year % 400U == 0U)) ? 1U : 0U;
    uint8_t month = 1U;
    for (uint8_t m = 11U; m >= 1U; m--) {
        uint16_t yd = s_yday[m] + ((m >= 2U && leap) ? 1U : 0U);
        if (rem >= (uint32_t)yd) { month = (uint8_t)(m + 1U); rem -= yd; break; }
        if (m == 1U) { month = 1U; }
    }
    uint8_t day  = (uint8_t)(rem + 1U);
    uint8_t wday = (uint8_t)((days + 3U) % 7U) + 1U;
    Power_RTC_SetDateTime(year, month, day, hour, min, sec2, wday);
}

/* ── Little-endian get/put helpers ──────────────────────────────────────── */
static uint32_t _get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t _get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static void _put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void _put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void _put_i16(uint8_t *p, int16_t v)
{
    _put_u16(p, (uint16_t)v);
}
static void _put_i32(uint8_t *p, int32_t v)
{
    _put_u32(p, (uint32_t)v);
}

/* ── Opcode handlers ─────────────────────────────────────────────────────── */

/* 0x00  PING → fw_ver(1) proto_ver(1) uid(4) uptime_s(4) */
static uint8_t _op_ping(uint8_t *out, uint8_t *out_len)
{
    out[0] = FW_VERSION_MAJOR;
    out[1] = PROTO_VERSION;
    _put_u32(&out[2], *(volatile uint32_t *)0x1FFF7590UL);
    _put_u32(&out[6], HAL_GetTick() / 1000U);
    *out_len = 10U;
    return CMD_OK;
}

/* 0x01  TIME_GET → rtc_unix(4) */
static uint8_t _op_time_get(uint8_t *out, uint8_t *out_len)
{
    _put_u32(out, Power_RTC_GetUnix());
    *out_len = 4U;
    return CMD_OK;
}

/* 0x02  TIME_SET  rtc_unix(4) → OK */
static uint8_t _op_time_set(const uint8_t *in, uint8_t in_len,
                             uint8_t *out_len)
{
    if (in_len < 4U) return CMD_ERR_LEN;
    uint32_t ts = _get_u32(in);
    if (ts < 946684800UL) return CMD_ERR_PARAM;  /* < 2000-01-01 */
    _unix_to_rtc(ts);
    *out_len = 0U;
    return CMD_OK;
}

/* 0x06  UART_MODE  mode(1): 0=verbose, 1=silent → OK */
static uint8_t _op_uart_mode(const uint8_t *in, uint8_t in_len,
                              uint8_t *out_len)
{
    *out_len = 0U;
    if (in_len < 1U) return CMD_ERR_LEN;
    UART_SilentSet(in[0] ? 1U : 0U);
    return CMD_OK;
}

/* 0x03  REBOOT  mode(1) → OK  (resets after RSP is sent — see _do_cmd) */
static uint8_t _op_reboot(const uint8_t *in, uint8_t in_len,
                           uint8_t *out_len)
{
    (void)in_len;
    (void)in;   /* bootloader mode (mode[0]) not supported — always cold reset */
    /* Force-save any pending flash writes so new settings survive the boot. */
    if (g_log_cfg_save_pending) {
        g_log_cfg_save_pending = 0U;
        FlashLog_CommitConfig();
    }
    if (g_config_save_pending) {
        g_config_save_pending = 0U;
        FlashConfig_Save();
    }
    s_reboot_pending = 1U;
    *out_len = 0U;
    return CMD_OK;
}

/* 0x10  CONFIG_INFO → total_len(2) version(1) dirty(1) crc32(4) */
static uint8_t _op_config_info(uint8_t *out, uint8_t *out_len)
{
    _put_u16(&out[0], (uint16_t)sizeof(TxConfigV2_t));
    out[2] = FLASH_CFG_VERSION_V2;
    out[3] = g_cfg_staging_dirty;
    _put_u32(&out[4], g_cfg_staging.crc32);
    *out_len = 8U;
    return CMD_OK;
}

/* 0x11  CONFIG_READ  offset(2) len(1) → data */
static uint8_t _op_config_read(const uint8_t *in, uint8_t in_len,
                                uint8_t *out, uint8_t *out_len)
{
    if (in_len < 3U) return CMD_ERR_LEN;
    uint16_t off = _get_u16(in);
    uint8_t  len = in[2];
    if (len == 0U || len > CMD_MAX_OUT_PAYLOAD) return CMD_ERR_PARAM;
    if ((uint32_t)off + len > sizeof(TxConfigV2_t)) return CMD_ERR_PARAM;
    memcpy(out, (const uint8_t *)&g_cfg_staging + off, len);
    *out_len = len;
    return CMD_OK;
}

/* 0x12  CONFIG_WRITE  offset(2) len(1) data[] → OK (RAM only) */
static uint8_t _op_config_write(const uint8_t *in, uint8_t in_len,
                                 uint8_t *out_len)
{
    if (in_len < 4U) return CMD_ERR_LEN;
    uint16_t off = _get_u16(in);
    uint8_t  len = in[2];
    if (len == 0U || in_len < (uint8_t)(3U + len)) return CMD_ERR_LEN;
    if ((uint32_t)off + len > sizeof(TxConfigV2_t)) return CMD_ERR_PARAM;
    /* Protect magic and version fields from accidental overwrite */
    if (off < 5U) return CMD_ERR_PARAM;
    memcpy((uint8_t *)&g_cfg_staging + off, &in[3], len);
    g_cfg_staging_dirty = 1U;
    *out_len = 0U;
    return CMD_OK;
}

/* 0x13  CONFIG_COMMIT → OK */
static uint8_t _op_config_commit(uint8_t *out_len)
{
    *out_len = 0U;
    if (BLE_IsConnected()) {
        /* Defer flash write — CPU2 busy with BLE, erase would cause disconnect.
         * Settings are already in RAM globals; main loop saves after disconnect. */
        g_config_save_pending = 1U;
        return CMD_OK;
    }
    uint8_t ok = FlashConfig_StagingCommit();
    return ok ? CMD_OK : CMD_ERR_FLASH;
}

/* 0x14  CONFIG_RESET  magic(4) → OK */
static uint8_t _op_config_reset(const uint8_t *in, uint8_t in_len,
                                 uint8_t *out_len)
{
    if (in_len < 4U) return CMD_ERR_LEN;
    if (_get_u32(in) != CONFIG_RESET_MAGIC) return CMD_ERR_PARAM;
    /* Rebuild staging from hard-coded defaults by reinitialising globals */
    g_ch_idx         = 0U;
    g_pwr_idx        = 0U;
    g_tx_mode        = TX_MODE_PULSE;
    g_tx_duration_ms = 20U;
    g_tx_period_ms   = 2000U;
    g_temp_mode      = TEMP_MODE_OFF;
    g_temp_period_s  = 60U;
    g_temp_offset_x10 = 0;
    g_batt_mode      = BATT_MODE_PERIODIC;
    g_batt_period_s  = 3600U;
    g_batt_scale_x10 = 20U;
    g_light_mode     = LIGHT_MODE_OFF;
    g_light_period_s = 60U;
    g_sched_en       = 0U;
    g_sched_scope    = 0U;
    g_active_hours_mask  = 0U;
    g_active_days_mask   = 0U;
    g_active_months_mask = 0U;
    g_accel_odr      = 0x01U;
    g_accel_fs       = 0x00U;
    g_accel_mode     = 0x00U;
    g_accel_lp_mode  = 0x00U;
    g_accel_bw       = 0x01U;
    g_accel_i2c_addr = 0x00U;
    g_wake_enable    = 0U;
    g_wake_threshold_mg = 250U;
    g_wake_duration_ms  = 0U;
    g_wake_action    = WAKE_ACTION_WAKE_MCU;
    g_accel_off_x = g_accel_off_y = g_accel_off_z = 0;
    FlashConfig_StagingLoad();
    g_cfg_staging_dirty = 1U;
    *out_len = 0U;
    return CMD_OK;
}

/* 0x20  LOG_INFO → total(4) used(4) free(4) write_addr(4) rec_size(1) fmt_ver(1) */
static uint8_t _op_log_info(uint8_t *out, uint8_t *out_len)
{
    uint32_t total = 0U, free_cnt = 0U, used_b = 0U, free_b = 0U;
    FlashLog_GetStatus(&total, &free_cnt, &used_b, &free_b);
    /* Cap 'readable' to LOG_ENTRIES_MAX: in circular mode total_records can exceed
     * the ring capacity; sending the raw count confuses the download loop. */
    uint32_t readable = (total > (uint32_t)LOG_ENTRIES_MAX) ? (uint32_t)LOG_ENTRIES_MAX : total;
    _put_u32(&out[0],  (uint32_t)LOG_ENTRIES_MAX);
    _put_u32(&out[4],  readable);
    _put_u32(&out[8],  free_cnt);
    _put_u32(&out[12], used_b);
    out[16] = LOG_ENTRY_SIZE;
    out[17] = LOG_FORMAT_VERSION_V2;
    LogConfig_t lc;
    FlashLog_GetConfig(&lc);
    out[18] = lc.ts_source;  /* 0=LOG_TS_BOOT, 1=LOG_TS_RTC (epoch since 2000-01-01) */
    *out_len = 19U;
    return CMD_OK;
}

/* 0x21  LOG_READ  index(4) count(1) → records */
static uint8_t _op_log_read(const uint8_t *in, uint8_t in_len,
                              uint8_t *out, uint8_t *out_len)
{
    if (in_len < 5U) return CMD_ERR_LEN;
    uint32_t idx   = _get_u32(in);
    uint8_t  count = in[4];
    if (count == 0U) return CMD_ERR_PARAM;
    /* Max 7 records per call to stay within CMD_MAX_OUT_PAYLOAD (128) */
    if (count > 7U) count = 7U;

    uint8_t n = 0U;
    for (uint8_t i = 0U; i < count; i++) {
        FlashLogRecord_t rec;
        if (FlashLog_ReadRecord(idx + i, &rec) != 0) break;
        memcpy(&out[n * LOG_ENTRY_SIZE], &rec, LOG_ENTRY_SIZE);
        n++;
    }
    *out_len = (uint8_t)(n * LOG_ENTRY_SIZE);
    return CMD_OK;
}

/* 0x22  LOG_ERASE  magic(4) → OK */
static uint8_t _op_log_erase(const uint8_t *in, uint8_t in_len,
                               uint8_t *out_len)
{
    if (in_len < 4U) return CMD_ERR_LEN;
    if (_get_u32(in) != LOG_ERASE_MAGIC) return CMD_ERR_PARAM;
    *out_len = 0U;
    if (BLE_IsConnected()) {
        /* Defer: flash page erase blocks CPU1 ~25ms/page + 50ms HSEM wait → BLE disconnect.
         * Main loop executes FlashLog_Clear() after BLE session ends. */
        g_log_erase_pending = 1U;
        return CMD_OK;
    }
    int rc = FlashLog_Clear();
    return (rc == 0) ? CMD_OK : CMD_ERR_FLASH;
}

/* 0x23  LOG_MARK  tag(1) → OK */
static uint8_t _op_log_mark(const uint8_t *in, uint8_t in_len,
                              uint8_t *out_len)
{
    if (in_len < 1U) return CMD_ERR_LEN;
    int rc = FlashLog_WriteMark(in[0]);
    *out_len = 0U;
    return (rc == 0) ? CMD_OK : CMD_ERR_FLASH;
}

/* 0x30  SENSOR_LIST → N(1) + N×{id(1) present(1) enabled(1) interval_s(4) has_ext(1)} */
static uint8_t _op_sensor_list(uint8_t *out, uint8_t *out_len)
{
    LogConfig_t lc;
    FlashLog_GetConfig(&lc);

    const uint8_t N = SENSOR_COUNT;
    out[0] = N;
    uint8_t pos = 1U;

    /* Helper: write one 8-byte sensor entry */
#define SENS_ENTRY(id_, present_, enabled_, iv_s_, has_ext_) do { \
    out[pos++] = (id_); \
    out[pos++] = (present_); \
    out[pos++] = (enabled_); \
    _put_u32(&out[pos], (iv_s_)); pos += 4U; \
    out[pos++] = (has_ext_); \
} while (0)

    SENS_ENTRY(SENSOR_ID_TEMP,
               1U,
               (g_temp_mode != TEMP_MODE_OFF) ? 1U : 0U,
               (uint32_t)g_temp_period_s,
               0U);

    SENS_ENTRY(SENSOR_ID_BATT,
               1U,
               (g_batt_mode != BATT_MODE_OFF) ? 1U : 0U,
               (uint32_t)g_batt_period_s,
               0U);

    SENS_ENTRY(SENSOR_ID_LIGHT,
               1U,
               (g_light_mode != LIGHT_MODE_OFF) ? 1U : 0U,
               (uint32_t)g_light_period_s,
               0U);

    SENS_ENTRY(SENSOR_ID_ACCEL,
               LIS2DW12_IsPowered(),
               (lc.active_mask & LOG_MASK_ACCEL) ? 1U : 0U,
               lc.accel_interval_s,
               1U);   /* has_extended_settings */
#undef SENS_ENTRY

    *out_len = pos;
    return CMD_OK;
}

/* 0x31  SENSOR_ENABLE  id(1) enable(1) → OK */
static uint8_t _op_sensor_enable(const uint8_t *in, uint8_t in_len,
                                  uint8_t *out_len)
{
    if (in_len < 2U) return CMD_ERR_LEN;
    uint8_t id  = in[0];
    uint8_t ena = in[1] ? 1U : 0U;

    switch (id) {
    case SENSOR_ID_TEMP:
        g_temp_mode  = ena ? TEMP_MODE_PERIODIC : TEMP_MODE_OFF;
        g_config_save_pending = 1U;
        break;
    case SENSOR_ID_BATT:
        g_batt_mode  = ena ? BATT_MODE_PERIODIC : BATT_MODE_OFF;
        g_config_save_pending = 1U;
        break;
    case SENSOR_ID_LIGHT:
        g_light_mode = ena ? LIGHT_MODE_PERIODIC : LIGHT_MODE_OFF;
        g_config_save_pending = 1U;
        break;
    case SENSOR_ID_ACCEL: {
        LogConfig_t lc;
        FlashLog_GetConfig(&lc);
        if (ena) lc.active_mask |=  LOG_MASK_ACCEL;
        else     lc.active_mask &= (uint8_t)~LOG_MASK_ACCEL;
        FlashLog_SetConfig(&lc);
        if (BLE_IsConnected()) g_log_cfg_save_pending = 1U;
        else                   FlashLog_CommitConfig();
        g_config_save_pending = 1U;
        break;
    }
    default:
        return CMD_ERR_PARAM;
    }
    *out_len = 0U;
    return CMD_OK;
}

/* 0x32  SENSOR_INTERVAL  id(1) interval_s(4) → OK */
static uint8_t _op_sensor_interval(const uint8_t *in, uint8_t in_len,
                                    uint8_t *out_len)
{
    if (in_len < 5U) return CMD_ERR_LEN;
    uint8_t  id  = in[0];
    uint32_t iv  = _get_u32(&in[1]);
    if (iv == 0U || iv > 65535U) return CMD_ERR_PARAM;

    LogConfig_t lc;
    FlashLog_GetConfig(&lc);

    switch (id) {
    case SENSOR_ID_TEMP:
        g_temp_period_s = (iv > 255U) ? 255U : (uint8_t)iv;
        lc.temp_interval_s = (uint16_t)iv;
        FlashLog_SetConfig(&lc);
        g_config_save_pending = 1U;
        break;
    case SENSOR_ID_BATT:
        g_batt_period_s = (iv > 3600U) ? 3600U : (uint16_t)iv;
        lc.battery_interval_s = (uint16_t)iv;
        FlashLog_SetConfig(&lc);
        g_config_save_pending = 1U;
        break;
    case SENSOR_ID_LIGHT:
        g_light_period_s = (iv > 255U) ? 255U : (uint8_t)iv;
        lc.light_interval_s = (uint16_t)iv;
        FlashLog_SetConfig(&lc);
        g_config_save_pending = 1U;
        break;
    case SENSOR_ID_ACCEL:
        lc.accel_interval_s = (uint16_t)iv;
        FlashLog_SetConfig(&lc);
        if (BLE_IsConnected()) g_log_cfg_save_pending = 1U;
        else                   FlashLog_CommitConfig();
        g_config_save_pending = 1U;
        break;
    default:
        return CMD_ERR_PARAM;
    }
    *out_len = 0U;
    return CMD_OK;
}

/* 0x33  SENSOR_READ_NOW  id(1) → raw_i16(2) scaled_i32(4) */
static uint8_t _op_sensor_read_now(const uint8_t *in, uint8_t in_len,
                                    uint8_t *out, uint8_t *out_len)
{
    if (in_len < 1U) return CMD_ERR_LEN;
    uint8_t id = in[0];
    int16_t raw16 = 0;
    int32_t scaled = 0;

    switch (id) {
    case SENSOR_ID_TEMP: {
        int32_t t; uint32_t vdda;
        if (!ChipTemp_MeasureNow(&t, &vdda)) return CMD_ERR_BUSY;
        raw16  = (int16_t)t;
        scaled = t;
        break;
    }
    case SENSOR_ID_BATT: {
        uint32_t mv; int8_t pct;
        if (!BattAdc_MeasureNow(&mv, &pct)) return CMD_ERR_BUSY;
        raw16  = (int16_t)(g_last_batt_raw & 0xFFFFU);
        scaled = (int32_t)mv;
        break;
    }
    case SENSOR_ID_LIGHT: {
        uint16_t raw; uint32_t lux;
        if (!LightAdc_MeasureNow(&raw, &lux)) return CMD_ERR_BUSY;
        raw16  = (int16_t)raw;
        scaled = (int32_t)lux;
        break;
    }
    case SENSOR_ID_ACCEL: {
        if (!LIS2DW12_IsPowered()) return CMD_ERR_STATE;
        int16_t x, y, z;
        if (LIS2DW12_ReadXYZ(&x, &y, &z) != HAL_OK) return CMD_ERR_BUSY;
        _put_i16(&out[0], x);
        _put_i16(&out[2], y);
        _put_i16(&out[4], z);
        *out_len = 6U;
        return CMD_OK;
    }
    default:
        return CMD_ERR_PARAM;
    }

    _put_i16(&out[0], raw16);
    _put_i32(&out[2], scaled);
    *out_len = 6U;
    return CMD_OK;
}

/* 0x34  ACCEL_CFG_GET → odr(1) fs(1) mode(1) lpf(1) */
static uint8_t _op_accel_cfg_get(uint8_t *out, uint8_t *out_len)
{
    out[0] = g_accel_odr;
    out[1] = g_accel_fs;
    out[2] = g_accel_mode;
    out[3] = g_accel_bw;
    *out_len = 4U;
    return CMD_OK;
}

/* 0x35  ACCEL_CFG_SET  odr(1) fs(1) mode(1) lpf(1) → OK */
static uint8_t _op_accel_cfg_set(const uint8_t *in, uint8_t in_len,
                                   uint8_t *out_len)
{
    if (in_len < 4U) return CMD_ERR_LEN;
    if (in[0] > (uint8_t)LIS2DW12_ODR_1600HZ) return CMD_ERR_PARAM;
    if (in[1] > (uint8_t)LIS2DW12_FS_16G)     return CMD_ERR_PARAM;
    if (in[2] > (uint8_t)LIS2DW12_MODE_HP)    return CMD_ERR_PARAM;
    if (in[3] > (uint8_t)LIS2DW12_BW_ODR_20)  return CMD_ERR_PARAM;

    g_accel_odr      = in[0];
    g_accel_fs       = in[1];
    g_accel_mode     = in[2];
    g_accel_bw       = in[3];
    g_config_save_pending = 1U;

    if (LIS2DW12_IsPowered()) {
        LIS2DW12_Config_t cfg = {
            .odr     = (LIS2DW12_ODR_t)g_accel_odr,
            .mode    = (LIS2DW12_Mode_t)g_accel_mode,
            .lp_mode = (LIS2DW12_LPMode_t)g_accel_lp_mode,
            .fs      = (LIS2DW12_FS_t)g_accel_fs,
            .bw      = (LIS2DW12_BW_t)g_accel_bw,
        };
        LIS2DW12_Configure(&cfg);
    }
    *out_len = 0U;
    return CMD_OK;
}

/* 0x36  ACCEL_POWER  on(1) → OK | ERR_STATE if WoM armed and trying to power off */
static uint8_t _op_accel_power(const uint8_t *in, uint8_t in_len,
                                 uint8_t *out_len)
{
    if (in_len < 1U) return CMD_ERR_LEN;
    uint8_t on = in[0] ? 1U : 0U;
    *out_len = 0U;

    if (on) {
        if (LIS2DW12_IsPowered()) return CMD_OK;
        LIS2DW12_Config_t cfg = {
            .odr     = (LIS2DW12_ODR_t)g_accel_odr,
            .mode    = (LIS2DW12_Mode_t)g_accel_mode,
            .lp_mode = (LIS2DW12_LPMode_t)g_accel_lp_mode,
            .fs      = (LIS2DW12_FS_t)g_accel_fs,
            .bw      = (LIS2DW12_BW_t)g_accel_bw,
        };
        return (LIS2DW12_PowerOn(&cfg) == HAL_OK) ? CMD_OK : CMD_ERR_BUSY;
    } else {
        /* PowerOff() returns HAL_ERROR if WoM is armed */
        if (LIS2DW12_PowerOff() != HAL_OK) return CMD_ERR_STATE;
        return CMD_OK;
    }
}

/* 0x37  ACCEL_PROBE → i2c_addr(1) who_am_i(1) ok(1) boot_ms(2) */
static uint8_t _op_accel_probe(uint8_t *out, uint8_t *out_len)
{
    LIS2DW12_ProbeResult_t res = {0};
    LIS2DW12_Probe(&res);
    out[0] = res.i2c_addr;
    out[1] = res.who_am_i;
    out[2] = res.ok;
    _put_u16(&out[3], res.boot_ms);
    if (res.ok) {
        g_accel_i2c_addr = res.i2c_addr;
        g_config_save_pending = 1U;
    }
    *out_len = 5U;
    return CMD_OK;
}

/* 0x40  TX_GET → mode(1) ch(1) pwr(1) pulse_ms(2) period_ms(4) */
static uint8_t _op_tx_get(uint8_t *out, uint8_t *out_len)
{
    out[0] = g_tx_mode;
    out[1] = g_ch_idx;
    out[2] = g_pwr_idx;
    _put_u16(&out[3], (uint16_t)(g_tx_duration_ms > 0xFFFFU ? 0xFFFFU : g_tx_duration_ms));
    _put_u32(&out[5], g_tx_period_ms);
    *out_len = 9U;
    return CMD_OK;
}

/* 0x41  TX_SET  mode(1) ch(1) pwr(1) pulse_ms(2) period_ms(4) → OK */
static uint8_t _op_tx_set(const uint8_t *in, uint8_t in_len,
                            uint8_t *out_len)
{
    if (in_len < 9U) return CMD_ERR_LEN;
    uint8_t  mode     = in[0];
    uint8_t  ch       = in[1];
    uint8_t  pwr      = in[2];
    uint16_t pulse_ms = _get_u16(&in[3]);
    uint32_t period   = _get_u32(&in[5]);

    if (mode > TX_MODE_OFF)        return CMD_ERR_PARAM;
    if (ch   > 3U)                 return CMD_ERR_PARAM;
    if (pwr  > 3U)                 return CMD_ERR_PARAM;
    if (pulse_ms < 5U || pulse_ms > 5000U) return CMD_ERR_PARAM;
    if (period < 100UL || period > 3600000UL) return CMD_ERR_PARAM;

    g_tx_mode        = mode;
    g_ch_idx         = ch;
    g_pwr_idx        = pwr;
    g_tx_duration_ms = pulse_ms;
    g_tx_period_ms   = period;
    RF_SetChannel((RF_Channel_t)ch);
    RF_SetPower((RF_Power_t)(pwr + 1U));
    g_config_save_pending = 1U;
    *out_len = 0U;
    return CMD_OK;
}

/* 0x42  TX_SCHEDULE_GET → en(1) scope(1) hours(4) days(1) months(2) */
static uint8_t _op_tx_sched_get(uint8_t *out, uint8_t *out_len)
{
    out[0] = g_sched_en;
    out[1] = g_sched_scope;
    _put_u32(&out[2], g_active_hours_mask);
    out[6] = g_active_days_mask;
    _put_u16(&out[7], g_active_months_mask);
    *out_len = 9U;
    return CMD_OK;
}

/* 0x43  TX_SCHEDULE_SET  en(1) scope(1) hours(4) days(1) months(2) → OK */
static uint8_t _op_tx_sched_set(const uint8_t *in, uint8_t in_len,
                                  uint8_t *out_len)
{
    if (in_len < 9U) return CMD_ERR_LEN;
    if (in[0] > 1U)                        return CMD_ERR_PARAM;
    if (in[1] > 1U)                        return CMD_ERR_PARAM;
    if (_get_u32(&in[2]) >= (1UL << 24U))  return CMD_ERR_PARAM;
    if (in[6] >= (1U << 7U))              return CMD_ERR_PARAM;
    if (_get_u16(&in[7]) >= (1U << 12U))  return CMD_ERR_PARAM;

    g_sched_en           = in[0];
    g_sched_scope        = in[1];
    g_active_hours_mask  = _get_u32(&in[2]);
    g_active_days_mask   = in[6];
    g_active_months_mask = _get_u16(&in[7]);
    g_config_save_pending = 1U;
    *out_len = 0U;
    return CMD_OK;
}

/* 0x50  WAKE_CFG_GET → en(1) thr_mg(2) dur_ms(2) action(1) */
static uint8_t _op_wake_cfg_get(uint8_t *out, uint8_t *out_len)
{
    LIS2DW12_WakeCfg_t cfg;
    SvcWake_GetConfig(&cfg);
    out[0] = cfg.enable;
    _put_u16(&out[1], cfg.threshold_mg);
    _put_u16(&out[3], cfg.duration_ms);
    out[5] = cfg.action;
    *out_len = 6U;
    return CMD_OK;
}

/* 0x51  WAKE_CFG_SET  en(1) thr_mg(2) dur_ms(2) action(1) → OK | ERR_STATE */
static uint8_t _op_wake_cfg_set(const uint8_t *in, uint8_t in_len,
                                  uint8_t *out_len)
{
    if (in_len < 6U) return CMD_ERR_LEN;
    LIS2DW12_WakeCfg_t cfg;
    cfg.enable       = in[0] ? 1U : 0U;
    cfg.threshold_mg = _get_u16(&in[1]);
    cfg.duration_ms  = _get_u16(&in[3]);
    cfg.action       = in[5];

    if (cfg.threshold_mg == 0U && cfg.enable) return CMD_ERR_PARAM;
    if (cfg.action & ~(WAKE_ACTION_WAKE_MCU | WAKE_ACTION_TX_BURST | WAKE_ACTION_LOG_MARKER))
        return CMD_ERR_PARAM;

    uint8_t res = SvcWake_SetConfig(&cfg);
    if (res == CMD_OK) {
        g_wake_enable       = cfg.enable;
        g_wake_threshold_mg = cfg.threshold_mg;
        g_wake_duration_ms  = cfg.duration_ms;
        g_wake_action       = cfg.action;
        g_config_save_pending = 1U;
    }
    *out_len = 0U;
    return res;
}

/* 0x52  WAKE_STATUS → armed(1) count(4) last_ts(4) */
static uint8_t _op_wake_status(uint8_t *out, uint8_t *out_len)
{
    WakeStatus_t st;
    SvcWake_GetStatus(&st);
    out[0] = st.armed;
    _put_u32(&out[1], st.trigger_count);
    _put_u32(&out[5], st.last_trigger_ts);
    *out_len = 9U;
    return CMD_OK;
}

/* 0x53  WAKE_CLEAR → OK */
static uint8_t _op_wake_clear(uint8_t *out_len)
{
    SvcWake_Clear();
    *out_len = 0U;
    return CMD_OK;
}

/* ── Main dispatch ───────────────────────────────────────────────────────── */
/* ── HwDesc compact blob helpers ────────────────────────────────────────────
 * Blob layout: see cmd_layer.h OP_HWDESC_GET comment (128 bytes total).     */
static void _hwdesc_to_blob(uint8_t *b)
{
    const HwDesc_t *d = &g_hw_desc;
    memset(b, 0, HWDESC_BLOB_SIZE);
    b[0] = d->hw_version;
    b[1] = d->temp_type;
    b[2] = d->light_type;
    b[3] = d->batt_type;
    b[4] = d->accel_type;
    b[5] = d->led_type;
    b[6] = d->tx_channels;
    b[7] = d->tx_pwr_levels;
    _put_u32(&b[8],  d->tx_freq_hz);
    _put_u16(&b[12], d->batt_full_mv);
    _put_u16(&b[14], d->batt_empty_mv);
    memcpy(&b[16], d->light_model, 16U);
    memcpy(&b[32], d->accel_model, 16U);
    memcpy(&b[48], d->tx_type,     16U);
    memcpy(&b[64], d->led_model,   16U);
    memcpy(&b[80], d->comment,     48U);   /* first 48 of 156 */
}

static void _blob_to_hwdesc(const uint8_t *b)
{
    HwDesc_t *d = &g_hw_desc;
    d->hw_version    = b[0];
    d->temp_type     = b[1];
    d->light_type    = b[2];
    d->batt_type     = b[3];
    d->accel_type    = b[4];
    d->led_type      = b[5];
    d->tx_channels   = b[6];
    d->tx_pwr_levels = b[7];
    d->tx_freq_hz    = _get_u32(&b[8]);
    d->batt_full_mv  = _get_u16(&b[12]);
    d->batt_empty_mv = _get_u16(&b[14]);
    memcpy(d->light_model, &b[16], 16U); d->light_model[15] = '\0';
    memcpy(d->accel_model, &b[32], 16U); d->accel_model[15] = '\0';
    memcpy(d->tx_type,     &b[48], 16U); d->tx_type[15]     = '\0';
    memcpy(d->led_model,   &b[64], 16U); d->led_model[15]   = '\0';
    memcpy(d->comment,     &b[80], 48U); d->comment[47]     = '\0';
    /* tag and uptime counters are NOT written from binary protocol */
}

/* 0x60  HWDESC_GET → 128-byte blob */
static uint8_t _op_hwdesc_get(uint8_t *out, uint8_t *out_len)
{
    _hwdesc_to_blob(out);
    *out_len = HWDESC_BLOB_SIZE;
    return CMD_OK;
}

/* 0x61  HWDESC_SET  128-byte blob → OK  (RAM only, no flash write) */
static uint8_t _op_hwdesc_set(const uint8_t *in, uint8_t in_len,
                               uint8_t *out_len)
{
    if (in_len < HWDESC_BLOB_SIZE) return CMD_ERR_LEN;
    _blob_to_hwdesc(in);
    *out_len = 0U;
    UART_Print("[HWDESC] fields updated (RAM) — use HWDESC_COMMIT to save\r\n");
    return CMD_OK;
}

/* 0x62  HWDESC_COMMIT — validate + write to flash page 59 */
static uint8_t _op_hwdesc_commit(uint8_t *out_len)
{
    *out_len = 0U;
    if (BLE_IsConnected()) {
        g_hwdesc_save_pending = 1U;
        return CMD_OK;
    }
    if (!HwDesc_Save()) {
        UART_Print("[HWDESC] COMMIT: flash write failed\r\n");
        return CMD_ERR_FLASH;
    }
    UART_Print("[HWDESC] COMMIT: saved to flash\r\n");
    return CMD_OK;
}

/* 0x70  BLE_GET → op_mode(1) tx_pwr(1) iv_s(2) dur_s(2) adv_ms(2) nm(1) name(12) */
static uint8_t _op_ble_get(uint8_t *out, uint8_t *out_len)
{
    out[0] = g_ble_op_mode;
    out[1] = g_ble_tx_power;
    _put_u16(&out[2], g_ble_interval_s);
    _put_u16(&out[4], g_ble_duration_sec);
    _put_u16(&out[6], g_ble_adv_interval_ms);
    out[8] = g_ble_name_mode;
    (void)memcpy(&out[9], g_ble_name, 12U);
    out[21] = g_ble_led_mode;
    *out_len = BLE_CMD_PAYLOAD_SIZE;
    return CMD_OK;
}

/* 0x71  BLE_SET  op_mode(1) tx_pwr(1) iv_s(2) dur_s(2) adv_ms(2) nm(1) name(12) → OK */
static uint8_t _op_ble_set(const uint8_t *in, uint8_t in_len, uint8_t *out_len)
{
    if (in_len < BLE_CMD_PAYLOAD_SIZE) return CMD_ERR_LEN;
    uint8_t  op_mode = in[0];
    uint8_t  tx_pwr  = in[1];
    uint16_t iv_s    = _get_u16(&in[2]);
    uint16_t dur_s   = _get_u16(&in[4]);
    uint16_t adv_ms  = _get_u16(&in[6]);
    uint8_t  nm      = in[8];

    if (op_mode > 0x07U)              return CMD_ERR_PARAM;  /* 3 bits: OFF/CONT/SCHED/GEKON */
    if (tx_pwr  > 31U)                return CMD_ERR_PARAM;
    if (nm > 1U)                      return CMD_ERR_PARAM;
    if (adv_ms > 0U && adv_ms < 100U) return CMD_ERR_PARAM;

    /* GEKON (double-press to open BLE) is always on unless BLE is fully off */
    if (op_mode != BLE_OP_OFF) op_mode |= BLE_OP_GEKON;
    g_ble_op_mode         = op_mode;
    g_ble_tx_power        = tx_pwr;
    g_ble_interval_s      = iv_s;
    g_ble_duration_sec    = dur_s;
    g_ble_adv_interval_ms = adv_ms ? adv_ms : 1000U;
    g_ble_name_mode       = nm;
    (void)memcpy(g_ble_name, &in[9], 12U);
    g_ble_name[11]        = '\0';
    g_ble_led_mode        = (in_len >= 22U && in[21] <= 2U) ? in[21] : 0U;
    /* Defer save to main loop (Flash_CPU2IsIdle guard). Immediate FlashConfig_Save()
     * here stalls CPU2 during flash erase → BLE supervision timeout → disconnect. */
    g_config_save_pending = 1U;
    *out_len = 0U;
    return CMD_OK;
}

/* 0x72  BLE_RSSI → rssi(1, int8 dBm); ERR_STATE if not connected */
static uint8_t _op_ble_rssi(uint8_t *out, uint8_t *out_len)
{
    if (!BLE_IsConnected()) return CMD_ERR_STATE;
    uint8_t rssi_u8 = 0U;
    tBleStatus ret = hci_read_rssi(BLE_GetConnHandle(), &rssi_u8);
    if (ret != BLE_STATUS_SUCCESS) return CMD_ERR_BUSY;
    out[0]  = rssi_u8;   /* int8 cast on GUI side */
    *out_len = 1U;
    return CMD_OK;
}

/* 0x80  EVT_GET → events blob (EVT_BLOB_SIZE = 112 bytes) */
static uint8_t _op_evt_get(uint8_t *out, uint8_t *out_len)
{
    svc_events_get_blob(out);
    *out_len = (uint8_t)EVT_BLOB_SIZE;
    return CMD_OK;
}

/* 0x81  EVT_SET  blob[112] → OK */
static uint8_t _op_evt_set(const uint8_t *in, uint8_t in_len, uint8_t *out_len)
{
    *out_len = 0U;
    if (in_len < (uint8_t)EVT_BLOB_SIZE) return CMD_ERR_LEN;
    return svc_events_set_blob(in) ? CMD_OK : CMD_ERR_FLASH;
}

/* 0x82  EVT_CLR  idx(1): 0-3 or 0xFF=all → OK */
static uint8_t _op_evt_clr(const uint8_t *in, uint8_t in_len, uint8_t *out_len)
{
    *out_len = 0U;
    if (in_len < 1U) return CMD_ERR_LEN;
    return svc_events_clear(in[0]) ? CMD_OK : CMD_ERR_PARAM;
}

/* 0x73  MEASURE_ALL → stat(24) accel_xyz(6, int16_le×3); force-reads all sensors */
static uint8_t _op_measure_all(uint8_t *out, uint8_t *out_len)
{
    /* Force-read temp, battery, light; build fresh STAT blob */
    uint8_t n = Proto_FreshStat(out);   /* 24 bytes */

    /* Accelerometer XYZ (optional — skip gracefully if off) */
    if (LIS2DW12_IsPowered()) {
        int16_t x = 0, y = 0, z = 0;
        if (LIS2DW12_ReadXYZ(&x, &y, &z) == HAL_OK) {
            out[n]   = (uint8_t)(x & 0xFF); out[n+1] = (uint8_t)(x >> 8);
            out[n+2] = (uint8_t)(y & 0xFF); out[n+3] = (uint8_t)(y >> 8);
            out[n+4] = (uint8_t)(z & 0xFF); out[n+5] = (uint8_t)(z >> 8);
            n += 6U;
        }
    }
    *out_len = n;
    return CMD_OK;
}

uint8_t CmdLayer_Dispatch(uint8_t opcode,
                           const uint8_t *in,  uint8_t in_len,
                           uint8_t       *out, uint8_t *out_len)
{
    *out_len = 0U;
    uint8_t res;

    switch (opcode) {
    /* System */
    case OP_PING:           res = _op_ping(out, out_len); break;
    case OP_TIME_GET:       res = _op_time_get(out, out_len); break;
    case OP_TIME_SET:       res = _op_time_set(in, in_len, out_len); break;
    case OP_REBOOT:         res = _op_reboot(in, in_len, out_len); break;
    case OP_UART_MODE:      res = _op_uart_mode(in, in_len, out_len); break;
    /* Config */
    case OP_CONFIG_INFO:    res = _op_config_info(out, out_len); break;
    case OP_CONFIG_READ:    res = _op_config_read(in, in_len, out, out_len); break;
    case OP_CONFIG_WRITE:   res = _op_config_write(in, in_len, out_len); break;
    case OP_CONFIG_COMMIT:  res = _op_config_commit(out_len); break;
    case OP_CONFIG_RESET:   res = _op_config_reset(in, in_len, out_len); break;
    /* Log */
    case OP_LOG_INFO:       res = _op_log_info(out, out_len); break;
    case OP_LOG_READ:       res = _op_log_read(in, in_len, out, out_len); break;
    case OP_LOG_ERASE:      res = _op_log_erase(in, in_len, out_len); break;
    case OP_LOG_MARK:       res = _op_log_mark(in, in_len, out_len); break;
    /* Sensors */
    case OP_SENSOR_LIST:    res = _op_sensor_list(out, out_len); break;
    case OP_SENSOR_ENABLE:  res = _op_sensor_enable(in, in_len, out_len); break;
    case OP_SENSOR_INTERVAL: res = _op_sensor_interval(in, in_len, out_len); break;
    case OP_SENSOR_READ_NOW: res = _op_sensor_read_now(in, in_len, out, out_len); break;
    case OP_ACCEL_CFG_GET:  res = _op_accel_cfg_get(out, out_len); break;
    case OP_ACCEL_CFG_SET:  res = _op_accel_cfg_set(in, in_len, out_len); break;
    case OP_ACCEL_POWER:    res = _op_accel_power(in, in_len, out_len); break;
    case OP_ACCEL_PROBE:    res = _op_accel_probe(out, out_len); break;
    /* Transmitter */
    case OP_TX_GET:         res = _op_tx_get(out, out_len); break;
    case OP_TX_SET:         res = _op_tx_set(in, in_len, out_len); break;
    case OP_TX_SCHEDULE_GET: res = _op_tx_sched_get(out, out_len); break;
    case OP_TX_SCHEDULE_SET: res = _op_tx_sched_set(in, in_len, out_len); break;
    /* Wake */
    case OP_WAKE_CFG_GET:   res = _op_wake_cfg_get(out, out_len); break;
    case OP_WAKE_CFG_SET:   res = _op_wake_cfg_set(in, in_len, out_len); break;
    case OP_WAKE_STATUS:    res = _op_wake_status(out, out_len); break;
    case OP_WAKE_CLEAR:     res = _op_wake_clear(out_len); break;
    /* Hardware descriptor */
    case OP_HWDESC_GET:     res = _op_hwdesc_get(out, out_len); break;
    case OP_HWDESC_SET:     res = _op_hwdesc_set(in, in_len, out_len); break;
    case OP_HWDESC_COMMIT:  res = _op_hwdesc_commit(out_len); break;
    /* BLE */
    case OP_BLE_GET:        res = _op_ble_get(out, out_len); break;
    case OP_BLE_SET:        res = _op_ble_set(in, in_len, out_len); break;
    case OP_BLE_RSSI:       res = _op_ble_rssi(out, out_len); break;
    case OP_MEASURE_ALL:    res = _op_measure_all(out, out_len); break;
    /* Events */
    case OP_EVT_GET:        res = _op_evt_get(out, out_len); break;
    case OP_EVT_SET:        res = _op_evt_set(in, in_len, out_len); break;
    case OP_EVT_CLR:        res = _op_evt_clr(in, in_len, out_len); break;

    default:
        *out_len = 0U;
        UART_Printf("[CMD] unsupported opcode=0x%02X\r\n", opcode);
        return CMD_ERR_UNSUPPORTED;
    }

    return res;
}
