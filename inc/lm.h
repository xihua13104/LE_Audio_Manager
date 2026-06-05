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
 * This section defines the LEA (Low Energy Audio) Manager APIs for Bluetooth LE Audio functionality.
 * The LEA Manager provides high-level abstractions for managing LE Audio devices, transports,
 * players, and GATT services based on BlueZ D-Bus interface.
 *
 * @section lea_manager_usage How to use this module
 *
 * - Initialize the LEA Manager by calling #lm_init(). This will create a D-Bus connection
 *   and start the main event loop in a separate thread.
 * - Register application callbacks using #lm_register_callback() to receive events from
 *   various modules.
 * - Use the adapter APIs to manage Bluetooth adapter, discover devices, and control advertising.
 * - Use the device APIs to connect/disconnect remote devices and manage broadcast synchronization.
 * - Use the transport APIs to manage audio streams and QoS settings.
 * - Use the player APIs to control media playback through AVRCP/MCP profiles.
 * - Use the GATT APIs to implement custom GATT servers and clients.
 * - Deinitialize by calling #lm_deinit() when done.
 *
 * - Sample code:
 *     @code
 *         // Initialize LEA Manager
 *         lm_status_t status = lm_init();
 *         if (status != LM_STATUS_SUCCESS) {
 *             // Handle initialization failure
 *         }
 *
 *         // Register application event callback
 *         lm_register_callback(LM_CALLBACK_TYPE_APP_EVENT,
 *                             MODULE_MASK_DEVICE | MODULE_MASK_TRANSPORT,
 *                             app_event_callback);
 *
 *         // Get default adapter and power on
 *         lm_adapter_t *adapter = lm_adapter_get_default();
 *         lm_adapter_power_on(adapter);
 *
 *         // Start device discovery
 *         lm_adapter_start_discovery(adapter);
 *
 *         // Application event callback
 *         lm_status_t app_event_callback(lm_msg_type_t msg, lm_status_t status, void *buf)
 *         {
 *             switch (msg) {
 *                 case LM_DEVICE_CONNECTED_IND:
 *                     // Handle device connected
 *                     break;
 *                 case LM_TRANSPORT_STATE_CHANGE_IND:
 *                     // Handle transport state change
 *                     break;
 *                 default:
 *                     break;
 *             }
 *             return LM_STATUS_SUCCESS;
 *         }
 *     @endcode
 *
 */

#ifndef __LM_H__
#define __LM_H__
#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_transport.h"

/**
 * @defgroup LEAManagerCore_define Define
 * @{
 */

/**
 * @brief LEA Manager callback types.
 */
typedef enum {
    LM_CALLBACK_TYPE_APP_EVENT = 0,                /**< Application event callback. */
    LM_CALLBACK_TYPE_GET_AUDIO_LOCATION_CFG = 1,   /**< Audio location configuration callback. */
    LM_CALLBACK_TYPE_MAX = 10                      /**< Maximum callback type value. */
} lm_callback_type_t;

/**
 * @brief Message format description.
 * The message type is a 32-bit value composed of module ID and event ID.
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |module id|                    event id                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  5bits  |                     27bits                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define LM_MODULE_MASK(module)          (1 << ((module) >> LM_MODULE_OFFSET))
#define MODULE_MASK_GENERAL             LM_MODULE_MASK(LM_MODULE_GENERAL)  /**< Module mask for GENERAL module. */
#define MODULE_MASK_ADAPTER             LM_MODULE_MASK(LM_MODULE_ADAPTER)  /**< Module mask for ADAPTER module. */
#define MODULE_MASK_ADV                 LM_MODULE_MASK(LM_MODULE_ADV)     /**< Module mask for ADV module. */
#define MODULE_MASK_AGENT               LM_MODULE_MASK(LM_MODULE_AGENT)   /**< Module mask for AGENT module. */
#define MODULE_MASK_DEVICE              LM_MODULE_MASK(LM_MODULE_DEVICE)   /**< Module mask for DEVICE module. */
#define MODULE_MASK_PLAYER              LM_MODULE_MASK(LM_MODULE_PLAYER)  /**< Module mask for PLAYER module. */
#define MODULE_MASK_TRANSPORT           LM_MODULE_MASK(LM_MODULE_TRANSPORT) /**< Module mask for TRANSPORT module. */
#define MODULE_MASK_GATT_SRV            LM_MODULE_MASK(LM_MODULE_GATT_SRV) /**< Module mask for GATT SERVER module. */
#define MODULE_MASK_GATT_CLI            LM_MODULE_MASK(LM_MODULE_GATT_CLI) /**< Module mask for GATT CLIENT module. */
#define MODULE_MASK_PROFILE             LM_MODULE_MASK(LM_MODULE_PROFILE) /**< Module mask for BREDR PROFILE module. */
typedef guint32 lm_callback_module_mask_t; /**< Type definition for callback module mask. */

/**
 * @}
 */

/**
 * @defgroup LEAManagerCore_callback Callback
 * @{
 */

/**
 * @brief Application event callback function type.
 * @param[in] msg    The message type containing module ID and event ID.
 * @param[in] status The status of the callback message.
 * @param[in] buf    The payload of the callback message.
 * @return           The status of this operation returned from the callback.
 */
typedef lm_status_t (*lm_app_callback_func_t)(lm_msg_type_t msg, lm_status_t status, void *buf);

/**
 * @brief Audio location configuration callback function type.
 * @param[in] profile   The transport profile type.
 * @param[out] location The audio location configuration to be filled by the callback.
 * @return              The status of this operation.
 */
typedef lm_status_t (*lm_get_audio_location_cfg_t)(lm_transport_profile_t profile,
                        lm_transport_audio_location_t *location);

/**
 * @}
 */

/**
 * @defgroup LEAManagerCore_function Function
 * @{
 */

/**
 * @brief     This function registers a callback for the specified callback type and module mask.
 * @param[in] type         The callback type (#lm_callback_type_t).
 * @param[in] module_mask  The module mask specifying which modules should trigger this callback.
 * @param[in] cb           The callback function pointer.
 * @return                 #LM_STATUS_SUCCESS if registration is successful.
 *                         #LM_STATUS_FAIL if registration fails.
 */
lm_status_t lm_register_callback(lm_callback_type_t type, lm_callback_module_mask_t module_mask, void *cb);

/**
 * @brief     This function unregisters a callback for the specified callback type and module mask.
 * @param[in] type         The callback type (#lm_callback_type_t).
 * @param[in] module_mask  The module mask (currently unused in implementation).
 * @param[in] cb           The callback function pointer to unregister.
 * @return                 #LM_STATUS_SUCCESS if unregistration is successful.
 *                         #LM_STATUS_FAIL if unregistration fails.
 */
lm_status_t lm_unregister_callback(lm_callback_type_t type, lm_callback_module_mask_t module_mask, void *cb);

/**
 * @brief     This is the default application event callback function that dispatches events
 *            to registered application callbacks based on the message type and module mask.
 * @param[in] msg    The message type containing module ID and event ID.
 * @param[in] status The status of the callback message.
 * @param[in] buf    The payload of the callback message.
 * @return           The status returned from the dispatched application callbacks.
 */
lm_status_t lm_app_event_callback(lm_msg_type_t msg, lm_status_t status, void *buf);

/**
 * @brief     This function initializes the LEA Manager. It establishes a D-Bus connection
 *            to the system bus and starts a main loop in a separate thread.
 * @return    #LM_STATUS_SUCCESS if initialization is successful.
 *            #LM_STATUS_FAIL if initialization fails (e.g., wrong state, D-Bus connection failed).
 * @note      Call this function before using any other LEA Manager APIs.
 */
lm_status_t lm_init(void);

/**
 * @brief     This function deinitializes the LEA Manager. It stops the main loop, terminates
 *            the D-Bus thread, and closes the D-Bus connection.
 * @return    #LM_STATUS_SUCCESS if deinitialization is successful.
 *            #LM_STATUS_FAIL if deinitialization fails (e.g., wrong state).
 * @note      Call this function when LEA Manager is no longer needed.
 */
lm_status_t lm_deinit(void);

/**
 * @brief     This function retrieves the D-Bus connection used by the LEA Manager.
 * @return    A pointer to the GDBusConnection if initialized, or NULL if not initialized.
 * @note      This can be used by applications to perform custom D-Bus operations if needed.
 */
GDBusConnection *lm_get_gdbus_connection(void);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_H__
