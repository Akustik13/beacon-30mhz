/**
  ******************************************************************************
  * File Name          : env_server_app.c
  * Description        : Handle HW/Environmental Service/Char
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2019-2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app_common.h"
#include "ble.h"
#include "dbg_trace.h"

#include "motenv_server_app.h"
#include "env_server_app.h"

#include "custom_stm.h"

extern uint16_t ADC_ReadVdd_mV(void);
extern int32_t  Beacon_GetChipTemp_x10(void);
extern uint32_t ADC_ReadLightRaw(void);
extern uint16_t ADC_ReadBatteryMv(uint16_t vdda_mv);
extern void     lis2dw12_read(int16_t *ax, int16_t *ay, int16_t *az);
extern uint8_t  g_lis2dw12_ok;

/* [ts:2B][batt_mv:2B][chip_temp*10:2B][light:2B][Ax_mg:2B][Ay_mg:2B][Az_mg:2B] */
#define VALUE_LEN_ENV  14

/* Private typedef -----------------------------------------------------------*/

/**
 * @brief  HW/Environmental Service/Char Context structure definition
 */
typedef struct
{
  uint8_t  NotificationStatus;

  int32_t PressureValue;
  uint16_t HumidityValue;
  int16_t TemperatureValue[1];
  uint8_t hasPressure;
  uint8_t hasHumidity;
  uint8_t hasTemperature;
} ENV_Server_App_Context_t;

/* Private macros ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/**
 * @brief  Environmental Capabilities
 */

static ENV_Server_App_Context_t ENV_Server_App_Context;

/* Global variables ----------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static void EnvSensor_GetCaps(void);

/* Functions Definition ------------------------------------------------------*/

/* Public functions ----------------------------------------------------------*/

/**
 * @brief  Init the HW/Environmental Service/Char Context
 * @param  None
 * @retval None
 */
void ENV_Context_Init(void)
{
  /* Env Sensors */

  ENV_Server_App_Context.hasPressure = 0;
  ENV_Server_App_Context.hasHumidity = 0;
  ENV_Server_App_Context.hasTemperature = 0;

  ENV_Set_Notification_Status(0);

  /* Check Env caps */
  EnvSensor_GetCaps();
}

/**
 * @brief  Set the notification status (enabled/disabled)
 * @param  status The new notification status
 * @retval None
 */
void ENV_Set_Notification_Status(uint8_t status)
{
  ENV_Server_App_Context.NotificationStatus = status;
}

/**
 * @brief  Send a notification for Environmental char
 * @param  None
 * @retval None
 */
void ENV_Send_Notification_Task(void)
{

  if(ENV_Server_App_Context.NotificationStatus)
  {
    APP_DBG_MSG("-- ENV APPLICATION SERVER : NOTIFY CLIENT WITH NEW ENV PARAMETER VALUE \n ");
    APP_DBG_MSG(" \n\r");
    ENV_Update();
  }
  else
  {
    APP_DBG_MSG("-- ENV APPLICATION SERVER : CAN'T INFORM CLIENT - NOTIFICATION DISABLED\n ");
  }

  return;
}

/**
 * @brief  Update the Environmental char value
 * @param  None
 * @retval None
 */
void ENV_Update(void)
{
  uint8_t value[VALUE_LEN_ENV];

  /* Timestamp [0:1] */
  STORE_LE_16(&value[0], (uint16_t)(HAL_GetTick() >> 3));

  /* Battery voltage [2:3] */
  uint16_t vdd_mv  = ADC_ReadVdd_mV();
  uint16_t batt_mv = ADC_ReadBatteryMv(vdd_mv);
  STORE_LE_16(&value[2], batt_mv);

  /* Chip temperature ×10 [4:5] */
  int16_t chip_x10 = (int16_t)Beacon_GetChipTemp_x10();
  STORE_LE_16(&value[4], (uint16_t)chip_x10);

  /* Light ADC raw [6:7] */
  uint16_t light = (uint16_t)ADC_ReadLightRaw();
  STORE_LE_16(&value[6], light);

  /* Accelerometer mg [8:9][10:11][12:13] */
  int16_t raw_x = 0, raw_y = 0, raw_z = 0;
  if (g_lis2dw12_ok) lis2dw12_read(&raw_x, &raw_y, &raw_z);
  int16_t ax_mg = (int16_t)((int32_t)raw_x * 244 / 1000);
  int16_t ay_mg = (int16_t)((int32_t)raw_y * 244 / 1000);
  int16_t az_mg = (int16_t)((int32_t)raw_z * 244 / 1000);
  STORE_LE_16(&value[8],  (uint16_t)ax_mg);
  STORE_LE_16(&value[10], (uint16_t)ay_mg);
  STORE_LE_16(&value[12], (uint16_t)az_mg);

  APP_DBG_MSG("Batt=%d mV  T=%d.%d C  Light=%d  Ax=%d Ay=%d Az=%d mg\n",
              batt_mv,
              chip_x10 / 10, (chip_x10 < 0 ? -chip_x10 : chip_x10) % 10,
              light, ax_mg, ay_mg, az_mg);

  Custom_STM_App_Update_Char(CUSTOM_STM_ENV_C, (uint8_t *)&value);
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  Check the Environmental active capabilities and set the ADV data accordingly
 * @param  None
 * @retval None
 */
static void EnvSensor_GetCaps(void)
{
  ENV_Server_App_Context.hasPressure = 0;
  ENV_Server_App_Context.hasHumidity = 0;
  ENV_Server_App_Context.hasTemperature = 1;
}
