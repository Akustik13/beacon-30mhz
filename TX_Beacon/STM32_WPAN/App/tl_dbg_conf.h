#ifndef TL_DBG_CONF_H
#define TL_DBG_CONF_H

/* All TL debug output suppressed — TX_Beacon uses its own UART driver */

#define TL_SHCI_CMD_DBG_MSG(...)
#define TL_SHCI_CMD_DBG_BUF(...)
#define TL_SHCI_CMD_DBG_RAW(...)

#define TL_SHCI_EVT_DBG_MSG(...)
#define TL_SHCI_EVT_DBG_BUF(...)
#define TL_SHCI_EVT_DBG_RAW(...)

#define TL_HCI_CMD_DBG_MSG(...)
#define TL_HCI_CMD_DBG_BUF(...)
#define TL_HCI_CMD_DBG_RAW(...)

#define TL_HCI_EVT_DBG_MSG(...)
#define TL_HCI_EVT_DBG_BUF(...)
#define TL_HCI_EVT_DBG_RAW(...)

#define TL_MM_DBG_MSG(...)

#endif /* TL_DBG_CONF_H */
