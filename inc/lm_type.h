/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

/**
 * @addtogroup LEAManager
 * @{
 * @addtogroup LEAManagerCore Core
 * @{
 * This section defines the fundamental types, status codes, and module identifiers
 * used throughout the LEA Manager.
 */

#ifndef __LM_TYPE_H__
#define __LM_TYPE_H__

#include <glib.h>
#include <gio/gio.h>

/**
 * @defgroup LEAManagerCore_define Define
 * @{
 */

/**
 * @brief Module ID offset in the message type.
 */
#define LM_MODULE_OFFSET                        27

/**
 * @brief Module IDs for different LEA Manager components.
 * These values are used to construct message types by shifting left by LM_MODULE_OFFSET.
 */
#define LM_MODULE_GENERAL                       (0x00 << LM_MODULE_OFFSET) /**< General module. */
#define LM_MODULE_ADAPTER                       (0x01 << LM_MODULE_OFFSET) /**< Adapter module. */
#define LM_MODULE_ADV                           (0x02 << LM_MODULE_OFFSET) /**< Advertising module. */
#define LM_MODULE_AGENT                         (0x03 << LM_MODULE_OFFSET) /**< Agent module. */
#define LM_MODULE_DEVICE                        (0x04 << LM_MODULE_OFFSET) /**< Device module. */
#define LM_MODULE_PLAYER                        (0x05 << LM_MODULE_OFFSET) /**< Player module. */
#define LM_MODULE_TRANSPORT                     (0x06 << LM_MODULE_OFFSET) /**< Transport module. */
#define LM_MODULE_ENDPOINT                      (0x07 << LM_MODULE_OFFSET) /**< Endpoint module. */
#define LM_MODULE_GATT_SRV                      (0x08 << LM_MODULE_OFFSET) /**< GATT Server module. */
#define LM_MODULE_GATT_CLI                      (0x09 << LM_MODULE_OFFSET) /**< GATT Client module. */
#define LM_MODULE_PROFILE                       (0x0A << LM_MODULE_OFFSET) /**< BREDR Profile module. */
#define LM_MODULE_MAX                           (0x1F << LM_MODULE_OFFSET) /**< Maximum module ID. */

/**
 * @brief General status codes.
 */
#define LM_STATUS_SUCCESS                       (LM_MODULE_GENERAL | 0 )    /**< Operation successful. */
#define LM_STATUS_FAIL                          (LM_MODULE_GENERAL | 1 )    /**< Operation failed. */
#define LM_STATUS_INVALID_ARGS                  (LM_MODULE_GENERAL | 2 )    /**< Invalid arguments. */
#define LM_STATUS_PENDING                       (LM_MODULE_GENERAL | 3 )    /**< Operation pending. */
#define LM_STATUS_BUSY                          (LM_MODULE_GENERAL | 4 )    /**< Resource busy. */
#define LM_STATUS_TIMEOUT                       (LM_MODULE_GENERAL | 5 )    /**< Operation timed out. */
#define LM_STATUS_NOT_READY                     (LM_MODULE_GENERAL | 6 )    /**< System not ready. */

/**
 * @}
 */

/**
 * @defgroup LEAManagerCore_typedef Typedef
 * @{
 */

/**
 * @brief Status type for LEA Manager operations.
 */
typedef guint32 lm_status_t;

/**
 * @brief Message type for LEA Manager events.
 * Message type is composed of module ID (upper 5 bits) and event ID (lower 27 bits).
 */
typedef guint32 lm_msg_type_t;

/**
 * @}
 */

/** @} */
/** @} */

#endif
