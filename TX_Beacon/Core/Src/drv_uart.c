#include "drv_uart.h"
#include "stm32wbxx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define TX_TIMEOUT_MS    100U
#define PRINTF_BUF       128U
#define RX_BUF_SIZE      192U   /* enlarged for machine protocol lines (up to 160 chars) */

static UART_HandleTypeDef s_uart;
static uint8_t s_active = 0;

/* ── Silent mode ──────────────────────────────────────────────────────────── */
uint8_t        g_uart_silent  = 0U;  /* 0 = verbose, 1 = silent               */
static uint8_t s_silent_ttl   = 0U;  /* countdown seconds until auto-revert   */

static volatile uint8_t  s_rx_buf[RX_BUF_SIZE];
static volatile uint8_t  s_rx_head = 0;
static volatile uint8_t  s_rx_tail = 0;
static volatile uint32_t s_last_rx_tick = 0U;  /* updated in ISR, guards CheckIdle */
static uint8_t           s_idle_cnt = 0U;       /* consecutive LOW readings before disconnect */
static UartNusHook_t     s_nus_hook = NULL;

/* ── HAL MSP callbacks (called by HAL_UART_Init / HAL_UART_DeInit) ────────── */

void HAL_UART_MspInit(UART_HandleTypeDef *h)
{
    GPIO_InitTypeDef g = {0};
    if (h->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Mode      = GPIO_MODE_AF_PP;
        g.Speed     = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF7_USART1;
        /* TX PA9: no pull — MCU drives it */
        g.Pin  = GPIO_PIN_9;
        g.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &g);
        /* RX PA10: pull-down → HIGH when PC connected, LOW when disconnected */
        g.Pin  = GPIO_PIN_10;
        g.Pull = GPIO_PULLDOWN;
        HAL_GPIO_Init(GPIOA, &g);
    }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *h)
{
    GPIO_InitTypeDef g = {0};
    if (h->Instance == USART1) {
        __HAL_RCC_USART1_CLK_DISABLE();
        /* TX PA9: float to save power */
        g.Pin  = GPIO_PIN_9;
        g.Mode = GPIO_MODE_ANALOG;
        g.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &g);
        /* RX PA10: keep INPUT+PULLDOWN so we can detect reconnection via IDR */
        g.Pin  = GPIO_PIN_10;
        g.Mode = GPIO_MODE_INPUT;
        g.Pull = GPIO_PULLDOWN;
        HAL_GPIO_Init(GPIOA, &g);
    }
}

/* ── Public ───────────────────────────────────────────────────────────────── */

uint8_t UART_IsConnected(void)
{
    /* Probe PA10 (RX line): if PC holds it HIGH via USB-UART adapter → connected.
     * Leave as INPUT+PULLDOWN so TryReconnect() can read IDR at any time. */
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin  = GPIO_PIN_10;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_Delay(2);
    return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == GPIO_PIN_SET) ? 1U : 0U;
}

void UART_Init(void)
{
    if (s_active) return;
    s_uart.Instance          = USART1;
    s_uart.Init.BaudRate     = 115200;
    s_uart.Init.WordLength   = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits     = UART_STOPBITS_1;
    s_uart.Init.Parity       = UART_PARITY_NONE;
    s_uart.Init.Mode         = UART_MODE_TX_RX;
    s_uart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&s_uart);
    s_active = 1;
    UART_RxEnable();
}

void UART_DeInit(void)
{
    if (!s_active) return;
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
    HAL_UART_DeInit(&s_uart);
    s_active = 0;
}

uint8_t UART_IsActive(void) { return s_active; }

void UART_SetNusHook(UartNusHook_t hook) { s_nus_hook = hook; }

void UART_ActivityUpdate(void) { /* reserved — line detection replaces idle timeout */ }

void UART_CheckIdle(void)
{
    if (!s_active) return;
    /* Suppress check while receiving or transmitting: guard 500ms after last activity. */
    if ((HAL_GetTick() - s_last_rx_tick) < 500U) { s_idle_cnt = 0U; return; }
    /* PA10 in AF+PULLDOWN: HIGH = PC connected, LOW = disconnected.
     * Debounce: require 3 consecutive LOW readings (≈15ms apart) to filter
     * glitches from RF coupling or UART start-bits during download bursts. */
    if ((GPIOA->IDR & GPIO_PIN_10) != 0U) { s_idle_cnt = 0U; return; }
    if (++s_idle_cnt < 3U) return;
    s_idle_cnt = 0U;
    UART_Print("[UART] disconnected\r\n");
    UART_DeInit();
}

void UART_TryReconnect(void)
{
    if (s_active) return;
    /* Debounce: require 3 consecutive HIGH readings before reconnecting.
     * Prevents a single glitch or partial byte from triggering reinit. */
    static uint8_t s_high_cnt = 0U;
    if (GPIOA->IDR & GPIO_PIN_10) {
        if (++s_high_cnt >= 3U) {
            s_high_cnt = 0U;
            s_idle_cnt = 0U;
            s_last_rx_tick = HAL_GetTick();  /* reset guard so CheckIdle won't fire immediately */
            UART_Init();
            UART_Print("[UART] reconnected\r\n");
        }
    } else {
        s_high_cnt = 0U;
    }
}

void UART_Print(const char *str)
{
    if (!str) return;
    if (!g_uart_silent && s_active) {
        HAL_UART_Transmit(&s_uart, (const uint8_t *)str,
                          (uint16_t)strlen(str), TX_TIMEOUT_MS);
        s_last_rx_tick = HAL_GetTick();
    }
    /* Route to NUS regardless of silent mode — GUI handles all output */
    if (s_nus_hook) s_nus_hook(str);
}

/* Machine-protocol output — always transmitted regardless of silent mode */
void UART_MachPrint(const char *str)
{
    if (!str) return;
    if (s_active) {
        HAL_UART_Transmit(&s_uart, (const uint8_t *)str,
                          (uint16_t)strlen(str), TX_TIMEOUT_MS);
        s_last_rx_tick = HAL_GetTick();
    }
    if (s_nus_hook) s_nus_hook(str);
}

void UART_MachPrintf(const char *fmt, ...)
{
    char buf[PRINTF_BUF];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UART_MachPrint(buf);
}

/* ── Silent-mode helpers ─────────────────────────────────────────────────── */

void UART_SilentSet(uint8_t on)
{
    g_uart_silent = on ? 1U : 0U;
    s_silent_ttl  = on ? 10U : 0U;
}

void UART_SilentKeepalive(void)
{
    /* Auto-activate silent on any machine command; reset 10-s inactivity window.
     * Terminal users never send #id lines → silent never activates → AT stays on. */
    g_uart_silent = 1U;
    s_silent_ttl  = 10U;
}

void UART_SilentTick(void)
{
    static uint32_t s_last_ms = 0U;
    if (!g_uart_silent) return;
    uint32_t now = HAL_GetTick();
    if (now - s_last_ms < 1000U) return;
    s_last_ms = now;
    if (s_silent_ttl > 0U) {
        if (--s_silent_ttl == 0U) {
            g_uart_silent = 0U;
            UART_Print("[UART] verbose restored (keepalive timeout)\r\n");
        }
    }
}

void UART_RxEnable(void)
{
    if (!s_active) return;
    __HAL_UART_ENABLE_IT(&s_uart, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void UART_RxIRQ(void)
{
    if (!s_active) return;
    /* Clear ORE (overrun) — without EIE it won't re-trigger, but the flag
     * can latch and prevent RXNE from being set for the next character. */
    if (__HAL_UART_GET_FLAG(&s_uart, UART_FLAG_ORE)) {
        USART1->ICR = USART_ICR_ORECF;
    }
    if (__HAL_UART_GET_FLAG(&s_uart, UART_FLAG_RXNE)) {
        uint8_t ch  = (uint8_t)(s_uart.Instance->RDR & 0xFFU);
        uint8_t nxt = (uint8_t)((s_rx_head + 1U) % RX_BUF_SIZE);
        if (nxt != s_rx_tail) {
            s_rx_buf[s_rx_head] = ch;
            s_rx_head = nxt;
        }
        s_last_rx_tick = HAL_GetTick();  /* suppress CheckIdle for 500ms after each byte */
    }
}

uint8_t UART_RxGetChar(uint8_t *ch)
{
    if (s_rx_tail == s_rx_head) return 0U;
    *ch      = s_rx_buf[s_rx_tail];
    s_rx_tail = (uint8_t)((s_rx_tail + 1U) % RX_BUF_SIZE);
    return 1U;
}

void UART_Printf(const char *fmt, ...)
{
    char buf[PRINTF_BUF];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UART_Print(buf);
}
