#pragma once
#include <stdint.h>
#include "stm32wbxx_hal.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * LIS2DW12TR accelerometer driver
 *
 * Hardware (confirmed):
 *   PA3  — sensor supply, push-pull output, 1=on  0=off; default 0.
 *   PB6  — I2C1 SCL
 *   PB7  — I2C1 SDA
 *   PA2  — interrupt INT1, active-high, EXTI rising edge, internal pull-down.
 *
 * Power-down discipline:
 *   Before PA3 goes low:
 *     1. DisableIRQ(EXTI2_IRQn) — PA2 EXTI masked while sensor is unpowered.
 *     2. Set PB6/PB7 to GPIO_MODE_ANALOG — prevents I2C bus pull-ups from
 *        back-feeding the sensor through its protection diodes.
 *     3. PA3 = 0.
 *     4. Set PA2 to plain input + pull-down (floating prevention).
 *   Before PA3 goes high:
 *     1. PA3 = 1.
 *     2. Restore I2C alternate function on PB6/PB7.
 *     3. Poll WHO_AM_I (see boot sequence).
 *   See LIS2DW12_PowerSet().
 *
 * Interrupt latch (CTRL3 LIR=1):
 *   The INT1 pin stays asserted until WAKE_UP_SRC (0x38) is read.
 *   The ISR MUST read WAKE_UP_SRC to clear the latch; if it does not, no
 *   further rising edges will arrive and wake-on-motion dies silently.
 * ───────────────────────────────────────────────────────────────────────────── */

/* ── GPIO / I2C resources ─────────────────────────────────────────────────── */
#define LIS2DW12_PWR_GPIO       GPIOA
#define LIS2DW12_PWR_PIN        GPIO_PIN_3   /* PA3 — sensor supply            */
#define LIS2DW12_INT_GPIO       GPIOA
#define LIS2DW12_INT_PIN        GPIO_PIN_2   /* PA2 — INT1                     */
#define LIS2DW12_INT_EXTI_IRQn  EXTI2_IRQn

extern I2C_HandleTypeDef hi2c1;              /* defined by CubeMX / main.c     */

/* ── Register addresses (LIS2DW12TR DS12395 rev 8) ───────────────────────── */
#define LIS2DW12_REG_WHO_AM_I    0x0FU  /* r   expected value: 0x44           */
#define LIS2DW12_REG_CTRL1       0x20U  /* rw  ODR[7:4]  MODE[3:2]  LP_MODE[1:0] */
#define LIS2DW12_REG_CTRL2       0x21U  /* rw  BOOT[7]  SOFT_RESET[6]  BDU[3] IF_ADD_INC[2] */
#define LIS2DW12_REG_CTRL3       0x22U  /* rw  ST[7:6]  PP_OD[5]  LIR[4]  H_LACTIVE[3] */
#define LIS2DW12_REG_CTRL4_INT1  0x23U  /* rw  INT1_6D[7] INT1_STAP[6] INT1_WU[5] INT1_FF[4] INT1_SLP[3] INT1_DTAP[2] INT1_TAP[1] INT1_DRDY[0] */
#define LIS2DW12_REG_CTRL5_INT2  0x24U  /* rw  INT2 routing (reserved here)   */
#define LIS2DW12_REG_CTRL6       0x25U  /* rw  BW_FILT[7:6]  FS[5:4]  FDS[3]  LOW_NOISE[2] */
#define LIS2DW12_REG_STATUS      0x27U  /* r   DRDY WU_IA ...                 */
#define LIS2DW12_REG_OUT_X_L     0x28U  /* r   (burst-read 6 bytes for X,Y,Z) */
#define LIS2DW12_REG_OUT_X_H     0x29U
#define LIS2DW12_REG_OUT_Y_L     0x2AU
#define LIS2DW12_REG_OUT_Y_H     0x2BU
#define LIS2DW12_REG_OUT_Z_L     0x2CU
#define LIS2DW12_REG_OUT_Z_H     0x2DU
#define LIS2DW12_REG_WAKE_UP_THS 0x34U  /* rw  WU_THS[5:0] in units FS/64    */
#define LIS2DW12_REG_WAKE_UP_DUR 0x35U  /* rw  FF_DUR5[7] WAKE_DUR[6:5] STATIONARY[4] SLEEP_DUR[3:0] */
#define LIS2DW12_REG_WAKE_UP_SRC 0x38U  /* r   WU_IA[3] — reading this clears LIR latch */
#define LIS2DW12_REG_CTRL7       0x3FU  /* rw  INTERRUPTS_ENABLE[5] (must be 1 for INT to reach pin) */

/* ── Register bit masks ───────────────────────────────────────────────────── */
#define LIS2DW12_CTRL2_SOFT_RESET    0x40U  /* bit 6 — self-clears             */
#define LIS2DW12_CTRL2_IF_ADD_INC    0x04U  /* bit 2 — auto-increment for burst */
#define LIS2DW12_CTRL2_BDU           0x08U  /* bit 3 — block data update       */
#define LIS2DW12_CTRL3_LIR           0x10U  /* bit 4 — latch interrupt         */
#define LIS2DW12_CTRL4_INT1_WU       0x20U  /* bit 5 — route wake-up to INT1   */
#define LIS2DW12_CTRL7_INT_EN        0x20U  /* bit 5 — enable interrupt output */
#define LIS2DW12_WHO_AM_I_VAL        0x44U

/* ── ODR codes (CTRL1[7:4]) ───────────────────────────────────────────────── */
typedef enum {
    LIS2DW12_ODR_OFF   = 0x00U,  /* power-down                               */
    LIS2DW12_ODR_1HZ6  = 0x01U,  /* 1.6 Hz (LP mode only)                    */
    LIS2DW12_ODR_12HZ5 = 0x02U,  /* 12.5 Hz                                  */
    LIS2DW12_ODR_25HZ  = 0x03U,
    LIS2DW12_ODR_50HZ  = 0x04U,
    LIS2DW12_ODR_100HZ = 0x05U,
    LIS2DW12_ODR_200HZ = 0x06U,
    LIS2DW12_ODR_400HZ = 0x07U,  /* HP only; LP saturates at 200 Hz          */
    LIS2DW12_ODR_800HZ = 0x08U,  /* HP only                                  */
    LIS2DW12_ODR_1600HZ= 0x09U,  /* HP only                                  */
} LIS2DW12_ODR_t;

/* ── Mode codes (CTRL1[3:2]) ──────────────────────────────────────────────── */
typedef enum {
    LIS2DW12_MODE_LP   = 0x00U,  /* low-power; LP_MODE selects LP1/2/3/4     */
    LIS2DW12_MODE_HP   = 0x01U,  /* high-performance (14-bit)                */
} LIS2DW12_Mode_t;

/* ── LP sub-mode codes (CTRL1[1:0], valid only when MODE=LP) ─────────────── */
typedef enum {
    LIS2DW12_LP1 = 0x00U,  /* 12-bit, lowest power  ~0.9 µA @ 1.6 Hz        */
    LIS2DW12_LP2 = 0x01U,  /* 14-bit                                         */
    LIS2DW12_LP3 = 0x02U,  /* 14-bit, noise averaging                        */
    LIS2DW12_LP4 = 0x03U,  /* 14-bit, highest LP ODR                         */
} LIS2DW12_LPMode_t;

/* ── Full-scale codes (CTRL6[5:4]) ───────────────────────────────────────── */
typedef enum {
    LIS2DW12_FS_2G  = 0x00U,
    LIS2DW12_FS_4G  = 0x01U,
    LIS2DW12_FS_8G  = 0x02U,
    LIS2DW12_FS_16G = 0x03U,
} LIS2DW12_FS_t;

/* ── Bandwidth-filter codes (CTRL6[7:6], effective in HP mode) ───────────── */
typedef enum {
    LIS2DW12_BW_ODR_2  = 0x00U,  /* LP mode fixed at ODR/2; BW_FILT ignored  */
    LIS2DW12_BW_ODR_4  = 0x01U,  /* default — also the power-on default       */
    LIS2DW12_BW_ODR_10 = 0x02U,
    LIS2DW12_BW_ODR_20 = 0x03U,
} LIS2DW12_BW_t;

/* ── Driver configuration ─────────────────────────────────────────────────── */
typedef struct {
    LIS2DW12_ODR_t    odr;
    LIS2DW12_Mode_t   mode;
    LIS2DW12_LPMode_t lp_mode;
    LIS2DW12_FS_t     fs;
    LIS2DW12_BW_t     bw;       /* only active in HP mode                    */
} LIS2DW12_Config_t;

/* ── Wake-on-motion configuration ────────────────────────────────────────── */
typedef struct {
    uint8_t  enable;          /* 0=off, 1=armed                               */
    uint16_t threshold_mg;    /* detection threshold in millig                */
    uint16_t duration_ms;     /* minimum event duration in ms                 */
    uint8_t  action;          /* WAKE_ACTION_* bitmask (from cmd_layer.h)     */
} LIS2DW12_WakeCfg_t;

/* ── Probe result ─────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t  i2c_addr;        /* 0x18 or 0x19 — which address responded       */
    uint8_t  who_am_i;        /* should be 0x44                               */
    uint8_t  ok;              /* 1 = healthy, 0 = probe failed                */
    uint16_t boot_ms;         /* ms from PA3 high to first successful WHO_AM_I */
} LIS2DW12_ProbeResult_t;

/* ── API ──────────────────────────────────────────────────────────────────── */

/*
 * Power the sensor on and perform the boot sequence:
 *   PA3 high → 5 ms wait → poll WHO_AM_I every 1 ms (timeout 20 ms)
 *   → SOFT_RESET → wait for bit to self-clear → apply *cfg → enable I2C auto-inc.
 * Stores the I2C address that responded in g_lis2dw12_addr.
 * Returns HAL_OK on success, HAL_ERROR otherwise.
 */
HAL_StatusTypeDef LIS2DW12_PowerOn(const LIS2DW12_Config_t *cfg);

/*
 * Power the sensor off:
 *   1. Disable EXTI2 IRQ (PA2)
 *   2. Set PB6/PB7 to GPIO_MODE_ANALOG
 *   3. PA3 low
 *   4. PA2 = plain input + pull-down
 * Returns HAL_OK.
 */
HAL_StatusTypeDef LIS2DW12_PowerOff(void);

/*
 * Probe without changing power state.
 * Used by OP_ACCEL_PROBE; measures boot time and fills *res.
 */
HAL_StatusTypeDef LIS2DW12_Probe(LIS2DW12_ProbeResult_t *res);

/*
 * (Re-)apply sensor configuration (ODR, FS, mode, LPF) to the running sensor.
 * PA3 must already be high.
 */
HAL_StatusTypeDef LIS2DW12_Configure(const LIS2DW12_Config_t *cfg);

/*
 * Read X, Y, Z as 16-bit two's-complement (left-justified in LP1 mode).
 * PA3 must be high; returns HAL_OK on success.
 */
HAL_StatusTypeDef LIS2DW12_ReadXYZ(int16_t *x, int16_t *y, int16_t *z);

/*
 * Configure wake-on-motion and arm/disarm the EXTI on PA2.
 *
 * CONFLICT RULE: if wake_enable=1 and the sensor supply (PA3) is off,
 * this function returns HAL_ERROR — you cannot arm wake-on-motion while the
 * sensor is unpowered.  The caller must call LIS2DW12_PowerOn() first.
 *
 * Conversely, LIS2DW12_PowerOff() will refuse (return HAL_ERROR) if
 * wake-on-motion is armed; call LIS2DW12_WakeConfigure(&cfg, enable=0) first.
 */
HAL_StatusTypeDef LIS2DW12_WakeConfigure(const LIS2DW12_WakeCfg_t *cfg);

/*
 * Clear the wake-up interrupt latch (read WAKE_UP_SRC) and reset the trigger
 * counter.  Must be called from the EXTI2 ISR (or by OP_WAKE_CLEAR).
 *
 * WHY: with LIR=1 the INT1 pin stays asserted until this register is read.
 * Failing to call this function after the first event leaves the line high
 * permanently — no further rising edges ever arrive.
 */
void LIS2DW12_ClearWakeIRQ(void);

/* Returns 1 if PA3 is currently high (sensor powered). */
uint8_t LIS2DW12_IsPowered(void);

/* Returns LIS2DW12_FS_t value applied at last Configure (0=2G,1=4G,2=8G,3=16G). */
uint8_t LIS2DW12_GetFS(void);

/* Returns 1 if wake-on-motion is currently armed. */
uint8_t LIS2DW12_IsWakeArmed(void);

/* Returns the I2C address that was discovered at last PowerOn. */
uint8_t LIS2DW12_GetAddr(void);

/* Trigger count incremented by ISR; cleared by LIS2DW12_ClearWakeIRQ(). */
extern volatile uint32_t g_lis2dw12_wake_count;
extern volatile uint32_t g_lis2dw12_wake_last_ts;  /* HAL_GetTick() at last event */
