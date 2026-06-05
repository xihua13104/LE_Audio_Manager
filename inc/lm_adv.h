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
 * @addtogroup LEAManagerAdv Advertising
 * @{
 * This section defines the APIs for managing Bluetooth LE advertising.
 * The advertising module provides functionality to configure and control
 * advertising parameters for peripheral and broadcaster roles.
 */

#ifndef __LM_ADV_H__
#define __LM_ADV_H__

#include <glib.h>
#include <gio/gio.h>
#include "lm_adapter.h"
#include "lm_forward_decl.h"

/**
 * @defgroup LEAManagerAdv_enum Enumeration
 * @{
 */

/**
 * @brief Secondary channel type for extended advertising.
 */
typedef enum {
    LM_ADV_SC_1M = 0,     /**< Secondary channel 1M PHY. */
    LM_ADV_SC_2M,         /**< Secondary channel 2M PHY. */
    LM_ADV_SC_CODED       /**< Secondary channel Coded PHY. */
} lm_adv_secondary_channel_t;

/**
 * @}
 */

/**
 * @defgroup LEAManagerAdv_function Function
 * @{
 */

/**
 * @brief     Create an advertising object.
 * @return    Pointer to the created advertising object, or NULL on failure.
 */
lm_adv_t *lm_adv_create(void);

/**
 * @brief     Destroy an advertising object.
 * @param[in] adv Pointer to the advertising object to destroy.
 */
void lm_adv_destroy(lm_adv_t *adv);

/**
 * @brief     Set the local device name in advertising data.
 * @param[in] adv  Pointer to the advertising object.
 * @param[in] name The local name to advertise.
 */
void lm_adv_set_local_name(lm_adv_t *adv, const gchar *name);

/**
 * @brief     Get the local device name.
 * @param[in] adv Pointer to the advertising object.
 * @return    The local name string.
 */
const gchar *lm_adv_get_local_name(lm_adv_t *adv);

/**
 * @brief     Set service UUIDs in advertising data.
 * @param[in] adv          Pointer to the advertising object.
 * @param[in] service_uuids Array of service UUID strings.
 */
void lm_adv_set_services(lm_adv_t *adv, const GPtrArray * service_uuids);

/**
 * @brief     Set manufacturer data in advertising data.
 * @param[in] adv              Pointer to the advertising object.
 * @param[in] manufacturer_id The Bluetooth manufacturer ID.
 * @param[in] byteArray        The manufacturer data.
 */
void lm_adv_set_manufacturer_data(lm_adv_t *adv, guint16 manufacturer_id, const GByteArray *byteArray);

/**
 * @brief     Set service data in advertising data.
 * @param[in] adv         Pointer to the advertising object.
 * @param[in] service_uuid The service UUID string.
 * @param[in] byteArray   The service data.
 */
void lm_adv_set_service_data(lm_adv_t *adv, const gchar* service_uuid, const GByteArray *byteArray);

/**
 * @brief     Set advertising interval.
 * @param[in] adv  Pointer to the advertising object.
 * @param[in] min  Minimum advertising interval in slots (0.625ms per slot).
 * @param[in] max  Maximum advertising interval in slots.
 */
void lm_adv_set_interval(lm_adv_t *adv, guint32 min, guint32 max);

/**
 * @brief     Get the D-Bus object path of the advertising object.
 * @param[in] adv Pointer to the advertising object.
 * @return    The D-Bus object path string.
 */
const gchar *lm_adv_get_path(const lm_adv_t *adv);

/**
 * @brief     Set the device appearance in advertising data.
 * @param[in] adv        Pointer to the advertising object.
 * @param[in] appearance The appearance value (Bluetooth SIG assigned numbers).
 */
void lm_adv_set_appearance(lm_adv_t *adv, guint16 appearance);

/**
 * @brief     Get the device appearance.
 * @param[in] adv Pointer to the advertising object.
 * @return    The appearance value.
 */
guint16 lm_adv_get_appearance(lm_adv_t *adv);

/**
 * @brief     Set discoverable mode.
 * @param[in] adv          Pointer to the advertising object.
 * @param[in] discoverable TRUE to enable discoverable mode.
 */
void lm_adv_set_discoverable(lm_adv_t *adv, gboolean discoverable);

/**
 * @brief     Check if discoverable mode is enabled.
 * @param[in] adv Pointer to the advertising object.
 * @return    TRUE if discoverable, FALSE otherwise.
 */
gboolean lm_adv_is_discoverable(lm_adv_t *adv);

/**
 * @brief     Set discoverable timeout.
 * @param[in] adv     Pointer to the advertising object.
 * @param[in] timeout Timeout in seconds (0 for unlimited).
 */
void lm_adv_set_discoverable_timeout(lm_adv_t *adv, guint16 timeout);

/**
 * @brief     Get discoverable timeout.
 * @param[in] adv Pointer to the advertising object.
 * @return    Timeout in seconds.
 */
guint16 lm_adv_get_discoverable_timeout(lm_adv_t *adv);

/**
 * @brief     Set TX power level.
 * @param[in] adv      Pointer to the advertising object.
 * @param[in] tx_power TX power level in dBm.
 */
void lm_adv_set_tx_power(lm_adv_t *adv, gint16 tx_power);

/**
 * @brief     Get TX power level.
 * @param[in] adv Pointer to the advertising object.
 * @return    TX power level in dBm.
 */
gint16 lm_adv_get_tx_power(lm_adv_t *adv);

/**
 * @brief     Set secondary channel type for extended advertising.
 * @param[in] adv              Pointer to the advertising object.
 * @param[in] secondary_channel The secondary channel type.
 */
void lm_adv_set_secondary_channel(lm_adv_t *adv, lm_adv_secondary_channel_t secondary_channel);

/**
 * @brief     Get secondary channel type.
 * @param[in] adv Pointer to the advertising object.
 * @return    The secondary channel type (#lm_adv_secondary_channel_t).
 */
lm_adv_secondary_channel_t lm_adv_get_secondary_channel(lm_adv_t *adv);

/**
 * @brief     Set RSI (Resolvable Set Identifier) for broadcast.
 * @param[in] adv Pointer to the advertising object.
 */
void lm_adv_set_rsi(lm_adv_t *adv);

/**
 * @brief     Register advertising with BlueZ.
 * @param[in] adv Pointer to the advertising object.
 * @return    #LM_STATUS_SUCCESS if registration is successful.
 */
lm_status_t lm_adv_register(lm_adv_t *adv);

/**
 * @brief     Unregister advertising from BlueZ.
 * @param[in] adv Pointer to the advertising object.
 * @return    #LM_STATUS_SUCCESS if unregistration is successful.
 */
lm_status_t lm_adv_unregister(lm_adv_t *adv);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_ADV_H__
