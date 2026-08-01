#include "lis2dw12.h"
#include "drv_uart.h"
#include "stm32wbxx_hal.h"
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * LIS2DW12TR driver
 *
 * Boot sequence rationale (from A1 answer):
 *   Use 20 ms as a timeout, not as a fixed wait.  The sensor is powered from
 *   a GPIO through a decoupling cap, so the actual VDD ramp depends on
 *   Rds(on) and cap value — datasheet tBOOT (measured with clean bench
 *   supply) does not transfer.  Polling is robust across parts, temperature,
 *   and cap choice.
 * ───────────────────────────────────────────────────────────────────────────── */

/* ── Module state ─────────────────────────────────────────────────────────── */
static uint8_t  s_i2c_addr    = 0x18U;  /* discovered at PowerOn             */
static uint8_t  s_powered     = 0U;
static uint8_t  s_wake_armed  = 0U;
static uint8_t  s_active_fs   = 0U;     /* LIS2DW12_FS_t value, set at Configure */
static LIS2DW12_WakeCfg_t s_wake_cfg = {0};

volatile uint32_t g_lis2dw12_wake_count   = 0U;
volatile uint32_t g_lis2dw12_wake_last_ts = 0U;

/* ── I2C helpers ──────────────────────────────────────────────────────────── */
static HAL_StatusTypeDef _i2c_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(s_i2c_addr << 1),
                                   buf, 2, 10);
}

static HAL_StatusTypeDef _i2c_read(uint8_t reg, uint8_t *val)
{
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c1,
                               (uint16_t)(s_i2c_addr << 1), &reg, 1, 10);
    if (st != HAL_OK) return st;
    return HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(s_i2c_addr << 1),
                                  val, 1, 10);
}

static HAL_StatusTypeDef _i2c_read_addr(uint8_t addr, uint8_t reg, uint8_t *val)
{
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c1,
                               (uint16_t)(addr << 1), &reg, 1, 10);
    if (st != HAL_OK) return st;
    return HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(addr << 1), val, 1, 10);
}

/* Burst-read N bytes starting at reg (auto-increment enabled in HW). */
static HAL_StatusTypeDef _i2c_burst(uint8_t reg, uint8_t *buf, uint8_t n)
{
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c1,
                               (uint16_t)(s_i2c_addr << 1), &reg, 1, 10);
    if (st != HAL_OK) return st;
    return HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(s_i2c_addr << 1),
                                  buf, n, 20);
}

/* ── GPIO helpers ─────────────────────────────────────────────────────────── */
static void _set_pa3(uint8_t on)
{
    HAL_GPIO_WritePin(LIS2DW12_PWR_GPIO, LIS2DW12_PWR_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Set PB6/PB7 to GPIO_MODE_ANALOG to prevent back-feed through I2C pull-ups
 * while sensor is unpowered.  Call BEFORE PA3 goes low.                      */
static void _i2c_pins_hiz(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin  = GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);
}

/* Restore PB6/PB7 to I2C1 alternate function.  Call AFTER PA3 goes high. */
static void _i2c_pins_restore(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &g);
}

/* Configure PA2 as plain input + internal pull-down (floating prevention
 * while sensor is unpowered).  EXTI is kept disabled by the caller.         */
static void _int_pin_input_pd(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin  = LIS2DW12_INT_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(LIS2DW12_INT_GPIO, &g);
}

/* Configure PA2 as EXTI rising-edge with internal pull-down.
 * The pull-down makes the idle state deterministic while the sensor INT1
 * output is not asserting.                                                   */
static void _int_pin_exti(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin  = LIS2DW12_INT_PIN;
    g.Mode = GPIO_MODE_IT_RISING;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(LIS2DW12_INT_GPIO, &g);
    HAL_NVIC_SetPriority(LIS2DW12_INT_EXTI_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(LIS2DW12_INT_EXTI_IRQn);
}

/* ── Address probe: try 0x18 then 0x19 ─────────────────────────────────────
 * Returns the address that responded with WHO_AM_I=0x44, or 0 on failure.   */
static uint8_t _probe_addr(uint32_t timeout_ms, uint16_t *boot_ms_out)
{
    static const uint8_t addrs[2] = { 0x18U, 0x19U };
    uint32_t t0 = HAL_GetTick();

    while (HAL_GetTick() - t0 < timeout_ms) {
        for (uint8_t ai = 0U; ai < 2U; ai++) {
            uint8_t who = 0U;
            if (_i2c_read_addr(addrs[ai], LIS2DW12_REG_WHO_AM_I, &who) == HAL_OK
                    && who == LIS2DW12_WHO_AM_I_VAL) {
                if (boot_ms_out)
                    *boot_ms_out = (uint16_t)(HAL_GetTick() - t0);
                return addrs[ai];
            }
        }
        HAL_Delay(1);
    }
    return 0U;
}

/* ── Soft reset ─────────────────────────────────────────────────────────────
 * Write SOFT_RESET, then poll until the bit self-clears (< 5 ms typically). */
static HAL_StatusTypeDef _soft_reset(void)
{
    if (_i2c_write(LIS2DW12_REG_CTRL2, LIS2DW12_CTRL2_SOFT_RESET) != HAL_OK)
        return HAL_ERROR;

    uint32_t t = HAL_GetTick();
    while (HAL_GetTick() - t < 20U) {
        uint8_t ctrl2 = 0U;
        if (_i2c_read(LIS2DW12_REG_CTRL2, &ctrl2) == HAL_OK
                && !(ctrl2 & LIS2DW12_CTRL2_SOFT_RESET))
            return HAL_OK;
        HAL_Delay(1);
    }
    UART_Print("[ACCEL] SOFT_RESET bit did not clear in 20 ms\r\n");
    return HAL_ERROR;
}

/* ── Apply configuration to a running (powered) sensor ──────────────────── */
HAL_StatusTypeDef LIS2DW12_Configure(const LIS2DW12_Config_t *cfg)
{
    /* CTRL2: enable register address auto-increment and block-data-update.   */
    if (_i2c_write(LIS2DW12_REG_CTRL2,
                   LIS2DW12_CTRL2_IF_ADD_INC | LIS2DW12_CTRL2_BDU) != HAL_OK)
        return HAL_ERROR;

    /* CTRL6: BW_FILT + FS.
     * XL_BW_SEL (bit 0 in CTRL3) is set to 1 so BW_FILT is used in HP mode. */
    uint8_t ctrl6 = (uint8_t)(((uint8_t)cfg->bw << 6) | ((uint8_t)cfg->fs << 4));
    if (_i2c_write(LIS2DW12_REG_CTRL6, ctrl6) != HAL_OK) return HAL_ERROR;
    s_active_fs = (uint8_t)cfg->fs;

    /* CTRL3: active-high interrupt, latched (H_LACTIVE=0 LIR=1).
     * XL_BW_SEL bit 0 = 1 enables CTRL6 BW_FILT in HP mode.                */
    uint8_t ctrl3 = LIS2DW12_CTRL3_LIR | 0x01U;  /* LIR=1, XL_BW_SEL=1    */
    if (_i2c_write(LIS2DW12_REG_CTRL3, ctrl3) != HAL_OK) return HAL_ERROR;

    /* CTRL1: ODR, MODE, LP_MODE — set last so sensor starts sampling.        */
    uint8_t ctrl1 = (uint8_t)(((uint8_t)cfg->odr    << 4)
                             | ((uint8_t)cfg->mode   << 2)
                             |  (uint8_t)cfg->lp_mode);
    if (_i2c_write(LIS2DW12_REG_CTRL1, ctrl1) != HAL_OK) return HAL_ERROR;

    return HAL_OK;
}

/* ── Power on ────────────────────────────────────────────────────────────── */
HAL_StatusTypeDef LIS2DW12_PowerOn(const LIS2DW12_Config_t *cfg)
{
    if (s_powered) return HAL_OK;   /* already on — caller should reconfigure */

    /* 1. Drive PA3 high, restore I2C pins. */
    _set_pa3(1U);
    _i2c_pins_restore();

    /* 2. Wait 5 ms initial — let decoupling cap charge via GPIO Rds(on). */
    HAL_Delay(5);

    /* 3. Poll WHO_AM_I (both addresses) with 15 ms remaining → 20 ms total. */
    uint16_t boot_ms = 0U;
    uint8_t addr = _probe_addr(15U, &boot_ms);
    boot_ms += 5U;   /* include the fixed 5 ms head start */
    if (addr == 0U) {
        UART_Printf("[ACCEL] WHO_AM_I timeout after %u ms (neither 0x18 nor 0x19)\r\n",
                    (unsigned)(boot_ms + 5U));
        _i2c_pins_hiz();
        _set_pa3(0U);
        _int_pin_input_pd();
        return HAL_ERROR;
    }
    s_i2c_addr = addr;
    UART_Printf("[ACCEL] found @ 0x%02X in %u ms\r\n", addr, (unsigned)boot_ms);

    /* 4. Soft reset — returns sensor to known default state. */
    if (_soft_reset() != HAL_OK) {
        _i2c_pins_hiz();
        _set_pa3(0U);
        _int_pin_input_pd();
        return HAL_ERROR;
    }

    /* 5. Apply configuration. */
    if (LIS2DW12_Configure(cfg) != HAL_OK) {
        _i2c_pins_hiz();
        _set_pa3(0U);
        _int_pin_input_pd();
        return HAL_ERROR;
    }

    /* 6. PA2: plain input + pull-down until wake-on-motion is explicitly armed. */
    _int_pin_input_pd();
    s_powered = 1U;
    UART_Print("[ACCEL] powered on OK\r\n");
    return HAL_OK;
}

/* ── Power off ───────────────────────────────────────────────────────────── */
HAL_StatusTypeDef LIS2DW12_PowerOff(void)
{
    if (s_wake_armed) {
        /* Conflict: wake-on-motion must stay powered to detect events.       */
        UART_Print("[ACCEL] PowerOff refused — wake-on-motion armed (disarm first)\r\n");
        return HAL_ERROR;
    }

    /* 1. Disable EXTI before anything else. */
    HAL_NVIC_DisableIRQ(LIS2DW12_INT_EXTI_IRQn);
    __HAL_GPIO_EXTI_CLEAR_IT(LIS2DW12_INT_PIN);

    /* 2. Set PB6/PB7 to Hi-Z to prevent back-feed through pull-ups. */
    _i2c_pins_hiz();

    /* 3. Cut sensor supply. */
    _set_pa3(0U);

    /* 4. PA2 as plain input + pull-down (prevents float). */
    _int_pin_input_pd();

    s_powered = 0U;
    UART_Print("[ACCEL] powered off\r\n");
    return HAL_OK;
}

/* ── Probe (power-cycle measure) ─────────────────────────────────────────── */
HAL_StatusTypeDef LIS2DW12_Probe(LIS2DW12_ProbeResult_t *res)
{
    memset(res, 0, sizeof(*res));

    /* If already powered, just re-read WHO_AM_I without cycling. */
    if (s_powered) {
        res->i2c_addr = s_i2c_addr;
        uint8_t who = 0U;
        HAL_StatusTypeDef st = _i2c_read(LIS2DW12_REG_WHO_AM_I, &who);
        res->who_am_i = who;
        res->ok       = (st == HAL_OK && who == LIS2DW12_WHO_AM_I_VAL) ? 1U : 0U;
        res->boot_ms  = 0U;   /* no power cycle — boot time not measurable    */
        return st;
    }

    /* Not powered: brief power-on probe. */
    _set_pa3(1U);
    _i2c_pins_restore();
    HAL_Delay(5);

    uint16_t boot_ms = 0U;
    uint8_t addr = _probe_addr(15U, &boot_ms);
    boot_ms += 5U;

    if (addr) {
        uint8_t who = 0U;
        _i2c_read_addr(addr, LIS2DW12_REG_WHO_AM_I, &who);
        res->i2c_addr = addr;
        res->who_am_i = who;
        res->ok       = (who == LIS2DW12_WHO_AM_I_VAL) ? 1U : 0U;
        res->boot_ms  = boot_ms;
    } else {
        res->boot_ms = boot_ms + 5U;
    }

    /* Power off again — probe was non-destructive. */
    _i2c_pins_hiz();
    _set_pa3(0U);
    _int_pin_input_pd();

    UART_Printf("[ACCEL] probe: addr=0x%02X who=0x%02X ok=%u boot=%u ms\r\n",
                res->i2c_addr, res->who_am_i, res->ok, (unsigned)res->boot_ms);
    return res->ok ? HAL_OK : HAL_ERROR;
}

/* ── Read XYZ ─────────────────────────────────────────────────────────────── */
HAL_StatusTypeDef LIS2DW12_ReadXYZ(int16_t *x, int16_t *y, int16_t *z)
{
    if (!s_powered) return HAL_ERROR;
    uint8_t buf[6] = {0};
    if (_i2c_burst(LIS2DW12_REG_OUT_X_L, buf, 6U) != HAL_OK) return HAL_ERROR;
    *x = (int16_t)((uint16_t)buf[1] << 8 | buf[0]);
    *y = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
    *z = (int16_t)((uint16_t)buf[5] << 8 | buf[4]);
    return HAL_OK;
}

/* ── Wake-on-motion configuration ────────────────────────────────────────── */
HAL_StatusTypeDef LIS2DW12_WakeConfigure(const LIS2DW12_WakeCfg_t *cfg)
{
    if (cfg->enable && !s_powered) {
        /* Sensor must be powered to detect motion. */
        UART_Print("[ACCEL] WakeConfigure: cannot arm — sensor not powered\r\n");
        return HAL_ERROR;
    }

    if (!cfg->enable) {
        /* Disarm: disable EXTI, disable interrupt routing in sensor. */
        HAL_NVIC_DisableIRQ(LIS2DW12_INT_EXTI_IRQn);
        __HAL_GPIO_EXTI_CLEAR_IT(LIS2DW12_INT_PIN);
        _int_pin_input_pd();
        if (s_powered) {
            _i2c_write(LIS2DW12_REG_CTRL4_INT1, 0x00U);
            _i2c_write(LIS2DW12_REG_CTRL7,      0x00U);
        }
        s_wake_armed = 0U;
        memset(&s_wake_cfg, 0, sizeof(s_wake_cfg));
        UART_Print("[ACCEL] wake-on-motion disarmed\r\n");
        return HAL_OK;
    }

    /* ── Arm wake-on-motion ─────────────────────────────────────────────── */

    /* Convert threshold_mg to register units: 1 unit = FS/64.
     * With FS=±2g → 1 unit = 2000/64 ≈ 31 mg.  Clamp to 63 (6-bit max).  */
    /* For now, default FS is LP1 ±2g (32 mg/unit). */
    uint8_t thr = (uint8_t)((cfg->threshold_mg + 15U) / 31U);  /* round to nearest */
    if (thr > 63U) thr = 63U;
    if (thr == 0U) thr = 1U;

    /* Convert duration_ms to register units WAKE_DUR (0-3 × 1/ODR).
     * Sensor is configured LP1 @ 1.6 Hz for WoM → 1 unit ≈ 625 ms.
     * Clamp to 0-3.                                                          */
    uint8_t dur = 0U;
    if      (cfg->duration_ms >=  625U) dur = 1U;
    if      (cfg->duration_ms >= 1250U) dur = 2U;
    if      (cfg->duration_ms >= 1875U) dur = 3U;

    /* Switch to LP1 @ 1.6 Hz — lowest power for WoM.
     * CTRL1: ODR=0x01(1.6Hz), MODE=00(LP), LP_MODE=00(LP1).                */
    if (_i2c_write(LIS2DW12_REG_CTRL1, 0x10U) != HAL_OK) return HAL_ERROR;

    /* WAKE_UP_THS: threshold. */
    if (_i2c_write(LIS2DW12_REG_WAKE_UP_THS, thr) != HAL_OK) return HAL_ERROR;

    /* WAKE_UP_DUR: WAKE_DUR[6:5]. */
    if (_i2c_write(LIS2DW12_REG_WAKE_UP_DUR,
                   (uint8_t)(dur << 5)) != HAL_OK) return HAL_ERROR;

    /* CTRL4_INT1_PAD_CTRL: route WU to INT1 (bit 5). */
    if (_i2c_write(LIS2DW12_REG_CTRL4_INT1,
                   LIS2DW12_CTRL4_INT1_WU) != HAL_OK) return HAL_ERROR;

    /* CTRL7: enable interrupt output pin. */
    if (_i2c_write(LIS2DW12_REG_CTRL7, LIS2DW12_CTRL7_INT_EN) != HAL_OK)
        return HAL_ERROR;

    /* Clear any stale latch from before arming. */
    uint8_t src = 0U;
    _i2c_read(LIS2DW12_REG_WAKE_UP_SRC, &src);

    /* Configure PA2 as EXTI rising-edge + pull-down. */
    _int_pin_exti();

    s_wake_armed = 1U;
    memcpy(&s_wake_cfg, cfg, sizeof(s_wake_cfg));

    UART_Printf("[ACCEL] wake armed: thr_reg=%u dur_reg=%u action=0x%02X\r\n",
                (unsigned)thr, (unsigned)dur, cfg->action);
    return HAL_OK;
}

/* ── Clear wake interrupt latch ─────────────────────────────────────────── */
void LIS2DW12_ClearWakeIRQ(void)
{
    uint8_t src = 0U;
    /* Must read WAKE_UP_SRC to deassert INT1 when LIR=1.
     * Failing to do this leaves INT1 asserted forever — no further edges.   */
    if (s_powered)
        _i2c_read(LIS2DW12_REG_WAKE_UP_SRC, &src);
    __HAL_GPIO_EXTI_CLEAR_IT(LIS2DW12_INT_PIN);
    g_lis2dw12_wake_count++;
    g_lis2dw12_wake_last_ts = HAL_GetTick();
}

/* ── State queries ───────────────────────────────────────────────────────── */
uint8_t LIS2DW12_IsPowered(void)   { return s_powered;    }
uint8_t LIS2DW12_IsWakeArmed(void) { return s_wake_armed; }
uint8_t LIS2DW12_GetFS(void)       { return s_active_fs;  }
uint8_t LIS2DW12_GetAddr(void)     { return s_i2c_addr;   }
