#pragma once

#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_HSEM_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_RTC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* STM32WB1M module: HSE = 32 MHz internal crystal */
#define HSE_VALUE                32000000U
#define HSE_STARTUP_TIMEOUT      100U
#define LSE_VALUE                32768U
#define LSE_STARTUP_TIMEOUT      5000U
#define HSI_VALUE                16000000U
#define MSI_VALUE                4000000U
#define LSI_VALUE                32000U
#define VDD_VALUE                3300U
#define TICK_INT_PRIORITY        0U
#define USE_RTOS                 0U
#define PREFETCH_ENABLE          0U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE        1U

#include "stm32wbxx_hal_def.h"

#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32wbxx_hal_flash.h"
  #include "stm32wbxx_hal_flash_ex.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32wbxx_hal_gpio.h"
#endif
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32wbxx_hal_rcc.h"
  #include "stm32wbxx_hal_rcc_ex.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32wbxx_hal_cortex.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32wbxx_hal_pwr.h"
  #include "stm32wbxx_hal_pwr_ex.h"
#endif
#ifdef HAL_RTC_MODULE_ENABLED
  #include "stm32wbxx_hal_rtc.h"
  #include "stm32wbxx_hal_rtc_ex.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32wbxx_hal_dma.h"
#endif
#ifdef HAL_ADC_MODULE_ENABLED
  #include "stm32wbxx_hal_adc.h"
  #include "stm32wbxx_hal_adc_ex.h"
#endif
#ifdef HAL_HSEM_MODULE_ENABLED
  #include "stm32wbxx_hal_hsem.h"
#endif
#ifdef HAL_I2C_MODULE_ENABLED
  #include "stm32wbxx_hal_i2c.h"
  #include "stm32wbxx_hal_i2c_ex.h"
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
  #include "stm32wbxx_hal_iwdg.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32wbxx_hal_uart.h"
  #include "stm32wbxx_hal_uart_ex.h"
#endif

#define assert_param(expr) ((void)0U)
