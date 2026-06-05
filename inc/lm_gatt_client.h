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
 * @addtogroup LEAManagerGattClient GATT Client
 * @{
 * This section defines the APIs for GATT client functionality.
 * The GATT client module provides functionality to discover services,
 * read/write characteristics, and subscribe to notifications on remote GATT servers.
 */

#ifndef __LM_GATT_CLI_H__
#define __LM_GATT_CLI_H__

#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_gatt.h"

/**
 * @defgroup LEAManagerGattClient_define Define
 * @{
 */

/**
 * @brief Characteristic read confirmation event.
 */
typedef struct {
    lm_device_t *device;         /**< Pointer to the device. */
    const gchar *service_uuid;  /**< Service UUID. */
    const gchar *char_uuid;      /**< Characteristic UUID. */
    GByteArray *byte_array;     /**< Read data. */
} lm_gatt_client_char_read_cnf_t;
#define LM_GATT_CLIENT_CHAR_READ_CNF            (LM_MODULE_GATT_CLI | 0x0001)

/**
 * @brief Characteristic write confirmation event.
 */
typedef struct {
    lm_device_t *device;         /**< Pointer to the device. */
    const gchar *service_uuid;  /**< Service UUID. */
    const gchar *char_uuid;      /**< Characteristic UUID. */
    GByteArray *byte_array;     /**< Written data. */
} lm_gatt_client_char_wrt_cnf_t;
#define LM_GATT_CLIENT_CHAR_WRT_CNF             (LM_MODULE_GATT_CLI | 0x0002)

/**
 * @brief Descriptor read confirmation event.
 */
typedef struct {
    lm_device_t *device;         /**< Pointer to the device. */
    const gchar *service_uuid;  /**< Service UUID. */
    const gchar *char_uuid;      /**< Characteristic UUID. */
    const gchar *desc_uuid;      /**< Descriptor UUID. */
    GByteArray *byte_array;     /**< Read data. */
} lm_gatt_client_desc_read_cnf_t;
#define LM_GATT_CLIENT_DESC_READ_CNF            (LM_MODULE_GATT_CLI | 0x0003)

/**
 * @brief Descriptor write confirmation event.
 */
typedef struct {
    lm_device_t *device;         /**< Pointer to the device. */
    const gchar *service_uuid;  /**< Service UUID. */
    const gchar *char_uuid;      /**< Characteristic UUID. */
    const gchar *desc_uuid;      /**< Descriptor UUID. */
    GByteArray *byte_array;     /**< Written data. */
} lm_gatt_client_desc_wrt_cnf_t;
#define LM_GATT_CLIENT_DESC_WRT_CNF             (LM_MODULE_GATT_CLI | 0x0004)

/**
 * @brief Notification enable confirmation event.
 */
typedef struct {
    lm_device_t *device;         /**< Pointer to the device. */
    const gchar *service_uuid;  /**< Service UUID. */
    const gchar *char_uuid;      /**< Characteristic UUID. */
} lm_gatt_client_ntf_enable_cnf_t;
#define LM_GATT_CLIENT_NTF_ENABLE_CNF           (LM_MODULE_GATT_CLI | 0x0005)

/**
 * @brief Notification disable confirmation event.
 */
typedef struct {
    lm_device_t *device;         /**< Pointer to the device. */
    const gchar *service_uuid;  /**< Service UUID. */
    const gchar *char_uuid;      /**< Characteristic UUID. */
} lm_gatt_client_ntf_disable_cnf_t;
#define LM_GATT_CLIENT_NTF_DISABLE_CNF          (LM_MODULE_GATT_CLI | 0x0006)

/**
 * @brief Notification/indication received event.
 */
typedef struct {
    lm_device_t *device;         /**< Pointer to the device. */
    const gchar *service_uuid;  /**< Service UUID. */
    const gchar *char_uuid;      /**< Characteristic UUID. */
    GByteArray *byte_array;     /**< Notification data. */
} lm_gatt_client_ntf_ind_t;
#define LM_GATT_CLIENT_NTF_IND                  (LM_MODULE_GATT_CLI | 0x0007)

/**
 * @brief Services resolved indication event.
 */
typedef struct {
    lm_device_t *device; /**< Pointer to the device. */
} lm_gatt_client_services_resolved_ind_t;
#define LM_GATT_CLIENT_SERVICES_RESOLVED_IND    (LM_MODULE_GATT_CLI | 0x0008)

/**
 * @}
 */

/**
 * @defgroup LEAManagerGattClient_function Function
 * @{
 */

/**
 * @brief     Read a characteristic value.
 * @param[in] device       Pointer to the device.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @return    #LM_STATUS_SUCCESS if the read request is sent successfully.
 */
lm_status_t lm_gatt_client_read_char(lm_device_t *device,
                                        const gchar *service_uuid,
                                        const gchar *char_uuid);

/**
 * @brief     Write a characteristic value.
 * @param[in] device       Pointer to the device.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @param[in] byte_array   Data to write.
 * @param[in] write_type   Write type (#lm_gatt_write_type_t).
 * @return    #LM_STATUS_SUCCESS if the write request is sent successfully.
 */
lm_status_t lm_gatt_client_write_char(lm_device_t *device,
                                        const gchar *service_uuid,
                                        const gchar *char_uuid,
                                        const GByteArray *byte_array,
                                        lm_gatt_write_type_t write_type);

/**
 * @brief     Read a descriptor value.
 * @param[in] device       Pointer to the device.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @param[in] desc_uuid    Descriptor UUID string.
 * @return    #LM_STATUS_SUCCESS if the read request is sent successfully.
 */
lm_status_t lm_gatt_client_read_desc(lm_device_t *device,
                                        const gchar *service_uuid,
                                        const gchar *char_uuid,
                                        const gchar *desc_uuid);

/**
 * @brief     Write a descriptor value.
 * @param[in] device       Pointer to the device.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @param[in] desc_uuid    Descriptor UUID string.
 * @param[in] byte_array   Data to write.
 * @return    #LM_STATUS_SUCCESS if the write request is sent successfully.
 */
lm_status_t lm_gatt_client_write_desc(lm_device_t *device,
                                        const gchar *service_uuid,
                                        const gchar *char_uuid,
                                        const gchar *desc_uuid,
                                        const GByteArray *byte_array);

/**
 * @brief     Enable notifications for a characteristic.
 * @param[in] device       Pointer to the device.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @return    #LM_STATUS_SUCCESS if the enable request is sent successfully.
 */
lm_status_t lm_gatt_client_enable_notify(lm_device_t *device,
                                                const gchar *service_uuid,
                                                const gchar *char_uuid);

/**
 * @brief     Disable notifications for a characteristic.
 * @param[in] device       Pointer to the device.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @return    #LM_STATUS_SUCCESS if the disable request is sent successfully.
 */
lm_status_t lm_gatt_client_disable_notify(lm_device_t *device,
                                                const gchar *service_uuid,
                                                const gchar *char_uuid);

/**
 * @brief     Check if notifications are enabled for a characteristic.
 * @param[in] device       Pointer to the device.
 * @param[in] service_uuid Service UUID string.
 * @param[in] char_uuid    Characteristic UUID string.
 * @return    TRUE if notifications are enabled, FALSE otherwise.
 */
gboolean lm_gatt_client_is_notify_enabled(lm_device_t *device,
                                            const gchar *service_uuid,
                                            const gchar *char_uuid);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_GATT_CLI_H__
