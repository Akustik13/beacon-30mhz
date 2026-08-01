#include "flash_config.h"
#include "app_ble.h"
#include "proto_uart.h"
#include "svc_uart_cmd.h"
#include "flash_log.h"
#include "drv_led.h"
#include "drv_uart.h"
#include "drv_chip_temp.h"
#include "drv_batt_adc.h"
#include "drv_light_adc.h"
#include "stm32wbxx_hal.h"
#include <stddef.h>
#include <string.h>

uint32_t g_flash_erase_count = 0U;

/* ── New v2 global runtime variables ─────────────────────────────────────── */
uint8_t  g_accel_odr          = 0x01U;  /* LIS2DW12_ODR_1HZ6                  */
uint8_t  g_accel_fs           = 0x00U;  /* LIS2DW12_FS_2G                     */
uint8_t  g_accel_mode         = 0x00U;  /* LIS2DW12_MODE_LP                   */
uint8_t  g_accel_lp_mode      = 0x00U;  /* LIS2DW12_LP1                       */
uint8_t  g_accel_bw           = 0x01U;  /* LIS2DW12_BW_ODR_4 (default)        */
uint8_t  g_accel_i2c_addr     = 0x00U;  /* 0 = probe on next boot             */
uint8_t  g_wake_enable        = 0U;
uint16_t g_wake_threshold_mg  = 250U;   /* 250 mg default                     */
uint16_t g_wake_duration_ms   = 0U;
uint8_t  g_wake_action        = 0x01U;  /* WAKE_ACTION_WAKE_MCU default       */
int16_t  g_accel_off_x        = 0;
int16_t  g_accel_off_y        = 0;
int16_t  g_accel_off_z        = 0;
uint16_t g_uptime_save_min    = 0U;  /* 0 = 24 h default */
uint8_t  g_led_mode           = 0U;  /* intended LED mode — not runtime state */

/* ── Config staging buffer ───────────────────────────────────────────────── */
TxConfigV2_t g_cfg_staging     = {0};
uint8_t       g_cfg_staging_dirty = 0U;

/*
 * STM32WB flash access protocol (AN5289 / CubeWB hw_conf.h for B-WB1M-WPAN1):
 *
 *   HSEM #2 (CFG_HW_FLASH_SEMID)                  — flash ownership
 *   HSEM #7 (CFG_HW_BLOCK_FLASH_REQ_BY_CPU2_SEMID) — CPU2/FUS signals "hold off"
 *
 * FUS (ROM on CPU2) respects both semaphores. While CPU1 holds HSEM#2,
 * FUS will not set FLASH_SR.PESD. HSEM#7 locked by CPU2 means FUS currently
 * needs flash — CPU1 must NOT start a flash operation yet.
 *
 * FUS boot phase can take several seconds (scans flash for BLE stack, loads NVM).
 * During this time HSEM#2 may be held by CPU2 continuously → need 10 s timeout.
 */
#define FLASH_SEMID         2U   /* CFG_HW_FLASH_SEMID */
#define CPU2_BLOCK_SEMID    7U   /* CFG_HW_BLOCK_FLASH_REQ_BY_CPU2_SEMID */

/* Hardware-verified: STM32WB Cortex-M4 AHB bus master ID = 4 in HSEM.
 * Log confirmed: HSEM->RLR[] after successful 1-step lock returns 0x80000400. */
#undef  HSEM_CPU1_COREID
#define HSEM_CPU1_COREID    0x00000400UL  /* Cortex-M4 MASTERID=4 at bits[11:8] */
#ifndef HSEM_RLR_LOCK
#define HSEM_RLR_LOCK       0x80000000UL
#endif
#ifndef HSEM_R_LOCK
#define HSEM_R_LOCK         0x80000000UL
#endif

static uint8_t _flash_take_access(void)
{
    __HAL_RCC_HSEM_CLK_ENABLE();

    UART_Printf("[CFG] waiting for flash access... SR2=0x%08lX HSEM2=0x%08lX HSEM7=0x%08lX\r\n",
                PWR->SR2, HSEM->R[FLASH_SEMID], HSEM->R[CPU2_BLOCK_SEMID]);

    uint32_t deadline = HAL_GetTick() + 10000U;   /* 10 s — covers FUS boot */
    uint32_t last_print = HAL_GetTick();

    while (HAL_GetTick() < deadline) {

        /* Periodic status print every 2 s */
        if (HAL_GetTick() - last_print > 2000U) {
            last_print = HAL_GetTick();
            UART_Printf("[CFG] still waiting... HSEM2=0x%08lX HSEM7=0x%08lX SR=0x%08lX\r\n",
                        HSEM->R[FLASH_SEMID], HSEM->R[CPU2_BLOCK_SEMID], FLASH->SR);
        }

        /* Step 1: CPU2 explicitly blocking? → wait */
        if (HSEM->R[CPU2_BLOCK_SEMID] & HSEM_R_LOCK) continue;

        /* Step 2: Claim flash ownership via 1-step lock */
        if (HSEM->RLR[FLASH_SEMID] != (HSEM_CPU1_COREID | HSEM_RLR_LOCK)) continue;

        /* Step 3: Re-check CPU2 block after lock (race protection) */
        if (HSEM->R[CPU2_BLOCK_SEMID] & HSEM_R_LOCK) {
            HSEM->R[FLASH_SEMID] = HSEM_CPU1_COREID;   /* release, retry */
            continue;
        }

        /* Force CPU2 into Shutdown and wait for it to acknowledge (SR2.C2DS=bit13).
         * Without waiting, FUS may still assert CFGBSY between our check and STRT. */
        uint32_t c2cr1_before = PWR->C2CR1;
        PWR->C2CR1 = (PWR->C2CR1 & ~0x7UL) | 0x7UL;   /* LPMS = Shutdown */
        uint32_t t2 = HAL_GetTick();
        while (!(PWR->SR2 & (1UL << 13U))) {            /* C2DS = bit 13 */
            if (HAL_GetTick() - t2 > 50U) break;        /* FUS usually ignores; don't wait long */
        }
        UART_Printf("[CFG] flash access (took %lu ms) C2CR1:0x%lX->0x%lX SR2=0x%08lX C2DS=%lu\r\n",
                    10000U - (uint32_t)(deadline - HAL_GetTick()),
                    c2cr1_before, PWR->C2CR1, PWR->SR2,
                    (PWR->SR2 >> 13U) & 1U);
        return 1U;
    }

    UART_Printf("[CFG] 10s timeout HSEM2=0x%08lX HSEM7=0x%08lX SR2=0x%08lX SR=0x%08lX\r\n",
                HSEM->R[FLASH_SEMID], HSEM->R[CPU2_BLOCK_SEMID], PWR->SR2, FLASH->SR);
    HSEM->R[FLASH_SEMID] = HSEM_CPU1_COREID;   /* release in case we hold it */
    return 0U;
}

static void _flash_release_access(void)
{
    HSEM->R[FLASH_SEMID] = HSEM_CPU1_COREID;   /* clear LOCK bit → release */
}

/* Public non-blocking check: 1 = CPU2 not holding HSEM#7 (flash is safe) */
uint8_t Flash_CPU2IsIdle(void)
{
    __HAL_RCC_HSEM_CLK_ENABLE();
    return ((HSEM->R[CPU2_BLOCK_SEMID] & HSEM_R_LOCK) == 0U) ? 1U : 0U;
}

/* ── Wait for flash bus to be idle, clear stale error flags ─────────────── */
static void _flash_sr_clear(void)
{
    uint32_t t = HAL_GetTick();
    while (FLASH->SR & (FLASH_SR_BSY | FLASH_SR_CFGBSY | FLASH_SR_PESD)) {
        if (HAL_GetTick() - t > 200U) break;
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
}

/* ── Direct page erase — PESD-safe ───────────────────────────────────────────
 * HAL_FLASHEx_Erase silently drops the erase when PESD is set at the moment
 * STRT is written: BSY never rises, HAL sees no error and returns HAL_OK, but
 * the page is unchanged.
 *
 * Fix: disable IRQs, check PESD one last time, write CR (PER+PNB) then STRT,
 * re-enable IRQs, then verify BSY actually went HIGH (confirms erase started).
 * If BSY never rises within ~100 µs, PESD stole STRT → retry.               */
/* page_addr: flash base address of the page = 0x08000000 + page * 4096 */
static uint8_t _erase_page_direct(uint32_t page, uint32_t page_addr)
{
    const uint32_t cr_per  = FLASH_CR_PER | (page << FLASH_CR_PNB_Pos);
    const uint32_t cr_strt = cr_per | FLASH_CR_STRT;

    for (uint32_t att = 0U; att < 300U; att++) {

        /* Wait for flash idle: C1SR and C2SR must both be clear */
        uint32_t t = HAL_GetTick();
        for (;;) {
            uint32_t sr1 = FLASH->SR;
            uint32_t sr2 = FLASH->C2SR;
            if (!(sr1 & (FLASH_SR_BSY | FLASH_SR_CFGBSY | FLASH_SR_PESD)) &&
                !(sr2 & (FLASH_SR_BSY | FLASH_SR_CFGBSY))) break;
            if (HAL_GetTick() - t > 500U) {
                UART_Printf("[FLS] idle timeout SR=0x%08lX C2SR=0x%08lX att=%lu\r\n",
                            sr1, sr2, att);
                return 0U;
            }
        }

        /* Atomic: re-check, clear errors, arm PER+PNB, fire STRT */
        if (att == 0U) {
            UART_Printf("[FLS] att0 before STRT: CR=0x%08lX SR=0x%08lX C2SR=0x%08lX\r\n",
                        FLASH->CR, FLASH->SR, FLASH->C2SR);
        }
        __disable_irq();
        uint32_t sr_pre = FLASH->SR;
        if ((sr_pre & (FLASH_SR_BSY | FLASH_SR_CFGBSY | FLASH_SR_PESD)) ||
            (FLASH->C2SR & (FLASH_SR_BSY | FLASH_SR_CFGBSY))) {
            __enable_irq();
            continue;
        }
        FLASH->SR = FLASH_FLAG_ALL_ERRORS;
        FLASH->CR = cr_per;
        __DSB();
        __NOP(); __NOP(); __NOP(); __NOP();
        FLASH->CR = cr_strt;
        __DSB();
        uint32_t sr_post = FLASH->SR;
        __enable_irq();
        if (att == 0U) {
            UART_Printf("[FLS] att0 after STRT: CR=0x%08lX SR_pre=0x%08lX SR_post=0x%08lX\r\n",
                        FLASH->CR, sr_pre, sr_post);
        }

        uint8_t started = 0U;
        for (uint32_t i = 0U; i < 3200U; i++) {
            if (FLASH->SR & FLASH_SR_BSY) { started = 1U; break; }
        }
        if (!started) {
            FLASH->CR = 0U;
            continue;
        }

        t = HAL_GetTick();
        while (FLASH->SR & FLASH_SR_BSY) {
            if (HAL_GetTick() - t > 2000U) {
                UART_Printf("[FLS] erase BSY timeout att=%lu\r\n", att);
                FLASH->CR = 0U;
                return 0U;
            }
        }
        FLASH->CR = 0U;

        if (FLASH->SR & FLASH_FLAG_ALL_ERRORS) {
            UART_Printf("[FLS] erase SR error=0x%08lX att=%lu\r\n",
                        FLASH->SR, att);
            return 0U;
        }

        /* Flush caches so verify reads physical flash */
        uint32_t acr = FLASH->ACR;
        if (acr & FLASH_ACR_ICEN) {
            CLEAR_BIT(FLASH->ACR, FLASH_ACR_ICEN);
            SET_BIT(FLASH->ACR,   FLASH_ACR_ICRST);
            CLEAR_BIT(FLASH->ACR, FLASH_ACR_ICRST);
            SET_BIT(FLASH->ACR,   FLASH_ACR_ICEN);
        }
        if (acr & FLASH_ACR_DCEN) {
            CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCEN);
            SET_BIT(FLASH->ACR,   FLASH_ACR_DCRST);
            CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCRST);
            SET_BIT(FLASH->ACR,   FLASH_ACR_DCEN);
        }
        __DSB();
        __ISB();

        if (*(volatile uint32_t *)page_addr != 0xFFFFFFFFUL) {
            continue;   /* erase dropped — retry */
        }

        UART_Printf("[FLS] erase pg%lu OK (att=%lu)\r\n", page, att + 1U);
        return 1U;
    }

    UART_Printf("[FLS] erase pg%lu: 300 attempts FAILED SR=0x%08lX\r\n",
                page, FLASH->SR);
    return 0U;
}

/* ── Public: erase any flash page ──────────────────────────────────────────── */
uint8_t Flash_ErasePage(uint32_t page)
{
    uint32_t addr = 0x08000000UL + page * 2048UL;   /* STM32WB1M: 2KB pages */

    UART_Printf("[FLS] erase page %lu @ 0x%08lX\r\n", page, addr);
    UART_Printf("[FLS] SFR=0x%08lX WRP1AR=0x%08lX CR=0x%08lX\r\n",
                FLASH->SFR, FLASH->WRP1AR, FLASH->CR);
    UART_Printf("[FLS] C2SR=0x%08lX C2CR=0x%08lX ACR=0x%08lX\r\n",
                FLASH->C2SR, FLASH->C2CR, FLASH->ACR);
    UART_Printf("[FLS] PCROP1ASR=0x%08lX PCROP1AER=0x%08lX ECCR=0x%08lX\r\n",
                FLASH->PCROP1ASR, FLASH->PCROP1AER, FLASH->ECCR);
    UART_Printf("[FLS] PCROP1BSR=0x%08lX PCROP1BER=0x%08lX C2CR1=0x%08lX\r\n",
                FLASH->PCROP1BSR, FLASH->PCROP1BER, PWR->C2CR1);

    if (!_flash_take_access()) {
        UART_Print("[FLS] FAILED: cannot get flash access (HSEM)\r\n");
        return 0U;
    }
    _flash_sr_clear();

    UART_Printf("[FLS] CR before unlock=0x%08lX\r\n", FLASH->CR);
    if (HAL_FLASH_Unlock() != HAL_OK) {
        UART_Printf("[FLS] unlock FAILED err=0x%08lX SR=0x%08lX\r\n",
                    HAL_FLASH_GetError(), FLASH->SR);
        _flash_release_access();
        return 0U;
    }
    UART_Printf("[FLS] CR after unlock=0x%08lX\r\n", FLASH->CR);

    _flash_sr_clear();
    uint8_t ok = _erase_page_direct(page, addr);
    if (ok) g_flash_erase_count++;

    HAL_FLASH_Lock();
    _flash_release_access();
    return ok;
}

/* ── Public: write one 64-bit double-word to any flash address ─────────────── */
uint8_t Flash_WriteDoubleWord(uint32_t addr, uint64_t dword)
{
    if (!_flash_take_access()) {
        UART_Print("[FLS] write: FAILED to get flash access\r\n");
        return 0U;
    }
    _flash_sr_clear();

    if (HAL_FLASH_Unlock() != HAL_OK) {
        UART_Printf("[FLS] write: unlock FAILED err=0x%08lX\r\n",
                    HAL_FLASH_GetError());
        _flash_release_access();
        return 0U;
    }

    _flash_sr_clear();
    HAL_StatusTypeDef st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, dword);
    if (st != HAL_OK) {
        UART_Printf("[FLS] write: program FAILED err=0x%08lX SR=0x%08lX\r\n",
                    HAL_FLASH_GetError(), FLASH->SR);
        HAL_FLASH_Lock();
        _flash_release_access();
        return 0U;
    }

    HAL_FLASH_Lock();
    _flash_release_access();

    uint64_t readback;
    __builtin_memcpy(&readback, (const void *)addr, sizeof(readback));
    if (readback != dword) {
        UART_Printf("[FLS] write: verify FAILED got=0x%08lX%08lX\r\n",
                    (uint32_t)(readback >> 32), (uint32_t)(readback & 0xFFFFFFFFUL));
        return 0U;
    }
    return 1U;
}

/* ── Batch write: one HSEM acquire, N double-words, silent on success ─────── */
uint8_t Flash_WriteBlock(uint32_t addr, const void *data, uint32_t bytes)
{
    if (!bytes || (bytes & 7U)) return 0U;

    __HAL_RCC_HSEM_CLK_ENABLE();

    uint32_t deadline = HAL_GetTick() + 10000U;
    uint8_t  got = 0U;
    while (HAL_GetTick() < deadline) {
        if (HSEM->R[CPU2_BLOCK_SEMID] & HSEM_R_LOCK) continue;
        if (HSEM->RLR[FLASH_SEMID] != (HSEM_CPU1_COREID | HSEM_RLR_LOCK)) continue;
        if (HSEM->R[CPU2_BLOCK_SEMID] & HSEM_R_LOCK) {
            HSEM->R[FLASH_SEMID] = HSEM_CPU1_COREID;
            continue;
        }
        PWR->C2CR1 = (PWR->C2CR1 & ~0x7UL) | 0x7UL;
        uint32_t t2 = HAL_GetTick();
        while (!(PWR->SR2 & (1UL << 13U)))
            if (HAL_GetTick() - t2 > 50U) break;
        got = 1U;
        break;
    }
    if (!got) {
        UART_Print("[FLS] WriteBlock: HSEM timeout\r\n");
        HSEM->R[FLASH_SEMID] = HSEM_CPU1_COREID;
        return 0U;
    }

    uint32_t t = HAL_GetTick();
    while (FLASH->SR & (FLASH_SR_BSY | FLASH_SR_CFGBSY | FLASH_SR_PESD))
        if (HAL_GetTick() - t > 200U) break;
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    if (HAL_FLASH_Unlock() != HAL_OK) {
        UART_Printf("[FLS] WriteBlock: unlock FAIL err=0x%08lX\r\n",
                    HAL_FLASH_GetError());
        HSEM->R[FLASH_SEMID] = HSEM_CPU1_COREID;
        return 0U;
    }

    uint8_t ok = 1U;
    for (uint32_t off = 0U; ok && off < bytes; off += 8U) {
        uint64_t dw;
        memcpy(&dw, (const uint8_t *)data + off, 8U);
        t = HAL_GetTick();
        while (FLASH->SR & (FLASH_SR_BSY | FLASH_SR_CFGBSY | FLASH_SR_PESD))
            if (HAL_GetTick() - t > 200U) break;
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + off, dw) != HAL_OK) {
            UART_Printf("[FLS] WriteBlock+%lu FAIL err=0x%08lX\r\n",
                        off, HAL_FLASH_GetError());
            ok = 0U;
        } else if (FLASH->SR & FLASH_FLAG_ALL_ERRORS) {
            UART_Printf("[FLS] WriteBlock+%lu SR error=0x%08lX\r\n", off, FLASH->SR);
            ok = 0U;
        }
    }

    HAL_FLASH_Lock();
    HSEM->R[FLASH_SEMID] = HSEM_CPU1_COREID;
    return ok;
}

/* ── Flash SR monitor: count CFGBSY/PESD/BSY pulses over <ms> milliseconds ── */
void Flash_Monitor(uint32_t ms)
{
    UART_Printf("[FMON] monitoring SR/C2SR for %lu ms...\r\n", ms);
    uint32_t cnt_pesd = 0U, cnt_cfgbsy = 0U, cnt_bsy1 = 0U, cnt_bsy2 = 0U;
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < ms) {
        uint32_t sr1 = FLASH->SR;
        uint32_t sr2 = FLASH->C2SR;
        if (sr1 & FLASH_SR_PESD)   cnt_pesd++;
        if (sr1 & FLASH_SR_CFGBSY) cnt_cfgbsy++;
        if (sr1 & FLASH_SR_BSY)    cnt_bsy1++;
        if (sr2 & FLASH_SR_BSY)    cnt_bsy2++;
    }
    UART_Printf("[FMON] PESD=%lu CFGBSY=%lu BSY1=%lu BSY2=%lu SR2=0x%08lX\r\n",
                cnt_pesd, cnt_cfgbsy, cnt_bsy1, cnt_bsy2, PWR->SR2);
}

/* ── Shared field-load helper (common to v1 and v2) ──────────────────────── */
static void _load_rf_fields(uint8_t ch, uint8_t pwr, uint8_t mode, uint8_t led,
                            uint32_t dur_ms, uint32_t per_ms)
{
    g_ch_idx         = ch;
    g_pwr_idx        = pwr;
    g_tx_mode        = mode;
    g_tx_paused      = 0U;
    g_tx_duration_ms = (dur_ms >= 1U && dur_ms <= 60000U)   ? dur_ms  : 20U;
    g_tx_period_ms   = (per_ms >= 100U && per_ms <= 3600000U) ? per_ms : 2000U;
    g_led_mode = (led <= (uint8_t)LED_TX) ? led : (uint8_t)LED_OFF;
    LED_SetMode((LED_Mode_t)g_led_mode);
}

/* ── Load ─────────────────────────────────────────────────────────────────── */
uint8_t FlashConfig_Load(void)
{
    const uint32_t *magic_ptr = (const uint32_t *)FLASH_CONFIG_ADDR;
    uint32_t magic = *magic_ptr;

    /* ── Try v2 first ────────────────────────────────────────────────────── */
    if (magic == FLASH_CFG_MAGIC_V2) {
        const TxConfigV2_t *p = (const TxConfigV2_t *)FLASH_CONFIG_ADDR;
        uint32_t crc = Proto_CRC32((const uint8_t *)p,
                                   offsetof(TxConfigV2_t, crc32));
        if (crc != p->crc32) {
            UART_Print("[CFG] v2 CRC mismatch — no valid config\r\n");
            return 0U;
        }
        if (p->ch_idx > 3U || p->pwr_idx > 3U || p->tx_mode > TX_MODE_OFF)
            return 0U;

        _load_rf_fields(p->ch_idx, p->pwr_idx, p->tx_mode, p->led_mode,
                        p->tx_duration_ms, p->tx_period_ms);

        g_active_hours_mask  = p->active_hours_mask;
        g_active_days_mask   = p->active_days_mask;
        g_active_months_mask = p->active_months_mask;
        g_sched_en    = p->sched_en ? 1U : 0U;
        g_sched_scope = (p->sched_scope <= 1U) ? p->sched_scope : 0U;

        g_temp_mode       = (p->temp_mode <= 2U) ? p->temp_mode : 0U;
        g_temp_period_s   = (p->temp_period_s >= 1U) ? p->temp_period_s : 1U;
        g_temp_offset_x10 = p->temp_offset_x10;
        g_rtc_live        = p->rtc_live ? 1U : 0U;
        g_batt_mode       = (p->batt_mode <= 1U)      ? p->batt_mode      : 0U;
        g_batt_period_s   = (p->batt_period_s >= 1U)  ? p->batt_period_s  : 5U;
        g_batt_scale_x10  = (p->batt_scale_x10 >= 1U) ? p->batt_scale_x10 : 20U;
        g_light_mode      = (p->light_mode <= 1U)      ? p->light_mode     : 0U;
        g_light_period_s  = (p->light_period_s >= 1U)  ? p->light_period_s : 5U;

        /* Log config */
        LogConfig_t lc;
        FlashLog_GetConfig(&lc);
        lc.active_mask        = p->log_active_mask;
        lc.overflow_mode      = p->log_overflow_mode;
        lc.write_mode         = p->log_write_mode;
        lc.checkpoint_n       = p->log_checkpoint_n ? p->log_checkpoint_n : 16U;
        lc.ts_source          = p->log_ts_source;
        lc.temp_interval_s    = p->log_temp_interval_s    ? p->log_temp_interval_s    : 60U;
        lc.light_interval_s   = p->log_light_interval_s   ? p->log_light_interval_s   : 300U;
        lc.battery_interval_s = p->log_batt_interval_s    ? p->log_batt_interval_s    : 3600U;
        lc.accel_interval_s   = p->log_accel_interval_s;
        lc.temp_delta_01c     = p->log_temp_delta_01c;
        lc.light_delta_pct    = p->log_light_delta_pct;
        lc.battery_delta_pct  = p->log_batt_delta_pct;
        FlashLog_SetConfig(&lc);

        /* New v2 fields — accelerometer */
        g_accel_odr       = p->accel_odr;
        g_accel_fs        = p->accel_fs;
        g_accel_mode      = p->accel_mode;
        g_accel_lp_mode   = p->accel_lp_mode;
        g_accel_bw        = p->accel_bw ? p->accel_bw : 0x01U; /* ODR/4 default */
        g_accel_i2c_addr  = p->accel_i2c_addr;

        /* Wake-on-motion */
        g_wake_enable       = p->wake_enable;
        g_wake_threshold_mg = p->wake_threshold_mg ? p->wake_threshold_mg : 250U;
        g_wake_duration_ms  = p->wake_duration_ms;
        g_wake_action       = p->wake_action;

        /* Calibration */
        g_accel_off_x = p->accel_off_x;
        g_accel_off_y = p->accel_off_y;
        g_accel_off_z = p->accel_off_z;
        g_uptime_save_min = p->uptime_save_min;

        /* BLE config (v2 headroom fields) */
        { uint8_t m = (p->ble_op_mode <= 0x07U) ? p->ble_op_mode : 0x01U;
          if (m != BLE_OP_OFF) m |= BLE_OP_GEKON; /* GEKON always enabled */
          g_ble_op_mode = m; }
        g_ble_tx_power        = p->ble_tx_power;
        g_ble_interval_s      = p->ble_interval_s    ? p->ble_interval_s    : 1800U;
        g_ble_duration_sec    = p->ble_duration_sec  ? p->ble_duration_sec  : 60U;
        g_ble_adv_interval_ms = p->ble_adv_interval_ms ? p->ble_adv_interval_ms : 1000U;
        g_ble_name_mode       = (p->ble_name_mode <= 1U) ? p->ble_name_mode : 0U;
        memcpy(g_ble_name, p->ble_name, sizeof(g_ble_name));
        g_ble_led_mode        = (p->ble_led_mode <= 1U) ? p->ble_led_mode : 0U;
        g_ble_name[sizeof(g_ble_name) - 1U] = '\0';

        UART_Printf("[CFG] v2 loaded: mode=%u ch=%u pwr=%u dur=%lu per=%lu led=%u\r\n",
                    g_tx_mode, g_ch_idx, g_pwr_idx,
                    g_tx_duration_ms, g_tx_period_ms, (uint8_t)LED_GetMode());
        UART_Printf("[CFG]   accel odr=%u fs=%u mode=%u addr=0x%02X\r\n",
                    g_accel_odr, g_accel_fs, g_accel_mode, g_accel_i2c_addr);
        return 1U;
    }

    /* ── Fall back to v1 — populate new fields with defaults ─────────────── */
    if (magic == FLASH_CONFIG_MAGIC) {
        const TxConfig_t *p = (const TxConfig_t *)FLASH_CONFIG_ADDR;
        uint32_t crc = Proto_CRC32((const uint8_t *)p, offsetof(TxConfig_t, crc));
        if (crc != p->crc) return 0U;
        if (p->ch_idx > 3U || p->pwr_idx > 3U || p->tx_mode > TX_MODE_OFF)
            return 0U;

        _load_rf_fields(p->ch_idx, p->pwr_idx, p->tx_mode, p->led_mode,
                        p->tx_duration_ms, p->tx_period_ms);
        g_active_hours_mask  = p->active_hours_mask;
        g_active_days_mask   = p->active_days_mask;
        g_active_months_mask = p->active_months_mask;
        g_sched_en    = p->sched_en ? 1U : 0U;
        g_sched_scope = (p->sched_scope <= 1U) ? p->sched_scope : 0U;
        g_temp_mode       = (p->temp_mode <= 2U) ? p->temp_mode : 0U;
        g_temp_period_s   = (p->temp_period_s >= 1U) ? p->temp_period_s : 1U;
        g_temp_offset_x10 = p->temp_offset_x10;
        g_rtc_live        = p->rtc_live ? 1U : 0U;
        g_batt_mode       = (p->batt_mode <= 1U)      ? p->batt_mode      : 0U;
        g_batt_period_s   = (p->batt_period_s >= 1U)  ? p->batt_period_s  : 5U;
        g_batt_scale_x10  = (p->batt_scale_x10 >= 1U) ? p->batt_scale_x10 : 20U;
        g_light_mode      = (p->light_mode <= 1U)      ? p->light_mode     : 0U;
        g_light_period_s  = (p->light_period_s >= 1U)  ? p->light_period_s : 5U;

        /* v2 fields — defaults */
        g_accel_odr = 0x01U; g_accel_fs = 0x00U; g_accel_mode = 0x00U;
        g_accel_lp_mode = 0x00U; g_accel_bw = 0x01U; g_accel_i2c_addr = 0x00U;
        g_wake_enable = 0U; g_wake_threshold_mg = 250U;
        g_wake_duration_ms = 0U; g_wake_action = 0x01U;
        g_accel_off_x = g_accel_off_y = g_accel_off_z = 0;

        UART_Printf("[CFG] v1 loaded (will upgrade on next save): mode=%u ch=%u\r\n",
                    g_tx_mode, g_ch_idx);
        return 1U;
    }

    return 0U;
}

/* ── Build v2 struct from runtime globals ─────────────────────────────────── */
static void _build_v2(TxConfigV2_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic        = FLASH_CFG_MAGIC_V2;
    cfg->version      = FLASH_CFG_VERSION_V2;
    cfg->ch_idx       = g_ch_idx  & 3U;
    cfg->pwr_idx      = g_pwr_idx & 3U;
    cfg->tx_mode      = g_tx_mode;
    cfg->led_mode     = g_led_mode;   /* intended mode, not runtime BLE state */
    cfg->tx_duration_ms = g_tx_duration_ms;
    cfg->tx_period_ms   = g_tx_period_ms;
    cfg->temp_mode       = g_temp_mode;
    cfg->temp_period_s   = (g_temp_period_s >= 1U)  ? g_temp_period_s  : 1U;
    cfg->temp_offset_x10 = g_temp_offset_x10;
    cfg->rtc_live        = g_rtc_live;
    cfg->batt_mode       = g_batt_mode;
    cfg->batt_period_s   = (g_batt_period_s >= 1U)  ? g_batt_period_s  : 5U;
    cfg->batt_scale_x10  = (g_batt_scale_x10 >= 1U) ? g_batt_scale_x10 : 20U;
    cfg->light_mode      = g_light_mode;
    cfg->light_period_s  = (g_light_period_s >= 1U)  ? g_light_period_s : 5U;
    cfg->sched_en        = g_sched_en;
    cfg->sched_scope     = g_sched_scope;
    cfg->active_hours_mask  = g_active_hours_mask;
    cfg->active_days_mask   = g_active_days_mask;
    cfg->active_months_mask = g_active_months_mask;

    LogConfig_t lc;
    FlashLog_GetConfig(&lc);
    cfg->log_active_mask      = lc.active_mask;
    cfg->log_overflow_mode    = lc.overflow_mode;
    cfg->log_write_mode       = lc.write_mode;
    cfg->log_checkpoint_n     = lc.checkpoint_n ? lc.checkpoint_n : 16U;
    cfg->log_ts_source        = lc.ts_source;
    cfg->log_temp_interval_s  = lc.temp_interval_s;
    cfg->log_light_interval_s = lc.light_interval_s;
    cfg->log_batt_interval_s  = lc.battery_interval_s;
    cfg->log_accel_interval_s = lc.accel_interval_s;
    cfg->log_temp_delta_01c   = lc.temp_delta_01c;
    cfg->log_light_delta_pct  = lc.light_delta_pct;
    cfg->log_batt_delta_pct   = lc.battery_delta_pct;

    cfg->accel_odr      = g_accel_odr;
    cfg->accel_fs       = g_accel_fs;
    cfg->accel_mode     = g_accel_mode;
    cfg->accel_lp_mode  = g_accel_lp_mode;
    cfg->accel_bw       = g_accel_bw;
    cfg->accel_i2c_addr = g_accel_i2c_addr;
    cfg->wake_enable       = g_wake_enable;
    cfg->wake_threshold_mg = g_wake_threshold_mg;
    cfg->wake_duration_ms  = g_wake_duration_ms;
    cfg->wake_action       = g_wake_action;
    cfg->accel_off_x = g_accel_off_x;
    cfg->accel_off_y = g_accel_off_y;
    cfg->accel_off_z = g_accel_off_z;
    cfg->uptime_save_min = g_uptime_save_min;

    /* BLE config */
    cfg->ble_op_mode         = g_ble_op_mode;
    cfg->ble_tx_power        = g_ble_tx_power;
    cfg->ble_interval_s      = g_ble_interval_s;
    cfg->ble_duration_sec    = g_ble_duration_sec;
    cfg->ble_adv_interval_ms = g_ble_adv_interval_ms;
    cfg->ble_name_mode       = g_ble_name_mode;
    memcpy(cfg->ble_name, g_ble_name, sizeof(cfg->ble_name));
    cfg->ble_led_mode        = g_ble_led_mode;

    cfg->crc32 = Proto_CRC32((const uint8_t *)cfg, offsetof(TxConfigV2_t, crc32));
}

/* ── Write v2 struct to flash page 60 ────────────────────────────────────── */
static uint8_t _write_v2_to_flash(const TxConfigV2_t *cfg)
{
    UART_Printf("[CFG] v2 save page=%u addr=0x%08lX\r\n",
                FLASH_CONFIG_PAGE, (uint32_t)FLASH_CONFIG_ADDR);

    /* Diagnose flash state before touching (split to stay within 128-byte printf buf) */
    UART_Printf("[CFG] page=%u addr=0x%08lX SFR=0x%08lX WRP1AR=0x%08lX\r\n",
                FLASH_CONFIG_PAGE, (uint32_t)FLASH_CONFIG_ADDR,
                FLASH->SFR, FLASH->WRP1AR);
    UART_Printf("[CFG] SFSA=%lu FSD=%lu WRP1BR=0x%08lX CR=0x%08lX SR2=0x%08lX\r\n",
                FLASH->SFR & 0xFFUL, (FLASH->SFR >> 8U) & 1U,
                FLASH->WRP1BR, FLASH->CR, PWR->SR2);

    /* Acquire exclusive flash access per STM32WB two-semaphore protocol */
    if (!_flash_take_access()) return 0U;

    _flash_sr_clear();

    UART_Printf("[CFG] CR before unlock=0x%08lX\r\n", FLASH->CR);
    if (HAL_FLASH_Unlock() != HAL_OK) {
        UART_Printf("[CFG] unlock FAIL err=0x%08lX SR=0x%08lX\r\n",
                    HAL_FLASH_GetError(), FLASH->SR);
        _flash_release_access();
        return 0U;
    }
    UART_Printf("[CFG] CR after unlock=0x%08lX\r\n", FLASH->CR);

    _flash_sr_clear();

    /* Direct erase — atomic PESD-check + STRT + BSY confirmation */
    if (!_erase_page_direct(FLASH_CONFIG_PAGE, FLASH_CONFIG_ADDR)) {
        HAL_FLASH_Lock();
        _flash_release_access();
        return 0U;
    }

    /* Quick sanity: first word must be 0xFF after erase */
    if (*(volatile uint32_t *)FLASH_CONFIG_ADDR != 0xFFFFFFFFUL) {
        UART_Printf("[CFG] erase verify FAIL: 0x%08lX\r\n",
                    *(volatile uint32_t *)FLASH_CONFIG_ADDR);
        HAL_FLASH_Lock();
        _flash_release_access();
        return 0U;
    }

    /* Write via memcpy — alignment-safe for 4-byte-aligned struct */
    for (uint32_t off = 0U; off < sizeof(*cfg); off += 8U) {
        uint64_t dw;
        memcpy(&dw, (const uint8_t *)cfg + off, sizeof(dw));
        _flash_sr_clear();
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              FLASH_CONFIG_ADDR + off, dw) != HAL_OK) {
            UART_Printf("[CFG] write FAIL off=%lu err=0x%08lX SR=0x%08lX\r\n",
                        off, HAL_FLASH_GetError(), FLASH->SR);
            HAL_FLASH_Lock();
            _flash_release_access();
            return 0U;
        }
    }

    HAL_FLASH_Lock();
    _flash_release_access();

    /* Flush data cache + barriers so verify reads physical flash, not stale cache */
    __DSB();
    __ISB();
    uint32_t acr_post = FLASH->ACR;
    if (acr_post & FLASH_ACR_DCEN) {
        CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCEN);
        SET_BIT(FLASH->ACR,   FLASH_ACR_DCRST);
        CLEAR_BIT(FLASH->ACR, FLASH_ACR_DCRST);
        SET_BIT(FLASH->ACR,   FLASH_ACR_DCEN);
    }
    if (acr_post & FLASH_ACR_ICEN) {
        CLEAR_BIT(FLASH->ACR, FLASH_ACR_ICEN);
        SET_BIT(FLASH->ACR,   FLASH_ACR_ICRST);
        CLEAR_BIT(FLASH->ACR, FLASH_ACR_ICRST);
        SET_BIT(FLASH->ACR,   FLASH_ACR_ICEN);
    }
    __DSB();
    __ISB();

    /* Verify: re-read and CRC-check */
    const TxConfigV2_t *saved = (const TxConfigV2_t *)FLASH_CONFIG_ADDR;
    uint32_t saved_crc = Proto_CRC32((const uint8_t *)saved, offsetof(TxConfigV2_t, crc32));
    if (saved->magic != FLASH_CFG_MAGIC_V2 || saved->crc32 != saved_crc) {
        UART_Printf("[CFG] verify FAIL magic=0x%08lX crc=0x%08lX calc=0x%08lX\r\n",
                    saved->magic, saved->crc32, saved_crc);
        return 0U;
    }

    UART_Printf("[CFG] v2 saved: mode=%u ch=%u pwr=%u dur=%lu per=%lu led=%u\r\n",
                cfg->tx_mode, cfg->ch_idx, cfg->pwr_idx,
                cfg->tx_duration_ms, cfg->tx_period_ms, cfg->led_mode);
    UART_Printf("[CFG]        hours=0x%08lX days=0x%02X months=0x%04X\r\n",
                cfg->active_hours_mask, cfg->active_days_mask, cfg->active_months_mask);
    return 1U;
}

/* ── Save ─────────────────────────────────────────────────────────────────── */
uint8_t FlashConfig_Save(void)
{
    TxConfigV2_t cfg;
    _build_v2(&cfg);
    return _write_v2_to_flash(&cfg);
}

/* ── Staging: populate RAM buffer from current globals ──────────────────── */
void FlashConfig_StagingLoad(void)
{
    _build_v2(&g_cfg_staging);
    g_cfg_staging_dirty = 0U;
}

/* ── Staging: validate and write to flash ───────────────────────────────── */
uint8_t FlashConfig_StagingCommit(void)
{
    /* Recompute CRC — caller may have patched individual fields via OP_CONFIG_WRITE */
    g_cfg_staging.crc32 = Proto_CRC32((const uint8_t *)&g_cfg_staging,
                                       offsetof(TxConfigV2_t, crc32));

    if (g_cfg_staging.magic != FLASH_CFG_MAGIC_V2) {
        g_cfg_staging.magic   = FLASH_CFG_MAGIC_V2;
        g_cfg_staging.version = FLASH_CFG_VERSION_V2;
        /* Recalculate after fixing magic */
        g_cfg_staging.crc32 = Proto_CRC32((const uint8_t *)&g_cfg_staging,
                                           offsetof(TxConfigV2_t, crc32));
    }

    uint8_t ok = _write_v2_to_flash(&g_cfg_staging);
    if (ok) {
        g_cfg_staging_dirty = 0U;
        UART_Print("[CFG] COMMIT OK\r\n");
    } else {
        UART_Print("[CFG] COMMIT FAILED\r\n");
    }
    return ok;
}
