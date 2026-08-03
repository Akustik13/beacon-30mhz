#pragma once
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Command layer — transport-agnostic opcode dispatcher.
 *
 * Any transport (UART, BLE) feeds a raw { opcode, payload } pair into
 * CmdLayer_Dispatch() and gets back a { result, payload } pair.
 * The transport is responsible for framing, sequencing, and error detection;
 * it knows nothing about command semantics.
 *
 * Over UART the existing text protocol keeps working unchanged.  The new
 * binary commands are exposed via the "CMD" text verb:
 *
 *   Request:  #<id> CMD <hex_opcode><hex_payload>\r\n
 *   Response: #<id> RSP <hex_result><hex_payload>\r\n
 *             #<id> OK\r\n
 *
 * Over BLE (future): opcode+payload go directly as a GATT write characteristic
 * value; result+payload come back as a GATT notify.  No change to this file.
 * ───────────────────────────────────────────────────────────────────────────── */

/* ── Result codes ─────────────────────────────────────────────────────────── */
#define CMD_OK               0x00U  /* success                                 */
#define CMD_ERR_LEN          0x01U  /* payload length wrong                    */
#define CMD_ERR_CRC          0x02U  /* integrity check failed                  */
#define CMD_ERR_UNSUPPORTED  0x03U  /* opcode unknown — always from cmd_layer  */
#define CMD_ERR_BUSY         0x04U  /* resource temporarily unavailable        */
#define CMD_ERR_STATE        0x05U  /* request conflicts with current state    */
#define CMD_ERR_PARAM        0x06U  /* parameter value out of range / invalid  */
#define CMD_ERR_FLASH        0x07U  /* flash erase or write failed             */

/* ── Opcode map ──────────────────────────────────────────────────────────────
 * Gaps within each range are reserved for future use.
 * 0x00–0x0F  System
 * 0x10–0x1F  Config (staging-buffer model — flash only on COMMIT)
 * 0x20–0x2F  Log
 * 0x30–0x3F  Sensors
 * 0x40–0x4F  Transmitter
 * 0x50–0x5F  Power / wake
 * 0x60+      Free
 * ─────────────────────────────────────────────────────────────────────────── */

/* System */
#define OP_PING              0x00U  /* — → fw_ver(1) proto_ver(1) uid(4) uptime_s(4) */
#define OP_TIME_GET          0x01U  /* — → rtc_unix(4)                         */
#define OP_TIME_SET          0x02U  /* rtc_unix(4) → OK                        */
#define OP_REBOOT            0x03U  /* mode(1): 0=cold, 1=bootloader → OK      */
#define OP_UART_MODE         0x06U  /* mode(1): 0=verbose, 1=silent → OK       */

/* Config — CONFIG_WRITE fills RAM only; flash is touched by CONFIG_COMMIT */
#define OP_CONFIG_INFO       0x10U  /* — → total_len(2) version(1) crc16(2)    */
#define OP_CONFIG_READ       0x11U  /* offset(2) len(1) → data                 */
#define OP_CONFIG_WRITE      0x12U  /* offset(2) len(1) data → OK              */
#define OP_CONFIG_COMMIT     0x13U  /* crc16(2) → OK  (validate CRC, write flash) */
#define OP_CONFIG_RESET      0x14U  /* magic(4) → OK  (restore defaults)       */
#define CONFIG_RESET_MAGIC   0xDEADC0DEUL

/* Log */
#define OP_LOG_INFO          0x20U  /* — → total(4) used(4) head(4) tail(4)    *
                                    *     rec_size(1) fmt_ver(1)               */
#define OP_LOG_READ          0x21U  /* index(4) count(1) → records             */
#define OP_LOG_ERASE         0x22U  /* magic(4) → OK  (works on every transport) */
#define LOG_ERASE_MAGIC      0xCA11AB1EUL
#define OP_LOG_MARK          0x23U  /* tag(1) → OK  (write marker record)      */

/* Sensors */
#define OP_SENSOR_LIST       0x30U  /* — → N × { id(1) present(1) enabled(1)  *
                                    *     interval_s(4) unit(4 chars)          *
                                    *     has_ext(1) } for each sensor         */
#define OP_SENSOR_ENABLE     0x31U  /* id(1) enable(1) → OK                   */
#define OP_SENSOR_INTERVAL   0x32U  /* id(1) interval_s(4) → OK               */
#define OP_SENSOR_READ_NOW   0x33U  /* id(1) → raw_i16(2) scaled_i32(4)       */
#define OP_ACCEL_CFG_GET     0x34U  /* — → odr(1) fs(1) mode(1) lpf(1)        */
#define OP_ACCEL_CFG_SET     0x35U  /* odr(1) fs(1) mode(1) lpf(1) → OK       */
#define OP_ACCEL_POWER       0x36U  /* on(1) → OK | ERR_STATE if WoM armed    */
#define OP_ACCEL_PROBE       0x37U  /* — → i2c_addr(1) who_am_i(1) ok(1)      *
                                    *     boot_ms(2)                           */

/* Transmitter */
#define OP_TX_GET            0x40U  /* — → mode(1) ch(1) pwr(1) pulse_ms(2) period_ms(4) */
#define OP_TX_SET            0x41U  /* mode(1) ch(1) pwr(1) pulse_ms(2) period_ms(4) → OK */
#define OP_TX_SCHEDULE_GET   0x42U  /* — → en(1) scope(1) hours(4) days(1) months(2) */
#define OP_TX_SCHEDULE_SET   0x43U  /* en(1) scope(1) hours(4) days(1) months(2) → OK */

/* Power / wake-on-motion */
#define OP_WAKE_CFG_GET      0x50U  /* — → en(1) thr_mg(2) dur_ms(2) action(1) */
#define OP_WAKE_CFG_SET      0x51U  /* en(1) thr_mg(2) dur_ms(2) action(1)     *
                                    *   → OK | ERR_STATE if accel power-off    */
#define OP_WAKE_STATUS       0x52U  /* — → armed(1) count(4) last_ts(4)        */
#define OP_WAKE_CLEAR        0x53U  /* — → OK  (reset counter, clear latch)    */

/* Hardware descriptor (flash page 59) — compact 128-byte blob
 * Layout:
 *   [0]     hw_version  u8
 *   [1]     temp_type   u8   (0=none 1=crystal 2=NTC 3=STTS22H 4=LIS2DW12)
 *   [2]     light_type  u8   (0=none 1=present)
 *   [3]     batt_type   u8   (0=none 1=ADC 2=fuel_gauge)
 *   [4]     accel_type  u8   (0=none 1=ISM330 2=LIS2DW12 3=other)
 *   [5]     led_type    u8   (0=none 1=single 2=RGB)
 *   [6]     tx_channels u8
 *   [7]     tx_pwr_lvls u8
 *   [8..11] tx_freq_hz  u32 LE
 *   [12..13] batt_full_mv  u16 LE
 *   [14..15] batt_empty_mv u16 LE
 *   [16..31] light_model   char[16]
 *   [32..47] accel_model   char[16]
 *   [48..63] tx_type       char[16]
 *   [64..79] led_model     char[16]
 *   [80..127] comment      char[48]  (first 48 of 156; full text via UART)
 */
#define OP_HWDESC_GET        0x60U  /* — → 128-byte blob (RAM copy)             */
#define OP_HWDESC_SET        0x61U  /* 128-byte blob → OK  (RAM only)           */
#define OP_HWDESC_COMMIT     0x62U  /* — → OK  (validate + write to flash p59)  */
#define HWDESC_BLOB_SIZE     128U

/* BLE */
#define OP_BLE_GET           0x70U  /* — → op_mode(1) tx_pwr(1) iv_min(2) dur_s(2) adv_ms(2) nm(1) name(12) led_mode(1) */
#define OP_BLE_SET           0x71U  /* op_mode(1) tx_pwr(1) iv_min(2) dur_s(2) adv_ms(2) nm(1) name(12) led_mode(1) → OK */
#define OP_BLE_RSSI          0x72U  /* — → rssi(1, int8) dBm; ERR_STATE if not connected */
#define OP_MEASURE_ALL       0x73U  /* — → stat(24) accel_xyz(6, int16×3); force-read all sensors */
#define BLE_CMD_PAYLOAD_SIZE 22U    /* total payload bytes for BLE GET/SET */

/* ── Sensor IDs ───────────────────────────────────────────────────────────── */
#define SENSOR_ID_TEMP   0U
#define SENSOR_ID_BATT   1U
#define SENSOR_ID_LIGHT  2U
#define SENSOR_ID_ACCEL  3U
#define SENSOR_COUNT     4U

/* ── Wake-on-motion action bitmask ───────────────────────────────────────── */
#define WAKE_ACTION_WAKE_MCU   (1U << 0)  /* wake from Stop mode via EXTI     */
#define WAKE_ACTION_TX_BURST   (1U << 1)  /* start a transmission burst       */
#define WAKE_ACTION_LOG_MARKER (1U << 2)  /* write marker record to log       */

/* OTA (dual-slot bootloader, see drv_ota.h) */
#define OP_OTA_STATUS    0x80U  /* — → active(1) pending(1) att(1) a_ver(4) b_ver(4) a_crc(4) b_crc(4) */
#define OP_OTA_BEGIN     0x81U  /* total_size(4) version(4) crc32(4) → OK      */
#define OP_OTA_CHUNK     0x82U  /* offset(4) data(≤116) → OK next_offset(4)    */
#define OP_OTA_FINISH    0x83U  /* — → OK (pending reboot) or ERR crc          */
#define OP_OTA_ABORT     0x84U  /* — → OK                                      */
#define OP_OTA_REBOOT    0x85U  /* — → OK then MCU reset (bootloader picks up) */

/* ── Dispatch API ─────────────────────────────────────────────────────────── */

/* Maximum payload sizes (caller must provide buffers at least this large).   */
#define CMD_MAX_IN_PAYLOAD   128U
#define CMD_MAX_OUT_PAYLOAD  128U

typedef struct {
    const uint8_t *data;
    uint8_t        len;
} CmdBuf_t;

/*
 * Dispatch one command.
 *
 *   opcode  — OP_* constant
 *   in      — caller-owned input payload (may be NULL / len=0)
 *   out     — caller-owned output buffer; filled on return
 *   out_len — on entry: capacity of out.data; on exit: bytes written
 *
 * Returns CMD_* result code.  ERR_UNSUPPORTED is returned here (never by
 * a transport driver) when the opcode is not in the table.
 */
uint8_t CmdLayer_Dispatch(uint8_t opcode,
                          const uint8_t *in,  uint8_t in_len,
                          uint8_t       *out, uint8_t *out_len);

/* One-time initialisation — call after all peripherals are up. */
void CmdLayer_Init(void);

/* Returns 1 if OP_REBOOT was dispatched — transport must reset after sending RSP. */
uint8_t CmdLayer_IsRebootPending(void);

/* ── Config staging buffer API (used by CONFIG_READ/WRITE/COMMIT) ─────────── */
/* Fills staging buffer from current runtime state. */
void CmdLayer_StagingLoad(void);
/* Returns pointer to the RAM staging copy (read-only for callers). */
const uint8_t *CmdLayer_StagingPtr(void);
/* Total size of the config structure. */
uint16_t       CmdLayer_StagingSize(void);
