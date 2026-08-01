#ifndef APP_COMMON_H
#define APP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "app_conf.h"

#undef NULL
#define NULL 0

#undef FALSE
#define FALSE 0

#undef TRUE
#define TRUE (!0)

#define BACKUP_PRIMASK()  uint32_t primask_bit = __get_PRIMASK()
#define DISABLE_IRQ()     __disable_irq()
#define RESTORE_PRIMASK() __set_PRIMASK(primask_bit)

#define M_BEGIN  do {
#define M_END    } while (0)

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define DIVC(x, y) (((x) + (y) - 1) / (y))
#define DIVR(x, y) (((x) + ((y) / 2)) / (y))

#define PLACE_IN_SECTION(__x__) __attribute__((section(__x__)))

#ifdef WIN32
#define ALIGN(n)
#else
#define ALIGN(n) __attribute__((aligned(n)))
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_H */
