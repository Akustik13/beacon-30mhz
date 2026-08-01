/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_entry.c
  * @author  MCD Application Team
  * @brief   Entry point of the application
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
#include "app_common.h"
#include "main.h"
#include "app_entry.h"
#include "app_ble.h"
#include "ble.h"
#include "tl.h"
#include "stm32_seq.h"
#include "shci_tl.h"
#include "stm32_lpm.h"
#include "app_debug.h"
#include "dbg_trace.h"
#include "shci.h"
#include "otp.h"

/* Private includes -----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "hw_if.h"
#include "rf_tx.h"
#include "beacon_config.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
extern RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private defines -----------------------------------------------------------*/
#define POOL_SIZE (CFG_TLBLE_EVT_QUEUE_LENGTH*4U*DIVC((sizeof(TL_PacketHeader_t) + TL_BLE_EVENT_FRAME_SIZE), 4U))

/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macros ------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t EvtPool[POOL_SIZE];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t SystemCmdBuffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t SystemSpareEvtBuffer[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255U];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t BleSpareEvtBuffer[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255];

/* USER CODE BEGIN PV */
volatile uint8_t g_cpu2_state = 0; /* 0=not started, 1=wireless OK, 2=FUS mode, 0xFF=timeout */

/* Global beacon config — loaded from Flash or defaults on boot */
BeaconConfig_t g_config;

/* Set 1 inside Uart_Cmd_Task, cleared + executed in main() while loop */
volatile uint8_t g_config_save_pending = 0;

/* Guards appe_Tl_Init() — called at most once (CPU2 can't restart without MCU reset) */
static uint8_t g_cpu2_tl_inited = 0;

/* BLE advertising/connection state flags defined in app_ble.c */
extern volatile uint8_t g_ble_advertising;
extern volatile uint8_t g_ble_connected;

/* Deferred BLE control: UART task sets this flag and schedules BLE_Ctrl_Task.
 * BLE_Ctrl_Task is in the HCI list and actually calls aci_* functions.
 * This way UART task (now in NO_HCI list) NEVER calls HCI — no deadlock possible. */
typedef enum {
  BLE_CTRL_NONE    = 0,
  BLE_CTRL_START   = 1,  /* set_tx_power + Adv_Restart */
  BLE_CTRL_STOP    = 2,  /* Adv_Stop */
  BLE_CTRL_RESTART = 3,  /* set_tx_power + Adv_Restart (after param change) */
  BLE_CTRL_PWR     = 4,  /* set_tx_power only (no adv change) */
} BleCtrl_t;
static volatile BleCtrl_t g_ble_ctrl_cmd = BLE_CTRL_NONE;

/* UART command interface (buffers defined in main.c, UART_CMD_BUF=64) */
extern volatile uint8_t uart_cmd_ready;
extern char             uart_cmd_line[64];
extern volatile uint8_t uart_cmd_pending;
extern char             uart_cmd_pending_line[64];
/* USER CODE END PV */

/* Private functions prototypes-----------------------------------------------*/
static void Config_HSE(void);
static void Reset_Device(void);
#if (CFG_HW_RESET_BY_FW == 1)
static void Reset_IPCC(void);
static void Reset_BackupDomain(void);
#endif /* CFG_HW_RESET_BY_FW == 1*/
static void System_Init(void);
static void SystemPower_Config(void);
static void appe_Tl_Init(void);
static void APPE_SysStatusNot(SHCI_TL_CmdStatus_t status);
static void APPE_SysUserEvtRx(void * pPayload);
static void APPE_SysEvtReadyProcessing(void * pPayload);
static void APPE_SysEvtError(void * pPayload);
static void Init_Rtc(void);

/* USER CODE BEGIN PFP */
static void diag_blink(void);
/* BSP I2C bus (avoid including b_wb1m_wpan1_bus.h — conflicts with main.h MX_I2C1_Init signature) */
extern int32_t  BSP_I2C1_Init(void);
/* LIS2DW12 + ADC functions defined in main.c */
extern uint8_t  g_lis2dw12_ok;
extern uint8_t  g_lis2dw12_addr;
extern uint8_t  lis2dw12_init(void);
extern void     lis2dw12_read(int16_t *ax, int16_t *ay, int16_t *az);
extern uint16_t ADC_ReadVdd_mV(void);
extern int32_t  ADC_ReadChipTemp_x10(uint16_t vdda_mv);
extern uint32_t ADC_ReadLightRaw(void);
extern uint16_t ADC_ReadBatteryMv(uint16_t vdda_mv);
/* Config + RF + UART command prototypes */
static uint32_t Config_CalcCrc(const BeaconConfig_t *c);
void            Config_SetDefaults(BeaconConfig_t *c);
static int      Config_Load(void);
int             Config_Save(void);
static void     Config_Print(void);
static void     rf_enable(uint8_t on);
static void     rf_set_ch(uint8_t ch);
static void     rf_set_pwr(uint8_t pwr);
static void     RF_Timer_Callback(void);
static void     RF_TX_Task(void);
static void     BLE_Ctrl_Task(void);
static void     Uart_Cmd_Task(void);
/* USER CODE END PFP */

/* Functions Definition ------------------------------------------------------*/
void MX_APPE_Config(void)
{
  /**
   * The OPTVERR flag is wrongly set at power on
   * It shall be cleared before using any HAL_FLASH_xxx() api
   */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);

  /**
   * Reset some configurations so that the system behave in the same way
   * when either out of nReset or Power On
   */
  Reset_Device();

  /* Configure HSE Tuning */
  Config_HSE();

  return;
}

void MX_APPE_Init(void)
{
  System_Init();       /**< System initialization */

  SystemPower_Config(); /**< Configure the system Power Mode */

  HW_TS_Init(hw_ts_InitMode_Full, &hrtc); /**< Initialize the TimerServer */

/* USER CODE BEGIN APPE_Init_1 */
  APPD_Init();
  diag_blink();
  printf("[INIT] system+UART OK\r\n");

  UTIL_LPM_SetOffMode(1 << CFG_LPM_APP, UTIL_LPM_DISABLE);
  APPE_Led_Init();
  /* APPE_Button_Init() moved below appe_Tl_Init() — configuring PB1 as EXTI
   * before the transport layer is up fires a spurious interrupt that calls
   * Adv_Request() with an uninitialized BLE stack → NULL-deref → HardFault */

  /* Init I2C bus then LIS2DW12 */
  BSP_I2C1_Init();
  g_lis2dw12_ok = lis2dw12_init();
  printf("[INIT] LIS2DW12: %s (addr=0x%02X)\r\n",
         g_lis2dw12_ok ? "OK" : "FAIL", (unsigned)g_lis2dw12_addr);
  diag_blink();

  /* Sensor snapshot before BLE starts */
  {
    uint16_t vdd     = ADC_ReadVdd_mV();
    int32_t  t_x10   = ADC_ReadChipTemp_x10(vdd);
    uint32_t light   = ADC_ReadLightRaw();
    uint16_t batt_mv = ADC_ReadBatteryMv(vdd);
    int16_t  ax = 0, ay = 0, az = 0;
    if (g_lis2dw12_ok) lis2dw12_read(&ax, &ay, &az);
    int32_t ax_mg = (int32_t)ax * 244 / 1000;
    int32_t ay_mg = (int32_t)ay * 244 / 1000;
    int32_t az_mg = (int32_t)az * 244 / 1000;
    int32_t t_int = t_x10 / 10;
    int32_t t_frac = t_x10 < 0 ? -(t_x10 % 10) : t_x10 % 10;
    printf("T=%ld.%ld C  Light=%lu  Ax=%ld Ay=%ld Az=%ld mg  Batt=%u mV  VDD=%u mV\r\n",
           (long)t_int, (long)t_frac, (unsigned long)light,
           (long)ax_mg, (long)ay_mg, (long)az_mg,
           (unsigned)batt_mv, (unsigned)vdd);
  }
  diag_blink();

  /* Load config from Flash page 26, fall back to defaults */
  {
    int r = Config_Load();
    if (r != 0) {
      Config_SetDefaults(&g_config);
      printf("[CFG] no valid Flash config (%d) — defaults loaded\r\n", r);
    } else {
      printf("[CFG] loaded from Flash  rf_mode=%u led=%u\r\n",
             (unsigned)g_config.rf_mode, (unsigned)g_config.led_mode);
    }
    /* Apply debug pin states from config (override boot defaults) */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, g_config.dbg_battery ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1,  g_config.dbg_light   ? GPIO_PIN_SET   : GPIO_PIN_RESET);
  }

  /* UART command task (NO_HCI — must never call HCI directly) */
  UTIL_SEQ_RegTask(1U << CFG_TASK_UART_CMD_ID, UTIL_SEQ_RFU, Uart_Cmd_Task);
  /* Deferred BLE control task (HCI list — executes aci_* calls on behalf of UART task) */
  UTIL_SEQ_RegTask(1U << CFG_TASK_BLE_CTRL_ID, UTIL_SEQ_RFU, BLE_Ctrl_Task);
  printf("[CFG] UART cmd task ready — type HELP\\r\\n for commands\r\n");

  /* RF TX 30 MHz — independent of BLE, init and start immediately */
  RF_TX_Init();
  RF_TX_Start();

  printf("[INIT] CPU2 not started — send CPU2INIT to start BLE stack\r\n");
/* USER CODE END APPE_Init_1 */
  /* appe_Tl_Init() NOT called here — CPU2 init is manual via CPU2INIT UART command.
   * This allows measuring MCU-only vs MCU+CPU2 current draw. */

  /**
   * From now, the application is waiting for the ready event (VS_HCI_C2_Ready)
   * received on the system channel before starting the Stack
   * This system event is received with APPE_SysUserEvtRx()
   */
/* USER CODE BEGIN APPE_Init_2 */

/* USER CODE END APPE_Init_2 */

   return;
}

void Init_Smps(void)
{
#if (CFG_USE_SMPS != 0)
  /**
   *  Configure and enable SMPS
   *
   *  The SMPS configuration is not yet supported by CubeMx
   *  when SMPS output voltage is set to 1.4V, the RF output power is limited to 3.7dBm
   *  the SMPS output voltage shall be increased for higher RF output power
   */
  LL_PWR_SMPS_SetStartupCurrent(LL_PWR_SMPS_STARTUP_CURRENT_80MA);
  LL_PWR_SMPS_SetOutputVoltageLevel(LL_PWR_SMPS_OUTPUT_VOLTAGE_1V40);
  LL_PWR_SMPS_Enable();
#endif /* CFG_USE_SMPS != 0 */

  return;
}

void Init_Exti(void)
{
  /* Enable IPCC(36), HSEM(38) wakeup interrupts on CPU1 */
  LL_EXTI_EnableIT_32_63(LL_EXTI_LINE_36 | LL_EXTI_LINE_38);

  return;
}

/* USER CODE BEGIN FD */

/* USER CODE END FD */

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/
static void Reset_Device(void)
{
#if (CFG_HW_RESET_BY_FW == 1)
  Reset_BackupDomain();

  Reset_IPCC();
#endif /* CFG_HW_RESET_BY_FW == 1 */

  return;
}

#if (CFG_HW_RESET_BY_FW == 1)
static void Reset_BackupDomain(void)
{
  if ((LL_RCC_IsActiveFlag_PINRST() != FALSE) && (LL_RCC_IsActiveFlag_SFTRST() == FALSE))
  {
    HAL_PWR_EnableBkUpAccess(); /**< Enable access to the RTC registers */

    /**
     *  Write twice the value to flush the APB-AHB bridge
     *  This bit shall be written in the register before writing the next one
     */
    HAL_PWR_EnableBkUpAccess();

    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();
  }

  return;
}

static void Reset_IPCC(void)
{
  LL_AHB3_GRP1_EnableClock(LL_AHB3_GRP1_PERIPH_IPCC);

  LL_C1_IPCC_ClearFlag_CHx(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C2_IPCC_ClearFlag_CHx(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C1_IPCC_DisableTransmitChannel(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C2_IPCC_DisableTransmitChannel(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C1_IPCC_DisableReceiveChannel(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C2_IPCC_DisableReceiveChannel(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  return;
}
#endif /* CFG_HW_RESET_BY_FW == 1 */

static void Config_HSE(void)
{
    OTP_ID0_t * p_otp;

  /**
   * Read HSE_Tuning from OTP
   */
  p_otp = (OTP_ID0_t *) OTP_Read(0);
  if (p_otp)
  {
    LL_RCC_HSE_SetCapacitorTuning(p_otp->hse_tuning);
  }

  return;
}

static void System_Init(void)
{
  Init_Smps();

  Init_Exti();

  Init_Rtc();

  return;
}

static void Init_Rtc(void)
{
  /* Disable RTC registers write protection */
  LL_RTC_DisableWriteProtection(RTC);

  LL_RTC_WAKEUP_SetClock(RTC, CFG_RTC_WUCKSEL_DIVIDER);

  /* Enable RTC registers write protection */
  LL_RTC_EnableWriteProtection(RTC);

  return;
}

/**
 * @brief  Configure the system for power optimization
 *
 * @note  This API configures the system to be ready for low power mode
 *
 * @param  None
 * @retval None
 */
static void SystemPower_Config(void)
{
  /**
   * Select HSI as system clock source after Wake Up from Stop mode
   */
  LL_RCC_SetClkAfterWakeFromStop(LL_RCC_STOP_WAKEUPCLOCK_HSI);

  /* Initialize low power manager */
  UTIL_LPM_Init();
  /* Initialize the CPU2 reset value before starting CPU2 with C2BOOT */
  LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);

#if (CFG_USB_INTERFACE_ENABLE != 0)
  /**
   *  Enable USB power
   */
  HAL_PWREx_EnableVddUSB();
#endif /* CFG_USB_INTERFACE_ENABLE != 0 */
  /**
   * Active SRAM retention for standby support
   */
  HAL_PWREx_EnableSRAMRetention();

  return;
}

static void appe_Tl_Init(void)
{
  TL_MM_Config_t tl_mm_config;
  SHCI_TL_HciInitConf_t SHci_Tl_Init_Conf;
  /**< Reference table initialization */
  TL_Init();

  /**< System channel initialization */
  UTIL_SEQ_RegTask(1 << CFG_TASK_SYSTEM_HCI_ASYNCH_EVT_ID, UTIL_SEQ_RFU, shci_user_evt_proc);
  /* BLE stack init runs as a task so SHCI_C2_BLE_Init doesn't deadlock inside SHCI event callback */
  UTIL_SEQ_RegTask(1 << CFG_TASK_BLE_INIT_ID, UTIL_SEQ_RFU, APP_BLE_Init);
  SHci_Tl_Init_Conf.p_cmdbuffer = (uint8_t*)&SystemCmdBuffer;
  SHci_Tl_Init_Conf.StatusNotCallBack = APPE_SysStatusNot;
  shci_init(APPE_SysUserEvtRx, (void*) &SHci_Tl_Init_Conf);

  /**< Memory Manager channel initialization */
  tl_mm_config.p_BleSpareEvtBuffer = BleSpareEvtBuffer;
  tl_mm_config.p_SystemSpareEvtBuffer = SystemSpareEvtBuffer;
  tl_mm_config.p_AsynchEvtPool = EvtPool;
  tl_mm_config.AsynchEvtPoolSize = POOL_SIZE;
  TL_MM_Init(&tl_mm_config);

  TL_Enable();

  return;
}

static void APPE_SysStatusNot(SHCI_TL_CmdStatus_t status)
{
  UNUSED(status);
  return;
}

/**
 * The type of the payload for a system user event is tSHCI_UserEvtRxParam
 * When the system event is both :
 *    - a ready event (subevtcode = SHCI_SUB_EVT_CODE_READY)
 *    - reported by the FUS (sysevt_ready_rsp == FUS_FW_RUNNING)
 * The buffer shall not be released
 * (eg ((tSHCI_UserEvtRxParam*)pPayload)->status shall be set to SHCI_TL_UserEventFlow_Disable)
 * When the status is not filled, the buffer is released by default
 */
static void APPE_SysUserEvtRx(void * pPayload)
{
  TL_AsynchEvt_t *p_sys_event;
  WirelessFwInfo_t WirelessInfo;

  p_sys_event = (TL_AsynchEvt_t*)(((tSHCI_UserEvtRxParam*)pPayload)->pckt->evtserial.evt.payload);

  switch(p_sys_event->subevtcode)
  {
  case SHCI_SUB_EVT_CODE_READY:
    /* Read the firmware version of both the wireless firmware and the FUS */
    SHCI_GetWirelessFwInfo(&WirelessInfo);
    APP_DBG_MSG("Wireless Firmware version %d.%d.%d\n", WirelessInfo.VersionMajor, WirelessInfo.VersionMinor, WirelessInfo.VersionSub);
    APP_DBG_MSG("Wireless Firmware build %d\n", WirelessInfo.VersionReleaseType);
    APP_DBG_MSG("FUS version %d.%d.%d\n", WirelessInfo.FusVersionMajor, WirelessInfo.FusVersionMinor, WirelessInfo.FusVersionSub);

    APP_DBG_MSG(">>== SHCI_SUB_EVT_CODE_READY\n\r");
    APPE_SysEvtReadyProcessing(pPayload);
    break;

  case SHCI_SUB_EVT_ERROR_NOTIF:
    APP_DBG_MSG(">>== SHCI_SUB_EVT_ERROR_NOTIF \n\r");
    APPE_SysEvtError(pPayload);
    break;

  case SHCI_SUB_EVT_BLE_NVM_RAM_UPDATE:
    APP_DBG_MSG(">>== SHCI_SUB_EVT_BLE_NVM_RAM_UPDATE -- BLE NVM RAM HAS BEEN UPDATED BY CPU2 \n");
    APP_DBG_MSG("     - StartAddress = %lx , Size = %ld\n",
                ((SHCI_C2_BleNvmRamUpdate_Evt_t*)p_sys_event->payload)->StartAddress,
                ((SHCI_C2_BleNvmRamUpdate_Evt_t*)p_sys_event->payload)->Size);
    break;

  case SHCI_SUB_EVT_NVM_START_WRITE:
    APP_DBG_MSG("==>> SHCI_SUB_EVT_NVM_START_WRITE : NumberOfWords = %ld\n",
                ((SHCI_C2_NvmStartWrite_Evt_t*)p_sys_event->payload)->NumberOfWords);
    break;

  case SHCI_SUB_EVT_NVM_END_WRITE:
    APP_DBG_MSG(">>== SHCI_SUB_EVT_NVM_END_WRITE\n\r");
    break;

  case SHCI_SUB_EVT_NVM_START_ERASE:
    APP_DBG_MSG("==>>SHCI_SUB_EVT_NVM_START_ERASE : NumberOfSectors = %ld\n",
                ((SHCI_C2_NvmStartErase_Evt_t*)p_sys_event->payload)->NumberOfSectors);
    break;

  case SHCI_SUB_EVT_NVM_END_ERASE:
    APP_DBG_MSG(">>== SHCI_SUB_EVT_NVM_END_ERASE\n\r");
    break;

  default:
    break;
  }

  return;
}

/**
 * @brief Notify a system error coming from the M0 firmware
 * @param  ErrorCode  : errorCode detected by the M0 firmware
 *
 * @retval None
 */
static void APPE_SysEvtError(void * pPayload)
{
  TL_AsynchEvt_t *p_sys_event;
  SCHI_SystemErrCode_t *p_sys_error_code;

  p_sys_event = (TL_AsynchEvt_t*)(((tSHCI_UserEvtRxParam*)pPayload)->pckt->evtserial.evt.payload);
  p_sys_error_code = (SCHI_SystemErrCode_t*) p_sys_event->payload;

  APP_DBG_MSG(">>== SHCI_SUB_EVT_ERROR_NOTIF WITH REASON %x \n\r",(*p_sys_error_code));

  if ((*p_sys_error_code) == ERR_BLE_INIT)
  {
    /* Error during BLE stack initialization */
    APP_DBG_MSG(">>== SHCI_SUB_EVT_ERROR_NOTIF WITH REASON - ERR_BLE_INIT \n");
  }
  else
  {
    APP_DBG_MSG(">>== SHCI_SUB_EVT_ERROR_NOTIF WITH REASON - BLE ERROR \n");
  }
  return;
}

static void APPE_SysEvtReadyProcessing(void * pPayload)
{
  TL_AsynchEvt_t *p_sys_event;
  SHCI_C2_Ready_Evt_t *p_sys_ready_event;

  SHCI_C2_CONFIG_Cmd_Param_t config_param = {0};
  uint32_t RevisionID=0;
  uint32_t DeviceID=0;

  p_sys_event = (TL_AsynchEvt_t*)(((tSHCI_UserEvtRxParam*)pPayload)->pckt->evtserial.evt.payload);
  p_sys_ready_event = (SHCI_C2_Ready_Evt_t*) p_sys_event->payload;

  if (p_sys_ready_event->sysevt_ready_rsp == WIRELESS_FW_RUNNING)
  {
    /**
    * The wireless firmware is running on the CPU2
    */
    APP_DBG_MSG(">>== WIRELESS_FW_RUNNING \n");

    /* Traces channel initialization — skipped (deadlocks inside SHCI event callback) */
    /* APPD_EnableCPU2(); */

    /* SHCI_C2_Config skipped — hangs inside SHCI event callback (IPCC/SEQ reentrancy).
     * NVM event notifications are not needed for beacon operation. */
    (void)config_param;
    (void)RevisionID;
    (void)DeviceID;

    g_cpu2_state = 1; /* wireless stack running */
    /* Schedule BLE init outside this SHCI event callback — direct call deadlocks
     * because SHCI_C2_BLE_Init → UTIL_SEQ_WaitEvt re-enters the sequencer */
    UTIL_SEQ_SetTask(1 << CFG_TASK_BLE_INIT_ID, CFG_SCH_PRIO_0);
    UTIL_LPM_SetOffMode(1U << CFG_LPM_APP, UTIL_LPM_ENABLE);
  }
  else if (p_sys_ready_event->sysevt_ready_rsp == FUS_FW_RUNNING)
  {
    /**
    * The FUS firmware is running on the CPU2
    * In the scope of this application, there should be no case when we get here
    */
    APP_DBG_MSG(">>== SHCI_SUB_EVT_CODE_READY - FUS_FW_RUNNING \n\r");

    /* The packet shall not be released as this is not supported by the FUS */
    ((tSHCI_UserEvtRxParam*)pPayload)->status = SHCI_TL_UserEventFlow_Disable;
  }
  else
  {
    APP_DBG_MSG(">>== SHCI_SUB_EVT_CODE_READY - UNEXPECTED CASE \n\r");
  }

  return;
}

/* USER CODE BEGIN FD_LOCAL_FUNCTIONS */
static void diag_blink(void)
{
  GPIOB->BSRR = (uint32_t)GPIO_PIN_0;           /* ON */
  HAL_Delay(80);
  GPIOB->BSRR = (uint32_t)GPIO_PIN_0 << 16U;    /* OFF */
  HAL_Delay(80);
}

void APPE_Led_Init( void )
{
#if (CFG_LED_SUPPORTED == 1)
  /**
   * Leds Initialization
   */
  BSP_LED_Init(LED_BLUE);
#endif

  return;
}

void APPE_Button_Init( void )
{
#if (CFG_BUTTON_SUPPORTED == 1)
  /**
   * Button Initialization
   */
  BSP_PB_Init(BUTTON_USER1, BUTTON_MODE_EXTI);
#endif

  return;
}
/* ===========================================================================
 * Config: CRC, Flash save/load, defaults
 * ===========================================================================*/
static uint32_t Config_CalcCrc(const BeaconConfig_t *c)
{
  const uint8_t *p = (const uint8_t *)c;
  uint32_t sum = 0;
  /* cover all fields except the trailing crc field */
  for (size_t i = 0; i < sizeof(BeaconConfig_t) - sizeof(uint32_t); i++)
    sum += p[i];
  return sum ^ 0xBEAC0001U;
}

void Config_SetDefaults(BeaconConfig_t *c)
{
  memset(c, 0, sizeof(*c));
  c->magic          = CONFIG_MAGIC;
  c->version        = CONFIG_VERSION;
  c->led_mode       = CFG_DEF_LED_MODE;
  c->rf_mode        = CFG_DEF_RF_MODE;
  c->rf_channel     = CFG_DEF_RF_CHANNEL;
  c->rf_power       = CFG_DEF_RF_POWER;
  c->rf_pulse_on_ms = CFG_DEF_RF_PULSE_ON_MS;
  c->rf_period_ms   = CFG_DEF_RF_PERIOD_MS;
  c->dbg_battery    = CFG_DEF_DBG_BATTERY;
  c->dbg_light      = CFG_DEF_DBG_LIGHT;
  c->ble_enabled    = CFG_DEF_BLE_ENABLED;
  c->ble_adv_ms     = CFG_DEF_BLE_ADV_MS;
  c->ble_tx_power   = CFG_DEF_BLE_TX_POWER;
  c->sensor_ms      = CFG_DEF_SENSOR_MS;
  c->crc            = Config_CalcCrc(c);
}

static int Config_Load(void)
{
  const BeaconConfig_t *fp = (const BeaconConfig_t *)CONFIG_FLASH_ADDR;
  if (fp->magic != CONFIG_MAGIC || fp->version != CONFIG_VERSION) return -1;
  BeaconConfig_t tmp;
  memcpy(&tmp, fp, sizeof(tmp));
  if (Config_CalcCrc(&tmp) != tmp.crc) return -2;
  memcpy(&g_config, &tmp, sizeof(g_config));
  return 0;
}

int Config_Save(void)
{
  g_config.magic   = CONFIG_MAGIC;
  g_config.version = CONFIG_VERSION;
  g_config.crc     = Config_CalcCrc(&g_config);

  uint32_t page_err = 0;
  FLASH_EraseInitTypeDef erase = {
    .TypeErase = FLASH_TYPEERASE_PAGES,
    .Page      = CONFIG_FLASH_PAGE,
    .NbPages   = 1,
  };
  /* IRQ-off approach: the only deadlock-free way to erase flash while
     CPU2 BLE is running.  FLASH_WaitForLastOperation spins on FLASH_SR_BSY
     which is cleared by hardware (~20 ms for page erase) regardless of
     HAL_GetTick() being frozen.  CPU2 stalls on flash for that same 20 ms
     — tolerable for a user-initiated save.  No SHCI call, no UTIL_SEQ
     re-entrancy, no risk of hang. */
  __disable_irq();
  HAL_FLASH_Unlock();
  if (HAL_FLASHEx_Erase(&erase, &page_err) != HAL_OK) {
    HAL_FLASH_Lock();
    __enable_irq();
    return -1;
  }
  const uint8_t *src  = (const uint8_t *)&g_config;
  uint32_t       addr = CONFIG_FLASH_ADDR;
  uint32_t       rem  = (uint32_t)sizeof(BeaconConfig_t);
  int            ret  = 0;
  while (rem > 0) {
    uint8_t  chunk[8];
    uint32_t n = (rem >= 8U) ? 8U : rem;
    memset(chunk, 0xFF, 8);
    memcpy(chunk, src, n);
    uint64_t dw; memcpy(&dw, chunk, 8);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, dw) != HAL_OK) {
      ret = -2; break;
    }
    src += n; addr += 8; rem -= n;
  }
  HAL_FLASH_Lock();
  __enable_irq();
  return ret;
}

static void Config_Print(void)
{
  printf("led=%u\r\nrf_mode=%u\r\nrf_ch=%u\r\nrf_pwr=%u\r\n"
         "rf_on_ms=%lu\r\nrf_period_ms=%lu\r\n"
         "dbg_batt=%u\r\ndbg_light=%u\r\n"
         "ble=%u\r\nble_adv_ms=%lu\r\nble_pwr=%u\r\nsensor_ms=%lu\r\n",
         (unsigned)g_config.led_mode,
         (unsigned)g_config.rf_mode,
         (unsigned)g_config.rf_channel,
         (unsigned)g_config.rf_power,
         (unsigned long)g_config.rf_pulse_on_ms,
         (unsigned long)g_config.rf_period_ms,
         (unsigned)g_config.dbg_battery,
         (unsigned)g_config.dbg_light,
         (unsigned)g_config.ble_enabled,
         (unsigned long)g_config.ble_adv_ms,
         (unsigned)g_config.ble_tx_power,
         (unsigned long)g_config.sensor_ms);
}

/* ===========================================================================
 * RF TX 30 MHz — config-driven state machine
 * ===========================================================================*/
#define RF_MS_TO_TICKS(ms)  ((uint32_t)((ms) * 1000UL / CFG_TS_TICK_VAL))

typedef enum {
  RF_STATE_IDLE,       /* off, waiting for period to expire then back to IDLE→dispatch */
  RF_STATE_FIXED_ON,   /* TX on, timer = rf_pulse_on_ms */
  RF_STATE_FIXED_OFF,  /* TX off, timer = rf_period_ms - rf_pulse_on_ms */
  RF_STATE_CH_SWEEP,   /* cycling ch 0..3, each rf_pulse_on_ms */
  RF_STATE_PWR_SWEEP,  /* cycling pwr 1..4, each rf_pulse_on_ms */
  RF_STATE_CONTINUOUS, /* TX always on, no timer */
} RF_State_t;

static uint8_t    rf_timer_id;
static RF_State_t rf_state = RF_STATE_IDLE;
static uint8_t    rf_step  = 0;

static void rf_enable(uint8_t on)
{
  /* PB5=1 → TX ON, PB5=0 → TX OFF */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  if (g_config.led_mode == LED_TX_PULSE)
    HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void rf_set_ch(uint8_t ch)
{
  /* CH0=PA6:0 PB8:0  CH1=PA6:1 PB8:0  CH2=PA6:0 PB8:1  CH3=PA6:1 PB8:1 */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, (ch & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, (ch & 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void rf_set_pwr(uint8_t pwr)
{
  if (pwr < 1U || pwr > 4U) pwr = 1U; /* clamp: 0 underflows, >4 invalid */
  uint8_t v = (uint8_t)(pwr - 1U);
  /* pwr=1: PA1=0 PA8=0 | pwr=2: PA1=1 PA8=0 | pwr=3: PA1=0 PA8=1 | pwr=4: PA1=1 PA8=1 */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, (v & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, (v & 2U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void RF_Timer_Callback(void)
{
  UTIL_SEQ_SetTask(1U << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
}

static void RF_TX_Task(void)
{
  if (g_config.rf_mode == RF_MODE_OFF) {
    rf_enable(0);
    return;
  }

  switch (rf_state) {
    case RF_STATE_IDLE:
      /* dispatch to the right mode */
      switch (g_config.rf_mode) {
        case RF_MODE_FIXED:
          rf_set_ch(g_config.rf_channel);
          rf_set_pwr(g_config.rf_power);
          rf_enable(1);
          rf_state = RF_STATE_FIXED_ON;
          HW_TS_Start(rf_timer_id, RF_MS_TO_TICKS(g_config.rf_pulse_on_ms));
          break;
        case RF_MODE_CONTINUOUS:
          rf_set_ch(g_config.rf_channel);
          rf_set_pwr(g_config.rf_power);
          rf_enable(1);
          rf_state = RF_STATE_CONTINUOUS;
          /* no timer — stays on until RF_TX_Start() resets state */
          break;
        case RF_MODE_CH_SWEEP:
          rf_step = 0; rf_state = RF_STATE_CH_SWEEP;
          UTIL_SEQ_SetTask(1U << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
          break;
        case RF_MODE_PWR_SWEEP:
          rf_step = 0; rf_state = RF_STATE_PWR_SWEEP;
          UTIL_SEQ_SetTask(1U << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
          break;
        case RF_MODE_FULL_SWEEP:
          rf_step = 0; rf_state = RF_STATE_CH_SWEEP;
          UTIL_SEQ_SetTask(1U << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
          break;
        default: break;
      }
      break;

    case RF_STATE_CONTINUOUS:
      /* TX permanently on — no action until mode changes via RF_TX_Start() */
      break;

    case RF_STATE_FIXED_ON:
      /* pulse_on elapsed → TX off, start off-phase */
      rf_enable(0);
      rf_state = RF_STATE_FIXED_OFF;
      {
        uint32_t off_ms = (g_config.rf_period_ms > g_config.rf_pulse_on_ms)
                          ? g_config.rf_period_ms - g_config.rf_pulse_on_ms
                          : 100U;
        HW_TS_Start(rf_timer_id, RF_MS_TO_TICKS(off_ms));
      }
      break;

    case RF_STATE_FIXED_OFF:
      /* off-phase elapsed → TX on again */
      rf_set_ch(g_config.rf_channel);
      rf_set_pwr(g_config.rf_power);
      rf_enable(1);
      rf_state = RF_STATE_FIXED_ON;
      HW_TS_Start(rf_timer_id, RF_MS_TO_TICKS(g_config.rf_pulse_on_ms));
      break;

    case RF_STATE_CH_SWEEP:
      if (rf_step < 4U) {
        rf_set_pwr(g_config.rf_power);
        rf_set_ch(rf_step);
        rf_enable(1);
        HW_TS_Start(rf_timer_id, RF_MS_TO_TICKS(g_config.rf_pulse_on_ms));
        rf_step++;
      } else {
        rf_enable(0);
        if (g_config.rf_mode == RF_MODE_FULL_SWEEP) {
          rf_step = 0; rf_state = RF_STATE_PWR_SWEEP;
          UTIL_SEQ_SetTask(1U << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
        } else {
          rf_step = 0; rf_state = RF_STATE_IDLE;
          uint32_t used_ms = 4U * g_config.rf_pulse_on_ms;
          uint32_t pause   = g_config.rf_period_ms > used_ms ? g_config.rf_period_ms - used_ms : 100U;
          HW_TS_Start(rf_timer_id, RF_MS_TO_TICKS(pause));
        }
      }
      break;

    case RF_STATE_PWR_SWEEP:
      if (rf_step < 4U) {
        rf_set_ch(g_config.rf_channel);
        rf_set_pwr(rf_step + 1U);
        rf_enable(1);
        HW_TS_Start(rf_timer_id, RF_MS_TO_TICKS(g_config.rf_pulse_on_ms));
        rf_step++;
      } else {
        rf_enable(0);
        rf_step = 0; rf_state = RF_STATE_IDLE;
        uint32_t used_ms = (g_config.rf_mode == RF_MODE_FULL_SWEEP)
                           ? 8U * g_config.rf_pulse_on_ms  /* CH(4) + PWR(4) */
                           : 4U * g_config.rf_pulse_on_ms;
        uint32_t pause = g_config.rf_period_ms > used_ms ? g_config.rf_period_ms - used_ms : 100U;
        HW_TS_Start(rf_timer_id, RF_MS_TO_TICKS(pause));
      }
      break;

    default: break;
  }
}

void RF_TX_Init(void)
{
  GPIO_InitTypeDef g = {0};
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  /* GPIOA: PA1=PWR-bit0, PA6=CH-bit0, PA8=PWR-bit1 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  g.Pin = GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_8;
  HAL_GPIO_Init(GPIOA, &g);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_8, GPIO_PIN_RESET);
  /* GPIOB: PB5=EN, PB8=CH-bit1 */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  g.Pin = GPIO_PIN_5 | GPIO_PIN_8;
  HAL_GPIO_Init(GPIOB, &g);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5 | GPIO_PIN_8, GPIO_PIN_RESET);
  UTIL_SEQ_RegTask(1U << CFG_TASK_RF_TX_ID, UTIL_SEQ_RFU, RF_TX_Task);
  HW_TS_Create(CFG_TIM_PROC_ID_ISR, &rf_timer_id, hw_ts_SingleShot, RF_Timer_Callback);
  printf("[RF] init OK  PB5=EN  PA6/PB8=CH  PA1/PA8=PWR\r\n");
}

void RF_TX_Start(void)
{
  HW_TS_Stop(rf_timer_id);
  rf_enable(0);
  rf_state = RF_STATE_IDLE;
  rf_step  = 0;
  UTIL_SEQ_SetTask(1U << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
}

/* ===========================================================================
 * BLE control task — executes HCI calls on behalf of UART task.
 * In HCI list → can call aci_* safely.  UART task just sets g_ble_ctrl_cmd.
 * ===========================================================================*/
static void BLE_Ctrl_Task(void)
{
  BleCtrl_t cmd = (BleCtrl_t)g_ble_ctrl_cmd;
  g_ble_ctrl_cmd = BLE_CTRL_NONE;
  if (g_cpu2_state == 0) {
    printf("[BLE] BLE_Ctrl_Task: CPU2 not ready (cmd=%u skipped)\r\n", (unsigned)cmd);
    return;
  }
  switch (cmd) {
    case BLE_CTRL_START:
    case BLE_CTRL_RESTART:
      aci_hal_set_tx_power_level(1, g_config.ble_tx_power);
      APP_BLE_Adv_Restart();
      break;
    case BLE_CTRL_STOP:
      APP_BLE_Adv_Stop();
      break;
    case BLE_CTRL_PWR: {
      uint8_t r = aci_hal_set_tx_power_level(1, g_config.ble_tx_power);
      printf("[BLE] set_pwr PA=%u ret=0x%02X\r\n", (unsigned)g_config.ble_tx_power, (unsigned)r);
      break;
    }
    default: break;
  }
}

/* ===========================================================================
 * UART command parser
 * ===========================================================================*/
static void Uart_Cmd_Task(void)
{
  if (!uart_cmd_ready) return;
  char line[64];
  memcpy(line, uart_cmd_line, sizeof(line));
  /* Atomically move pending slot → active slot before releasing slot 0,
     so ISR can't overwrite uart_cmd_line while we're still reading it */
  __disable_irq();
  uart_cmd_ready = 0;
  if (uart_cmd_pending) {
    memcpy(uart_cmd_line, uart_cmd_pending_line, sizeof(uart_cmd_line));
    uart_cmd_ready   = 1;
    uart_cmd_pending = 0;
  }
  __enable_irq();
  if (uart_cmd_ready)
    UTIL_SEQ_SetTask(1U << CFG_TASK_UART_CMD_ID, CFG_SCH_PRIO_0);

  /* Simple tokenizer */
  char cmd[16] = {0}, param[24] = {0};
  int  val = 0;
  int  n = sscanf(line, "%15s %23s %d", cmd, param, &val);

  if (n < 1) return;

  if (!strcmp(cmd, "GET")) {
    Config_Print();
    printf("OK GET\r\n");  /* not bare "OK" — avoids false ACK in GUI deque */
  }
  else if (!strcmp(cmd, "SET") && n == 3) {
    uint8_t restart_rf = 0;
    if (!strcmp(param, "led")) {
      g_config.led_mode = (uint8_t)val;
      /* Apply LED state immediately */
      switch (g_config.led_mode) {
        case LED_OFF:
          HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin, GPIO_PIN_RESET); break;
        case LED_ALWAYS_ON:
          HAL_GPIO_WritePin(Blue_Led_GPIO_Port, Blue_Led_Pin, GPIO_PIN_SET);   break;
        default: break; /* other modes driven by events */
      }
    }
    else if (!strcmp(param, "rf_mode"))    { g_config.rf_mode        = (uint8_t)val;  restart_rf = 1; }
    else if (!strcmp(param, "rf_ch"))      { g_config.rf_channel     = (uint8_t)val;  restart_rf = 1; }
    else if (!strcmp(param, "rf_pwr"))     { g_config.rf_power       = (uint8_t)val;  restart_rf = 1; }
    else if (!strcmp(param, "rf_on_ms"))   { g_config.rf_pulse_on_ms = (uint32_t)val; restart_rf = 1; }
    else if (!strcmp(param, "rf_period_ms")){ g_config.rf_period_ms  = (uint32_t)val; restart_rf = 1; }
    else if (!strcmp(param, "dbg_batt")) {
      g_config.dbg_battery = (uint8_t)val;
      /* PA12=LOW → divider ON (always), HIGH → divider managed around each read */
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, val ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
    else if (!strcmp(param, "dbg_light")) {
      g_config.dbg_light = (uint8_t)val;
      /* PB1=HIGH → sensor ON (always), LOW → sensor managed around each read */
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, val ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    else if (!strcmp(param, "ble")) {
      g_config.ble_enabled = (uint8_t)val;
      g_ble_ctrl_cmd = val ? BLE_CTRL_START : BLE_CTRL_STOP;
      if (g_cpu2_state != 0)
        UTIL_SEQ_SetTask(1U << CFG_TASK_BLE_CTRL_ID, CFG_SCH_PRIO_0);
    }
    else if (!strcmp(param, "ble_adv_ms")) {
      g_config.ble_adv_ms = (uint32_t)val;
      /* Config stored; applied on next BLESTART / SET ble 1 */
    }
    else if (!strcmp(param, "ble_pwr")) {
      g_config.ble_tx_power = (uint8_t)val;
      /* Config stored; applied on next BLESTART / SET ble 1 */
    }
    else if (!strcmp(param, "sensor_ms"))    g_config.sensor_ms      = (uint32_t)val;
    else { printf("ERR:unknown param '%s'\r\n", param); return; }
    if (restart_rf) RF_TX_Start();
    printf("OK\r\n");
  }
  else if (!strcmp(cmd, "SAVE")) {
    /* Defer actual Flash write to main() loop — calling Config_Save() from
       inside a UTIL_SEQ task causes SHCI_C2_FLASH_EraseActivity to re-enter
       UTIL_SEQ_WaitEvt and deadlock, hanging UART indefinitely */
    g_config_save_pending = 1;
    printf("saving...\r\n");
  }
  else if (!strcmp(cmd, "LOAD")) {
    int r = Config_Load();
    if (r == 0) {
      printf("OK loaded from Flash\r\n");
      Config_Print();
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, g_config.dbg_battery ? GPIO_PIN_RESET : GPIO_PIN_SET);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1,  g_config.dbg_light   ? GPIO_PIN_SET   : GPIO_PIN_RESET);
      g_ble_ctrl_cmd = BLE_CTRL_PWR; /* apply BLE TX power via deferred task */
      if (g_cpu2_state != 0)
        UTIL_SEQ_SetTask(1U << CFG_TASK_BLE_CTRL_ID, CFG_SCH_PRIO_0);
      RF_TX_Start();
    }
    else printf("ERR:no valid config in Flash (%d)\r\n", r);
  }
  else if (!strcmp(cmd, "RESET")) {
    Config_SetDefaults(&g_config);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, g_config.dbg_battery ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1,  g_config.dbg_light   ? GPIO_PIN_SET   : GPIO_PIN_RESET);
    g_ble_ctrl_cmd = BLE_CTRL_PWR; /* apply BLE TX power via deferred task */
    if (g_cpu2_state != 0)
      UTIL_SEQ_SetTask(1U << CFG_TASK_BLE_CTRL_ID, CFG_SCH_PRIO_0);
    RF_TX_Start();
    printf("OK defaults restored\r\n");
  }
  else if (!strcmp(cmd, "STATUS")) {
    printf("cpu2=%u tl=%u ble_st=%u ble_adv=%u ble_conn=%u\r\n",
           (unsigned)g_cpu2_state,
           (unsigned)g_cpu2_tl_inited,
           (unsigned)APP_BLE_Get_Server_Connection_Status(),
           (unsigned)g_ble_advertising,
           (unsigned)g_ble_connected);
    printf("rf_mode=%u rf_state=%u rf_step=%u led=%u\r\n",
           (unsigned)g_config.rf_mode,
           (unsigned)rf_state,
           (unsigned)rf_step,
           (unsigned)g_config.led_mode);
    printf("rf_ch=%u rf_pwr=%u rf_on_ms=%lu rf_period=%lu\r\n",
           (unsigned)g_config.rf_channel,
           (unsigned)g_config.rf_power,
           (unsigned long)g_config.rf_pulse_on_ms,
           (unsigned long)g_config.rf_period_ms);
  }
  else if (!strcmp(cmd, "CPU2INIT")) {
    if (g_cpu2_tl_inited) {
      printf("ERR:CPU2 already init (state=%u)\r\n", (unsigned)g_cpu2_state);
    } else {
      g_cpu2_tl_inited = 1;
      appe_Tl_Init();
      APPE_Button_Init();
      diag_blink();
      printf("OK CPU2 transport starting — wait for [BLE] ready message\r\n");
    }
  }
  else if (!strcmp(cmd, "CPU2STOP")) {
    if (!g_cpu2_tl_inited) {
      printf("ERR:CPU2 not started\r\n");
    } else {
      LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);
      g_cpu2_state      = 0;
      g_ble_advertising = 0;
      g_ble_connected   = 0;
      printf("OK CPU2 shutdown — MCU reset required to restart\r\n");
    }
  }
  else if (!strcmp(cmd, "BLESTART")) {
    if (g_cpu2_state == 0) {
      printf("ERR:CPU2 not ready — send CPU2INIT first\r\n");
    } else {
      g_ble_ctrl_cmd = BLE_CTRL_START;
      UTIL_SEQ_SetTask(1U << CFG_TASK_BLE_CTRL_ID, CFG_SCH_PRIO_0);
      printf("OK BLESTART (queued)\r\n");
    }
  }
  else if (!strcmp(cmd, "BLESTOP")) {
    if (g_cpu2_state == 0) {
      printf("ERR:CPU2 not ready\r\n");
    } else {
      g_ble_ctrl_cmd = BLE_CTRL_STOP;
      UTIL_SEQ_SetTask(1U << CFG_TASK_BLE_CTRL_ID, CFG_SCH_PRIO_0);
      printf("OK BLESTOP (queued)\r\n");
    }
  }
  else if (!strcmp(cmd, "HELP")) {
    printf("Commands: GET  SET <param> <val>  SAVE  LOAD  RESET  STATUS  HELP\r\n");
    printf("Params: led rf_mode rf_ch rf_pwr rf_on_ms rf_period_ms\r\n");
    printf("        dbg_batt dbg_light  ble ble_adv_ms ble_pwr(0-7)  sensor_ms\r\n");
    printf("led: 0=off 1=init_only 2=ble_status 3=tx_pulse 4=heartbeat 5=always_on\r\n");
    printf("rf_mode: 0=off 1=fixed 2=ch_sweep 3=pwr_sweep 4=full_sweep 5=continuous\r\n");
    printf("rf_mode=1: ON rf_on_ms, OFF (rf_period_ms-rf_on_ms), repeat\r\n");
    printf("rf_mode=5: TX always on (ch+pwr from config)\r\n");
    printf("dbg_batt=1: PA12=LOW(divider always on)  dbg_light=1: PB1=HIGH(always on)\r\n");
    printf("CPU2INIT  — start CPU2+BLE stack (for power measurement)\r\n");
    printf("CPU2STOP  — shutdown CPU2 (MCU reset required to restart)\r\n");
    printf("BLESTART  — start BLE advertising  |  BLESTOP — stop advertising\r\n");
  }
  else {
    printf("ERR:unknown command '%s' — type HELP\r\n", cmd);
  }
}
/* USER CODE END FD_LOCAL_FUNCTIONS */

/*************************************************************
 *
 * WRAP FUNCTIONS
 *
 *************************************************************/
void HAL_Delay(uint32_t Delay)
{
  uint32_t tickstart = HAL_GetTick();
  uint32_t wait = Delay;

  /* Add a freq to guarantee minimum wait */
  if (wait < HAL_MAX_DELAY)
  {
    wait += HAL_GetTickFreq();
  }

  while ((HAL_GetTick() - tickstart) < wait)
  {
    /************************************************************************************
     * ENTER SLEEP MODE
     ***********************************************************************************/
    LL_LPM_EnableSleep(); /**< Clear SLEEPDEEP bit of Cortex System Control Register */

    /**
     * This option is used to ensure that store operations are completed
     */
  #if defined (__CC_ARM) || defined (__ARMCC_VERSION)
    __force_stores();
  #endif /* __ARMCC_VERSION */

    __WFI();
  }
}

void MX_APPE_Process(void)
{
  /* USER CODE BEGIN MX_APPE_Process_1 */

  /* USER CODE END MX_APPE_Process_1 */
  UTIL_SEQ_Run(UTIL_SEQ_DEFAULT);
  /* USER CODE BEGIN MX_APPE_Process_2 */

  /* USER CODE END MX_APPE_Process_2 */
}

void UTIL_SEQ_Idle(void)
{
#if (CFG_LPM_SUPPORTED == 1)
  UTIL_LPM_EnterLowPower();
#endif /* CFG_LPM_SUPPORTED == 1 */
  return;
}

void shci_notify_asynch_evt(void* pdata)
{
  UTIL_SEQ_SetTask(1<<CFG_TASK_SYSTEM_HCI_ASYNCH_EVT_ID, CFG_SCH_PRIO_0);
  return;
}

void shci_cmd_resp_release(uint32_t flag)
{
  UTIL_SEQ_SetEvt(1<< CFG_IDLEEVT_SYSTEM_HCI_CMD_EVT_RSP_ID);
  return;
}

void shci_cmd_resp_wait(uint32_t timeout)
{
  UTIL_SEQ_WaitEvt(1<< CFG_IDLEEVT_SYSTEM_HCI_CMD_EVT_RSP_ID);
  return;
}

/* USER CODE BEGIN FD_WRAP_FUNCTIONS */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  switch (GPIO_Pin)
  {
    case SW1_User_Pin:
      /* Never call HCI from EXTI interrupt — schedule deferred BLE toggle */
      if (g_cpu2_state != 0) {
        g_ble_ctrl_cmd = (g_ble_advertising) ? BLE_CTRL_STOP : BLE_CTRL_START;
        UTIL_SEQ_SetTask(1U << CFG_TASK_BLE_CTRL_ID, CFG_SCH_PRIO_0);
      }
      break;
    default:
      break;
  }
  return;
}
/* USER CODE END FD_WRAP_FUNCTIONS */
