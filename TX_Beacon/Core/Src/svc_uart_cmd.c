#include "svc_uart_cmd.h"
#include "proto_uart.h"
#include "flash_config.h"
#include "flash_log.h"
#include "hw_desc.h"
#include "drv_uart.h"
#include "drv_rf_tx.h"
#include "drv_led.h"
#include "drv_gekon.h"
#include "drv_chip_temp.h"
#include "drv_batt_adc.h"
#include "drv_light_adc.h"
#include "svc_power.h"
#include "app_ble.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* 320 bytes: #65535 CMD 61<256 hex chars>\r\n = 271 chars max */
#define CMD_BUF  320U

static char     s_buf[CMD_BUF];
static uint16_t s_len = 0;

static const char        *s_ch_name[]   = { "CH0", "CH1", "CH2", "CH3" };
static const char        *s_pwr_name[]  = { "PWR1", "PWR2", "PWR3", "PWR4" };
static const char        *s_led_name[]  = { "off", "on", "heartbeat", "tx" };
static const RF_Channel_t s_channels[]  = { RF_CH0, RF_CH1, RF_CH2, RF_CH3 };
static const RF_Power_t   s_powers[]    = { RF_PWR1, RF_PWR2, RF_PWR3, RF_PWR4 };

uint8_t          g_tx_paused            = 0U;
uint8_t          g_rtc_live             = 0U;
volatile uint8_t g_config_save_pending  = 0U;
volatile uint8_t g_cfg_changed          = 0U;
volatile uint8_t g_log_cfg_save_pending = 0U;
volatile uint8_t g_hwdesc_save_pending  = 0U;
volatile uint8_t g_reboot_after_save    = 0U;
volatile uint8_t g_log_erase_pending    = 0U;

/* Schedule masks -- 0 = dimension not filtered (always active) */
uint32_t g_active_hours_mask  = 0U;
uint8_t  g_active_days_mask   = 0U;
uint16_t g_active_months_mask = 0U;

/* Schedule enable + scope — set by binary protocol CFG! or text 'sched scope' */
uint8_t g_sched_en    = 0U;   /* 0 = always active; 1 = mask-gated */
uint8_t g_sched_scope = 0U;   /* 0 = TX only;       1 = TX + logging */

/* ── Schedule check ──────────────────────────────────────────────────────── */
uint8_t Schedule_IsActive(void)
{
    /* Schedule disabled OR no masks configured → always active */
    if (!g_sched_en) return 1U;
    if (!g_active_hours_mask && !g_active_days_mask && !g_active_months_mask)
        return 1U;
    if (!Power_RTC_IsSet()) return 1U;

    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    Power_RTC_GetDateTime(&t, &d);

    if (g_active_hours_mask  && !(g_active_hours_mask  & (1UL << t.Hours)))           return 0U;
    if (g_active_days_mask   && !(g_active_days_mask   & (1U  << (d.WeekDay - 1U))))  return 0U;
    if (g_active_months_mask && !(g_active_months_mask & (1U  << (d.Month   - 1U))))  return 0U;
    return 1U;
}

uint32_t Schedule_SecsToNextSlot(void)
{
    if (!g_active_hours_mask || !Power_RTC_IsSet()) return 3600U;

    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    Power_RTC_GetDateTime(&t, &d);

    uint32_t secs_in_hour = (uint32_t)t.Minutes * 60U + t.Seconds;
    for (uint32_t delta = 1U; delta <= 24U; delta++) {
        uint8_t h = (uint8_t)((t.Hours + delta) % 24U);
        if (g_active_hours_mask & (1UL << h))
            return (delta - 1U) * 3600U + (3600U - secs_in_hour);
    }
    return 3600U;
}

/* ── Print helpers for sched show ────────────────────────────────────────── */
static void _print_hours(uint32_t mask)
{
    if (!mask) { UART_Print("all\r\n"); return; }
    for (int h = 0; h < 24; h++)
        if (mask & (1UL << h)) UART_Printf("%d ", h);
    UART_Print("\r\n");
}
static void _print_days(uint8_t mask)
{
    static const char *dn[] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    if (!mask) { UART_Print("all\r\n"); return; }
    for (int i = 0; i < 7; i++)
        if (mask & (1U << i)) UART_Printf("%s ", dn[i]);
    UART_Print("\r\n");
}
static void _print_months(uint16_t mask)
{
    static const char *mn[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                "Jul","Aug","Sep","Oct","Nov","Dec"};
    if (!mask) { UART_Print("all\r\n"); return; }
    for (int i = 0; i < 12; i++)
        if (mask & (1U << i)) UART_Printf("%s ", mn[i]);
    UART_Print("\r\n");
}

/* Compute weekday from date (Tomohiko Sakamoto). Returns 0=Sun..6=Sat. */
static uint8_t _weekday(uint16_t y, uint8_t m, uint8_t day)
{
    static const uint8_t t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    uint16_t yr = y;
    if (m < 3U) yr--;
    return (uint8_t)((yr + yr/4U - yr/100U + yr/400U + t[m-1U] + day) % 7U);
}

/* Parse "1.2" / "-2.5" / "3" → ×10 integer. Returns INT16_MIN on error. */
static int16_t _parse_x10(const char *s)
{
    if (!s || !*s) return INT16_MIN;
    int sign = 1;
    if      (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    if (!(*s >= '0' && *s <= '9')) return INT16_MIN;
    int16_t whole = 0, frac = 0;
    while (*s >= '0' && *s <= '9') whole = (int16_t)(whole * 10 + (*s++ - '0'));
    if (*s == '.' || *s == ',') {
        s++;
        if (*s >= '0' && *s <= '9') frac = (int16_t)(*s - '0');
    }
    return (int16_t)(sign * (whole * 10 + frac));
}

static void _exec(char *line)
{
    char *cmd = strtok(line, " \r\n");
    if (!cmd || cmd[0] == '\0') return;

    /* ── help ──────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "help")) {
        UART_Print(
            "Commands:\r\n"
            "  status               current state + schedule\r\n"
            "  mode pulse|cont|eco|off\r\n"
            "  tx on|off\r\n"
            "  ch <0-3>  pwr <1-4>  pulse <ms>  period <ms>\r\n"
            "  led off|on|heartbeat|tx\r\n"
            "  temp [mode off|periodic|tx|read]\r\n"
            "  temp period <1-255>     periodic interval in seconds\r\n"
            "  temp offset <-9.9..9.9> calibration offset degC  e.g. -2.5\r\n"
            "  batt [mode off|periodic] [period <1-255>] [scale <x.xxx>] [read]\r\n"
            "  light [mode off|periodic] [period <1-255>] [read]\r\n"
            "  save  reset  sleep  regs\r\n"
            "  rtc get\r\n"
            "  rtc set YYYY-MM-DD HH:MM:SS\r\n"
            "  rtc live on|off         send RTC every 1s while UART active\r\n"
            "  sched show\r\n"
            "  sched hours H H...   (0-23, empty=all)\r\n"
            "  sched days  D D...   (1=Mon..7=Sun, empty=all)\r\n"
            "  sched months M M...  (1=Jan..12=Dec, empty=all)\r\n"
            "  sched off            disable schedule (TX always)\r\n"
            "  hwdesc show|save|clear\r\n"
            "  hwdesc ver <1-255>\r\n"
            "  hwdesc temp  none|crystal|ntc|stts22h|lis2dw12\r\n"
            "  hwdesc light none|<model>\r\n"
            "  hwdesc batt  none|adc <full_mv> <empty_mv>|fuel\r\n"
            "  hwdesc accel none|ism330|lis2dw12|<model>\r\n"
            "  hwdesc led   none|led <model>|rgb <model>\r\n"
            "  hwdesc tx    <freq_hz> <ch> <pwr_lvls> <type>\r\n"
            "  hwdesc comment <free text>\r\n"
            "  frd <page>  ferase <page>  fwrite <page> <hex>  fmon [ms]\r\n"
        );
    }

    /* ── status ─────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "status")) {
        const char *mode_str = (g_tx_mode == TX_MODE_CONT) ? "cont" :
                               (g_tx_mode == TX_MODE_ECO)  ? "eco"  :
                               (g_tx_mode == TX_MODE_OFF)  ? "off"  : "pulse";
        UART_Printf("  mode      = %s\r\n", mode_str);
        UART_Printf("  ch        = %s\r\n", s_ch_name[g_ch_idx & 3]);
        UART_Printf("  pwr       = %s\r\n", s_pwr_name[g_pwr_idx & 3]);
        UART_Printf("  pulse_ms  = %lu\r\n", g_tx_duration_ms);
        UART_Printf("  period_ms = %lu\r\n", g_tx_period_ms);
        UART_Print("  ---\r\n");
        UART_Printf("  led_mode = %s\r\n", s_led_name[(uint8_t)LED_GetMode()]);
        UART_Print("  ---\r\n");
        {
            const char *tm = (g_temp_mode == TEMP_MODE_PERIODIC)  ? "periodic" :
                             (g_temp_mode == TEMP_MODE_BEFORE_TX)  ? "tx"       : "off";
            UART_Printf("  temp_mode   = %s\r\n", tm);
            UART_Printf("  temp_period = %u\r\n", g_temp_period_s);
            {
                int8_t ov = g_temp_offset_x10;
                int oa = ov < 0 ? -ov : ov;
                UART_Printf("  temp_offset = %s%d.%01d\r\n",
                            ov < 0 ? "-" : "", oa / 10, oa % 10);
            }
        }
        if (g_last_temp_x10 != INT32_MIN) {
            int32_t whole = g_last_temp_x10 / 10;
            int32_t frac  = g_last_temp_x10 >= 0 ? g_last_temp_x10 % 10 : -(g_last_temp_x10 % 10);
            UART_Printf("  temp_c   = %ld.%01ld\r\n", (long)whole, (long)frac);
            UART_Printf("  vdda_mv  = %lu\r\n", (unsigned long)g_last_vdda_mV);
        } else {
            UART_Print("  temp_c   = --\r\n");
            UART_Print("  vdda_mv  = --\r\n");
        }
        UART_Printf("  rtc_live    = %s\r\n", g_rtc_live ? "on" : "off");
        UART_Print("  ---\r\n");
        {
            const char *bm = (g_batt_mode == BATT_MODE_PERIODIC) ? "periodic" : "off";
            UART_Printf("  batt_mode   = %s\r\n", bm);
            UART_Printf("  batt_period = %u\r\n", g_batt_period_s);
            UART_Printf("  batt_scale  = %u.%01u\r\n",
                        g_batt_scale_x10 / 10U, g_batt_scale_x10 % 10U);
            if (g_last_batt_pct >= 0)
                UART_Printf("  batt_mv     = %lu\r\n  batt_pct    = %d\r\n",
                            (unsigned long)g_last_batt_mV, (int)g_last_batt_pct);
            else
                UART_Print("  batt_mv     = --\r\n  batt_pct    = --\r\n");
        }
        UART_Print("  ---\r\n");
        {
            const char *lm = (g_light_mode == LIGHT_MODE_PERIODIC) ? "periodic" : "off";
            UART_Printf("  light_mode   = %s\r\n", lm);
            UART_Printf("  light_period = %u\r\n", g_light_period_s);
            if (g_light_ever_read)
                UART_Printf("  light_raw    = %u\r\n  light_lux    = %lu\r\n",
                            g_last_light_raw, (unsigned long)g_last_light_lux);
            else
                UART_Print("  light_raw    = --\r\n  light_lux    = --\r\n");
        }
        UART_Print("  ---\r\n");
        Power_RTC_PrintDateTime();
        UART_Print("  hours  : "); _print_hours(g_active_hours_mask);
        UART_Print("  days   : "); _print_days(g_active_days_mask);
        UART_Print("  months : "); _print_months(g_active_months_mask);
        UART_Printf("  active now: %s\r\n", Schedule_IsActive() ? "YES" : "NO");
        UART_Print("  ---\r\n");
        {
            LogConfig_t lc; FlashLog_GetConfig(&lc);
            UART_Printf("[LOG] mask=0x%02X  oflow=%u  mode=%u  ckp=%u  ts=%u\r\n",
                        lc.active_mask, lc.overflow_mode, lc.write_mode, lc.checkpoint_n, lc.ts_source);
            UART_Printf("[LOG] temp=%us  light=%us  bat=%us\r\n",
                        lc.temp_interval_s, lc.light_interval_s, lc.battery_interval_s);
        }
    }

    /* ── mode ───────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "mode")) {
        char *arg = strtok(NULL, " \r\n");
        if (arg && !strcmp(arg, "cont")) {
            g_tx_paused = 0U;
            g_tx_mode = TX_MODE_CONT;
            g_config_save_pending = 1U;
            UART_Print("[MODE] cont — TX until 'tx off' or short press\r\n");
        } else if (arg && !strcmp(arg, "pulse")) {
            if (RF_IsEnabled()) RF_Stop();
            g_tx_paused = 0U;
            g_tx_mode = TX_MODE_PULSE;
            g_config_save_pending = 1U;
            UART_Printf("[MODE] pulse: %lums TX, %lums pause\r\n", g_tx_duration_ms, g_tx_period_ms);
        } else if (arg && !strcmp(arg, "eco")) {
            if (RF_IsEnabled()) RF_Stop();
            g_tx_paused = 0U;
            g_tx_mode = TX_MODE_ECO;
            g_config_save_pending = 1U;
            UART_Printf("[MODE] eco: %lums TX (MCU sleeps), %lums pause\r\n",
                        g_tx_duration_ms, g_tx_period_ms);
        } else if (arg && !strcmp(arg, "off")) {
            if (RF_IsEnabled()) RF_Stop();
            g_tx_paused = 0U;
            g_tx_mode = TX_MODE_OFF;
            g_config_save_pending = 1U;
            UART_Print("[MODE] off — TX disabled, saving to flash\r\n");
        } else {
            UART_Print("usage: mode pulse|cont|eco|off\r\n");
        }
    }

    /* ── tx ─────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "tx")) {
        char *arg = strtok(NULL, " \r\n");
        if (arg && !strcmp(arg, "off")) {
            RF_Stop();
            g_tx_paused = 1U;
            UART_Print("[TX] OFF — paused ('tx on' to resume)\r\n");
        } else if (arg && !strcmp(arg, "on")) {
            g_tx_paused = 0U;
            RF_Start(s_channels[g_ch_idx & 3], s_powers[g_pwr_idx & 3]);
            UART_Printf("[TX] ON %s %s\r\n", s_ch_name[g_ch_idx & 3], s_pwr_name[g_pwr_idx & 3]);
        } else {
            UART_Print("usage: tx on|off\r\n");
        }
    }

    /* ── ch ─────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "ch")) {
        char *arg = strtok(NULL, " \r\n");
        if (arg) {
            int v = atoi(arg);
            if (v >= 0 && v <= 3) {
                g_ch_idx = (uint8_t)v;
                g_config_save_pending = 1U;
                UART_Printf("[CH] %s\r\n", s_ch_name[v]);
                if (g_tx_mode == TX_MODE_CONT && RF_IsEnabled()) {
                    RF_Stop();
                    RF_Start(s_channels[g_ch_idx], s_powers[g_pwr_idx & 3]);
                    UART_Printf("[CONT] live: %s %s\r\n", s_ch_name[g_ch_idx], s_pwr_name[g_pwr_idx & 3]);
                }
            } else UART_Print("range: 0-3\r\n");
        }
    }

    /* ── pwr ─────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "pwr")) {
        char *arg = strtok(NULL, " \r\n");
        if (arg) {
            int v = atoi(arg);
            if (v >= 1 && v <= 4) {
                g_pwr_idx = (uint8_t)(v - 1);
                g_config_save_pending = 1U;
                UART_Printf("[PWR] %s\r\n", s_pwr_name[v - 1]);
                if (g_tx_mode == TX_MODE_CONT && RF_IsEnabled()) {
                    RF_Stop();
                    RF_Start(s_channels[g_ch_idx & 3], s_powers[g_pwr_idx]);
                    UART_Printf("[CONT] live: %s %s\r\n", s_ch_name[g_ch_idx & 3], s_pwr_name[g_pwr_idx]);
                }
            } else UART_Print("range: 1-4\r\n");
        }
    }

    /* ── pulse ───────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "pulse")) {
        char *arg = strtok(NULL, " \r\n");
        if (arg) {
            uint32_t v = (uint32_t)atoi(arg);
            if (v >= 1 && v <= 60000) { g_tx_duration_ms = v; g_config_save_pending = 1U; UART_Printf("[PULSE] %lu ms\r\n", v); }
            else UART_Print("range: 1-60000 ms\r\n");
        }
    }

    /* ── period ──────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "period")) {
        char *arg = strtok(NULL, " \r\n");
        if (arg) {
            uint32_t v = (uint32_t)atoi(arg);
            if (v >= 100U && v <= 3600000U) { g_tx_period_ms = v; g_config_save_pending = 1U; UART_Printf("[PERIOD] %lu ms\r\n", v); }
            else UART_Print("range: 100-3600000 ms\r\n");
        }
    }

    /* ── on ──────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "on")) {
        char *arg = strtok(NULL, " \r\n");
        if (arg) {
            uint32_t v = (uint32_t)atoi(arg);
            if (v >= 1 && v <= 3600) { g_cont_on_s = v; UART_Printf("[ON] %lu s\r\n", v); }
            else UART_Print("range: 1-3600 s\r\n");
        }
    }

    /* ── led ────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "led")) {
        char *arg = strtok(NULL, " \r\n");
        if (!arg) { UART_Print("usage: led off|on|heartbeat|tx\r\n"); return; }
        LED_Mode_t m;
        if      (!strcmp(arg, "off"))       m = LED_OFF;
        else if (!strcmp(arg, "on"))        m = LED_ON;
        else if (!strcmp(arg, "heartbeat")) m = LED_HEARTBEAT;
        else if (!strcmp(arg, "tx"))        m = LED_TX;
        else { UART_Print("usage: led off|on|heartbeat|tx\r\n"); return; }
        g_led_mode = (uint8_t)m;
        LED_SetMode(m);
        g_config_save_pending = 1U;
        UART_Printf("[LED] mode=%s\r\n", arg);
    }

    /* ── temp ──────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "temp")) {
        char *arg = strtok(NULL, " \r\n");
        if (!arg || !strcmp(arg, "show")) {
            const char *tm = (g_temp_mode == TEMP_MODE_PERIODIC)  ? "periodic" :
                             (g_temp_mode == TEMP_MODE_BEFORE_TX)  ? "tx"       : "off";
            UART_Printf("[TEMP] mode=%s\r\n", tm);
            if (g_last_temp_x10 != INT32_MIN) {
                int32_t whole = g_last_temp_x10 / 10;
                int32_t frac  = g_last_temp_x10 >= 0 ?
                                g_last_temp_x10 % 10 : -(g_last_temp_x10 % 10);
                UART_Printf("[TEMP] last: chip=%ld.%01ldC VDDA=%lumV\r\n",
                            (long)whole, (long)frac, (unsigned long)g_last_vdda_mV);
            } else {
                UART_Print("[TEMP] last: no reading yet\r\n");
            }
        } else if (!strcmp(arg, "mode")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2 && !strcmp(a2, "off")) {
                g_temp_mode = TEMP_MODE_OFF;
                g_config_save_pending = 1U;
                UART_Print("[TEMP] mode=off\r\n");
            } else if (a2 && !strcmp(a2, "periodic")) {
                g_temp_mode = TEMP_MODE_PERIODIC;
                g_config_save_pending = 1U;
                UART_Print("[TEMP] mode=periodic\r\n");
            } else if (a2 && !strcmp(a2, "tx")) {
                g_temp_mode = TEMP_MODE_BEFORE_TX;
                g_config_save_pending = 1U;
                UART_Print("[TEMP] mode=tx\r\n");
            } else {
                UART_Print("usage: temp mode off|periodic|tx\r\n");
            }
        } else if (!strcmp(arg, "period")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                int v = atoi(a2);
                if (v >= 1 && v <= 255) {
                    g_temp_period_s = (uint8_t)v;
                    g_config_save_pending = 1U;
                    UART_Printf("[TEMP] period=%us\r\n", (unsigned)g_temp_period_s);
                } else {
                    UART_Print("range: 1-255 s\r\n");
                }
            } else {
                UART_Printf("[TEMP] period=%us\r\n", (unsigned)g_temp_period_s);
            }
        } else if (!strcmp(arg, "offset")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                int16_t v = _parse_x10(a2);
                if (v != INT16_MIN && v >= -99 && v <= 99) {
                    g_temp_offset_x10 = (int8_t)v;
                    g_config_save_pending = 1U;
                    int8_t ov = g_temp_offset_x10;
                    int oa = ov < 0 ? -ov : ov;
                    UART_Printf("[TEMP] offset=%s%d.%01d degC\r\n",
                                ov < 0 ? "-" : "", oa / 10, oa % 10);
                } else {
                    UART_Print("range: -9.9..+9.9 degC  e.g. temp offset -2.5\r\n");
                }
            } else {
                int8_t ov = g_temp_offset_x10;
                int oa = ov < 0 ? -ov : ov;
                UART_Printf("[TEMP] offset=%s%d.%01d degC\r\n",
                            ov < 0 ? "-" : "", oa / 10, oa % 10);
            }
        } else if (!strcmp(arg, "read")) {
            UART_Print("[TEMP] measuring...\r\n");
            int32_t  t; uint32_t v;
            if (ChipTemp_MeasureNow(&t, &v)) {
                int32_t calibrated = t + (int32_t)g_temp_offset_x10;
                g_last_temp_x10 = calibrated;
                g_last_vdda_mV  = v;
                int32_t whole = calibrated / 10;
                int32_t frac  = calibrated >= 0 ? calibrated % 10 : -(calibrated % 10);
                UART_Printf("[TEMP] chip=%ld.%01ldC VDDA=%lumV\r\n",
                            (long)whole, (long)frac, (unsigned long)v);
            } else {
                UART_Print("[TEMP] ADC error\r\n");
            }
        } else {
            UART_Print("usage: temp [mode off|periodic|tx] [period <s>] [offset <n>] [read]\r\n");
        }
    }

    /* ── rtc ────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "rtc")) {
        char *sub = strtok(NULL, " \r\n");
        if (!sub || !strcmp(sub, "get")) {
            Power_RTC_PrintDateTime();
        } else if (!strcmp(sub, "live")) {
            char *a = strtok(NULL, " \r\n");
            if (a && !strcmp(a, "on")) {
                g_rtc_live = 1U;
                g_config_save_pending = 1U;
                UART_Print("[RTC] live=on — sending date/time every 1s\r\n");
            } else if (a && !strcmp(a, "off")) {
                g_rtc_live = 0U;
                g_config_save_pending = 1U;
                UART_Print("[RTC] live=off\r\n");
            } else {
                UART_Printf("[RTC] live=%s\r\n", g_rtc_live ? "on" : "off");
                UART_Print("usage: rtc live on|off\r\n");
            }
        } else if (!strcmp(sub, "set")) {
            /* rtc set YYYY-MM-DD HH:MM:SS */
            char *ds = strtok(NULL, " \r\n");
            char *ts = strtok(NULL, " \r\n");
            if (!ds || !ts) { UART_Print("usage: rtc set YYYY-MM-DD HH:MM:SS\r\n"); return; }

            /* parse date */
            char db[12]; uint8_t n = 0;
            while (ds[n] && n < 11U) { db[n] = ds[n]; n++; } db[n] = '\0';
            char *p = db;
            uint16_t yr  = (uint16_t)strtoul(p, &p, 10); if (*p) p++;
            uint8_t  mo  = (uint8_t) strtoul(p, &p, 10); if (*p) p++;
            uint8_t  dy  = (uint8_t) strtoul(p, NULL, 10);

            /* parse time */
            char tb[12]; n = 0;
            while (ts[n] && n < 11U) { tb[n] = ts[n]; n++; } tb[n] = '\0';
            p = tb;
            uint8_t hr  = (uint8_t)strtoul(p, &p, 10); if (*p) p++;
            uint8_t mi  = (uint8_t)strtoul(p, &p, 10); if (*p) p++;
            uint8_t sec = (uint8_t)strtoul(p, NULL, 10);

            if (yr < 2024U || yr > 2099U || mo < 1U || mo > 12U ||
                dy < 1U || dy > 31U || hr > 23U || mi > 59U || sec > 59U) {
                UART_Print("[RTC] invalid date/time values\r\n"); return;
            }

            /* compute weekday (0=Sun..6=Sat), convert to HAL (1=Mon..7=Sun) */
            uint8_t wd0  = _weekday(yr, mo, dy);       /* 0=Sun */
            uint8_t wday = (wd0 == 0U) ? 7U : wd0;     /* HAL: 7=Sun, 1=Mon */

            Power_RTC_SetDateTime(yr, mo, dy, hr, mi, sec, wday);
            Power_RTC_PrintDateTime();
        } else {
            UART_Print("usage: rtc get | rtc set YYYY-MM-DD HH:MM:SS | rtc live on|off\r\n");
        }
    }

    /* ── sched ───────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "sched")) {
        char *sub = strtok(NULL, " \r\n");
        if (!sub || !strcmp(sub, "show")) {
            Power_RTC_PrintDateTime();
            UART_Printf("  enabled: %s\r\n", g_sched_en ? "yes" : "no (TX always)");
            UART_Printf("  scope  : %s\r\n", g_sched_scope ? "tx+log" : "tx-only");
            UART_Print("  hours  : "); _print_hours(g_active_hours_mask);
            UART_Print("  days   : "); _print_days(g_active_days_mask);
            UART_Print("  months : "); _print_months(g_active_months_mask);
            UART_Printf("  active now: %s\r\n", Schedule_IsActive() ? "YES" : "NO");
            if (!Schedule_IsActive()) {
                uint32_t ns = Schedule_SecsToNextSlot();
                UART_Printf("  next slot in: %lu min\r\n", ns / 60U);
            }
        } else if (!strcmp(sub, "off")) {
            g_active_hours_mask  = 0U;
            g_active_days_mask   = 0U;
            g_active_months_mask = 0U;
            g_sched_en           = 0U;
            g_config_save_pending = 1U;
            UART_Print("[SCHED] cleared — TX always active\r\n");
        } else if (!strcmp(sub, "scope")) {
            char *a = strtok(NULL, " \r\n");
            if (a && (!strcmp(a, "0") || !strcmp(a, "1"))) {
                g_sched_scope = (uint8_t)atoi(a);
                g_config_save_pending = 1U;
                UART_Printf("[SCHED] scope: %s\r\n",
                            g_sched_scope ? "tx+log" : "tx-only");
            } else {
                UART_Print("usage: sched scope 0|1\r\n");
            }
        } else if (!strcmp(sub, "hours")) {
            uint32_t mask = 0U;
            char *a;
            while ((a = strtok(NULL, " \r\n")) != NULL) {
                int h = atoi(a);
                if (h >= 0 && h <= 23) mask |= (1UL << h);
            }
            g_active_hours_mask = mask;
            g_sched_en = (mask || g_active_days_mask || g_active_months_mask) ? 1U : 0U;
            g_config_save_pending = 1U;
            UART_Print("[SCHED] hours: "); _print_hours(mask);
        } else if (!strcmp(sub, "days")) {
            uint8_t mask = 0U;
            char *a;
            while ((a = strtok(NULL, " \r\n")) != NULL) {
                int d = atoi(a);
                if (d >= 1 && d <= 7) mask |= (uint8_t)(1U << (d - 1));
            }
            g_active_days_mask = mask;
            g_sched_en = (g_active_hours_mask || mask || g_active_months_mask) ? 1U : 0U;
            g_config_save_pending = 1U;
            UART_Print("[SCHED] days: "); _print_days(mask);
        } else if (!strcmp(sub, "months")) {
            uint16_t mask = 0U;
            char *a;
            while ((a = strtok(NULL, " \r\n")) != NULL) {
                int m = atoi(a);
                if (m >= 1 && m <= 12) mask |= (uint16_t)(1U << (m - 1));
            }
            g_active_months_mask = mask;
            g_sched_en = (g_active_hours_mask || g_active_days_mask || mask) ? 1U : 0U;
            g_config_save_pending = 1U;
            UART_Print("[SCHED] months: "); _print_months(mask);
        } else {
            UART_Print("usage: sched show|off|scope 0|1|hours H...|days D...|months M...\r\n");
        }
    }

    /* ── batt ──────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "batt")) {
        char *arg = strtok(NULL, " \r\n");
        if (!arg || !strcmp(arg, "show")) {
            const char *bm = (g_batt_mode == BATT_MODE_PERIODIC) ? "periodic" : "off";
            UART_Printf("[BATT] mode=%s period=%us scale=%u.%01u\r\n",
                        bm, g_batt_period_s,
                        g_batt_scale_x10 / 10U, g_batt_scale_x10 % 10U);
            if (g_last_batt_pct >= 0)
                UART_Printf("[BATT] Battery: %lumV %d%% raw=%lu vref=%lu\r\n",
                            (unsigned long)g_last_batt_mV, (int)g_last_batt_pct,
                            (unsigned long)g_last_batt_raw, (unsigned long)g_last_vref_raw);
            else
                UART_Print("[BATT] no reading yet\r\n");
        } else if (!strcmp(arg, "mode")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2 && !strcmp(a2, "off")) {
                g_batt_mode = BATT_MODE_OFF;
                g_config_save_pending = 1U;
                UART_Print("[BATT] mode=off\r\n");
            } else if (a2 && !strcmp(a2, "periodic")) {
                g_batt_mode = BATT_MODE_PERIODIC;
                g_config_save_pending = 1U;
                UART_Print("[BATT] mode=periodic\r\n");
            } else {
                UART_Print("usage: batt mode off|periodic\r\n");
            }
        } else if (!strcmp(arg, "period")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                int v = atoi(a2);
                if (v >= 1 && v <= 3600) {
                    g_batt_period_s = (uint16_t)v;
                    g_config_save_pending = 1U;
                    UART_Printf("[BATT] period=%us\r\n", (unsigned)g_batt_period_s);
                } else UART_Print("range: 1-3600 s\r\n");
            }
        } else if (!strcmp(arg, "scale")) {
            /* parse e.g. "2.0" or "1.5" → ×10 stored as uint8 */
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                uint32_t whole = 0U, frac = 0U;
                char *p = a2;
                while (*p >= '0' && *p <= '9') whole = whole * 10U + (uint32_t)(*p++ - '0');
                if ((*p == '.' || *p == ',') && p[1] >= '0' && p[1] <= '9')
                    frac = (uint32_t)(p[1] - '0');
                uint32_t sv = whole * 10U + frac;
                if (sv >= 1U && sv <= 250U) {
                    g_batt_scale_x10 = (uint8_t)sv;
                    g_config_save_pending = 1U;
                    UART_Printf("[BATT] scale=%u.%01u\r\n", sv / 10U, sv % 10U);
                } else UART_Print("range: 0.1 – 25.0\r\n");
            }
        } else if (!strcmp(arg, "read")) {
            UART_Print("[BATT] measuring...\r\n");
            uint32_t mv; int8_t pct;
            if (BattAdc_MeasureNow(&mv, &pct))
                UART_Printf("[BATT] Battery: %lumV %d%% raw=%lu vref=%lu\r\n",
                            (unsigned long)mv, (int)pct,
                            (unsigned long)g_last_batt_raw, (unsigned long)g_last_vref_raw);
            else
                UART_Print("[BATT] ADC error\r\n");
        } else {
            UART_Print("usage: batt [mode off|periodic] [period <s>] [scale <x.xxx>] [read]\r\n");
        }
    }

    /* ── light ─────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "light")) {
        char *arg = strtok(NULL, " \r\n");
        if (!arg || !strcmp(arg, "show")) {
            const char *lm = (g_light_mode == LIGHT_MODE_PERIODIC) ? "periodic" : "off";
            UART_Printf("[LIGHT] mode=%s period=%us\r\n", lm, g_light_period_s);
            if (g_light_ever_read)
                UART_Printf("[LIGHT] Light: %u (raw) ~%lu lux\r\n",
                            g_last_light_raw, (unsigned long)g_last_light_lux);
            else
                UART_Print("[LIGHT] no reading yet\r\n");
        } else if (!strcmp(arg, "mode")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2 && !strcmp(a2, "off")) {
                g_light_mode = LIGHT_MODE_OFF;
                g_config_save_pending = 1U;
                UART_Print("[LIGHT] mode=off\r\n");
            } else if (a2 && !strcmp(a2, "periodic")) {
                g_light_mode = LIGHT_MODE_PERIODIC;
                g_config_save_pending = 1U;
                UART_Print("[LIGHT] mode=periodic\r\n");
            } else {
                UART_Print("usage: light mode off|periodic\r\n");
            }
        } else if (!strcmp(arg, "period")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                int v = atoi(a2);
                if (v >= 1 && v <= 255) {
                    g_light_period_s = (uint8_t)v;
                    g_config_save_pending = 1U;
                    UART_Printf("[LIGHT] period=%us\r\n", (unsigned)g_light_period_s);
                } else UART_Print("range: 1-255 s\r\n");
            }
        } else if (!strcmp(arg, "read")) {
            UART_Print("[LIGHT] measuring...\r\n");
            uint16_t raw; uint32_t lux;
            if (LightAdc_MeasureNow(&raw, &lux))
                UART_Printf("[LIGHT] Light: %u (raw) ~%lu lux\r\n",
                            (unsigned)raw, (unsigned long)lux);
            else
                UART_Print("[LIGHT] ADC error\r\n");
        } else {
            UART_Print("usage: light [mode off|periodic] [period <s>] [read]\r\n");
        }
    }

    /* ── save ───────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "save")) {
        UART_Print("[CFG] saving to flash...\r\n");
        g_config_save_pending = 0U;   /* suppress duplicate auto-save */
        if (FlashConfig_Save())
            UART_Print("[CFG] saved OK\r\n");
        else
            UART_Print("[CFG] save FAILED!\r\n");
    }

    /* ── reset ──────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "reset")) {
        UART_Print("[RESET] rebooting...\r\n");
        UART_CheckIdle();
        HAL_Delay(100);
        NVIC_SystemReset();
    }

    /* ── sleep ───────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "sleep")) {
        UART_Print("[CMD] Shutdown in 1s...\r\n");
        UART_CheckIdle();
        LED_Blink(5, 50, 50);
        HAL_Delay(1000);
        GEKON_ClearPending();
        Power_EnterShutdown();
    }

    /* ── regs ────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "regs")) {
        Power_PrintDiag();
    }

    /* ── frd <page> ─── read flash page (first 128 bytes) ───────────── */
    else if (!strcmp(cmd, "frd")) {
        char *a1 = strtok(NULL, " \r\n");
        if (!a1) { UART_Print("usage: frd <page>\r\n"); return; }
        uint32_t pg   = (uint32_t)strtoul(a1, NULL, 0);
        uint32_t addr = 0x08000000UL + pg * 2048UL;   /* STM32WB1M: 2KB pages */
        UART_Printf("[FRD] page=%lu addr=0x%08lX\r\n", pg, addr);
        for (uint32_t i = 0; i < 32U; i++) {
            uint32_t val = *(volatile uint32_t *)(addr + i * 4U);
            if ((i & 3U) == 0U)
                UART_Printf("  +%03lu:", i * 4U);
            UART_Printf(" %08lX", val);
            if ((i & 3U) == 3U)
                UART_Print("\r\n");
        }
    }

    /* ── ferase <page> ─── erase flash page ─────────────────────────── */
    else if (!strcmp(cmd, "ferase")) {
        char *a1 = strtok(NULL, " \r\n");
        if (!a1) { UART_Print("usage: ferase <page>\r\n"); return; }
        uint32_t pg = (uint32_t)strtoul(a1, NULL, 0);
        /* Guard: only allow data pages 59-67 (hwdesc, config, log header, log data).
         * Pages 0-58 = firmware; pages 68+ = BLE stack — erasing either bricks the device. */
        if (pg < 59U || pg > 67U) {
            UART_Printf("[FERASE] REFUSED: page %lu outside safe range 59-67\r\n", pg);
            return;
        }
        if (Flash_ErasePage(pg))
            UART_Printf("[FERASE] page %lu erased OK\r\n", pg);
        else
            UART_Printf("[FERASE] page %lu FAILED\r\n", pg);
    }

    /* ── fmon [ms] ─── monitor flash SR for CFGBSY/PESD/BSY pulses ──── */
    else if (!strcmp(cmd, "fmon")) {
        char *a1 = strtok(NULL, " \r\n");
        uint32_t dur = a1 ? (uint32_t)strtoul(a1, NULL, 0) : 5000U;
        Flash_Monitor(dur);
    }

    /* ── fwrite <page> <hex32> ─── write 8 bytes at page start ─────── */
    else if (!strcmp(cmd, "fwrite")) {
        char *a1 = strtok(NULL, " \r\n");
        char *a2 = strtok(NULL, " \r\n");
        if (!a1) { UART_Print("usage: fwrite <page> <hex32>\r\n"); return; }
        uint32_t pg   = (uint32_t)strtoul(a1, NULL, 0);
        if (pg < 59U || pg > 67U) {
            UART_Printf("[FWRITE] REFUSED: page %lu outside safe range 59-67\r\n", pg);
            return;
        }
        uint32_t val  = a2 ? (uint32_t)strtoul(a2, NULL, 16) : 0xDEADBEEFUL;
        uint32_t addr = 0x08000000UL + pg * 2048UL;   /* STM32WB1M: 2KB pages */
        uint64_t dw   = ((uint64_t)val << 32) | val;
        UART_Printf("[FWRITE] page=%lu addr=0x%08lX data=0x%08lX%08lX\r\n",
                    pg, addr, val, val);
        if (Flash_WriteDoubleWord(addr, dw))
            UART_Printf("[FWRITE] OK: 0x%08lX\r\n", *(volatile uint32_t *)addr);
        else
            UART_Printf("[FWRITE] FAILED\r\n");
    }

    /* ── hwdesc ─────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "hwdesc")) {
        char *sub = strtok(NULL, " \r\n");
        if (!sub || !strcmp(sub, "show")) {
            HwDesc_Print();
        }
        else if (!strcmp(sub, "save")) {
            UART_Print("[HWDESC] saving to flash...\r\n");
            if (HwDesc_Save()) UART_Print("[HWDESC] saved OK\r\n");
            else               UART_Print("[HWDESC] FAILED!\r\n");
        }
        else if (!strcmp(sub, "clear")) {
            HwDesc_SetDefaults();
            UART_Print("[HWDESC] cleared to defaults  (use 'hwdesc save' to persist)\r\n");
        }
        else if (!strcmp(sub, "ver")) {
            char *a = strtok(NULL, " \r\n");
            if (a) {
                int v = atoi(a);
                if (v >= 1 && v <= 255) {
                    g_hw_desc.hw_version = (uint8_t)v;
                    UART_Printf("[HWDESC] ver=%u\r\n", g_hw_desc.hw_version);
                } else UART_Print("range: 1-255\r\n");
            }
        }
        else if (!strcmp(sub, "temp")) {
            char *a = strtok(NULL, " \r\n");
            if (!a) { UART_Print("usage: hwdesc temp none|crystal|ntc|stts22h|lis2dw12\r\n"); return; }
            if      (!strcmp(a, "none"))     g_hw_desc.temp_type = HW_TEMP_NONE;
            else if (!strcmp(a, "crystal"))  g_hw_desc.temp_type = HW_TEMP_CRYSTAL;
            else if (!strcmp(a, "ntc"))      g_hw_desc.temp_type = HW_TEMP_NTC;
            else if (!strcmp(a, "stts22h"))  g_hw_desc.temp_type = HW_TEMP_STTS22H;
            else if (!strcmp(a, "lis2dw12")) g_hw_desc.temp_type = HW_TEMP_LIS2DW12;
            else { UART_Print("usage: hwdesc temp none|crystal|ntc|stts22h|lis2dw12\r\n"); return; }
            UART_Printf("[HWDESC] temp=%s\r\n", a);
        }
        else if (!strcmp(sub, "light")) {
            char *a = strtok(NULL, " \r\n");
            if (!a) { UART_Print("usage: hwdesc light none|<model>\r\n"); return; }
            if (!strcmp(a, "none")) {
                g_hw_desc.light_type    = HW_LIGHT_NONE;
                g_hw_desc.light_model[0] = '\0';
                UART_Print("[HWDESC] light=none\r\n");
            } else {
                g_hw_desc.light_type = HW_LIGHT_PRESENT;
                strncpy(g_hw_desc.light_model, a, sizeof(g_hw_desc.light_model) - 1U);
                g_hw_desc.light_model[sizeof(g_hw_desc.light_model) - 1U] = '\0';
                UART_Printf("[HWDESC] light=present (%s)\r\n", g_hw_desc.light_model);
            }
        }
        else if (!strcmp(sub, "batt")) {
            char *a = strtok(NULL, " \r\n");
            if (!a) { UART_Print("usage: hwdesc batt none|adc <full_mv> <empty_mv>|fuel\r\n"); return; }
            if (!strcmp(a, "none")) {
                g_hw_desc.batt_type = HW_BATT_NONE;
                UART_Print("[HWDESC] batt=none\r\n");
            } else if (!strcmp(a, "adc")) {
                g_hw_desc.batt_type = HW_BATT_ADC;
                char *s1 = strtok(NULL, " \r\n");
                char *s2 = strtok(NULL, " \r\n");
                if (s1) g_hw_desc.batt_full_mv  = (uint16_t)atoi(s1);
                if (s2) g_hw_desc.batt_empty_mv = (uint16_t)atoi(s2);
                UART_Printf("[HWDESC] batt=ADC  full=%umV  empty=%umV\r\n",
                            g_hw_desc.batt_full_mv, g_hw_desc.batt_empty_mv);
            } else if (!strcmp(a, "fuel")) {
                g_hw_desc.batt_type = HW_BATT_FUELGAUGE;
                UART_Print("[HWDESC] batt=fuel_gauge\r\n");
            } else {
                UART_Print("usage: hwdesc batt none|adc <full_mv> <empty_mv>|fuel\r\n");
            }
        }
        else if (!strcmp(sub, "accel")) {
            char *a = strtok(NULL, " \r\n");
            if (!a) { UART_Print("usage: hwdesc accel none|ism330|lis2dw12|<model>\r\n"); return; }
            if (!strcmp(a, "none")) {
                g_hw_desc.accel_type    = HW_ACCEL_NONE;
                g_hw_desc.accel_model[0] = '\0';
                UART_Print("[HWDESC] accel=none\r\n");
            } else if (!strcmp(a, "ism330")) {
                g_hw_desc.accel_type    = HW_ACCEL_ISM330;
                g_hw_desc.accel_model[0] = '\0';
                UART_Print("[HWDESC] accel=ISM330DHCX\r\n");
            } else if (!strcmp(a, "lis2dw12")) {
                g_hw_desc.accel_type    = HW_ACCEL_LIS2DW12;
                g_hw_desc.accel_model[0] = '\0';
                UART_Print("[HWDESC] accel=LIS2DW12\r\n");
            } else {
                g_hw_desc.accel_type = HW_ACCEL_OTHER;
                strncpy(g_hw_desc.accel_model, a, sizeof(g_hw_desc.accel_model) - 1U);
                g_hw_desc.accel_model[sizeof(g_hw_desc.accel_model) - 1U] = '\0';
                UART_Printf("[HWDESC] accel=other (%s)\r\n", g_hw_desc.accel_model);
            }
        }
        else if (!strcmp(sub, "led")) {
            char *a = strtok(NULL, " \r\n");
            if (!a) { UART_Print("usage: hwdesc led none|led <model>|rgb <model>\r\n"); return; }
            if (!strcmp(a, "none")) {
                g_hw_desc.led_type    = HW_LED_NONE;
                g_hw_desc.led_model[0] = '\0';
                UART_Print("[HWDESC] led=none\r\n");
            } else {
                g_hw_desc.led_type = (!strcmp(a, "rgb")) ? HW_LED_RGB : HW_LED_SINGLE;
                char *b = strtok(NULL, " \r\n");
                if (b) {
                    strncpy(g_hw_desc.led_model, b, sizeof(g_hw_desc.led_model) - 1U);
                    g_hw_desc.led_model[sizeof(g_hw_desc.led_model) - 1U] = '\0';
                } else {
                    g_hw_desc.led_model[0] = '\0';
                }
                UART_Printf("[HWDESC] led=%s (%s)\r\n",
                            (g_hw_desc.led_type == HW_LED_RGB) ? "RGB_LED" : "LED",
                            g_hw_desc.led_model[0] ? g_hw_desc.led_model : "?");
            }
        }
        else if (!strcmp(sub, "tx")) {
            /* hwdesc tx <freq_hz> <channels> <pwr_levels> <type> */
            char *a1 = strtok(NULL, " \r\n");
            char *a2 = strtok(NULL, " \r\n");
            char *a3 = strtok(NULL, " \r\n");
            char *a4 = strtok(NULL, " \r\n");
            if (a1) g_hw_desc.tx_freq_hz    = (uint32_t)strtoul(a1, NULL, 10);
            if (a2) {
                int v = atoi(a2);
                if (v >= 1 && v <= 255) g_hw_desc.tx_channels = (uint8_t)v;
            }
            if (a3) {
                int v = atoi(a3);
                if (v >= 1 && v <= 255) g_hw_desc.tx_pwr_levels = (uint8_t)v;
            }
            if (a4) {
                strncpy(g_hw_desc.tx_type, a4, sizeof(g_hw_desc.tx_type) - 1U);
                g_hw_desc.tx_type[sizeof(g_hw_desc.tx_type) - 1U] = '\0';
            }
            UART_Printf("[HWDESC] tx %lu Hz  ch=%u  pwr=%u  type=%s\r\n",
                        g_hw_desc.tx_freq_hz, g_hw_desc.tx_channels,
                        g_hw_desc.tx_pwr_levels, g_hw_desc.tx_type);
        }
        else if (!strcmp(sub, "comment")) {
            /* collect rest of line including spaces */
            char *rest = strtok(NULL, "\r\n");
            if (rest) {
                while (*rest == ' ') rest++;   /* skip leading space */
                uint32_t len = (uint32_t)strlen(rest);
                if (len >= sizeof(g_hw_desc.comment)) {
                    UART_Printf("[HWDESC] WARNING: comment too long (%lu chars), "
                                "truncated to %u chars\r\n",
                                len, (uint32_t)sizeof(g_hw_desc.comment) - 1U);
                }
                strncpy(g_hw_desc.comment, rest, sizeof(g_hw_desc.comment) - 1U);
                g_hw_desc.comment[sizeof(g_hw_desc.comment) - 1U] = '\0';
                UART_Printf("[HWDESC] comment=%s\r\n", g_hw_desc.comment);
            } else {
                g_hw_desc.comment[0] = '\0';
                UART_Print("[HWDESC] comment cleared\r\n");
            }
        }
        else if (!strcmp(sub, "tag")) {
            char *rest = strtok(NULL, "\r\n");
            if (rest) {
                while (*rest == ' ') rest++;
                strncpy(g_hw_desc.tag, rest, sizeof(g_hw_desc.tag) - 1U);
                g_hw_desc.tag[sizeof(g_hw_desc.tag) - 1U] = '\0';
                if (HwDesc_Save())
                    UART_Printf("[HWDESC] tag=%s  saved\r\n", g_hw_desc.tag);
                else
                    UART_Print("[HWDESC] tag set but SAVE FAILED!\r\n");
            } else {
                UART_Printf("[HWDESC] tag=%s\r\n", g_hw_desc.tag[0] ? g_hw_desc.tag : "(none)");
            }
        }
        else {
            UART_Print("usage: hwdesc show|save|clear|ver|temp|light|batt|accel|led|tx|comment|tag\r\n");
        }
    }

    /* ── uptime ──────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "uptime")) {
        char *sub = strtok(NULL, " \r\n");
        if (!sub || !strcmp(sub, "show")) {
            uint32_t a = HwDesc_GetTotalActiveH();
            uint32_t s = HwDesc_GetTotalStop1H();
            uint32_t d = HwDesc_GetTotalShutdownH();
            UART_Printf("[UPTIME] active   = %lu h (%lu d %lu h)\r\n", a, a/24U, a%24U);
            UART_Printf("[UPTIME] stop1    = %lu h (%lu d %lu h)\r\n", s, s/24U, s%24U);
            UART_Printf("[UPTIME] shutdown = %lu h (%lu d %lu h)\r\n", d, d/24U, d%24U);
            UART_Printf("[UPTIME] total    = %lu h\r\n", a + s + d);
        } else if (!strcmp(sub, "reset")) {
            HwDesc_ResetUptime();
        } else {
            UART_Print("usage: uptime show|reset\r\n");
        }
    }

    /* ── log ──────────────────────────────────────────────────────────── */
    else if (!strcmp(cmd, "log")) {
        char *arg = strtok(NULL, " \r\n");
        if (!arg || !strcmp(arg, "info")) {
            FlashLog_PrintStatus();
        } else if (!strcmp(arg, "calc")) {
            FlashLog_PrintCalc();
        } else if (!strcmp(arg, "write")) {
            if (FlashLog_WriteNow() == 0)
                UART_Print("[LOG] written\r\n");
            else
                UART_Print("[LOG] nothing to write / error\r\n");
        } else if (!strcmp(arg, "pages")) {
            UART_Printf("[LOG] Config: pg%u @ 0x%08lX\r\n",
                        FLASH_CONFIG_PAGE, (uint32_t)FLASH_CONFIG_ADDR);
            UART_Printf("[LOG] Header: pg%u @ 0x%08lX\r\n",
                        LOG_HEADER_PAGE, (uint32_t)LOG_HEADER_ADDR);
            UART_Printf("[LOG] Data:   pg%u-%u @ 0x%08lX-0x%08lX\r\n",
                        LOG_DATA_START_PAGE, LOG_DATA_END_PAGE,
                        (uint32_t)LOG_DATA_START, (uint32_t)LOG_DATA_END);
            UART_Printf("[LOG] BLE:    pg%u+ (protected)\r\n", BLE_STACK_FIRST_PAGE);
        } else if (!strcmp(arg, "dump")) {
            FlashLog_DumpCSV(0U, 0U);
        } else if (!strcmp(arg, "read")) {
            char *a2 = strtok(NULL, " \r\n");
            char *a3 = strtok(NULL, " \r\n");
            if (!a2) { UART_Print("usage: log read <N> [count]\r\n"); }
            else if (a3) FlashLog_DumpCSV((uint32_t)strtoul(a2, NULL, 0),
                                           (uint32_t)strtoul(a3, NULL, 0));
            else         FlashLog_PrintRecord((uint32_t)strtoul(a2, NULL, 0));
        } else if (!strcmp(arg, "clear")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2 && !strcmp(a2, "yes")) {
                FlashLog_Clear();
            } else {
                UART_Print("[LOG] Type 'log clear yes' to confirm\r\n");
            }
        } else if (!strcmp(arg, "get")) {
            LogConfig_t c; FlashLog_GetConfig(&c);
            UART_Printf("[LOG] mask=0x%02X  oflow=%u  mode=%u  ckp=%u  ts=%u\r\n",
                        c.active_mask, c.overflow_mode, c.write_mode, c.checkpoint_n, c.ts_source);
            UART_Printf("[LOG] temp=%us  light=%us  bat=%us\r\n",
                        c.temp_interval_s, c.light_interval_s, c.battery_interval_s);
            UART_Printf("[LOG] dt=%u  dl=%u  db=%u\r\n",
                        c.temp_delta_01c, c.light_delta_pct, c.battery_delta_pct);
        } else if (!strcmp(arg, "mask")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                LogConfig_t c; FlashLog_GetConfig(&c);
                c.active_mask = (uint8_t)strtoul(a2, NULL, 16);
                FlashLog_SetConfig(&c);
                UART_Printf("[LOG] mask=0x%02X\r\n", c.active_mask);
            }
        } else if (!strcmp(arg, "temp")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                LogConfig_t c; FlashLog_GetConfig(&c);
                c.temp_interval_s = (uint16_t)atoi(a2);
                FlashLog_SetConfig(&c);
                UART_Printf("[LOG] temp interval=%us\r\n", c.temp_interval_s);
            }
        } else if (!strcmp(arg, "light")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                LogConfig_t c; FlashLog_GetConfig(&c);
                c.light_interval_s = (uint16_t)atoi(a2);
                FlashLog_SetConfig(&c);
                UART_Printf("[LOG] light interval=%us\r\n", c.light_interval_s);
            }
        } else if (!strcmp(arg, "bat")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                LogConfig_t c; FlashLog_GetConfig(&c);
                c.battery_interval_s = (uint16_t)atoi(a2);
                FlashLog_SetConfig(&c);
                UART_Printf("[LOG] bat interval=%us\r\n", c.battery_interval_s);
            }
        } else if (!strcmp(arg, "overflow")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                LogConfig_t c; FlashLog_GetConfig(&c);
                c.overflow_mode = (uint8_t)atoi(a2) ? LOG_OVERFLOW_CIRCULAR : LOG_OVERFLOW_STOP;
                FlashLog_SetConfig(&c);
                UART_Printf("[LOG] overflow=%s\r\n",
                            c.overflow_mode == LOG_OVERFLOW_CIRCULAR ? "circular" : "stop");
            }
        } else if (!strcmp(arg, "mode")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                int v = atoi(a2);
                if (v >= 0 && v <= 2) {
                    LogConfig_t c; FlashLog_GetConfig(&c);
                    c.write_mode = (uint8_t)v;
                    FlashLog_SetConfig(&c);
                    UART_Printf("[LOG] mode=%u\r\n", c.write_mode);
                }
            }
        } else if (!strcmp(arg, "dt")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                LogConfig_t c; FlashLog_GetConfig(&c);
                c.temp_delta_01c = (uint8_t)atoi(a2);
                FlashLog_SetConfig(&c);
                UART_Printf("[LOG] temp delta=%u (0.1C units)\r\n", c.temp_delta_01c);
            }
        } else if (!strcmp(arg, "dl")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                LogConfig_t c; FlashLog_GetConfig(&c);
                c.light_delta_pct = (uint8_t)atoi(a2);
                FlashLog_SetConfig(&c);
                UART_Printf("[LOG] light delta=%u%%\r\n", c.light_delta_pct);
            }
        } else if (!strcmp(arg, "db")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                LogConfig_t c; FlashLog_GetConfig(&c);
                c.battery_delta_pct = (uint8_t)atoi(a2);
                FlashLog_SetConfig(&c);
                UART_Printf("[LOG] bat delta=%u%%\r\n", c.battery_delta_pct);
            }
        } else if (!strcmp(arg, "ts")) {
            char *a2 = strtok(NULL, " \r\n");
            if (a2) {
                LogConfig_t c; FlashLog_GetConfig(&c);
                c.ts_source = (!strcmp(a2, "rtc")) ? LOG_TS_RTC : LOG_TS_BOOT;
                FlashLog_SetConfig(&c);
                UART_Printf("[LOG] ts=%s\r\n",
                            c.ts_source == LOG_TS_RTC ? "rtc" : "boot");
            } else {
                UART_Print("usage: log ts boot|rtc\r\n");
            }
        } else if (!strcmp(arg, "save")) {
            if (FlashLog_CommitConfig() == 0)
                UART_Print("[LOG] config saved to flash\r\n");
            else
                UART_Print("[LOG] config save FAILED\r\n");
        } else {
            UART_Print("log: info|calc|write|read <N>|dump|clear [yes]\r\n");
            UART_Print("     mask|temp|light|bat|overflow|mode|dt|dl|db|ts|save\r\n");
        }
    }

    else {
        UART_Printf("unknown: '%s'  (help)\r\n", cmd);
    }
}

void UartCmd_Init(void) { s_len = 0; }

void UartCmd_Wait(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < ms) {
        UartCmd_Poll();
        LED_Update();           /* keep heartbeat animation alive during wait */
        BLE_LED_Update();       /* BLE adv blink during UART wait */
        BLE_ProcessEvents();    /* service BLE stack — critical for NUS responsiveness */
        UART_CheckIdle();       /* detect physical disconnect mid-wait */
        if (!UART_IsActive() && BLE_IsIdle()) return;
        if (g_cfg_changed) { g_cfg_changed = 0U; return; } /* new period takes effect now */
        HAL_Delay(5);
    }
}

void UartCmd_Poll(void)
{
    UART_SilentTick();  /* check 30-s keepalive; auto-revert to verbose if expired */
    uint8_t ch;
    while (UART_RxGetChar(&ch)) {
        UART_ActivityUpdate();
        if (ch == '\r' || ch == '\n') {
            if (s_len > 0) {
                s_buf[s_len] = '\0';
                s_len = 0;
                /* Route machine-protocol lines (#id VERB ...) to proto handler */
                if (s_buf[0] == '#')
                    Proto_HandleLine(s_buf);
                else
                    _exec(s_buf);
            }
        } else if (ch == 0x7FU || ch == '\b') {
            if (s_len > 0) s_len--;
        } else if (ch >= 0x20U && s_len < CMD_BUF - 1U) {
            s_buf[s_len++] = (char)ch;
        }
    }
}
