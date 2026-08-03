#pragma once
#include <stdint.h>

/*
 * Hardware descriptor — one-time burned to flash after board assembly.
 * Documents which sensors, LED, battery monitor, and TX circuit are installed.
 *
 * Flash page 59 (0x0801D800), 272 bytes = 34 × 8.
 * Page 59 = hw_desc, page 60 = config (TxConfig_t), pages 61-67 = flash log.
 * IMPORTANT: must not overlap log pages (61-67) or config (60).
 *
 * Comment field: 183 printable chars + null.
 * Exceeding this limit produces a UART warning and the text is truncated.
 *
 * UART commands (see svc_uart_cmd.c):
 *   hwdesc show
 *   hwdesc ver <1-255>
 *   hwdesc temp  none|crystal|ntc|stts22h|lis2dw12
 *   hwdesc light none|<model>
 *   hwdesc batt  none|adc <full_mv> <empty_mv>|fuel
 *   hwdesc accel none|ism330|lis2dw12|<model>
 *   hwdesc led   none|led <model>|rgb <model>
 *   hwdesc tx    <freq_hz> <channels> <pwr_levels> <type>
 *   hwdesc comment <free text …>
 *   hwdesc save
 *   hwdesc clear
 */

/* ── Constants ────────────────────────────────────────────────────────────── */
#define HW_DESC_MAGIC  0xBEAC0002UL
#define HW_DESC_PAGE   59U
#define HW_DESC_ADDR   (0x08000000UL + (uint32_t)HW_DESC_PAGE * 2048UL)

/* Tag max length (chars, including null). Bound to MCU UID — set once per device. */
#define HW_DESC_TAG_MAX      12U
/* Comment max length (chars, including null) */
#define HW_DESC_COMMENT_MAX  156U

/* temp_type */
#define HW_TEMP_NONE     0U
#define HW_TEMP_CRYSTAL  1U   /* internal MCU temperature sensor */
#define HW_TEMP_NTC      2U   /* external NTC resistor */
#define HW_TEMP_STTS22H  3U   /* STTS22H I2C sensor */
#define HW_TEMP_LIS2DW12 4U   /* LIS2DW12 built-in temp */

/* light_type */
#define HW_LIGHT_NONE    0U
#define HW_LIGHT_PRESENT 1U

/* batt_type */
#define HW_BATT_NONE     0U
#define HW_BATT_ADC      1U   /* resistor divider + MCU ADC */
#define HW_BATT_FUELGAUGE 2U

/* accel_type */
#define HW_ACCEL_NONE    0U
#define HW_ACCEL_ISM330  1U   /* ISM330DHCX */
#define HW_ACCEL_LIS2DW12 2U
#define HW_ACCEL_OTHER   3U   /* see accel_model string */

/* led_type */
#define HW_LED_NONE   0U
#define HW_LED_SINGLE 1U   /* single-colour LED */
#define HW_LED_RGB    2U   /* RGB LED */

/* ── Struct ───────────────────────────────────────────────────────────────── */
/*
 * Layout (total 272 bytes = 34 × 8, no compiler padding):
 *
 *  offset   0  magic              (4)
 *  offset   4  hw_version (1)  temp_type (1)  light_type (1)  batt_type (1)
 *  offset   8  tx_freq_hz         (4)
 *  offset  12  batt_full_mv (2)   batt_empty_mv (2)
 *  offset  16  accel_type (1)  tx_channels (1)  tx_pwr_levels (1)  led_type (1)
 *  offset  20  light_model        (16)
 *  offset  36  accel_model        (16)
 *  offset  52  tx_type            (16)
 *  offset  68  led_model          (16)
 *  offset  84  tag                (12)   ← animal tag, bound to MCU UID
 *  offset  96  total_active_h     (4)    ← CPU active time hours (excludes Stop1)
 *  offset 100  flash_erase_count  (4)    ← total successful Flash_ErasePage() calls
 *  offset 104  total_stop1_h      (4)    ← hours spent in Stop1 sleep
 *  offset 108  total_shutdown_h   (4)    ← hours in Shutdown between sessions
 *  offset 112  comment            (156)
 *  offset 268  crc                (4)
 */
typedef struct {
    uint32_t magic;
    uint8_t  hw_version;
    uint8_t  temp_type;
    uint8_t  light_type;
    uint8_t  batt_type;
    uint32_t tx_freq_hz;
    uint16_t batt_full_mv;
    uint16_t batt_empty_mv;
    uint8_t  accel_type;
    uint8_t  tx_channels;
    uint8_t  tx_pwr_levels;
    uint8_t  led_type;
    char     light_model[16];
    char     accel_model[16];
    char     tx_type[16];
    char     led_model[16];
    char     tag[12];             /* animal tag, null-padded (set once per device) */
    uint32_t total_active_h;      /* CPU active hours (HAL_GetTick minus Stop1)     */
    uint32_t flash_erase_count;   /* lifetime flash page erase count                */
    uint32_t total_stop1_h;       /* hours spent in Stop1 sleep mode                */
    uint32_t total_shutdown_h;    /* hours in Shutdown between sessions (RTC-based) */
    char     comment[156];
    uint32_t crc;
} HwDesc_t;

_Static_assert(sizeof(HwDesc_t) == 272U, "HwDesc_t must be exactly 272 bytes (34 double-words)");

/* ── API ──────────────────────────────────────────────────────────────────── */
/* g_hw_desc is populated by HwDesc_Load() at boot.
 * svc_uart_cmd updates fields in RAM; HwDesc_Save() burns to flash. */
extern HwDesc_t g_hw_desc;

/* Returns 1 = valid descriptor loaded; 0 = no valid data (defaults applied) */
uint8_t HwDesc_Load(void);

/* Returns 1 = saved OK; 0 = flash erase/write failed */
uint8_t HwDesc_Save(void);

/* Print all fields to UART as [HWDESC] key = value lines */
void HwDesc_Print(void);

/* Fill g_hw_desc with factory defaults (30 MHz, 4ch, 4pwr, colpitts) */
void HwDesc_SetDefaults(void);

/* CPU active hours = saved + elapsed since last save (excludes Stop1). */
uint32_t HwDesc_GetTotalActiveH(void);
/* Stop1 sleep hours = saved + accumulated since last save. */
uint32_t HwDesc_GetTotalStop1H(void);
/* Shutdown hours (RTC-based, only valid if RTC was set). */
uint32_t HwDesc_GetTotalShutdownH(void);

/* Zero all three uptime counters and save immediately. */
void HwDesc_ResetUptime(void);
