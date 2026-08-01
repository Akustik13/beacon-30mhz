#ifndef BLE_CONF_H
#define BLE_CONF_H

#include "app_conf.h"

/* Peripheral/central role */
#define BLE_CFG_PERIPHERAL                  1
#define BLE_CFG_CENTRAL                     0

/* Service controller: max registered handlers (one per custom service) */
#define BLE_CFG_SVC_MAX_NBR_CB              4
#define BLE_CFG_CLT_MAX_NBR_CB              0

/* GAP appearance — generic unknown */
#define BLE_CFG_UNKNOWN_APPEARANCE          0
#define BLE_CFG_GAP_APPEARANCE              BLE_CFG_UNKNOWN_APPEARANCE

#endif /* BLE_CONF_H */
