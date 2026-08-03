#pragma once
#include <stdint.h>

/*
 * STM32WB1M (WB15): flash page = 2048 bytes, 160 pages total (0-159).
 * Flash config -- page 60 (0x0801E000), well within CPU1 area.
 * Survives power-off, Shutdown, and reset.
 *
 * Layout: 48 bytes = 6 double-words (STM32WB requires 64-bit program units).
 *
 *   offset  0  magic                (4)
 *   offset  4  ch/pwr/mode/led      (4×1)
 *   offset  8  tx_duration_ms       (4)
 *   offset 12  tx_period_ms         (4)
 *   offset 16  temp_mode            (1)  0=off 1=periodic 2=before_tx
 *   offset 17  temp_period_s        (1)  1-255 s between periodic reads
 *   offset 18  temp_offset_x10      (1)  calibration offset ×0.1°C (int8)
 *   offset 19  eco_uart_sleep       (1)  reserved, always 0
 *   offset 20  rtc_live             (1)  1=print RTC every 1s when UART active
 *   offset 21  batt_mode            (1)  BATT_MODE_*
 *   offset 22  batt_period_s        (1)  1-255 s
 *   offset 23  light_mode           (1)  LIGHT_MODE_*
 *   offset 24  light_period_s       (1)  1-255 s
 *   offset 25  batt_scale_x10       (1)  divider multiplier ×10 (20=2.0×)
 *   offset 26  sched_en             (1)  0=always active; 1=schedule-gated
 *   offset 27  sched_scope          (1)  0=TX only; 1=TX+logging
 *   offset 28  _reserved[8]         padding (always 0)
 *   offset 36  active_hours_mask    (4)  bit N = hour N active (0-23); 0 = all
 *   offset 40  active_months_mask   (2)  bit N = month N (0=Jan..11=Dec); 0 = all
 *   offset 42  active_days_mask     (1)  bit N = weekday N (0=Mon..6=Sun); 0 = all
 *   offset 43  _pad2                (1)
 *   offset 44  crc                  (4)  CRC32 of bytes 0-43
 */

/* ── v1 (legacy) ──────────────────────────────────────────────────────────── */
#define FLASH_CONFIG_MAGIC  0xBEAC0001UL  /* v1 — 48-byte flat struct          */

/* Page location depends on flash layout:
 *   OTA_LAYOUT (production, BLE light stack): config on page 90
 *   default  (development, BLE full stack):   config on page 60
 * Define OTA_LAYOUT in the Makefile (-DOTA_LAYOUT) when building with bootloader. */
#ifdef OTA_LAYOUT
  #define FLASH_CONFIG_PAGE   90U         /* after slots A/B and OTA meta       */
#else
  #define FLASH_CONFIG_PAGE   60U         /* original location                   */
#endif
#define FLASH_CONFIG_ADDR   (0x08000000UL + (uint32_t)(FLASH_CONFIG_PAGE) * 2048UL)

/* ── v2 (current) ─────────────────────────────────────────────────────────── */
#define FLASH_CFG_MAGIC_V2   0xBEAC0003UL  /* v2 — 256-byte struct (v1→v2 migration) */
#define FLASH_CFG_VERSION_V2 2U
/*
 * Slot layout in page 60 (2 KB total):
 *   bytes   0..255  slot A  (active on fresh firmware)
 *   bytes 256..511  slot B  (swapped on CONFIG_COMMIT when A is active)
 *   bytes 512..2047 unused  (headroom — never written)
 *
 * Active slot = the one with valid magic + CRC.  CONFIG_COMMIT erases the page
 * and writes both slots only if needed; the page erase cost is unavoidable on
 * STM32WB (no sub-page erase).
 *
 * CONFIG_WRITE → RAM staging buffer only.
 * CONFIG_COMMIT → validate staging CRC, erase page 60, write staging to slot A.
 */
#define FLASH_CFG_SLOT_SIZE  256U   /* bytes per A/B slot                      */
#define FLASH_CFG_SLOT_A     FLASH_CONFIG_ADDR
#define FLASH_CFG_SLOT_B     (FLASH_CONFIG_ADDR + FLASH_CFG_SLOT_SIZE)

/*
 * v2 config struct — 256 bytes = 32 double-words.
 * Headroom: 256 − (used fields 72) − (CRC 4) = 180 bytes ≥ 128 required.
 *
 * Field offsets are fixed forever; add new fields before _headroom.
 * Migration: if page 60 contains a v1 magic (0xBEAC0001), load v1 fields
 * into runtime globals and use defaults for all new fields.  The next
 * CONFIG_COMMIT writes the v2 struct.
 *
 * Bump FLASH_CFG_VERSION_V2 only ONCE — version already bumped to 2 here.
 */
typedef struct __attribute__((packed)) {
    /* [0..4] header */
    uint32_t magic;                 /* = FLASH_CFG_MAGIC_V2                    */
    uint8_t  version;               /* = FLASH_CFG_VERSION_V2                  */
    /* [5..17] RF */
    uint8_t  ch_idx;
    uint8_t  pwr_idx;
    uint8_t  tx_mode;
    uint8_t  led_mode;
    uint8_t  _pad_rf;
    uint32_t tx_duration_ms;
    uint32_t tx_period_ms;
    /* [18..29] sensors */
    uint8_t  temp_mode;
    uint8_t  temp_period_s;
    int8_t   temp_offset_x10;
    uint8_t  rtc_live;
    uint8_t  batt_mode;
    uint8_t  batt_period_s;
    uint8_t  batt_scale_x10;
    uint8_t  light_mode;
    uint8_t  light_period_s;
    uint8_t  _pad_sens;
    uint8_t  sched_en;
    uint8_t  sched_scope;
    /* [30..53] log config */
    uint32_t active_hours_mask;
    uint16_t active_months_mask;
    uint8_t  active_days_mask;
    uint8_t  log_active_mask;
    uint8_t  log_overflow_mode;
    uint8_t  log_write_mode;
    uint8_t  log_checkpoint_n;
    uint8_t  log_ts_source;
    uint16_t log_temp_interval_s;
    uint16_t log_light_interval_s;
    uint16_t log_batt_interval_s;
    uint16_t log_accel_interval_s;  /* NEW — accel periodic logging interval   */
    uint8_t  log_temp_delta_01c;
    uint8_t  log_light_delta_pct;
    uint8_t  log_batt_delta_pct;
    uint8_t  _pad_log;
    /* [54..58] accelerometer config (NEW) */
    uint8_t  accel_odr;             /* LIS2DW12_ODR_t value                    */
    uint8_t  accel_fs;              /* LIS2DW12_FS_t  value                    */
    uint8_t  accel_mode;            /* LIS2DW12_Mode_t value (0=LP,1=HP)       */
    uint8_t  accel_lp_mode;         /* LIS2DW12_LPMode_t (LP1..LP4)           */
    uint8_t  accel_bw;              /* LIS2DW12_BW_t value; ODR/4 default      */
    /* [59] accelerometer I2C address cache (NEW) */
    uint8_t  accel_i2c_addr;        /* 0x18 or 0x19; 0 = probe on next boot    */
    /* [60..65] wake-on-motion config (NEW) */
    uint8_t  wake_enable;
    uint16_t wake_threshold_mg;
    uint16_t wake_duration_ms;
    uint8_t  wake_action;           /* WAKE_ACTION_* bitmask                   */
    /* [66..71] accelerometer calibration zero-g offsets (NEW) */
    int16_t  accel_off_x;
    int16_t  accel_off_y;
    int16_t  accel_off_z;
    /* [72..73] uptime auto-save interval — mirrors ConfigBlob.uptime_save_min */
    uint16_t uptime_save_min;       /* minutes; 0 = 24 h firmware default      */
    /* [74..95] BLE config (22 bytes) */
    uint8_t  ble_op_mode;           /* BleOpMode_t: 0=off 1=cont 2=sched 3=gekon */
    uint8_t  ble_tx_power;          /* aci_hal_set_tx_power_level param (0-31)   */
    uint16_t ble_interval_s;        /* SCHEDULE: seconds between windows         */
    uint16_t ble_duration_sec;      /* SCHEDULE/GEKON: advertising window (s)    */
    uint16_t ble_adv_interval_ms;   /* advertising interval in ms (100-10000)    */
    uint8_t  ble_name_mode;         /* 0=auto "BCN_XXXX", 1=manual ble_name      */
    uint8_t  _pad_ble;
    char     ble_name[12];          /* manual device name (null-terminated)      */
    /* [96] BLE LED indicator mode */
    uint8_t  ble_led_mode;          /* BLE_LED_NORMAL=0, BLE_LED_OFF=1           */
    /* [97..98] extended battery period in seconds (uint16; 0 = use legacy batt_period_s) */
    uint16_t batt_period_s_ext;
    /* [99..251] reserved headroom (153 bytes) */
    uint8_t  _headroom[153];        /* always written 0, never loaded            */
    /* [252..255] integrity */
    uint32_t crc32;                 /* CRC32 over bytes [0..251]               */
} TxConfigV2_t;
_Static_assert(sizeof(TxConfigV2_t) == 256U, "TxConfigV2_t must be 256 bytes (32 double-words)");

#ifndef OTA_LAYOUT
_Static_assert(FLASH_CONFIG_ADDR == 0x0801E000UL, "Flash page/address mismatch");
#endif

typedef struct {
    uint32_t magic;
    uint8_t  ch_idx;
    uint8_t  pwr_idx;
    uint8_t  tx_mode;
    uint8_t  led_mode;
    uint32_t tx_duration_ms;
    uint32_t tx_period_ms;
    uint8_t  temp_mode;          /* 0=off 1=periodic 2=before_tx             */
    uint8_t  temp_period_s;      /* 1-255 s between periodic readings         */
    int8_t   temp_offset_x10;   /* calibration offset ×0.1°C                */
    uint8_t  eco_uart_sleep;    /* reserved — always 0, never loaded         */
    uint8_t  rtc_live;          /* 1 = print RTC every 1s when UART active   */
    uint8_t  batt_mode;         /* BATT_MODE_*                               */
    uint8_t  batt_period_s;     /* 1-255 s between periodic battery reads    */
    uint8_t  light_mode;        /* LIGHT_MODE_*                              */
    uint8_t  light_period_s;    /* 1-255 s between periodic light reads      */
    uint8_t  batt_scale_x10;    /* divider multiplier ×10 (20=2.0×)         */
    uint8_t  sched_en;          /* 0=always active; 1=schedule-gated        */
    uint8_t  sched_scope;       /* 0=TX only; 1=TX + logging                */
    uint8_t  _reserved[8];      /* padding — always written 0, never loaded  */
    uint32_t active_hours_mask;   /* bit N = hour N active (0-23); 0 = all   */
    uint16_t active_months_mask;  /* bit N = month N active (0=Jan); 0 = all */
    uint8_t  active_days_mask;    /* bit N = weekday N active (0=Mon); 0 = all*/
    uint8_t  _pad2;
    uint32_t crc;
} TxConfig_t;

_Static_assert(sizeof(TxConfig_t) == 48U, "TxConfig_t must be 48 bytes (6 doublewords)");

/* Total successful flash page erases since last HwDesc_Load (lifetime counter). */
extern uint32_t g_flash_erase_count;

/* Intended LED mode — set on load and on 'led' command; used in FlashConfig_Save()
 * so that BLE heartbeat state never overwrites the user-configured LED mode. */
extern uint8_t g_led_mode;

/* Returns 1 = valid config loaded (v1 or v2), 0 = nothing valid */
uint8_t FlashConfig_Load(void);

/* Returns 1 = saved OK, 0 = erase/write failed (writes v2 format) */
uint8_t FlashConfig_Save(void);
/* Returns 1 if CPU2 is not blocking flash (HSEM#7 free) — non-blocking check */
uint8_t Flash_CPU2IsIdle(void);

/* RAM staging buffer for CONFIG_WRITE/CONFIG_COMMIT — never flash until COMMIT */
extern TxConfigV2_t g_cfg_staging;
extern uint8_t      g_cfg_staging_dirty;  /* 1 = staging differs from flash   */

/* Populate g_cfg_staging from current runtime state. */
void FlashConfig_StagingLoad(void);

/* Validate g_cfg_staging CRC, then write to flash.  Returns 1 on success. */
uint8_t FlashConfig_StagingCommit(void);

/* Global accel/wake runtime vars (set by FlashConfig_Load, used by cmd_layer) */
extern uint8_t  g_accel_odr;
extern uint8_t  g_accel_fs;
extern uint8_t  g_accel_mode;
extern uint8_t  g_accel_lp_mode;
extern uint8_t  g_accel_bw;
extern uint8_t  g_accel_i2c_addr;
extern uint8_t  g_wake_enable;
extern uint16_t g_wake_threshold_mg;
extern uint16_t g_wake_duration_ms;
extern uint8_t  g_wake_action;
extern int16_t  g_accel_off_x;
extern int16_t  g_accel_off_y;
extern int16_t  g_accel_off_z;
extern uint16_t g_uptime_save_min; /* minutes; 0 = 24 h default */
/* BLE runtime settings (defined in app_ble.c, loaded/saved here) */
extern uint8_t  g_ble_op_mode;
extern uint8_t  g_ble_tx_power;
extern uint16_t g_ble_interval_s;
extern uint16_t g_ble_duration_sec;
extern uint16_t g_ble_adv_interval_ms;
extern uint8_t  g_ble_name_mode;
extern char     g_ble_name[12];
extern uint8_t  g_ble_led_mode;  /* BLE_LED_NORMAL=0, BLE_LED_OFF=1 */

/* Raw page operations for any page (for diagnostics/UART commands) */
uint8_t Flash_ErasePage(uint32_t page);
uint8_t Flash_WriteDoubleWord(uint32_t addr, uint64_t dword);

/* Batch write: takes HSEM once, programs <bytes> (must be multiple of 8).
 * Silent on success — suitable for log entry writes. */
uint8_t Flash_WriteBlock(uint32_t addr, const void *data, uint32_t bytes);

/* Monitor FLASH->SR for CFGBSY/PESD/BSY pulses, print counts after <ms> ms */
void Flash_Monitor(uint32_t ms);
