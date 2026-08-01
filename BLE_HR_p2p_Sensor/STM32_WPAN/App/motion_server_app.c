/**
  ******************************************************************************
  * File Name          : motion_server_app.c
  * Description        : Handle HW/Motion (Acc/Gyro/Mag) Service/Char
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
#include <string.h>

#include "motenv_server_app.h"
#include "motion_server_app.h"

#include "custom_stm.h"

/* LIS2DW12TR driver (defined in main.c) */
extern void    lis2dw12_read(int16_t *ax, int16_t *ay, int16_t *az);
extern uint8_t g_lis2dw12_ok;

/* Private defines -----------------------------------------------------------*/
#define ACC_BYTES               (2)
#define GYRO_BYTES              (2)

#define VALUE_LEN_MOTION        (2+3*ACC_BYTES+3*GYRO_BYTES)

/* Private typedef -----------------------------------------------------------*/

typedef struct { int32_t x; int32_t y; int32_t z; } Axes3_t;

/**
 * @brief  Motion Service/Char Context structure definition
 */
typedef struct
{
  uint8_t  NotificationStatus;

  Axes3_t acceleration;
  Axes3_t angular_velocity;
  Axes3_t magnetic_field;
  uint8_t hasAcc;
  uint8_t hasGyro;
  uint8_t hasMag;
} MOTION_Server_App_Context_t;

/* Private macros ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

static MOTION_Server_App_Context_t MOTION_Server_App_Context;

/* Global variables ----------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static void MOTION_Handle_Sensor(void);
static void MOTION_GetCaps(void);

/* Functions Definition ------------------------------------------------------*/

/* Public functions ----------------------------------------------------------*/

/**
 * @brief  Init the HW/Motion Service/Char Context
 * @param  None
 * @retval None
 */
void MOTION_Context_Init(void)
{
  /* Motion Sensors */
  MOTION_Server_App_Context.hasAcc = 0;
  MOTION_Server_App_Context.hasGyro = 0;
  MOTION_Server_App_Context.hasMag = 0;

  MOTION_Set_Notification_Status(0);

  /* Check Motion caps */
  MOTION_GetCaps();
}

/**
 * @brief  Set the notification status (enabled/disabled)
 * @param  status The new notification status
 * @retval None
 */
void MOTION_Set_Notification_Status(uint8_t status)
{
  MOTION_Server_App_Context.NotificationStatus = status;
}

/**
 * @brief  Send a notification for Motion (Acc/Gyro/Mag) char
 * @param  None
 * @retval None
 */
void MOTION_Send_Notification_Task(void)
{
  uint8_t value[VALUE_LEN_MOTION];
  memset(value, 0, sizeof(value)); /* gyro bytes [8..13] always zero — LIS2DW12 has no gyro */

  /* Read Motion values */
  MOTION_Handle_Sensor();

  /* Timestamp */
  STORE_LE_16(value, (HAL_GetTick()>>3));

  if(MOTION_Server_App_Context.hasAcc == 1)
  {
    STORE_LE_16(value+2, MOTION_Server_App_Context.acceleration.x);
    STORE_LE_16(value+4, MOTION_Server_App_Context.acceleration.y);
    STORE_LE_16(value+6, MOTION_Server_App_Context.acceleration.z);
  }

  if(MOTION_Server_App_Context.hasGyro == 1)
  {
    MOTION_Server_App_Context.angular_velocity.x/=100;
    MOTION_Server_App_Context.angular_velocity.y/=100;
    MOTION_Server_App_Context.angular_velocity.z/=100;

    STORE_LE_16(value+8, MOTION_Server_App_Context.angular_velocity.x);
    STORE_LE_16(value+10, MOTION_Server_App_Context.angular_velocity.y);
    STORE_LE_16(value+12, MOTION_Server_App_Context.angular_velocity.z);
  }

  if(MOTION_Server_App_Context.NotificationStatus)
  {
    APP_DBG_MSG("-- MOTION APPLICATION SERVER : NOTIFY CLIENT WITH NEW MOTION PARAMETER VALUE \n ");
    APP_DBG_MSG(" \n\r");
    Custom_STM_App_Update_Char(CUSTOM_STM_MOTION_C, (uint8_t *)&value);
  }
  else
  {
    APP_DBG_MSG("-- MOTION APPLICATION SERVER : CAN'T INFORM CLIENT - NOTIFICATION DISABLED\n ");
  }

  return;
}

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  Parse the values read by Motion sensors
 * @param  None
 * @retval None
 */
static void MOTION_Handle_Sensor(void)
{
  if (MOTION_Server_App_Context.hasAcc == 1 && g_lis2dw12_ok)
  {
    int16_t raw_x, raw_y, raw_z;
    lis2dw12_read(&raw_x, &raw_y, &raw_z);
    /* Convert 14-bit raw to mg: 244 µg/LSB at ±2g */
    MOTION_Server_App_Context.acceleration.x = (int32_t)raw_x * 244 / 1000;
    MOTION_Server_App_Context.acceleration.y = (int32_t)raw_y * 244 / 1000;
    MOTION_Server_App_Context.acceleration.z = (int32_t)raw_z * 244 / 1000;
  }
  /* LIS2DW12 has no gyro — zeros already in context */
}

/**
 * @brief  Check the Motion active capabilities and set the ADV data accordingly
 * @param  None
 * @retval None
 */
static void MOTION_GetCaps(void)
{
  MOTION_Server_App_Context.hasMag  = 0;
  MOTION_Server_App_Context.hasGyro = 0; /* LIS2DW12 has no gyro */
  MOTION_Server_App_Context.hasAcc  = 1;
}

