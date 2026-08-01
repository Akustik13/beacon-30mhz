#include "svc_power.h"
#include "hw_desc.h"
#include "stm32wbxx_hal.h"
#include "drv_uart.h"
#include "app_ble.h"

/* RTC backup register used to detect Shutdown wakeup.
 * WUF1 and C1SBF are not reliably preserved through Shutdown on WB15. */
#define SHUTDOWN_MAGIC  0x5A8F3C1EUL
#define SHUTDOWN_BKUP   RTC_BKP_DR0

#define RTC_ISSET_MAGIC 0xC10C1000UL
#define RTC_ISSET_BKUP  RTC_BKP_DR1

#define SHUTDOWN_TS_BKUP RTC_BKP_DR2  /* unix ts written before Shutdown */

/* HAL tick variable — advance after sleep so LED/timers stay in sync */
extern __IO uint32_t uwTick;

RTC_HandleTypeDef  hrtc;
WakeupReason_t     g_wakeup_reason    = WAKEUP_UNKNOWN;
uint32_t           g_stop1_accumulated_s = 0U;

/* ── RTC MSP (called by HAL_RTC_Init) ────────────────────────────────────── */
void HAL_RTC_MspInit(RTC_HandleTypeDef *h)
{
    (void)h;
    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    clk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
    HAL_RCCEx_PeriphCLKConfig(&clk);
    __HAL_RCC_RTC_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
}

/* ── Init ─────────────────────────────────────────────────────────────────── */
void Power_Init(void)
{
    hrtc.Instance            = RTC;
    hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv   = 127;    /* LSE 32768 / 128 / 256 = 1 Hz */
    hrtc.Init.SynchPrediv    = 255;
    hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutRemap    = RTC_OUTPUT_REMAP_NONE;
    hrtc.Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN;
    HAL_RTC_Init(&hrtc);

    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
}

/* ── Private ──────────────────────────────────────────────────────────────── */

/*
 * Restore only HSE after Stop exit.
 * LSE stays running during Stop2 — never touch its config on wakeup.
 * Calling the full SystemClock_Config() (which writes LSEDRIVE / BDCR) after
 * every wakeup can glitch the LSE oscillator and cause a 5-second stall
 * waiting for LSERDY in HAL_RCC_OscConfig().
 */
static void _restore_hse_clocks(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_HCLK4 | RCC_CLOCKTYPE_HCLK2 |
                         RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_HSE;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    clk.AHBCLK2Divider = RCC_SYSCLK_DIV1;
    clk.AHBCLK4Divider = RCC_SYSCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1);
}

static void _detect_wakeup_reason(void)
{
    if (__HAL_RTC_WAKEUPTIMER_GET_FLAG(&hrtc, RTC_FLAG_WUTF)) {
        g_wakeup_reason = WAKEUP_RTC;
        __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
    } else {
        g_wakeup_reason = WAKEUP_GEKON;
    }
}

static void _uart_rx_exti_enable(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin  = GPIO_PIN_10;
    g.Mode = GPIO_MODE_IT_RISING;   /* PA10 HIGH = UART adapter plugged in */
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &g);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_10);
    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

static void _uart_rx_exti_disable(void)
{
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_10);
    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    /* restore plain input+pulldown so UART_TryReconnect() can read IDR */
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin  = GPIO_PIN_10;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &g);
}

static void _enter_stop2_raw(void)
{
    /*
     * STM32WB1M (WB15xx) does NOT support Stop2.
     * PWR_SUPPORT_STOP2 is undefined for this device — hardware ignores LPMS=010.
     * Deepest available mode is Stop1 (LPMS=001, ~2 µA). Name kept for API compat.
     */

    /* Disable debug stop */
    CLEAR_BIT(DBGMCU->CR, DBGMCU_CR_DBG_STOP);

    /* NOTE: PWR_CR1_FPDS on WB15 = "Flash PD during LPsleep", NOT Stop modes.
     * In Stop1, Flash is powered down automatically — no FPDS write needed. */

    /* CPU2 Shutdown (0b111) — only when BLE is not advertising/connected.
     * While BLE is active, CPU2 must stay awake to run the radio. */
    if (BLE_IsIdle()) {
        MODIFY_REG(PWR->C2CR1, PWR_C2CR1_LPMS,
                   PWR_C2CR1_LPMS_2 | PWR_C2CR1_LPMS_1 | PWR_C2CR1_LPMS_0);
    }

    /* CPU1 → Stop1 (LPMS = 001) — deepest mode on WB15xx */
    MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_0);
    SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

    /*
     * One-time dump just before WFI.
     * Expected: CR1=0x301 (LPMS=1=Stop1) C2CR1=0x7 SCR=0x4 DBGMCU=0x0
     */
    {
        static uint8_t s_first = 1;
        if (s_first) {
            s_first = 0;
            UART_Printf("[PRE-WFI] CR1  =0x%08lX  LPMS=%lu\r\n",
                        PWR->CR1, PWR->CR1 & 7U);
            UART_Printf("[PRE-WFI] C2CR1=0x%08lX  C2LPMS=%lu\r\n",
                        PWR->C2CR1, PWR->C2CR1 & 7U);
            UART_Printf("[PRE-WFI] SCR  =0x%08lX  SLEEPDEEP=%lu\r\n",
                        SCB->SCR, (SCB->SCR >> SCB_SCR_SLEEPDEEP_Pos) & 1U);
            UART_Printf("[PRE-WFI] DBGMCU=0x%08lX DBG_STOP=%lu\r\n",
                        DBGMCU->CR, (DBGMCU->CR >> 1U) & 1U);
            UART_Print("[PRE-WFI] want: CR1=0x301 C2CR1=0x7 SCR=0x4 DBGMCU=0x0\r\n");
            UART_Print("[PRE-WFI] WB1M=WB15xx: Stop2 not supported, using Stop1 ~2uA\r\n");
        }
    }

    __DSB();
    __ISB();
    __WFI();

    /* ═══ Woke up ═══ */
    CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
}

/* ── RTC → Unix timestamp ─────────────────────────────────────────────────── */
uint32_t Power_RTC_GetUnix(void)
{
    if (!Power_RTC_IsSet()) return 0U;
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    Power_RTC_GetDateTime(&t, &d);
    static const uint8_t mdays[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t y    = (uint32_t)d.Year;
    uint32_t m    = (uint32_t)d.Month;
    uint32_t dy   = (uint32_t)d.Date;
    uint32_t days = y * 365U + (y + 3U) / 4U;
    for (uint32_t i = 1U; i < m; i++) {
        days += mdays[i];
        if (i == 2U && (y % 4U == 0U)) days += 1U;
    }
    days += dy - 1U;
    return (10957UL + days) * 86400UL
           + (uint32_t)t.Hours   * 3600U
           + (uint32_t)t.Minutes * 60U
           + (uint32_t)t.Seconds;
}

/* ── Shutdown timestamp ───────────────────────────────────────────────────── */
void Power_SaveShutdownTimestamp(void)
{
    uint32_t ts = Power_RTC_GetUnix();
    if (ts == 0U) return;
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, SHUTDOWN_TS_BKUP, ts);
}

uint32_t Power_GetShutdownElapsedS(void)
{
    HAL_PWR_EnableBkUpAccess();
    uint32_t saved = HAL_RTCEx_BKUPRead(&hrtc, SHUTDOWN_TS_BKUP);
    if (saved == 0U) return 0U;
    uint32_t now = Power_RTC_GetUnix();
    if (now == 0U || now <= saved) return 0U;
    HAL_RTCEx_BKUPWrite(&hrtc, SHUTDOWN_TS_BKUP, 0U);  /* consume once */
    return now - saved;
}

/* ── Stop2 ────────────────────────────────────────────────────────────────── */
void Power_EnterStop2(uint32_t seconds)
{
    HAL_SuspendTick();
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;

    /* ck_spre = 1 Hz → wakeup after (seconds) */
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, seconds - 1U,
                                RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
    _uart_rx_exti_enable();
    _enter_stop2_raw();
    _uart_rx_exti_disable();

    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    /* Restore HSE FIRST — SysTick runs on SYSCLK, must be at correct freq */
    _restore_hse_clocks();
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
    HAL_ResumeTick();

    _detect_wakeup_reason();
    /* Always advance uwTick: EXTI early-wake (PA10/GEKON) leaves it at 0
     * otherwise — sensors and flash-log timers would freeze. */
    uwTick += seconds * 1000U;
    g_stop1_accumulated_s += seconds;
}

/* ── Stop2 ms ─────────────────────────────────────────────────────────────── */

static void _set_rtc_wakeup_ms(uint32_t ms)
{
    if (ms <= 32000U) {
        /* RTCCLK/16: LSE 32768 Hz / 16 = 2048 Hz → 1 tick ≈ 488 µs */
        uint32_t ticks = (ms * 2048U + 999U) / 1000U;
        if (ticks == 0U) ticks = 1U;
        if (ticks > 65535U) ticks = 65535U;
        HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, ticks - 1U, RTC_WAKEUPCLOCK_RTCCLK_DIV16);
    } else {
        uint32_t secs = (ms + 999U) / 1000U;
        HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, secs - 1U, RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
    }
}

static void _wakeup_restore_ms(uint32_t ms)
{
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    /* Restore HSE FIRST — SysTick must run at correct SYSCLK freq */
    _restore_hse_clocks();
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
    HAL_ResumeTick();
    _detect_wakeup_reason();
    uwTick += ms;
    g_stop1_accumulated_s += (ms + 999U) / 1000U;
}

/* Pause / schedule sleep — PA10 EXTI enabled for UART reconnect detection. */
void Power_EnterStop2_ms(uint32_t ms)
{
    if (ms == 0U) return;
    HAL_SuspendTick();
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    _set_rtc_wakeup_ms(ms);
    _uart_rx_exti_enable();
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
    _enter_stop2_raw();
    _uart_rx_exti_disable();
    _wakeup_restore_ms(ms);
}

/* ECO TX sleep — NO PA10 EXTI.
 * RF is ON during this sleep; PA10 IT_RISING would trigger on RF-coupled
 * edges and wake the MCU immediately, creating rapid needle pulses. */
void Power_EnterStop2_ms_tx(uint32_t ms)
{
    if (ms == 0U) return;
    HAL_SuspendTick();
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    _set_rtc_wakeup_ms(ms);
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
    _enter_stop2_raw();
    _wakeup_restore_ms(ms);
}

/* ── Stop1 ─────────────────────────────────────────────────────────────────── */
void Power_EnterStop1(uint32_t seconds)
{
    HAL_SuspendTick();
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    CLEAR_BIT(DBGMCU->CR, DBGMCU_CR_DBG_STOP);

    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, seconds - 1U,
                                RTC_WAKEUPCLOCK_CK_SPRE_16BITS);

    /* CPU2 Shutdown — guarded same as _enter_stop2_raw */
    if (BLE_IsIdle()) {
        MODIFY_REG(PWR->C2CR1, PWR_C2CR1_LPMS,
                   PWR_C2CR1_LPMS_2 | PWR_C2CR1_LPMS_1 | PWR_C2CR1_LPMS_0);
    }

    /* CPU1 → Stop1 (LPMS = 001) */
    MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, PWR_CR1_LPMS_0);
    SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

    __DSB(); __ISB(); __WFI();

    CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
    HAL_ResumeTick();
    _restore_hse_clocks();
    _detect_wakeup_reason();
    g_stop1_accumulated_s += seconds;
}

/* ── Shutdown ─────────────────────────────────────────────────────────────── */
void Power_EnterShutdown(void)
{
    /* Commit uptime counters and save shutdown timestamp BEFORE suspending tick.
     * Flash operations use HAL_GetTick() timeouts — must run while tick is live. */
    HwDesc_Save();
    Power_SaveShutdownTimestamp();

    HAL_SuspendTick();

    /* CPU2 Shutdown */
    MODIFY_REG(PWR->C2CR1, PWR_C2CR1_LPMS,
               PWR_C2CR1_LPMS_2 | PWR_C2CR1_LPMS_1 | PWR_C2CR1_LPMS_0);

    /* WKUP1=PA0, falling edge (button press pulls PA0 LOW).
     * Long press may enter Shutdown with PA0 already LOW — edge detection
     * means no spurious wakeup; only the NEXT falling edge (new press) wakes. */
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN1);
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN4);
    SET_BIT(PWR->CR4, PWR_CR4_WP1);              /* falling edge (active-LOW) */
    HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1);
    WRITE_REG(PWR->SCR, PWR_SCR_CWUF);           /* clear WUF after enable */

    /* CPU1 Shutdown (LPMS = 111) */
    MODIFY_REG(PWR->CR1, PWR_CR1_LPMS,
               PWR_CR1_LPMS_0 | PWR_CR1_LPMS_1 | PWR_CR1_LPMS_2);
    SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

    /* Expect: LPMS=7, WP1=1 (CR4 bit0), EWUP1=1 (CR3 bit0), SR1=0 */
    UART_Printf("[SHUTDOWN] CR1=0x%08lX CR3=0x%08lX CR4=0x%08lX SR1=0x%08lX\r\n",
                PWR->CR1, PWR->CR3, PWR->CR4, PWR->SR1);
    UART_Print("[SHUTDOWN] entering — short press PA0 to wake\r\n");
    UART_CheckIdle();

    /* Mark Shutdown in RTC backup register (VBAT domain survives Shutdown) */
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, SHUTDOWN_BKUP, SHUTDOWN_MAGIC);

    /* Disable LSE to eliminate ~700 nA oscillator current in Shutdown.
     * WKUP1 (PA0) wakes the device without needing LSE/RTC.
     * After wakeup, SystemClock_Config() re-enables LSE (~5 s stabilisation). */
    __HAL_RCC_LSE_CONFIG(RCC_LSE_OFF);

    __DSB();
    __ISB();
    __WFI();

    /* Reached only if Shutdown entry was blocked */
    CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
    HAL_RTCEx_BKUPWrite(&hrtc, SHUTDOWN_BKUP, 0);
    HAL_ResumeTick();
    UART_Printf("[SHUTDOWN] FAILED — SR1=0x%08lX CR4=0x%08lX\r\n",
                PWR->SR1, PWR->CR4);
}

/* ── Wakeup detection ─────────────────────────────────────────────────────── */
uint8_t Power_WokeFromShutdown(void)
{
    /* Check RTC backup register written before Shutdown.
     * VBAT domain (RTC BKP regs) survives Shutdown on WB15. */
    HAL_PWR_EnableBkUpAccess();
    if (HAL_RTCEx_BKUPRead(&hrtc, SHUTDOWN_BKUP) == SHUTDOWN_MAGIC) {
        HAL_RTCEx_BKUPWrite(&hrtc, SHUTDOWN_BKUP, 0);
        return 1U;
    }
    return 0U;
}

/* ── RTC: is-set marker ──────────────────────────────────────────────────── */
uint8_t Power_RTC_IsSet(void)
{
    HAL_PWR_EnableBkUpAccess();
    return HAL_RTCEx_BKUPRead(&hrtc, RTC_ISSET_BKUP) == RTC_ISSET_MAGIC ? 1U : 0U;
}

/* ── RTC: set date/time ─────────────────────────────────────────────────── */
void Power_RTC_SetDateTime(uint16_t year, uint8_t month, uint8_t day,
                           uint8_t hour, uint8_t min, uint8_t sec, uint8_t wday)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    t.Hours          = hour;
    t.Minutes        = min;
    t.Seconds        = sec;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;

    d.WeekDay = wday;
    d.Month   = month;
    d.Date    = day;
    d.Year    = (uint8_t)(year >= 2000U ? year - 2000U : 0U);

    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);

    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_ISSET_BKUP, RTC_ISSET_MAGIC);
}

/* ── RTC: get date/time ─────────────────────────────────────────────────── */
void Power_RTC_GetDateTime(RTC_TimeTypeDef *t, RTC_DateTypeDef *d)
{
    HAL_RTC_GetTime(&hrtc, t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, d, RTC_FORMAT_BIN);   /* must follow GetTime */
}

/* ── RTC: print to UART ─────────────────────────────────────────────────── */
void Power_RTC_PrintDateTime(void)
{
    static const char *s_wday[] = {"?","Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    static const char *s_mon[]  = {"?","Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
    RTC_TimeTypeDef t; RTC_DateTypeDef d;
    Power_RTC_GetDateTime(&t, &d);
    uint8_t wd = (d.WeekDay <= 7U) ? d.WeekDay : 0U;
    uint8_t mo = (d.Month   <= 12U) ? d.Month  : 0U;
    UART_Printf("[RTC] 20%02u-%02u-%02u %02u:%02u:%02u %s %s\r\n",
                d.Year, d.Month, d.Date,
                t.Hours, t.Minutes, t.Seconds,
                s_wday[wd], Power_RTC_IsSet() ? "(SET)" : "(NOT SET)");
    (void)s_mon[mo];   /* suppress unused warning; kept for future use */
}

/* ── Diagnostic ───────────────────────────────────────────────────────────── */
void Power_PrintDiag(void)
{
    uint32_t sr2  = PWR->SR2;
    uint32_t cr1  = PWR->CR1;
    uint32_t c2cr = PWR->C2CR1;
    uint32_t dbg  = DBGMCU->CR;

    UART_Print("\r\n[PWR DIAG] --- before first Stop1 (WB1M max) ---\r\n");

    /* CPU2 boot status: bit14 of SR2 */
    UART_Printf("  SR2    = 0x%08lX  C2BOOTS(bit14)=%lu  ",
                sr2, (sr2 >> 14U) & 1U);
    UART_Print((sr2 & (1U << 14U)) ? "CPU2 RUNNING!\r\n" : "CPU2 in reset OK\r\n");

    /* CR1: LPMS[2:0]=bits[2:0], FPDS=bit5 on WB15 (LPsleep only, not Stop) */
    UART_Printf("  CR1    = 0x%08lX  LPMS=%lu  FPDS(bit5)=%lu\r\n",
                cr1, cr1 & 0x07U, (cr1 >> 5U) & 1U);

    /* C2CR1: LPMS[2:0]=bits[2:0], want 0b111=7 for CPU2 Shutdown */
    UART_Printf("  C2CR1  = 0x%08lX  C2_LPMS=%lu  ",
                c2cr, c2cr & 0x07U);
    UART_Print((c2cr & 0x07U) == 0x07U ? "Shutdown OK\r\n" : "NOT Shutdown!\r\n");

    /* DBGMCU->CR: bit1=DBG_STOP, want 0 */
    UART_Printf("  DBGMCU = 0x%08lX  DBG_STOP(bit1)=%lu  ",
                dbg, (dbg >> 1U) & 1U);
    UART_Print(((dbg >> 1U) & 1U) ? "SET (ST-LINK holds MCU awake!)\r\n"
                                   : "cleared OK\r\n");

    UART_Print("[PWR DIAG] ----------------------------\r\n\r\n");
}
