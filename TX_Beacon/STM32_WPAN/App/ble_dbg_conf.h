#ifndef BLE_DBG_CONF_H
#define BLE_DBG_CONF_H

/* All BLE debug output suppressed — TX_Beacon uses its own UART driver.
 * APP_DBG_MSG and BLE_DBG_SVCCTL_MSG are already defined as no-ops in app_conf.h. */
#include "app_conf.h"

#endif /* BLE_DBG_CONF_H */
