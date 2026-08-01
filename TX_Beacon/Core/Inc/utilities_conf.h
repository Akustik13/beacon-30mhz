#ifndef UTILITIES_CONF_H
#define UTILITIES_CONF_H

#include "app_common.h"

/* __WEAK used by stm32_seq.c weak function definitions */
#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif

/* Critical section — save/restore PRIMASK */
#define UTILS_ENTER_CRITICAL_SECTION() \
    uint32_t _utils_primask = __get_PRIMASK(); __disable_irq()
#define UTILS_EXIT_CRITICAL_SECTION() \
    __set_PRIMASK(_utils_primask)

/* Sequencer critical section (mapped to PRIMASK) */
#define UTIL_SEQ_ENTER_CRITICAL_SECTION() \
    uint32_t _seq_primask = __get_PRIMASK(); __disable_irq()
#define UTIL_SEQ_EXIT_CRITICAL_SECTION() \
    __set_PRIMASK(_seq_primask)

/* Sequencer task count and priority levels */
#define UTIL_SEQ_CONF_TASK_NBR    (32U)
#define UTIL_SEQ_CONF_PRIO_NBR    CFG_SCH_PRIO_NBR

/* No standby support — we manage power ourselves in svc_power.c */
#define CFG_LPM_STANDBY_SUPPORTED  0

#endif /* UTILITIES_CONF_H */
