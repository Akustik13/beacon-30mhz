/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "b_wb1m_wpan1_env_sensors.h"
#include "b_wb1m_wpan1_motion_sensors.h"
#include "beacon_config.h"
#include "stm32_seq.h"
#include "hw_if.h"
/* BSP I2C bus — declared extern to avoid MX_I2C1_Init signature conflict from bus.h */
extern int32_t BSP_I2C1_Init(void);
extern int32_t BSP_I2C1_ReadReg(uint16_t DevAddr, uint16_t Reg, uint8_t *pData, uint16_t Length);
extern int32_t BSP_I2C1_WriteReg(uint16_t DevAddr, uint16_t Reg, uint8_t *pData, uint16_t Length);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

IPCC_HandleTypeDef hipcc;

RTC_HandleTypeDef hrtc;

UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_tx;

/* USER CODE BEGIN PV */
uint8_t g_lis2dw12_ok   = 0;
uint8_t g_lis2dw12_addr = 0;
extern volatile uint8_t g_cpu2_state;

/* UART command receive — 2-deep queue so rapid GUI commands aren't silently dropped.
   64 bytes per buffer: longest command is "SET rf_period_ms 300000\0" = 25 chars. */
#define UART_CMD_BUF  64
static uint8_t   uart_rx_byte;
static char      uart_rx_buf[UART_CMD_BUF];
static uint8_t   uart_rx_pos;
volatile uint8_t uart_cmd_ready   = 0;
char             uart_cmd_line[UART_CMD_BUF];
volatile uint8_t uart_cmd_pending  = 0;
char             uart_cmd_pending_line[UART_CMD_BUF];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_DMA_Init(void);
static void MX_GPIO_Init(void);
static void MX_IPCC_Init(void);
static void MX_RTC_Init(void);
static void MX_ADC1_Init(void);
static void MX_RF_Init(void);
/* USER CODE BEGIN PFP */
static uint32_t ADC_ReadCh_raw(uint32_t channel, uint32_t sampling);
uint16_t ADC_ReadVdd_mV(void);
int32_t ADC_ReadChipTemp_x10(uint16_t vdda_mv);
uint32_t ADC_ReadLightRaw(void);
uint16_t ADC_ReadBatteryMv(uint16_t vdda_mv);
uint8_t lis2dw12_init(void);
void lis2dw12_read(int16_t *ax, int16_t *ay, int16_t *az);
int32_t Beacon_GetChipTemp_x10(void);
static void Uart_RxByte_Cb(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
  /* Config code for STM32_WPAN (HSE Tuning must be done before system clock configuration) */
  MX_APPE_Config();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* IPCC initialisation */
  MX_IPCC_Init();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_DMA_Init();
  MX_GPIO_Init();
  MX_RTC_Init();
  MX_ADC1_Init();
  MX_RF_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin, GPIO_PIN_SET); HAL_Delay(80);
  HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin, GPIO_PIN_RESET); HAL_Delay(80);

  /* Beacon hardware GPIO overrides */
  {
    GPIO_InitTypeDef gp = {0};
    gp.Speed = GPIO_SPEED_FREQ_LOW;
    /* PA5 = ADC_IN10 = light sensor analog output */
    gp.Pin = GPIO_PIN_5; gp.Mode = GPIO_MODE_ANALOG; gp.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gp);
    /* PA7 = ADC_IN12 = battery divider signal */
    gp.Pin = GPIO_PIN_7; gp.Mode = GPIO_MODE_ANALOG; gp.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gp);
    /* PA12: battery divider power — LOW=ON, HIGH=OFF */
    gp.Pin = GPIO_PIN_12; gp.Mode = GPIO_MODE_OUTPUT_PP; gp.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gp);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET); /* divider ON */
    /* PB1: light sensor power enable — HIGH=ON.
       Disable EXTI first (CubeMX configured PB1 as button interrupt on eval board). */
    HAL_NVIC_DisableIRQ(EXTI1_IRQn);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_1);
    gp.Pin = GPIO_PIN_1; gp.Mode = GPIO_MODE_OUTPUT_PP; gp.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gp);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); /* light sensor power ON */
  }
  /* USER CODE END 2 */

  /* Init code for STM32_WPAN */
  MX_APPE_Init();

  /* USER CODE BEGIN 2b */
  /* LIS2DW12 init is done inside MX_APPE_Init before CPU2 BLE starts */
  /* Start UART command receiver via hw_uart layer (owns HAL_UART_RxCpltCallback) */
  HW_UART_Receive_IT(hw_uart1, &uart_rx_byte, 1, Uart_RxByte_Cb);
  printf("[UART] CMD receiver ready\r\n");
  /* USER CODE END 2b */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  static uint32_t blink_tick = 0;
  static uint8_t  blink_state = 0;
  while (1)
  {
    /* USER CODE END WHILE */
    MX_APPE_Process();

    /* USER CODE BEGIN 3 */
    /* Deferred Flash save — executed here (main loop, outside UTIL_SEQ) to
       avoid SHCI_C2_FLASH_EraseActivity/UTIL_SEQ_WaitEvt re-entrancy deadlock */
    if (g_config_save_pending) {
      g_config_save_pending = 0;
      int _r = Config_Save();
      /* Config_Save() disables IRQs for ~25ms during flash erase.
         Any UART byte arriving in that window causes overrun → HAL aborts
         receive IT → Uart_RxByte_Cb never fires again.
         Force-restart the receive chain unconditionally after save. */
      HAL_UART_AbortReceive(&huart1);
      uart_rx_pos = 0;
      HW_UART_Receive_IT(hw_uart1, &uart_rx_byte, 1, Uart_RxByte_Cb);
      if (_r == 0) printf("OK saved to Flash page %u (0x%08lX)\r\n",
                          (unsigned)CONFIG_FLASH_PAGE, (unsigned long)CONFIG_FLASH_ADDR);
      else          printf("ERR:flash write failed %d\r\n", _r);
    }

    static uint32_t dbg_last = 0;
    uint32_t now = HAL_GetTick();

    /* Diagnostic fast blink ONLY when CPU2 started but failed (state>=2).
       state=0 = intentionally not started → not an error, use normal LED mode. */
    if (g_cpu2_state >= 2U) {
      if ((now - blink_tick) >= 100U) {
        blink_tick = now;
        blink_state ^= 1U;
        HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin,
                          blink_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
      }
    } else {
      /* LED management for state=0 (CPU2 not started) and state=1 (CPU2 ready) */
      switch (g_config.led_mode) {
        case LED_OFF:
          HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin, GPIO_PIN_RESET);
          break;
        case LED_INIT_ONLY:
          /* boot blink already done in diag_blink(), leave LED off */
          HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin, GPIO_PIN_RESET);
          break;
        case LED_BLE_STATUS:
          /* Owned by BLE_Led_Timer_Callback() in app_ble.c.
             Main loop must not write the LED pin in this mode — would fight the timer. */
          break;
        case LED_TX_PULSE:
          /* handled directly in RF_TX_Task() */
          break;
        case LED_HEARTBEAT:
          if ((now - blink_tick) >= 500U) {
            blink_tick = now;
            blink_state ^= 1U;
            HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin,
                              blink_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
          }
          break;
        case LED_ALWAYS_ON:
          HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin, GPIO_PIN_SET);
          break;
        default:
          break;
      }
    }

    /* CPU2 error messages — only when CPU2 was started but failed (state>=2) */
    static uint32_t cpu2_err_last = 0;
    if (g_cpu2_state >= 2U && (now - cpu2_err_last) >= 2000UL) {
      cpu2_err_last = now;
      if      (g_cpu2_state == 2)    printf("[CPU2] FUS mode! Use CubeProgrammer 'Start Wireless Stack'\r\n");
      else if (g_cpu2_state == 0xFF) printf("[CPU2] timeout - no response\r\n");
      else                           printf("[CPU2] error state=0x%02X\r\n", (unsigned int)g_cpu2_state);
    }

    /* Sensor print — always includes Light + Battery regardless of dbg flags
       (dbg flags control whether enable pins stay permanently ON) */
    uint32_t s_period = g_config.sensor_ms > 0 ? g_config.sensor_ms : 1000U;
    if ((now - dbg_last) >= s_period)
    {
      dbg_last = now;
      uint16_t vdd      = ADC_ReadVdd_mV();
      int32_t  chip_x10 = ADC_ReadChipTemp_x10(vdd);
      int16_t  ax = 0, ay = 0, az = 0;
      if (g_lis2dw12_ok) lis2dw12_read(&ax, &ay, &az);
      long ax_mg = (long)ax * 244L / 1000L;
      long ay_mg = (long)ay * 244L / 1000L;
      long az_mg = (long)az * 244L / 1000L;
      uint32_t _sq  = (uint32_t)(ax_mg*ax_mg + ay_mg*ay_mg + az_mg*az_mg);
      uint32_t _mag = _sq > 1u ? _sq >> 1u : 1u;
      for (int _n = 0; _n < 16; _n++) { if (_mag) _mag = (_mag + _sq/_mag) >> 1u; }
      int32_t chip_i = chip_x10 / 10, chip_f = chip_x10 % 10;
      if (chip_f < 0) chip_f = -chip_f;
      uint32_t light   = ADC_ReadLightRaw();   /* manages PB1 internally */
      uint16_t batt_mv = ADC_ReadBatteryMv(vdd); /* manages PA12 internally */
      printf("T=%ld.%ld C  Ax=%ld Ay=%ld Az=%ld |g|=%lu mg  Light=%lu  Batt=%u mV  VDD=%u mV\r\n",
             (long)chip_i, (long)chip_f, ax_mg, ay_mg, az_mg, (unsigned long)_mag,
             (unsigned long)light, (unsigned)batt_mv, (unsigned)vdd);
    }
  /* USER CODE END 3 */
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE
                              |RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the SYSCLKSource, HCLK, PCLK1 and PCLK2 clocks dividers
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK4|RCC_CLOCKTYPE_HCLK2
                              |RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK2Divider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK4Divider = RCC_SYSCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SMPS|RCC_PERIPHCLK_RFWAKEUP;
  PeriphClkInitStruct.RFWakeUpClockSelection = RCC_RFWKPCLKSOURCE_LSE;
  PeriphClkInitStruct.SmpsClockSelection = RCC_SMPSCLKSOURCE_HSE;
  PeriphClkInitStruct.SmpsDivSelection = RCC_SMPSCLKDIV_RANGE1;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN Smps */

  /* USER CODE END Smps */
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */
  /* ADC async clock = HSI16 (16 MHz): 160 cycles = 10 µs — meets tSTART for TEMPSENSOR */
  RCC_PeriphCLKInitTypeDef PeriphClkInitAdc = {0};
  PeriphClkInitAdc.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInitAdc.AdcClockSelection    = RCC_ADCCLKSOURCE_HSI;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitAdc);
  __HAL_RCC_ADC_CLK_ENABLE();
  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;
  hadc1.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_79CYCLES_5;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_VREFINT;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLINGTIME_COMMON_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */
  HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief IPCC Initialization Function
  * @param None
  * @retval None
  */
static void MX_IPCC_Init(void)
{

  /* USER CODE BEGIN IPCC_Init 0 */

  /* USER CODE END IPCC_Init 0 */

  /* USER CODE BEGIN IPCC_Init 1 */

  /* USER CODE END IPCC_Init 1 */
  hipcc.Instance = IPCC;
  if (HAL_IPCC_Init(&hipcc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IPCC_Init 2 */

  /* USER CODE END IPCC_Init 2 */

}

/**
  * @brief RF Initialization Function
  * @param None
  * @retval None
  */
static void MX_RF_Init(void)
{

  /* USER CODE BEGIN RF_Init 0 */

  /* USER CODE END RF_Init 0 */

  /* USER CODE BEGIN RF_Init 1 */

  /* USER CODE END RF_Init 1 */
  /* USER CODE BEGIN RF_Init 2 */

  /* USER CODE END RF_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = CFG_RTC_ASYNCH_PRESCALER;
  hrtc.Init.SynchPrediv = CFG_RTC_SYNCH_PRESCALER;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the WakeUp
  */
  if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 0, RTC_WAKEUPCLOCK_RTCCLK_DIV16) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_8;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 15, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : SPI1_MOSI_Pin */
  GPIO_InitStruct.Pin = SPI1_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(SPI1_MOSI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PA11 */
  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : SW1_User_Pin */
  GPIO_InitStruct.Pin = SW1_User_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SW1_User_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : Blue_Led_Pin */
  GPIO_InitStruct.Pin = Blue_Led_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(Blue_Led_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* Called by hw_uart.c HAL_UART_RxCpltCallback dispatcher — runs in ISR context */
static void Uart_RxByte_Cb(void)
{
  char c = (char)uart_rx_byte;
  HW_UART_Receive_IT(hw_uart1, &uart_rx_byte, 1, Uart_RxByte_Cb); /* re-arm */
  if (c == '\r' || c == '\n') {
    if (uart_rx_pos > 0) {
      uart_rx_buf[uart_rx_pos] = '\0';
      if (!uart_cmd_ready) {
        /* Slot 0 free — put command here and schedule task */
        memcpy(uart_cmd_line, uart_rx_buf, uart_rx_pos + 1);
        uart_cmd_ready = 1;
        UTIL_SEQ_SetTask(1U << CFG_TASK_UART_CMD_ID, CFG_SCH_PRIO_0);
      } else if (!uart_cmd_pending) {
        /* Slot 0 busy, slot 1 free — buffer in pending slot */
        memcpy(uart_cmd_pending_line, uart_rx_buf, uart_rx_pos + 1);
        uart_cmd_pending = 1;
        /* Task already scheduled; it will move pending→slot0 when done */
      }
      /* else: both slots full → drop (shouldn't happen at GUI 40 ms rate) */
    }
    uart_rx_pos = 0;
  } else if (uart_rx_pos < (uint8_t)(sizeof(uart_rx_buf) - 1)) {
    uart_rx_buf[uart_rx_pos++] = (uint8_t)c;
  }
}

/* Returns VDD in mV by measuring internal VREFINT (calibrated at 3.0V).
   Explicitly configures VREFINT every call — after HAL_ADCEx_Calibration_Start
   the ADC is left disabled and SQR state is unreliable without explicit re-config.
   Averages 4 readings to reduce settling/noise artefacts. */
uint16_t ADC_ReadVdd_mV(void)
{
  ADC_ChannelConfTypeDef s = {0};
  s.Channel      = ADC_CHANNEL_VREFINT;
  s.Rank         = ADC_REGULAR_RANK_1;
  s.SamplingTime = ADC_SAMPLINGTIME_COMMON_1; /* 160 cyc = 10 µs */
  HAL_ADC_ConfigChannel(&hadc1, &s);

  uint32_t sum = 0;
  for (int i = 0; i < 4; i++) {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) { HAL_ADC_Stop(&hadc1); return 0; }
    sum += HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
  }
  uint32_t raw = (sum + 2U) >> 2;  /* rounded average */
  if (raw == 0) return 0;
  /* Restore VREFINT with COMMON_2 so ADC_ReadCh_raw cleanup stays consistent */
  s.SamplingTime = ADC_SAMPLINGTIME_COMMON_2;
  HAL_ADC_ConfigChannel(&hadc1, &s);
  uint32_t cal = *((volatile uint16_t*)0x1FFF75AAU); /* VREFINT factory cal @3.0V */
  uint16_t result = (uint16_t)((3000UL * cal) / raw);
  static uint8_t vdd_dbg_printed = 0;
  if (!vdd_dbg_printed) {
    printf("[VDD_DBG] raw=%lu cal=%lu result=%u mV\r\n", raw, cal, result);
    vdd_dbg_printed = 1;
  }
  return result;
}

/* Switch ADC to given channel, read one sample, restore VREFINT. */
static uint32_t ADC_ReadCh_raw(uint32_t channel, uint32_t sampling)
{
  ADC_ChannelConfTypeDef s = {0};
  s.Channel = channel; s.Rank = ADC_REGULAR_RANK_1; s.SamplingTime = sampling;
  HAL_ADC_ConfigChannel(&hadc1, &s);
  HAL_ADC_Start(&hadc1);
  uint32_t val = 0;
  if (HAL_ADC_PollForConversion(&hadc1, 50) == HAL_OK)
    val = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  s.Channel = ADC_CHANNEL_VREFINT; s.SamplingTime = ADC_SAMPLINGTIME_COMMON_2;
  HAL_ADC_ConfigChannel(&hadc1, &s);
  return val;
}

/* Internal chip temperature. Returns temperature in 0.1°C units.
   tSTART for TEMPSENSOR = max 10 µs after TSEN=1. We enable TSEN, then
   busy-wait ≥10 µs before sampling so the sensor is settled. */
int32_t ADC_ReadChipTemp_x10(uint16_t vdda_mv)
{
  if (vdda_mv == 0) return -9990;
  ADC_ChannelConfTypeDef s = {0};
  s.Channel      = ADC_CHANNEL_TEMPSENSOR;
  s.Rank         = ADC_REGULAR_RANK_1;
  s.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;   /* 160 cyc = 10 µs */
  HAL_ADC_ConfigChannel(&hadc1, &s);            /* sets TSEN = 1 */
  /* Busy-wait ≥10 µs for tSTART: ~700 iterations × ~3 cycles @ 64 MHz ≈ 33 µs */
  volatile uint32_t dly = 700;
  while (dly--) { }
  HAL_ADC_Start(&hadc1);
  uint32_t raw = 0;
  if (HAL_ADC_PollForConversion(&hadc1, 50) == HAL_OK) raw = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  /* Restore VREFINT for subsequent ADC_ReadVdd_mV / ADC_ReadCh_raw calls */
  s.Channel = ADC_CHANNEL_VREFINT; s.SamplingTime = ADC_SAMPLINGTIME_COMMON_2;
  HAL_ADC_ConfigChannel(&hadc1, &s);
  int32_t temp_c = __HAL_ADC_CALC_TEMPERATURE((uint32_t)vdda_mv, raw, ADC_RESOLUTION_12B);
  return temp_c * 10;
}

/* Light sensor on PA5 (ADC_IN10). PB1=HIGH powers the sensor.
   If dbg_light=1 the pin is kept ON continuously (set once via SET command).
   If dbg_light=0 the pin is toggled around each read to save power. */
uint32_t ADC_ReadLightRaw(void)
{
  if (!g_config.dbg_light) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);  /* sensor ON */
    volatile uint32_t d = 8000; while (d--);             /* ~0.5 ms settle @ 64 MHz */
  }
  uint32_t val = ADC_ReadCh_raw(ADC_CHANNEL_10, ADC_SAMPLINGTIME_COMMON_2);
  if (!g_config.dbg_light) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET); /* sensor OFF */
  }
  return val;
}

/* Battery divider on PA7 (ADC_IN12). PA12=LOW enables the divider.
   If dbg_battery=1 the pin is kept LOW continuously.
   If dbg_battery=0 the pin is toggled around each read. */
uint16_t ADC_ReadBatteryMv(uint16_t vdda_mv)
{
  if (!g_config.dbg_battery) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET); /* divider ON */
    volatile uint32_t d = 8000; while (d--);               /* ~0.5 ms settle */
  }
  uint32_t raw = ADC_ReadCh_raw(ADC_CHANNEL_12, ADC_SAMPLINGTIME_COMMON_2);
  if (!g_config.dbg_battery) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_SET);   /* divider OFF */
  }
  /* 10k+10k divider → Vbat = 2 × Vpin */
  return (uint16_t)((uint32_t)raw * (uint32_t)vdda_mv * 2UL / 4096UL);
}

/* ── LIS2DW12TR accelerometer ─────────────────────────────────────────────── */
#define LIS2DW12_WHOAMI  0x0FU
#define LIS2DW12_CTRL1   0x20U
#define LIS2DW12_OUT_X_L 0x28U

uint8_t lis2dw12_init(void)
{
  /* SA0=VCC on beacon board → I2C addr 0x32 (0x19<<1) */
  const uint8_t addr = (0x19U << 1U);
  uint8_t id = 0;
  BSP_I2C1_ReadReg(addr, LIS2DW12_WHOAMI, &id, 1);
  printf("[LIS] addr=0x%02X WHO_AM_I=0x%02X\r\n", addr, id);
  if (id == 0x44U) {
    g_lis2dw12_addr = addr;
    /* CTRL1: ODR=12.5Hz LP, LP_MODE=01 (14-bit), FS=±2g */
    uint8_t ctrl = 0x25U;
    BSP_I2C1_WriteReg(g_lis2dw12_addr, LIS2DW12_CTRL1, &ctrl, 1);
    return 1;
  }
  return 0;
}

/* Read X/Y/Z axes — 14-bit signed (>>2 from 16-bit raw). 244 µg/LSB at ±2g. */
void lis2dw12_read(int16_t *ax, int16_t *ay, int16_t *az)
{
  uint8_t buf[6] = {0};
  BSP_I2C1_ReadReg(g_lis2dw12_addr, LIS2DW12_OUT_X_L, buf, 6);
  *ax = (int16_t)((uint16_t)buf[1] << 8 | buf[0]) >> 2;
  *ay = (int16_t)((uint16_t)buf[3] << 8 | buf[2]) >> 2;
  *az = (int16_t)((uint16_t)buf[5] << 8 | buf[4]) >> 2;
}

/* Wrapper for BLE server apps — reads VDD internally (no parameter needed). */
int32_t Beacon_GetChipTemp_x10(void)
{
  return ADC_ReadChipTemp_x10(ADC_ReadVdd_mV());
}

int __io_putchar(int ch)
{
  uint32_t timeout = 100000U;

  while (!(USART1->ISR & USART_ISR_TXE_TXFNF))
  {
    if (--timeout == 0U)
    {
      return ch;
    }
  }

  USART1->TDR = (uint8_t)ch;
  return ch;
}

/* Override weak HAL callback — restarts receive chain after any UART error
   (overrun, framing error, etc.) so the byte-by-byte chain never stays dead. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) {
    HAL_UART_AbortReceive(huart);
    uart_rx_pos = 0;
    HW_UART_Receive_IT(hw_uart1, &uart_rx_byte, 1, Uart_RxByte_Cb);
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  /* Fast blink on PB0: ~5Hz = init failed somewhere.
     Count blinks from power-on to estimate which init step failed. */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIOB->MODER = (GPIOB->MODER & ~(3U << (0U * 2U))) | (1U << (0U * 2U));
  while (1)
  {
    GPIOB->ODR ^= GPIO_PIN_0;
    for (volatile uint32_t _d = 0; _d < 50000U; _d++);
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
