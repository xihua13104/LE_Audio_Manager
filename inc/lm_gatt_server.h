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
 * @addtogroup LEAManagerGattServer GATT Server
 * @{
 * This section defines the APIs for GATT server functionality.
 * The GATT server module provides functionality to expose local services,
 * characteristics, and descriptors to remote GATT clients.
 */

#ifndef __LM_GATT_SRV_H__
#define __LM_GATT_SRV_H__

#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_adapter.h"
#include "lm_gatt.h"

/**
 * @defgroup LEAManagerGattServer_define Define
 * @{
 */

/**
 * @brief Characteristic read request indication event.
 */
typedef struct {
    const lm_gatt_server_t *gatt_server; /**< Pointer to the GATT server. */
    const gchar *client_addr;           /**< Client address. */
    const gchar *service_uuid;         /**< Service UUID. */
    const gchar *char_uuid;             /**< Characteristic UUID. */
    const guint16 mtu;                  /**< MTU size. */
    const guint16 offset;               /**< Read offset. */
} lm_gatt_server_char_read_ind_t;
#define LM_GATT_SERVER_CHAR_READ_IND               (LM_MODULE_GATT_SRV | 0x0001)

/**
 * @brief Characteristic write request indication event.
 */
typedef struct {
    const lm_gatt_server_t *gatt_server; /**< Pointer to the GATT server. */
    const gchar *client_addr;           /**< Client address. */
    const gchar *service_uuid;         /**< Service UUID. */
    const gchar *char_uuid;             /**< Characteristic UUID. */
    GByteArray *byte_array;            /**< Write data. */
    const guint16 mtu;                  /**< MTU size. */
    const guint16 offset;               /**< Write offset. */
} lm_gatt_server_char_wrt_ind_t;
#define LM_GATT_SERVER_CHAR_WRT_IND                (LM_MODULE_GATT_SRV | 0x0002)

/**
 * @brief Characteristic value updated indication event.
 */
typedef struct {
    const lm_gatt_server_t *gatt_server; /**< Pointer to the GATT server. */
    const gchar *service_uuid;         /**< Service UUID. */
    const gchar *char_uuid;             /**< Characteristic UUID. */
    GByteArray *byte_array;            /**< Updated value. */
} lm_gatt_server_char_updated_ind_t;
#define LM_GATT_SERVER_CHAR_UPDATED_IND            (LM_MODULE_GATT_SRV | 0x0003)

/**
 * @brief Characteristic notification enabled indication event.
 */
typedef struct {
    const lm_gatt_server_t *gatt_server; /**< Pointer to the GATT server. */
    const gchar *service_uuid;         /**< Service UUID. */
    const gchar *char_uuid;             /**< Characteristic UUID. */
} lm_gatt_server_char_ntf_enabled_ind_t;
#define LM_GATT_SERVER_CHAR_NTF_ENABLED_IND        (LM_MODULE_GATT_SRV | 0x0004)

/**
 * @brief Characteristic notification disabled indication event.
 */
typedef struct {
    const lm_gatt_server_t *gatt_server; /**< Pointer to the GATT server. */
    const gchar *service_uuid;         /**< Service UUID. */
    const gchar *char_uuid;             /**< Characteristic UUID. */
} lm_gatt_server_char_ntf_disabled_ind_t;
#define LM_GATT_SERVER_CHAR_NTF_DISABLED_IND       (LM_MODULE_GATT_SRV | 0x0005)

/**
 * @brief Descriptor read request indication event.
 */
typedef struct {
    const lm_gatt_server_t *gatt_server; /**< Pointer to the GATT server. */
    const gchar *client_addr;           /**< Client address. */
    const gchar *service_uuid;         /**< Service UUID. */
    const gchar *char_uuid;             /**< Characteristic UUID. */
    const gchar *desc_uuid;             /**< Descriptor UUID. */
} lm_gatt_server_desc_read_ind_t;
#define LM_GATT_SERVER_DESC_READ_IND               (LM_MODULE_GATT_SRV | 0x0006)

/**
 * @brief Descriptor write request indication event.
 */
typedef struct {
    const lm_gatt_server_t *gatt_server; /**< Pointer to the GATT server. */
    const gchar *client_addr;           /**< Client address. */
    const gchar *service_uuid;         /**< Service UUID. */
    const gchar *char_uuid;             /**< Characteristic UUID. */
    const gchar *desc_uuid;             /**< Descriptor UUID. */
    GByteArray *byte_array;            /**< Write data. */
} lm_gatt_server_desc_wrt_ind_t;
#define LM_GATT_SERVER_DESC_WRT_IND                (LM_MODULE_GATT_SRV | 0x0007)

/**
 * @brief Descriptor value updated indication event.
 */
typedef struct {
    const lm_gatt_server_t *gatt_server; /**< Pointer to the GATT server. */
    const gchar *service_uuid;         /**< Service UUID. */
    const gchar *char_uuid;             /**< Characteristic UUID. */
    const gchar *desc_uuid;             /**< Descriptor UUID. */
    GByteArray *byte_array;            /**< Updated value. */
} lm_gatt_server_desc_updated_ind_t;
#define LM_GATT_SERVER_DESC_UPDATED_IND            (LM_MODULE_GATT_SRV | 0x0008)

/**
 * @brief GATT server error status codes.
 */
#define LM_STATUS_GATT_SRV_FAILED                  (LM_MODULE_GATT_SRV | 0x0100)   /**< Operation failed. */
#define LM_STATUS_GATT_SRV_REJECTED                (LM_MODULE_GATT_SRV | 0x0101)   /**< Request rejected. */
#define LM_STATUS_GATT_SRV_INPROGRESS              (LM_MODULE_GATT_SRV | 0x0102)   /**< Operation in progress. */
#define LM_STATUS_GATT_SRV_NOT_PERMITTED           (LM_MODULE_GATT_SRV | 0x0103)   /**< Operation not permitted. */
#define LM_STATUS_GATT_SRV_INVALID_VALUE_LEN       (LM_MODULE_GATT_SRV | 0x0104)   /**< Invalid value length. */
#define LM_STATUS_GATT_SRV_NOT_AUTHORIZED          (LM_MODULE_GATT_SRV | 0x0105)   /**< Not authorized. */
#define LM_STATUS_GATT_SRV_NOT_SUPPORTED           (LM_MODULE_GATT_SRV | 0x0106)   /**< Feature not supported. */

/**
 * @}
 */

/**
 * @defgroup LEAManagerGattServer_function Function
 * @{
 */

/**
 * @brief     Create a GATT server.
 * @param[in] adapter Pointer to the adapter.
 * @return    Pointer to the created GATT server, or NULL on failure.
 */
lm_gatt_server_t *lm_gatt_server_create(lm_adapter_t *adapter);

/**
 * @brief     Destroy a GATT server.
 * @param[in] server Pointer to the GATT server to destroy.
 */
void lm_gatt_server_destroy(lm_gatt_server_t *server);

/**
 * @brief     Get the D-Bus object path of the GATT server.
 * @param[in] server Pointer to the GATT server.
 * @return    The D-Bus object path string.
 */
const gchar *lm_gatt_server_get_path(const lm_gatt_server_t *server);

/**
 * @brief     Add a service to the GATT server.
 * @param[in] server       Pointer to the GATT server.
 * @param[in] service_uuid Service UUID string.
 * @return    #LM_STATUS_SUCCESS if the service is added successfully.
 */
lm_status_t lm_gatt_server_add_service(lm_gatt_server_t *server, const gchar *service_uuid);

/**
 * @brief     Add a characteristic to a service.
 * @param[in] server       Pointer to the GATT server.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @param[in] prop         Characteristic properties (#lm_gatt_property_t).
 * @return    #LM_STATUS_SUCCESS if the characteristic is added successfully.
 */
lm_status_t lm_gatt_server_add_char(lm_gatt_server_t *server, const gchar *service_uuid,
                                        const gchar *char_uuid, lm_gatt_property_t prop);

/**
 * @brief     Add a descriptor to a characteristic.
 * @param[in] server       Pointer to the GATT server.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @param[in] desc_uuid    Descriptor UUID string.
 * @param[in] prop         Descriptor properties (#lm_gatt_property_t).
 * @return    #LM_STATUS_SUCCESS if the descriptor is added successfully.
 */
lm_status_t lm_gatt_server_add_desc(lm_gatt_server_t *server, const gchar *service_uuid,
                    const gchar *char_uuid, const gchar *desc_uuid, lm_gatt_property_t prop);

/**
 * @brief     Set a characteristic value.
 * @param[in] server       Pointer to the GATT server.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @param[in] byte_array   Value data.
 * @return    #LM_STATUS_SUCCESS if the value is set successfully.
 */
lm_status_t lm_gatt_server_set_char_value(const lm_gatt_server_t *server, const gchar *service_uuid,
                                    const gchar *char_uuid, GByteArray *byte_array);

/**
 * @brief     Get a characteristic value.
 * @param[in] server       Pointer to the GATT server.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @return    Pointer to the characteristic value, or NULL if not found.
 */
GByteArray *lm_gatt_server_get_char_value(const lm_gatt_server_t *server, const gchar *service_uuid,
                                            const gchar *char_uuid);

/**
 * @brief     Set a descriptor value.
 * @param[in] server       Pointer to the GATT server.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @param[in] desc_uuid    Descriptor UUID string.
 * @param[in] byte_array   Value data.
 * @return    #LM_STATUS_SUCCESS if the value is set successfully.
 */
lm_status_t lm_gatt_server_set_desc_value(const lm_gatt_server_t *server, const gchar *service_uuid,
                    const gchar *char_uuid, const gchar *desc_uuid, GByteArray *byte_array);

/**
 * @brief     Send a notification for a characteristic.
 * @param[in] server       Pointer to the GATT server.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @param[in] byte_array   Notification data.
 * @return    #LM_STATUS_SUCCESS if the notification is sent successfully.
 */
lm_status_t lm_gatt_server_send_notify(const lm_gatt_server_t *server, const gchar *service_uuid,
                                const gchar *char_uuid, const GByteArray *byte_array);

/**
 * @brief     Check if notifications are enabled for a characteristic.
 * @param[in] server       Pointer to the GATT server.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @return    TRUE if notifications are enabled, FALSE otherwise.
 */
gboolean lm_gatt_server_is_notify_enabled(const lm_gatt_server_t *server, const gchar *service_uuid,
                    const gchar *char_uuid);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_GATT_SRV_H__
