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
 * @addtogroup LEAManagerDevice Device
 * @{
 * This section defines the APIs for managing remote Bluetooth devices.
 * The device module provides functionality to connect, disconnect, and pair
 * with remote devices, as well as manage broadcast synchronization.
 */

#ifndef __LM_DEVICE_H__
#define __LM_DEVICE_H__

#include <glib.h>
#include <gio/gio.h>
#include "lm_type.h"
#include "lm_transport.h"
#include "lm_forward_decl.h"

/**
 * @defgroup LEAManagerDevice_enum Enumeration
 * @{
 */

/**
 * @brief Device bearer type.
 */
typedef enum {
    LM_DEVICE_BEARER_LE = 0,   /**< LE (Low Energy) bearer. */
    LM_DEVICE_BEARER_BREDR     /**< BR/EDR bearer. */
} lm_device_bearer_type_t;

/**
 * @brief Device disconnection reason.
 */
typedef enum {
    LM_DEVICE_DISCONN_UNKNOWN = 0,         /**< Unknown reason. */
    LM_DEVICE_DISCONN_TIMEOUT,            /**< Connection timeout. */
    LM_DEVICE_DISCONN_LOCAL_HOST,         /**< Local host initiated disconnect. */
    LM_DEVICE_DISCONN_REMOTE,            /**< Remote device initiated disconnect. */
    LM_DEVICE_DISCONN_AUTH_FAILURE,       /**< Authentication failure. */
    LM_DEVICE_DISCONN_LOCAL_HOST_SUSPEND, /**< Local host suspended connection. */
} lm_device_disconn_reason_t;

/**
 * @brief Device connection type.
 */
typedef enum {
    LM_DEVICE_CONN_NONE = 0,              /**< No connection. */
    LM_DEVICE_CONN_LE = (1 << 0),        /**< LE connection. */
    LM_DEVICE_CONN_BREDR = (1 << 1),     /**< BR/EDR connection. */
    LM_DEVICE_CONN_DUAL = (LM_DEVICE_CONN_LE | LM_DEVICE_CONN_BREDR) /**< Dual mode connection. */
} lm_device_conn_type_t;

/**
 * @brief Device connection state.
 */
typedef enum {
    LM_DEVICE_DISCONNECTED = 0,   /**< Disconnected. */
    LM_DEVICE_CONNECTED = 1,     /**< Connected. */
    LM_DEVICE_CONNECTING = 2,    /**< Connecting. */
    LM_DEVICE_DISCONNECTING = 3  /**< Disconnecting. */
} lm_device_conn_state_t;

/**
 * @brief Device bonding state.
 */
typedef enum {
    LM_DEVICE_BOND_NONE = 0, /**< Not bonded. */
    LM_DEVICE_BONDING = 1,   /**< Bonding in progress. */
    LM_DEVICE_BONDED = 2     /**< Bonded. */
} lm_device_bonding_state_t;

/**
 * @brief Broadcast synchronization state.
 */
typedef enum {
    LM_DEVICE_BCAST_IDLE = 0,        /**< Idle. */
    LM_DEVICE_BCAST_SYNCING,        /**< Syncing to broadcast. */
    LM_DEVICE_BCAST_SYNCED,         /**< Synced to broadcast. */
    LM_DEVICE_BCAST_LOST,           /**< Lost broadcast sync. */
    LM_DEVICE_BCAST_TERMINATING     /**< Terminating sync. */
} lm_device_bcast_sync_state_t;

/**
 * @}
 */

/**
 * @defgroup LEAManagerDevice_define Define
 * @{
 */

/**
 * @brief Device connected indication event.
 */
typedef struct {
    lm_adapter_t *adapter;               /**< Pointer to the adapter which the device is associated. */
    lm_device_t *device;                 /**< Pointer to the device. */
    lm_device_bearer_type_t type;       /**< Bearer type (LE or BREDR). */
} lm_device_connected_ind_t;
#define LM_DEVICE_CONNECTED_IND        (LM_MODULE_DEVICE | 0x0001)

/**
 * @brief Device disconnected indication event.
 */
typedef struct {
    lm_adapter_t *adapter;               /**< Pointer to the adapter which the device is associated. */
    lm_device_t *device;                 /**< Pointer to the device. */
    lm_device_bearer_type_t type;       /**< Bearer type. */
    lm_device_disconn_reason_t reason;  /**< Disconnection reason. */
} lm_device_disconnected_ind_t;
#define LM_DEVICE_DISCONNECTED_IND     (LM_MODULE_DEVICE | 0x0002)

/**
 * @brief Device removed indication event.
 */
typedef struct {
    lm_adapter_t *adapter; /**< Pointer to the adapter which the device is associated. */
    lm_device_t *device;   /**< Pointer to the device. */
} lm_device_removed_ind_t;
#define LM_DEVICE_REMOVED_IND          (LM_MODULE_DEVICE | 0x0003)

/**
 * @brief Broadcast sync up indication event.
 */
typedef struct {
    lm_device_t *device; /**< Pointer to the device. */
} lm_device_bcast_sync_up_ind_t;
#define LM_DEVICE_BCAST_SYNC_UP_IND    (LM_MODULE_DEVICE | 0x0004)

/**
 * @brief Broadcast sync lost indication event.
 */
typedef struct {
    lm_device_t *device; /**< Pointer to the device. */
} lm_device_bcast_sync_lost_ind_t;
#define LM_DEVICE_BCAST_SYNC_LOST_IND   (LM_MODULE_DEVICE | 0x0005)

/**
 * @brief Device connection state change indication event.
 */
typedef struct {
    lm_adapter_t *adapter; /**< Pointer to the adapter which the device is associated. */
    lm_device_t *device;   /**< Pointer to the device. */
} lm_device_conn_state_change_ind_t;
#define LM_DEVICE_CONN_STATE_CHANGE_IND  (LM_MODULE_DEVICE | 0x0006)

/**
 * @}
 */

/**
 * @defgroup LEAManagerDevice_function Function
 * @{
 */

/**
 * @brief     Look up a device by D-Bus object path.
 * @param[in] adapter Pointer to the adapter.
 * @param[in] path    The D-Bus object path string.
 * @return    Pointer to the device, or NULL if not found.
 */
lm_device_t *lm_device_lookup_by_path(lm_adapter_t *adapter, const gchar *path);

/**
 * @brief     Get the device name.
 * @param[in] device Pointer to the device.
 * @return    The device name string, or NULL if not available.
 */
const gchar *lm_device_get_name(lm_device_t *device);

/**
 * @brief     Get the device Bluetooth address string.
 * @param[in] device Pointer to the device.
 * @return    The Bluetooth address string.
 */
const gchar *lm_device_get_address(const lm_device_t *device);

/**
 * @brief     Get the D-Bus connection associated with the device.
 * @param[in] device Pointer to the device.
 * @return    Pointer to the GDBusConnection.
 */
GDBusConnection *lm_device_get_dbus_conn(const lm_device_t *device);

/**
 * @brief     Convert connection state to string.
 * @param[in] state The connection state.
 * @return    String representation of the state.
 */
const gchar *lm_device_conn_state_to_string(lm_device_conn_state_t state);

/**
 * @brief     Convert disconnection reason to string.
 * @param[in] reason The disconnection reason.
 * @return    String representation of the reason.
 */
const gchar *lm_device_disconn_reason_to_string(lm_device_disconn_reason_t reason);

/**
 * @brief     Get the overall connection state of the device.
 * @param[in] device Pointer to the device.
 * @return    The connection state (#lm_device_conn_state_t).
 */
lm_device_conn_state_t lm_device_get_conn_state(const lm_device_t *device);

/**
 * @brief     Get the connection state for a specific bearer.
 * @param[in] device Pointer to the device.
 * @param[in] type   The bearer type.
 * @return    The connection state for the bearer.
 */
lm_device_conn_state_t lm_device_get_bearer_conn_state(const lm_device_t *device,
                lm_device_bearer_type_t type);

/**
 * @brief     Get the bonding state of the device.
 * @param[in] device Pointer to the device.
 * @return    The bonding state (#lm_device_bonding_state_t).
 */
lm_device_bonding_state_t lm_device_get_bonding_state(const lm_device_t *device);

/**
 * @brief     Get the bonding state for a specific bearer.
 * @param[in] device Pointer to the device.
 * @param[in] type   The bearer type.
 * @return    The bonding state for the bearer.
 */
lm_device_bonding_state_t lm_device_get_bearer_bonding_state(const lm_device_t *device,
                lm_device_bearer_type_t type);

/**
 * @brief     Get the RSSI of the device.
 * @param[in] device Pointer to the device.
 * @return    The RSSI value in dBm.
 */
gint16 lm_device_get_rssi(const lm_device_t *device);

/**
 * @brief     Check if the device has a specific service.
 * @param[in] device      Pointer to the device.
 * @param[in] service_uuid The service UUID to check.
 * @return    TRUE if the device has the service, FALSE otherwise.
 */
gboolean lm_device_has_service(const lm_device_t *device, const gchar *service_uuid);

/**
 * @brief     Get the service data from the device.
 * @param[in] device Pointer to the device.
 * @return    Hash table of service data (do not free).
 */
GHashTable *lm_device_get_service_data(const lm_device_t *device);

/**
 * @brief     Get the D-Bus object path of the device.
 * @param[in] device Pointer to the device.
 * @return    The D-Bus object path string.
 */
const gchar *lm_device_get_path(lm_device_t *device);

/**
 * @brief     Get the list of UUIDs supported by the device.
 * @param[in] device Pointer to the device.
 * @return    List of UUID strings (owned by the device, caller MUST not free).
 */
GList *lm_device_get_uuids(lm_device_t *device);

/**
 * @brief     Get the adapter associated with the device.
 * @param[in] device Pointer to the device.
 * @return    Pointer to the adapter.
 */
lm_adapter_t *lm_device_get_adapter(const lm_device_t *device);

/**
 * @brief     Get a string representation of the device.
 * @param[in] device Pointer to the device.
 * @return    String representation (caller must free with g_free).
 */
gchar *lm_device_to_string(const lm_device_t *device);

/**
 * @brief     Check if the device is a broadcast device.
 * @param[in] device Pointer to the device.
 * @return    TRUE if the device is a broadcast device, FALSE otherwise.
 */
gboolean lm_device_is_bcast_device(lm_device_t *device);

/**
 * @brief     Get the active player of the device.
 * @param[in] device Pointer to the device.
 * @return    Pointer to the active player, or NULL if none.
 */
lm_player_t *lm_device_get_active_player(lm_device_t *device);

/**
 * @brief     Get the active transport of the device.
 * @param[in] device Pointer to the device.
 * @return    Pointer to the active transport, or NULL if none.
 */
lm_transport_t *lm_device_get_active_transport(lm_device_t *device);

/**
 * @brief     Get the connection type of the device.
 * @param[in] device Pointer to the device.
 * @return    The connection type (#lm_device_conn_type_t).
 */
lm_device_conn_type_t lm_device_get_conn_type(const lm_device_t *device);

/**
 * @brief     Initiate pairing with the device.
 * @param[in] device Pointer to the device.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_device_pair(lm_device_t *device);

/**
 * @brief     Connect to the device with all available bearers.
 * @param[in] device Pointer to the device.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_device_connect(lm_device_t *device);

/**
 * @brief     Disconnect from the device with all available bearers.
 * @param[in] device Pointer to the device.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_device_disconnect(lm_device_t *device);

/**
 * @brief     Connect to the device with a specific bearer.
 * @param[in] device Pointer to the device.
 * @param[in] type   The bearer type to connect with.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_device_connect_bearer(lm_device_t *device, lm_device_bearer_type_t type);

/**
 * @brief     Disconnect a specific bearer.
 * @param[in] device Pointer to the device.
 * @param[in] type   The bearer type to disconnect.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_device_disconnect_bearer(lm_device_t *device, lm_device_bearer_type_t type);

/**
 * @brief     Check if the broadcast device is encrypted.
 * @param[in] device Pointer to the device.
 * @return    TRUE if encrypted, FALSE otherwise.
 */
gboolean lm_device_is_bcast_encrypted(lm_device_t *device);

/**
 * @brief     Get the broadcast synchronization state.
 * @param[in] device Pointer to the device.
 * @return    The broadcast sync state (#lm_device_bcast_sync_state_t).
 */
lm_device_bcast_sync_state_t lm_device_get_bcast_sync_state(lm_device_t *device);

/**
 * @brief     Start synchronizing to a broadcast.
 * @param[in] device   Pointer to the device.
 * @param[in] location Audio location configuration.
 * @param[in] bcode    Broadcast code.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_device_start_sync_bcast(lm_device_t *device,
                lm_transport_audio_location_t location,
                lm_transport_bcast_code_t *bcode);

/**
 * @brief     Stop synchronizing to a broadcast.
 * @param[in] device Pointer to the device.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_device_stop_sync_bcast(lm_device_t *device);

/**
 * @brief     Get transports associated with the device.
 * @param[in] device  Pointer to the device.
 * @param[in] profile Transport profile filter.
 * @param[in] array  Array to store the transports.
 */
void lm_device_get_transports(lm_device_t *device,
                    lm_transport_profile_t profile, GPtrArray *array);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_DEVICE_H__
