/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @addtogroup LEAManager
 * @{
 * @addtogroup LEAManagerAdapter Adapter
 * @{
 * This section defines the APIs for managing Bluetooth adapters.
 * The adapter module provides functionality to control Bluetooth adapter power state,
 * device discovery, advertising, and device management.
 *
 * @section bt_adapter_usage How to use this module
 *
 * - Get the default adapter using #lm_adapter_get_default().
 * - Control adapter power state with #lm_adapter_power_on() and #lm_adapter_power_off().
 * - Discover remote devices with #lm_adapter_start_discovery() and #lm_adapter_stop_discovery().
 * - Set discoverable mode with #lm_adapter_discoverable_on() and #lm_adapter_discoverable_off().
 * - Iterate through known devices using #lm_adapter_foreach_device().
 * - Register GATT servers using #lm_adapter_register_gatt_server().
 *
 * - Sample code:
 *     @code
 *         // Get default adapter
 *         lm_adapter_t *adapter = lm_adapter_get_default();
 *
 *         // Power on adapter
 *         lm_adapter_power_on(adapter);
 *
 *         // Set discoverable and start discovery
 *         lm_adapter_discoverable_on(adapter);
 *         lm_adapter_start_discovery(adapter);
 *
 *         // Iterate through discovered devices
 *         lm_adapter_foreach_device(adapter, device_callback, NULL);
 *     @endcode
 *
 */

#ifndef __LM_ADAPTER_H__
#define __LM_ADAPTER_H__
#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_device.h"

/**
 * @defgroup LEAManagerAdapter_enum Enumeration
 * @{
 */

/**
 * @brief Adapter power state.
 */
typedef enum {
    LM_ADAPTER_POWER_ON = 0,           /**< Adapter is powered on. */
    LM_ADAPTER_POWER_OFF,             /**< Adapter is powered off. */
    LM_ADAPTER_POWER_TURNING_ON,      /**< Adapter is turning on. */
    LM_ADAPTER_POWER_TURNING_OFF,     /**< Adapter is turning off. */
    LM_ADAPTER_POWER_OFF_BLOCKED      /**< Adapter power off is blocked. */
} lm_adapter_power_state_t;

/**
 * @brief Adapter discovery state.
 */
typedef enum {
    LM_ADAPTER_DISCOVERY_STOPPED = 0, /**< Discovery is stopped. */
    LM_ADAPTER_DISCOVERY_STARTING,     /**< Discovery is starting. */
    LM_ADAPTER_DISCOVERY_STARTED,     /**< Discovery is active. */
    LM_ADAPTER_DISCOVERY_STOPPING,    /**< Discovery is stopping. */
} lm_adapter_discovery_state_t;

/**
 * @brief Broadcast device discovery method.
 */
typedef enum {
    LM_ADAPTER_BCAST_DISCOVERED_BY_ASSISTANT = 0, /**< Discovered via LE Audio Assistant. */
    LM_ADAPTER_BCAST_DISCOVERED_BY_SINK_SCAN = 1, /**< Discovered by local scanning. */
} lm_adapter_bcast_discovery_method_t;

/**
 * @brief Callback function type for iterating devices.
 * @param[in] device    Pointer to the device.
 * @param[in] user_data User data passed to the iterator.
 */
typedef void (*lm_adapter_device_func_t)(lm_device_t *device, void *user_data);

/**
 * @}
 */

/**
 * @defgroup LEAManagerAdapter_define Define
 * @{
 */

/**
 * @brief Adapter power on confirmation event.
 */
typedef struct {
    lm_adapter_t *adapter; /**< Pointer to the adapter. */
} lm_adapter_power_on_cnf_t;
#define LM_ADAPTER_POWER_ON_CNF         (LM_MODULE_ADAPTER | 0x0001)

/**
 * @brief Adapter power off confirmation event.
 */
typedef struct {
    lm_adapter_t *adapter; /**< Pointer to the adapter. */
} lm_adapter_power_off_cnf_t;
#define LM_ADAPTER_POWER_OFF_CNF        (LM_MODULE_ADAPTER | 0x0002)

/**
 * @brief Adapter discovery state change indication event.
 */
typedef struct {
    lm_adapter_t *adapter; /**< Pointer to the adapter. */
} lm_adapter_discovery_state_change_ind_t;
#define LM_ADAPTER_DISCOVERY_STATE_CHANGE_IND  (LM_MODULE_ADAPTER | 0x0003)

/**
 * @brief Adapter discovery result indication event.
 */
typedef struct {
    lm_adapter_t *adapter; /**< Pointer to the adapter which the device is associated. */
    lm_device_t *device;   /**< Pointer to the discovered device. */
} lm_adapter_discovery_result_ind_t;
#define LM_ADAPTER_DISCOVERY_RESULT_IND        (LM_MODULE_ADAPTER | 0x0004)

/**
 * @brief Broadcast device discovered indication event.
 */
typedef struct {
    lm_adapter_bcast_discovery_method_t method;    /**< Discovery method. */
    lm_device_t *device;                           /**< Pointer to the broadcast device. */
    GPtrArray *bcast_transports;                   /**< Array of broadcast transports. */
    lm_device_t *assistant;                        /**< Assistant device (valid if method is #LM_ADAPTER_BCAST_DISCOVERED_BY_ASSISTANT). */
} lm_adapter_bcast_discovered_ind_t;
#define LM_ADAPTER_BCAST_DISCOVERED_IND  (LM_MODULE_ADAPTER | 0x0005)

/**
 * @brief Adapter discovery complete indication event.
 */
typedef struct {
    lm_adapter_t *adapter; /**< Pointer to the adapter. */
} lm_adapter_discovery_complete_ind_t;
#define LM_ADAPTER_DISCOVERY_COMPLETE_IND      (LM_MODULE_ADAPTER | 0x0006)

/**
 * @brief Local broadcast transport state change indication event.
 */
typedef struct {
    lm_adapter_t *adapter;     /**< Pointer to the adapter which the transport is associated. */
    lm_transport_t *transport; /**< Pointer to the transport. */
} lm_adapter_local_bcast_transport_state_change_ind_t;
#define LM_ADAPTER_LOCAL_BCAST_TRANSPORT_STATE_CHANGE_IND  (LM_MODULE_ADAPTER | 0x0007)

/**
 * @}
 */

/**
 * @defgroup LEAManagerAdapter_function Function
 * @{
 */

/**
 * @brief     Get the default Bluetooth adapter.
 * @return    Pointer to the default adapter, or NULL if none available.
 */
lm_adapter_t *lm_adapter_get_default(void);

/**
 * @brief     Destroy an adapter handle.
 * @param[in] adapter Pointer to the adapter to destroy.
 */
void lm_adapter_destroy(lm_adapter_t *adapter);

/**
 * @brief     Check if adapter is powered on.
 * @param[in] adapter Pointer to the adapter.
 * @return    TRUE if powered on, FALSE otherwise.
 */
gboolean lm_adapter_is_power_on(lm_adapter_t *adapter);

/**
 * @brief     Get the current power state of the adapter.
 * @param[in] adapter Pointer to the adapter.
 * @return    The current power state (#lm_adapter_power_state_t).
 */
lm_adapter_power_state_t lm_adapter_get_power_state(lm_adapter_t *adapter);

/**
 * @brief     Power on the adapter.
 * @param[in] adapter Pointer to the adapter.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_power_on(lm_adapter_t *adapter);

/**
 * @brief     Power off the adapter.
 * @param[in] adapter Pointer to the adapter.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_power_off(lm_adapter_t *adapter);

/**
 * @brief     Get the current discovery state.
 * @param[in] adapter Pointer to the adapter.
 * @return    The current discovery state (#lm_adapter_discovery_state_t).
 */
lm_adapter_discovery_state_t lm_adapter_get_discovery_state(lm_adapter_t *adapter);

/**
 * @brief     Start device discovery.
 * @param[in] adapter Pointer to the adapter.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_start_discovery(lm_adapter_t *adapter);

/**
 * @brief     Stop device discovery.
 * @param[in] adapter Pointer to the adapter.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_stop_discovery(lm_adapter_t *adapter);

/**
 * @brief     Set discovery filter parameters.
 * @param[in] adapter         Pointer to the adapter.
 * @param[in] rssi_threshold RSSI threshold for filtering (-127 to 127).
 * @param[in] service_uuids   Array of service UUIDs to filter (can be NULL).
 * @param[in] pattern         Pattern to match in device name (can be NULL).
 * @param[in] max_devices     Maximum number of devices to discover.
 * @param[in] timeout         Discovery timeout in milliseconds.
 */
void lm_adapter_set_discovery_filter(lm_adapter_t *adapter,
                                            gint16 rssi_threshold,
                                            const GPtrArray *service_uuids,
                                            const gchar *pattern,
                                            guint max_devices,
                                            guint timeout);

/**
 * @brief     Enable discoverable mode.
 * @param[in] adapter Pointer to the adapter.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_discoverable_on(lm_adapter_t *adapter);

/**
 * @brief     Disable discoverable mode.
 * @param[in] adapter Pointer to the adapter.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_discoverable_off(lm_adapter_t *adapter);

/**
 * @brief     Check if adapter is in discoverable mode.
 * @param[in] adapter Pointer to the adapter.
 * @return    TRUE if discoverable, FALSE otherwise.
 */
gboolean lm_adapter_is_discoverable(lm_adapter_t *adapter);

/**
 * @brief     Enable connectable mode.
 * @param[in] adapter Pointer to the adapter.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_connectable_on(lm_adapter_t *adapter);

/**
 * @brief     Disable connectable mode.
 * @param[in] adapter Pointer to the adapter.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_connectable_off(lm_adapter_t *adapter);

/**
 * @brief     Check if adapter is in connectable mode.
 * @param[in] adapter Pointer to the adapter.
 * @return    TRUE if connectable, FALSE otherwise.
 */
gboolean lm_adapter_is_connectable(lm_adapter_t *adapter);

/**
 * @brief     Get the D-Bus object path of the adapter.
 * @param[in] adapter Pointer to the adapter.
 * @return    The D-Bus object path string.
 */
const gchar *lm_adapter_get_path(lm_adapter_t *adapter);

/**
 * @brief     Set the adapter alias.
 * @param[in] adapter Pointer to the adapter.
 * @param[in] alias   The alias string to set.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_set_alias(lm_adapter_t *adapter, const gchar *alias);

/**
 * @brief     Get the adapter alias.
 * @param[in] adapter Pointer to the adapter.
 * @return    The current alias string.
 */
const gchar *lm_adapter_get_alias(lm_adapter_t *adapter);

/**
 * @brief     Get the adapter Bluetooth address.
 * @param[in] adapter Pointer to the adapter.
 * @return    The Bluetooth address string.
 */
const gchar *lm_adapter_get_address(lm_adapter_t *adapter);

/**
 * @brief     Iterate through all known devices.
 * @param[in] adapter   Pointer to the adapter.
 * @param[in] func      Callback function to call for each device.
 * @param[in] user_data User data to pass to the callback.
 */
void lm_adapter_foreach_device(lm_adapter_t *adapter,
                                lm_adapter_device_func_t func,
                                void *user_data);

/**
 * @brief     Get list of connected devices.
 * @param[in]  adapter  Pointer to the adapter.
 * @param[out] out_list Returned list of connected devices (GList of lm_device_t*).
 *                       Caller must free the list using g_list_free().
 *                       Device pointers are owned by adapter and must NOT be freed.
 */
void lm_adapter_get_connected_devices(lm_adapter_t *adapter, GList **out_list);

/**
 * @brief     Get list of paired devices.
 * @param[in]  adapter  Pointer to the adapter.
 * @param[out] out_list Returned list of paired devices (GList of lm_device_t*).
 *                       Caller must free the list using g_list_free().
 *                       Device pointers are owned by adapter and must NOT be freed.
 */
void lm_adapter_get_paired_devices(lm_adapter_t *adapter, GList **out_list);

/**
 * @brief     Get list of broadcast source devices.
 * @param[in]  adapter  Pointer to the adapter.
 * @param[out] out_list Returned list of broadcast source devices (GList of lm_device_t*).
 *                       Caller must free the list using g_list_free().
 *                       Device pointers are owned by adapter and must NOT be freed.
 */
void lm_adapter_get_bcast_source_devices(lm_adapter_t *adapter, GList **out_list);

/**
 * @brief     Get the D-Bus connection associated with the adapter.
 * @param[in] adapter Pointer to the adapter.
 * @return    Pointer to the GDBusConnection.
 */
GDBusConnection *lm_adapter_get_dbus_conn(lm_adapter_t *adapter);

/**
 * @brief     Check if adapter is currently advertising.
 * @param[in] adapter Pointer to the adapter.
 * @return    TRUE if advertising, FALSE otherwise.
 */
gboolean lm_adapter_is_advertising(lm_adapter_t *adapter);

/**
 * @brief     Start advertising.
 * @param[in] adapter Pointer to the adapter.
 * @param[in] adv     Pointer to the advertising object.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_start_adv(lm_adapter_t *adapter, lm_adv_t *adv);

/**
 * @brief     Stop advertising.
 * @param[in] adapter Pointer to the adapter.
 * @param[in] adv     Pointer to the advertising object.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_stop_adv(lm_adapter_t *adapter, lm_adv_t *adv);

/**
 * @brief     Remove a device from the adapter.
 * @param[in] adapter Pointer to the adapter.
 * @param[in] device  Pointer to the device to remove.
 * @return    #LM_STATUS_SUCCESS if request is sent successfully.
 */
lm_status_t lm_adapter_remove_device(lm_adapter_t *adapter, lm_device_t *device);

/**
 * @brief     Get the device cache hash table.
 * @param[in] adapter Pointer to the adapter.
 * @return    Hash table containing cached devices (do not free).
 */
GHashTable *lm_adapter_get_device_cache(lm_adapter_t *adapter);

/**
 * @brief     Get local broadcast source transports.
 * @param[in] adapter Pointer to the adapter.
 * @param[in] array   Array to store the transports.
 */
void lm_adapter_get_local_bcast_source_transports(lm_adapter_t *adapter, GPtrArray *array);

/**
 * @brief     Register a GATT server with the adapter.
 * @param[in] adapter     Pointer to the adapter.
 * @param[in] gatt_server Pointer to the GATT server.
 * @return    #LM_STATUS_SUCCESS if registration is successful.
 */
lm_status_t lm_adapter_register_gatt_server(lm_adapter_t *adapter, lm_gatt_server_t *gatt_server);

/**
 * @brief     Unregister a GATT server from the adapter.
 * @param[in] adapter     Pointer to the adapter.
 * @param[in] gatt_server Pointer to the GATT server.
 * @return    #LM_STATUS_SUCCESS if unregistration is successful.
 */
lm_status_t lm_adapter_unregister_gatt_server(lm_adapter_t *adapter, lm_gatt_server_t *gatt_server);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_ADAPTER_H__
