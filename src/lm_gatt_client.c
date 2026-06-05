/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

#include "lm_device.h"
#include "lm_device_priv.h"
#include "bluez_dbus.h"
#include "lm_adapter.h"
#include "lm_gatt_char_priv.h"
#include "lm_gatt_desc_priv.h"
#include "lm_gatt_svc_priv.h"
#include "lm_gatt_client.h"
#include "lm_log.h"
#include "lm_utils.h"
#include "lm_uuids.h"
#include "lm.h"
#include <glib.h>
#include <gio/gio.h>

#define TAG "lm_gatt_cli"

lm_status_t lm_gatt_client_read_char(lm_device_t *device,
                                        const gchar *service_uuid,
                                        const gchar *char_uuid)
{
    g_assert(device != NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail(g_uuid_string_is_valid(char_uuid), LM_STATUS_INVALID_ARGS);

    lm_gatt_char_t *gatt_char = lm_device_get_gatt_char(device, service_uuid, char_uuid);
    if (!gatt_char) {
        lm_log_error(TAG, "gatt char <%s> not found on device '%s'",
                                        char_uuid, lm_device_get_path(device));
        return LM_STATUS_FAIL;
    }

    return lm_gatt_char_read(gatt_char);
}

lm_status_t lm_gatt_client_write_char(lm_device_t *device,
                                        const gchar *service_uuid,
                                        const gchar *char_uuid,
                                        const GByteArray *byte_array,
                                        lm_gatt_write_type_t write_type)
{
    g_assert(device != NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail(g_uuid_string_is_valid(char_uuid), LM_STATUS_INVALID_ARGS);

    lm_gatt_char_t *gatt_char = lm_device_get_gatt_char(device, service_uuid, char_uuid);
    if (!gatt_char) {
        lm_log_error(TAG, "gatt char <%s> not found on device '%s'",
                                        char_uuid, lm_device_get_path(device));
        return LM_STATUS_FAIL;
    }

    return lm_gatt_char_write(gatt_char, byte_array, write_type);
}

lm_status_t lm_gatt_client_read_desc(lm_device_t *device,
                                        const gchar *service_uuid,
                                        const gchar *char_uuid,
                                        const gchar *desc_uuid)
{
    g_assert(device != NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail(g_uuid_string_is_valid(char_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail(g_uuid_string_is_valid(desc_uuid), LM_STATUS_INVALID_ARGS);

    lm_gatt_char_t *gatt_char = lm_device_get_gatt_char(device, service_uuid, char_uuid);
    if (!gatt_char) {
        lm_log_error(TAG, "gatt char <%s> not found on device '%s'",
                                        char_uuid, lm_device_get_path(device));
        return LM_STATUS_FAIL;
    }

    lm_gatt_desc_t *desc = lm_gatt_char_get_desc(gatt_char, desc_uuid);
    if (!desc) {
        lm_log_error(TAG, "gatt desc <%s> not found on device '%s'",
                                        desc_uuid, lm_device_get_path(device));
        return LM_STATUS_FAIL;
    }

    return lm_gatt_desc_read(desc);
}

lm_status_t lm_gatt_client_write_desc(lm_device_t *device,
                                        const gchar *service_uuid,
                                        const gchar *char_uuid,
                                        const gchar *desc_uuid,
                                        const GByteArray *byte_array)
{
    g_assert(device != NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail(g_uuid_string_is_valid(char_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail(g_uuid_string_is_valid(desc_uuid), LM_STATUS_INVALID_ARGS);

    lm_gatt_char_t *gatt_char = lm_device_get_gatt_char(device, service_uuid, char_uuid);
    if (!gatt_char) {
        lm_log_error(TAG, "gatt char <%s> not found on device '%s'",
                                        char_uuid, lm_device_get_path(device));
        return LM_STATUS_FAIL;
    }

    lm_gatt_desc_t *desc = lm_gatt_char_get_desc(gatt_char, desc_uuid);
    if (!desc) {
        lm_log_error(TAG, "gatt desc <%s> not found on device '%s'",
                                        desc_uuid, lm_device_get_path(device));
        return LM_STATUS_FAIL;
    }

    return lm_gatt_desc_write(desc, byte_array);
}

lm_status_t lm_gatt_client_enable_notify(lm_device_t *device,
                                                const gchar *service_uuid,
                                                const gchar *char_uuid)
{
    g_assert(device != NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail(g_uuid_string_is_valid(char_uuid), LM_STATUS_INVALID_ARGS);

    lm_gatt_char_t *gatt_char = lm_device_get_gatt_char(device, service_uuid, char_uuid);
    if (!gatt_char) {
        lm_log_error(TAG, "gatt char <%s> not found on device '%s'",
                                        char_uuid, lm_device_get_path(device));
        return LM_STATUS_FAIL;
    }

    return lm_gatt_char_start_notify(gatt_char);
}

lm_status_t lm_gatt_client_disable_notify(lm_device_t *device,
                                                const gchar *service_uuid,
                                                const gchar *char_uuid)
{
    g_assert(device != NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail(g_uuid_string_is_valid(char_uuid), LM_STATUS_INVALID_ARGS);

    lm_gatt_char_t *gatt_char = lm_device_get_gatt_char(device, service_uuid, char_uuid);
    if (!gatt_char) {
        lm_log_error(TAG, "gatt char <%s> not found on device '%s'",
                                        char_uuid, lm_device_get_path(device));
        return LM_STATUS_FAIL;
    }

    return lm_gatt_char_stop_notify(gatt_char);
}

gboolean lm_gatt_client_is_notify_enabled(lm_device_t *device,
                                            const gchar *service_uuid,
                                            const gchar *char_uuid)
{
    g_assert(device != NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(service_uuid), FALSE);
    g_return_val_if_fail(g_uuid_string_is_valid(char_uuid), FALSE);

    lm_gatt_char_t *gatt_char = lm_device_get_gatt_char(device, service_uuid, char_uuid);
    if (!gatt_char) {
        lm_log_error(TAG, "gatt char <%s> not found on device '%s'",
                                        char_uuid, lm_device_get_path(device));
        return FALSE;
    }

    return lm_gatt_char_is_notifying(gatt_char);
}