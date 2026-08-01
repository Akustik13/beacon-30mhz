#include "app_common.h"
#include "hw_timerserver.h"
#include "stm32_seq.h"
#include "rf_tx.h"

/* Timer periods: (ms * 1000) / CFG_TS_TICK_VAL */
#define RF_PERIOD_100MS   ((uint32_t)(0.1 * 1000 * 1000 / CFG_TS_TICK_VAL))
#define RF_PERIOD_1000MS  ((uint32_t)(1.0 * 1000 * 1000 / CFG_TS_TICK_VAL))

typedef enum { RF_STATE_PAUSE, RF_STATE_CH_SWEEP, RF_STATE_PWR_SWEEP } RF_State_t;

static uint8_t    rf_timer_id;
static RF_State_t rf_state = RF_STATE_PAUSE;
static uint8_t    rf_step  = 0;

static void rf_enable(uint8_t on)
{
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void rf_set_ch(uint8_t ch) /* 0-3 */
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, (ch & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, (ch & 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void rf_set_pwr(uint8_t pwr) /* 1-4 */
{
  uint8_t v = pwr - 1;
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, (v & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, (v & 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void RF_TX_Task(void)
{
  rf_enable(0); /* TX off before any switch */

  switch (rf_state)
  {
    case RF_STATE_PAUSE:
      rf_state = RF_STATE_CH_SWEEP;
      rf_step  = 0;
      printf("[RF] CH sweep pwr=1 ch=0..3\r\n");
      UTIL_SEQ_SetTask(1 << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
      break;

    case RF_STATE_CH_SWEEP:
      if (rf_step < 4) {
        rf_set_pwr(1);
        rf_set_ch(rf_step);
        rf_enable(1);
        rf_step++;
        HW_TS_Start(rf_timer_id, RF_PERIOD_100MS);
      } else {
        rf_state = RF_STATE_PWR_SWEEP;
        rf_step  = 0;
        printf("[RF] PWR sweep ch=0 pwr=1..4\r\n");
        UTIL_SEQ_SetTask(1 << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
      }
      break;

    case RF_STATE_PWR_SWEEP:
      if (rf_step < 4) {
        rf_set_ch(0);
        rf_set_pwr(rf_step + 1);
        rf_enable(1);
        rf_step++;
        HW_TS_Start(rf_timer_id, RF_PERIOD_100MS);
      } else {
        rf_state = RF_STATE_PAUSE;
        rf_step  = 0;
        printf("[RF] pause 1s\r\n");
        HW_TS_Start(rf_timer_id, RF_PERIOD_1000MS);
      }
      break;

    default:
      break;
  }
}

static void RF_Timer_Callback(void)
{
  UTIL_SEQ_SetTask(1 << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
}

void RF_TX_Init(void)
{
  GPIO_InitTypeDef g = {0};
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;

  __HAL_RCC_GPIOA_CLK_ENABLE();
  g.Pin = GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_8;
  HAL_GPIO_Init(GPIOA, &g);

  __HAL_RCC_GPIOB_CLK_ENABLE();
  g.Pin = GPIO_PIN_5 | GPIO_PIN_8;
  HAL_GPIO_Init(GPIOB, &g);

  /* All off, channel 0, power 0 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5 | GPIO_PIN_8, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_8, GPIO_PIN_RESET);

  UTIL_SEQ_RegTask(1 << CFG_TASK_RF_TX_ID, UTIL_SEQ_RFU, RF_TX_Task);
  HW_TS_Create(CFG_TIM_PROC_ID_ISR, &rf_timer_id, hw_ts_SingleShot, RF_Timer_Callback);

  printf("[RF] init OK  PB5=EN  PA6/PB8=CH  PA8/PA1=PWR\r\n");
}

void RF_TX_Start(void)
{
  rf_state = RF_STATE_PAUSE;
  rf_step  = 0;
  UTIL_SEQ_SetTask(1 << CFG_TASK_RF_TX_ID, CFG_SCH_PRIO_0);
}
