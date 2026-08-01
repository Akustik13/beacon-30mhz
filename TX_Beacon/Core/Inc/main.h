#pragma once
#include "stm32wbxx_hal.h"

void SystemClock_Config(void);  /* defined in main.c, used by svc_power */
void Error_Handler(void);
