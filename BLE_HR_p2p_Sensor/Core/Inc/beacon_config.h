#ifndef BEACON_CONFIG_H
#define BEACON_CONFIG_H

#include <stdint.h>
#include <string.h>

#define CONFIG_MAGIC        0xBEAC0001U
#define CONFIG_VERSION      2U           /* bump when struct layout changes */
#define CONFIG_FLASH_PAGE   26U
#define CONFIG_FLASH_ADDR   (0x08000000U + CONFIG_FLASH_PAGE * 4096U)

typedef enum {
  LED_OFF        = 0,
  LED_INIT_ONLY  = 1,
  LED_BLE_STATUS = 2,  /* blink=adv, solid=connected */
  LED_TX_PULSE   = 3,  /* blink on each TX pulse */
  LED_HEARTBEAT  = 4,  /* 1 Hz always */
  LED_ALWAYS_ON  = 5,  /* always on */
} LedMode_t;

typedef enum {
  RF_MODE_OFF        = 0,
  RF_MODE_FIXED      = 1,  /* fixed ch+pwr, pulse on/off per period */
  RF_MODE_CH_SWEEP   = 2,  /* sweep ch 0-3, fixed pwr */
  RF_MODE_PWR_SWEEP  = 3,  /* fixed ch, sweep pwr 1-4 */
  RF_MODE_FULL_SWEEP = 4,  /* CH sweep then PWR sweep */
  RF_MODE_CONTINUOUS = 5,  /* fixed ch+pwr, TX always on */
} RfMode_t;

/* Struct layout: all uint8_t grouped to avoid padding, total = 40 bytes */
typedef struct {
  uint32_t magic;           /*  0: identification */
  uint32_t version;         /*  4: struct version */

  uint8_t  led_mode;        /*  8: LedMode_t */
  uint8_t  rf_mode;         /*  9: RfMode_t */
  uint8_t  rf_channel;      /* 10: 0-3 */
  uint8_t  rf_power;        /* 11: 1-4 */

  uint32_t rf_pulse_on_ms;  /* 12: TX ON duration per step */
  uint32_t rf_period_ms;    /* 16: full cycle period */

  uint8_t  dbg_battery;     /* 20: 1=always print battery */
  uint8_t  dbg_light;       /* 21: 1=always print light */
  uint8_t  ble_enabled;     /* 22 */
  uint8_t  ble_tx_power;    /* 23: PA_Level 0-7 (0=-40dBm, 7=+6dBm) */

  uint32_t ble_adv_ms;      /* 24: advertising interval */
  uint32_t sensor_ms;       /* 28: sensor read period */
  uint32_t crc;             /* 32: checksum */
} BeaconConfig_t;           /* 36 bytes total */

/* Defaults */
#define CFG_DEF_LED_MODE       LED_BLE_STATUS
#define CFG_DEF_RF_MODE        RF_MODE_FIXED
#define CFG_DEF_RF_CHANNEL     0U
#define CFG_DEF_RF_POWER       1U
#define CFG_DEF_RF_PULSE_ON_MS 100U
#define CFG_DEF_RF_PERIOD_MS   2000U
#define CFG_DEF_DBG_BATTERY    0U
#define CFG_DEF_DBG_LIGHT      0U
#define CFG_DEF_BLE_ENABLED    1U
#define CFG_DEF_BLE_TX_POWER   7U
#define CFG_DEF_BLE_ADV_MS     2000U
#define CFG_DEF_SENSOR_MS      1000U

/* Global config — defined in app_entry.c */
extern BeaconConfig_t g_config;

/* Set 1 from UART task → main loop calls Config_Save() outside UTIL_SEQ */
extern volatile uint8_t g_config_save_pending;

/* Public flash functions */
int  Config_Save(void);
void Config_SetDefaults(BeaconConfig_t *c);

/* BLE state flags — defined in app_ble.c */
extern volatile uint8_t g_ble_advertising;
extern volatile uint8_t g_ble_connected;

#endif /* BEACON_CONFIG_H */
