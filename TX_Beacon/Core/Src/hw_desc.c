#include "hw_desc.h"
#include "flash_config.h"
#include "proto_uart.h"
#include "svc_power.h"
#include "drv_uart.h"
#include "stm32wbxx_hal.h"
#include <string.h>
#include <stdlib.h>

HwDesc_t g_hw_desc;

/* Anchors for delta computation — updated at each Load/Save */
static uint32_t s_ticks_at_save  = 0U;   /* HAL_GetTick() at last save */
static uint32_t s_stop1_at_save  = 0U;   /* g_stop1_accumulated_s at last save */
/* Sub-hour remainders carried across saves so no partial hours are lost */
static uint32_t s_active_carry_ms = 0U;
static uint32_t s_stop1_carry_s   = 0U;

static const char *s_temp_name[]  = { "none", "crystal", "NTC", "STTS22H", "LIS2DW12" };
static const char *s_batt_name[]  = { "none", "ADC", "fuel_gauge" };
static const char *s_accel_name[] = { "none", "ISM330DHCX", "LIS2DW12", "other" };
static const char *s_led_name[]   = { "none", "LED", "RGB_LED" };

void HwDesc_SetDefaults(void)
{
    memset(&g_hw_desc, 0, sizeof(g_hw_desc));
    g_hw_desc.magic         = HW_DESC_MAGIC;
    g_hw_desc.hw_version    = 1U;
    g_hw_desc.tx_freq_hz    = 30000000UL;
    g_hw_desc.tx_channels   = 4U;
    g_hw_desc.tx_pwr_levels = 4U;
    g_hw_desc.batt_full_mv  = 4200U;
    g_hw_desc.batt_empty_mv = 3000U;
    strncpy(g_hw_desc.tx_type, "colpitts", sizeof(g_hw_desc.tx_type) - 1U);
}

uint8_t HwDesc_Load(void)
{
    const HwDesc_t *p = (const HwDesc_t *)HW_DESC_ADDR;
    if (p->magic != HW_DESC_MAGIC) {
        HwDesc_SetDefaults();
        return 0U;
    }
    uint32_t expected = Proto_CRC32((const uint8_t *)p, sizeof(HwDesc_t) - 4U);
    if (expected != p->crc) {
        HwDesc_SetDefaults();
        return 0U;
    }
    memcpy(&g_hw_desc, p, sizeof(HwDesc_t));
    g_flash_erase_count = p->flash_erase_count;
    s_ticks_at_save     = HAL_GetTick();
    s_stop1_at_save     = 0U;   /* g_stop1_accumulated_s not yet running at Load */
    return 1U;
}

uint8_t HwDesc_Save(void)
{
    uint32_t now        = HAL_GetTick();
    uint32_t stop1_now  = g_stop1_accumulated_s;

    /* milliseconds elapsed since last save, split into active vs stop1 */
    uint32_t delta_ms   = now - s_ticks_at_save;
    uint32_t stop1_s    = stop1_now - s_stop1_at_save;
    uint32_t stop1_ms   = stop1_s * 1000U;
    uint32_t active_ms  = (delta_ms > stop1_ms) ? (delta_ms - stop1_ms) : 0U;

    s_active_carry_ms            += active_ms;
    g_hw_desc.total_active_h    += s_active_carry_ms / 3600000U;
    s_active_carry_ms            %= 3600000U;

    s_stop1_carry_s              += stop1_s;
    g_hw_desc.total_stop1_h     += s_stop1_carry_s / 3600U;
    s_stop1_carry_s              %= 3600U;
    g_hw_desc.flash_erase_count  = g_flash_erase_count;

    s_ticks_at_save = now;
    s_stop1_at_save = stop1_now;

    g_hw_desc.magic = HW_DESC_MAGIC;
    g_hw_desc.crc   = Proto_CRC32((const uint8_t *)&g_hw_desc, sizeof(HwDesc_t) - 4U);

    if (!Flash_ErasePage(HW_DESC_PAGE)) return 0U;
    if (!Flash_WriteBlock(HW_DESC_ADDR, &g_hw_desc, sizeof(HwDesc_t))) return 0U;
    return 1U;
}

uint32_t HwDesc_GetTotalActiveH(void)
{
    uint32_t delta_ms  = HAL_GetTick() - s_ticks_at_save;
    uint32_t stop1_ms  = (g_stop1_accumulated_s - s_stop1_at_save) * 1000U;
    uint32_t active_ms = (delta_ms > stop1_ms) ? (delta_ms - stop1_ms) : 0U;
    return g_hw_desc.total_active_h + (s_active_carry_ms + active_ms) / 3600000U;
}

uint32_t HwDesc_GetTotalStop1H(void)
{
    uint32_t stop1_since_save = g_stop1_accumulated_s - s_stop1_at_save;
    return g_hw_desc.total_stop1_h + (s_stop1_carry_s + stop1_since_save) / 3600U;
}

uint32_t HwDesc_GetTotalShutdownH(void)
{
    return g_hw_desc.total_shutdown_h;
}

void HwDesc_ResetUptime(void)
{
    g_hw_desc.total_active_h   = 0U;
    g_hw_desc.total_stop1_h    = 0U;
    g_hw_desc.total_shutdown_h = 0U;
    s_ticks_at_save   = HAL_GetTick();
    s_stop1_at_save   = g_stop1_accumulated_s;
    s_active_carry_ms = 0U;
    s_stop1_carry_s   = 0U;
    HwDesc_Save();
    UART_Print("[HWDESC] all uptime counters reset to 0\r\n");
}

void HwDesc_Print(void)
{
    const HwDesc_t *d = &g_hw_desc;

    UART_Printf("[HWDESC] hw_version   = %u\r\n", d->hw_version);

    const char *tn = (d->temp_type <= HW_TEMP_LIS2DW12) ? s_temp_name[d->temp_type] : "?";
    UART_Printf("[HWDESC] temp         = %s\r\n", tn);

    if (d->light_type != HW_LIGHT_NONE)
        UART_Printf("[HWDESC] light        = present (%s)\r\n",
                    d->light_model[0] ? d->light_model : "?");
    else
        UART_Print("[HWDESC] light        = none\r\n");

    if (d->batt_type != HW_BATT_NONE) {
        const char *bn = (d->batt_type <= HW_BATT_FUELGAUGE) ? s_batt_name[d->batt_type] : "?";
        UART_Printf("[HWDESC] batt         = %s  full=%umV  empty=%umV\r\n",
                    bn, d->batt_full_mv, d->batt_empty_mv);
    } else {
        UART_Print("[HWDESC] batt         = none\r\n");
    }

    if (d->accel_type == HW_ACCEL_OTHER)
        UART_Printf("[HWDESC] accel        = other (%s)\r\n",
                    d->accel_model[0] ? d->accel_model : "?");
    else {
        const char *an = (d->accel_type <= HW_ACCEL_OTHER) ? s_accel_name[d->accel_type] : "?";
        UART_Printf("[HWDESC] accel        = %s\r\n", an);
    }

    if (d->led_type != HW_LED_NONE) {
        const char *ln = (d->led_type <= HW_LED_RGB) ? s_led_name[d->led_type] : "?";
        UART_Printf("[HWDESC] led          = %s (%s)\r\n",
                    ln, d->led_model[0] ? d->led_model : "?");
    } else {
        UART_Print("[HWDESC] led          = none\r\n");
    }

    UART_Printf("[HWDESC] tx_freq      = %lu Hz\r\n", d->tx_freq_hz);
    UART_Printf("[HWDESC] tx_channels  = %u\r\n",     d->tx_channels);
    UART_Printf("[HWDESC] tx_pwr_lvls  = %u\r\n",     d->tx_pwr_levels);
    UART_Printf("[HWDESC] tx_type      = %s\r\n",     d->tx_type[0] ? d->tx_type : "(none)");

    UART_Printf("[HWDESC] tag           = %s\r\n",
                d->tag[0] ? d->tag : "(none)");
    UART_Printf("[HWDESC] uptime_active = %lu h\r\n", HwDesc_GetTotalActiveH());
    UART_Printf("[HWDESC] uptime_stop1  = %lu h\r\n", HwDesc_GetTotalStop1H());
    UART_Printf("[HWDESC] uptime_shutdn = %lu h\r\n", HwDesc_GetTotalShutdownH());
    UART_Printf("[HWDESC] flash_erases  = %lu\r\n",   g_flash_erase_count);

    if (d->comment[0])
        UART_Printf("[HWDESC] comment      = %s\r\n", d->comment);
    else
        UART_Print("[HWDESC] comment      = (none)\r\n");
}
