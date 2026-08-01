#include "stm32wbxx_hal.h"
#include "main.h"
#include "svc_power.h"
#include "drv_gekon.h"
#include "drv_uart.h"
#include "hw.h"

void NMI_Handler(void)           { while (1); }
void HardFault_Handler(void)     { Error_Handler(); }
void MemManage_Handler(void)     { while (1); }
void BusFault_Handler(void)      { while (1); }
void UsageFault_Handler(void)    { while (1); }
void SVC_Handler(void)           {}
void DebugMon_Handler(void)      {}
void PendSV_Handler(void)        {}
void SysTick_Handler(void)       { HAL_IncTick(); }
void RTC_WKUP_IRQHandler(void)   { HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc); }
void EXTI0_IRQHandler(void)      { GEKON_EXTI_IRQHandler(); }
void EXTI15_10_IRQHandler(void)  { __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_10); }
void USART1_IRQHandler(void)     { UART_RxIRQ(); }
volatile uint32_t g_ipcc_rx_irq_cnt = 0U;
void IPCC_C1_RX_IRQHandler(void) { g_ipcc_rx_irq_cnt++; HW_IPCC_Rx_Handler(); }
void IPCC_C1_TX_IRQHandler(void) { HW_IPCC_Tx_Handler(); }
void HSEM_IRQHandler(void)       { HAL_HSEM_IRQHandler(); }
