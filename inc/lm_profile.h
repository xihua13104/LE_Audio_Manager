/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

/**
 * @addtogroup LEAManager
 * @{
 * @addtogroup LEAManagerProfile BREDR Profiles
 * @{
 * This section defines the APIs for managing Bluetooth BREDR profiles.
 * The profile module provides functionality to register custom RFCOMM/L2CAP
 * profiles through BlueZ ProfileManager1 and handle incoming profile
 * connections from remote devices.
 *
 * @section lm_profile_usage How to use this module
 *
 * - Create a profile object using #lm_profile_create().
 * - Configure profile parameters such as RFCOMM channel, authentication,
 *   authorization, and SDP service record.
 * - Register the profile using #lm_profile_register().
 * - Handle incoming connections through application event callbacks.
 * - Unregister and destroy the profile when no longer needed.
 *
 * - Sample code:
 *     @code
 *         lm_profile_t *profile;
 *         lm_adapter_t *adapter = lm_adapter_get_default();
 *
 *         profile = lm_profile_create(adapter, "0000111e-0000-1000-8000-00805f9b34fb");
 *
 *         lm_profile_set_name(profile, "HFP HS");
 *         lm_profile_set_channel(profile, 7);
 *         lm_profile_set_features(profile, 0x0001);
 *         lm_profile_set_require_auth(profile, TRUE);
 *
 *         lm_profile_register(profile);
 *         lm_profile_destroy(profile);
 *     @endcode
 */

#ifndef __LM_PROFILE_H__
#define __LM_PROFILE_H__

#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_type.h"

/**
 * @defgroup LEAManagerProfile_define Define
 * @{
 */

/**
 * @brief Profile connection indication event.
 */
typedef struct {
    lm_profile_t *profile;    /**< Pointer to the profile. */
    lm_device_t *device;      /**< Pointer to the connected device. */
    gint fd;                  /**< File descriptor for the new connection. */
    guint16 version;          /**< Bluetooth profile version. */
    guint16 features;         /**< Supported features bitmask. */
} lm_profile_connection_ind_t;

/**
 * @brief Event ID for profile connection indication.
 */
#define LM_PROFILE_CONNECTION_IND         (LM_MODULE_PROFILE | 0x0001)

/**
 * @brief Profile disconnection indication event.
 */
typedef struct {
    lm_profile_t *profile;    /**< Pointer to the profile. */
    lm_device_t *device;      /**< Pointer to the disconnected device. */
} lm_profile_disconnection_ind_t;

/**
 * @brief Event ID for profile disconnection indication.
 */
#define LM_PROFILE_DISCONNECTION_IND      (LM_MODULE_PROFILE | 0x0002)

/**
 * @}
 */

/**
 * @defgroup LEAManagerProfile_function Function
 * @{
 */

/**
 * @brief     Create a BREDR profile object.
 * @param[in] adapter Pointer to the adapter object that the profile belongs to.
 * @param[in] uuid UUID string of the profile.
 * @return    Pointer to the created profile object.
 */
lm_profile_t *lm_profile_create(lm_adapter_t *adapter, const gchar *uuid);

/**
 * @brief     Destroy a profile object.
 * @param[in] profile Pointer to the profile object.
 */
void lm_profile_destroy(lm_profile_t *profile);

/**
 * @brief     Set the human-readable profile name.
 * @param[in] profile Pointer to the profile object.
 * @param[in] name    Profile name string.
 */
void lm_profile_set_name(lm_profile_t *profile, const gchar *name);

/**
 * @brief     Get the human-readable profile name.
 * @param[in] profile Pointer to the profile object.
 * @return    Profile name string.
 */
const gchar *lm_profile_get_name(lm_profile_t *profile);

/**
 * @brief     Set RFCOMM channel number.
 * @param[in] profile Pointer to the profile object.
 * @param[in] channel RFCOMM channel number.
 */
void lm_profile_set_channel(lm_profile_t *profile, guint16 channel);

/**
 * @brief     Set PSM number that is used for client and server UUIDs.
 * @param[in] profile Pointer to the profile object.
 * @param[in] psm     L2CAP PSM value.
 */
void lm_profile_set_psm(lm_profile_t *profile, guint16 psm);

/**
 * @brief     Set the Bluetooth profile version(for SDP record).
 * @param[in] profile Pointer to the profile object.
 * @param[in] version Profile version number.
 */
void lm_profile_set_version(lm_profile_t *profile, guint16 version);

/**
 * @brief     Set the supported features bitmask(for SDP record).
 * @param[in] profile Pointer to the profile object.
 * @param[in] features Features bitmask.
 */
void lm_profile_set_features(lm_profile_t *profile, guint16 features);
/**
 * @brief     Enable or disable authentication requirement.
 * @param[in] profile Pointer to the profile object.
 * @param[in] enable  TRUE to require authentication.
 */
void lm_profile_set_require_auth(lm_profile_t *profile, gboolean enable);

/**
 * @brief     Enable or disable authorization requirement.
 * @param[in] profile Pointer to the profile object.
 * @param[in] enable  TRUE to require authorization.
 */
void lm_profile_set_require_authorize(lm_profile_t *profile,
                                      gboolean enable);

/**
 * @brief     Enable or disable automatic connection.
 * @param[in] profile Pointer to the profile object.
 * @param[in] enable  TRUE to enable auto connect.
 */
void lm_profile_set_auto_connect(lm_profile_t *profile, gboolean enable);

/**
 * @brief     Set custom SDP service record.
 * @param[in] profile Pointer to the profile object.
 * @param[in] record  XML SDP service record string.
 */
void lm_profile_set_service_record(lm_profile_t *profile,
                                   const gchar *record);

/**
 * @brief     Get the D-Bus object path of the profile.
 * @param[in] profile Pointer to the profile object.
 * @return    D-Bus object path string.
 */
const gchar *lm_profile_get_path(lm_profile_t *profile);

/**
 * @brief     Get the adapter associated with the profile.
 * @param[in] profile Pointer to the profile object.
 * @return    Pointer to the adapter object.
 */
lm_adapter_t *lm_profile_get_adapter(lm_profile_t *profile);

/**
 * @brief     Register the profile with BlueZ ProfileManager1.
 * @param[in] profile Pointer to the profile object.
 * @return    #LM_STATUS_SUCCESS if registration succeeds.
 */
lm_status_t lm_profile_register(lm_profile_t *profile);

/**
 * @brief     Unregister the profile from BlueZ ProfileManager1.
 * @param[in] profile Pointer to the profile object.
 * @return    #LM_STATUS_SUCCESS if unregistration succeeds.
 */
lm_status_t lm_profile_unregister(lm_profile_t *profile);

/**
 * @}
 */

/** @} */
/** @} */

#endif // __LM_PROFILE_H__