/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

#include "lm_adapter.h"
#include "bluez_dbus.h"
#include "lm_log.h"
#include "lm.h"
#include "lm_utils.h"
#include "lm_gatt_svc_priv.h"
#include "lm_gatt_char_priv.h"
#include "lm_gatt_desc_priv.h"
#include "lm_gatt_client.h"
#include "bluez_iface.h"
#include <glib.h>
#include <gio/gio.h>

#define TAG "lm_gatt_desc"

struct lm_gatt_desc {
    lm_device_t *device; // Borrowed
    lm_gatt_char_t *gatt_char; // Borrowed
    GDBusConnection *connection; // Borrowed
    const gchar *path; // Owned
    const gchar *char_path; // Owned
    const gchar *uuid; // Owned
    GList *flags; // Owned
};

typedef struct lm_desc_write_data {
    GVariant *value;
    lm_gatt_desc_t *desc;
} write_desc_data;

lm_gatt_desc_t *lm_gatt_desc_create(lm_device_t *device, const gchar *path) {
    g_assert(device != NULL);
    g_assert(path != NULL);
    g_assert(strlen(path) > 0);

    lm_gatt_desc_t *desc = g_new0(lm_gatt_desc_t, 1);
    desc->device = device;
    desc->connection = lm_device_get_dbus_conn(device);
    desc->path = g_strdup(path);
    return desc;
}

void lm_gatt_desc_destroy(lm_gatt_desc_t *desc) {
    g_assert(desc != NULL);

    if (desc->flags != NULL) {
        g_list_free_full(desc->flags, g_free);
        desc->flags = NULL;
    }

    g_free((gchar *) desc->uuid);
    desc->uuid = NULL;
    g_free((gchar *) desc->path);
    desc->path = NULL;
    g_free((gchar *) desc->char_path);
    desc->char_path = NULL;

    desc->gatt_char = NULL;
    desc->device = NULL;
    desc->connection = NULL;
    g_free(desc);
}

const gchar *lm_gatt_desc_to_string(const lm_gatt_desc_t *desc) {
    g_assert(desc != NULL);

    GString *flags = g_string_new("[");
    if (g_list_length(desc->flags) > 0) {
        for (GList *iterator = desc->flags; iterator; iterator = iterator->next) {
            g_string_append_printf(flags, "%s, ", (gchar *) iterator->data);
        }
        g_string_truncate(flags, flags->len - 2);
    }
    g_string_append(flags, "]");

    gchar *result = g_strdup_printf(
            "lm_gatt_desc_t{uuid='%s', flags='%s', properties=%d, char_uuid='%s'}",
            desc->uuid,
            flags->str,
            0,
            lm_gatt_char_get_uuid(desc->gatt_char));

    g_string_free(flags, TRUE);
    return result;
}

void lm_gatt_desc_set_uuid(lm_gatt_desc_t *desc, const gchar *uuid) {
    g_assert(desc != NULL);
    g_assert(lm_utils_is_valid_uuid(uuid));

    g_free((gchar *) desc->uuid);
    desc->uuid = g_strdup(uuid);
}

void lm_gatt_desc_set_char_path(lm_gatt_desc_t *desc, const gchar *path) {
    g_assert(desc != NULL);
    g_assert(path != NULL);

    g_free((gchar *) desc->char_path);
    desc->char_path = g_strdup(path);
}

const gchar *lm_gatt_desc_get_char_path(const lm_gatt_desc_t *desc) {
    g_assert(desc != NULL);
    return desc->char_path;
}

const gchar *lm_gatt_desc_get_uuid(const lm_gatt_desc_t *desc) {
    g_assert(desc != NULL);
    return desc->uuid;
}

void lm_gatt_desc_set_char(lm_gatt_desc_t *desc, lm_gatt_char_t *gatt_char) {
    g_assert(desc != NULL);
    g_assert(gatt_char != NULL);

    desc->gatt_char = gatt_char;
}

void lm_gatt_desc_set_flags(lm_gatt_desc_t *desc, GList *flags) {
    g_assert(desc != NULL);
    g_assert(flags != NULL);

    if (desc->flags != NULL) {
        g_list_free_full(desc->flags, g_free);
    }
    desc->flags = flags;
}

static void lm_gatt_desc_read_cb(__attribute__((unused)) GObject *source_object,
                        GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    GByteArray *byte_array = NULL;
    GVariant *innerArray = NULL;
    lm_status_t status = LM_STATUS_FAIL;
    lm_gatt_desc_t *desc = (lm_gatt_desc_t *) user_data;
    g_assert(desc != NULL);

    GVariant *value = g_dbus_connection_call_finish(desc->connection, res, &error);
    if (value != NULL) {
        g_assert(g_str_equal(g_variant_get_type_string(value), "(ay)"));
        innerArray = g_variant_get_child_value(value, 0);
        byte_array = lm_utils_g_variant_get_byte_array(innerArray);
    }

    lm_gatt_client_desc_read_cnf_t cnf = {
        .device = desc->device,
        .service_uuid = lm_gatt_svc_get_uuid(lm_gatt_char_get_service(desc->gatt_char)),
        .char_uuid = lm_gatt_char_get_uuid(desc->gatt_char),
        .desc_uuid = desc->uuid,
        .byte_array = byte_array
    };

    if (error) {
        lm_log_debug(TAG, "failed to call '%s' (error %d: %s)", GATT_DESC_METHOD_READ_VALUE, error->code,
                  error->message);
        g_clear_error(&error);
    } else {
        status = LM_STATUS_SUCCESS;
    }

    lm_app_event_callback(LM_GATT_CLIENT_DESC_READ_CNF, status, &cnf);

    if (byte_array != NULL) {
        g_byte_array_free(byte_array, FALSE);
    }

    if (innerArray != NULL) {
        g_variant_unref(innerArray);
    }

    if (value != NULL) {
        g_variant_unref(value);
    }
}

lm_status_t lm_gatt_desc_read(lm_gatt_desc_t *desc) {
    g_assert(desc != NULL);

    lm_log_debug(TAG, "reading <%s>", desc->uuid);

    guint16 offset = 0;
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(builder, "{sv}", "offset", g_variant_new_uint16(offset));
    GVariant *options = g_variant_builder_end(builder);
    g_variant_builder_unref(builder);

    g_dbus_connection_call(desc->connection,
                           BLUEZ_DBUS,
                           desc->path,
                           INTERFACE_GATT_DESC,
                           GATT_DESC_METHOD_READ_VALUE,
                           g_variant_new("(@a{sv})", options),
                           G_VARIANT_TYPE("(ay)"),
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_gatt_desc_read_cb,
                           desc);

    return LM_STATUS_SUCCESS;
}

static void lm_gatt_desc_write_cb(__attribute__((unused)) GObject *source_object,
                    GAsyncResult *res, gpointer user_data) {
    write_desc_data *write_data = (write_desc_data *) user_data;
    lm_gatt_desc_t *desc = write_data->desc;
    g_assert(desc != NULL);

    GByteArray *byte_array = NULL;
    GError *error = NULL;
    lm_status_t status = LM_STATUS_FAIL;
    GVariant *value = g_dbus_connection_call_finish(desc->connection, res, &error);

    if (write_data->value != NULL) {
        byte_array = lm_utils_g_variant_get_byte_array(write_data->value);
    }

    lm_gatt_client_desc_wrt_cnf_t cnf = {
        .device = desc->device,
        .service_uuid = lm_gatt_svc_get_uuid(lm_gatt_char_get_service(desc->gatt_char)),
        .char_uuid = lm_gatt_char_get_uuid(desc->gatt_char),
        .desc_uuid = desc->uuid,
        .byte_array = byte_array
    };

    if (error) {
        lm_log_debug(TAG, "failed to call '%s' (error %d: %s)", GATT_DESC_METHOD_WRITE_VALUE, error->code,
                  error->message);
        g_clear_error(&error);
    } else {
        status = LM_STATUS_SUCCESS;
    }

    lm_app_event_callback(LM_GATT_CLIENT_DESC_WRT_CNF, status, &cnf);

    if (byte_array != NULL) {
        g_byte_array_free(byte_array, FALSE);
    }
    g_variant_unref(write_data->value);
    g_free(write_data);

    if (value != NULL) {
        g_variant_unref(value);
    }
}

lm_status_t lm_gatt_desc_write(lm_gatt_desc_t *desc, const GByteArray *byte_array) {
    g_assert(desc != NULL);
    g_assert(byte_array != NULL);
    g_assert(byte_array->len > 0);

    GString *byteArrayStr = lm_utils_g_byte_array_as_hex(byte_array);
    lm_log_debug(TAG, "writing <%s> to <%s>", byteArrayStr->str, desc->uuid);
    g_string_free(byteArrayStr, TRUE);

    GVariant *value = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, byte_array->data, byte_array->len, sizeof(guint8));

    write_desc_data *write_data = g_new0(write_desc_data, 1);
    write_data->value = g_variant_ref(value);
    write_data->desc = desc;

    guint16 offset = 0;
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(builder, "{sv}", "offset", g_variant_new_uint16(offset));
    GVariant *options = g_variant_builder_end(builder);
    g_variant_builder_unref(builder);

    g_dbus_connection_call(desc->connection,
                           BLUEZ_DBUS,
                           desc->path,
                           INTERFACE_GATT_DESC,
                           GATT_DESC_METHOD_WRITE_VALUE,
                           g_variant_new("(@ay@a{sv})", value, options),
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_gatt_desc_write_cb,
                           write_data);

    return LM_STATUS_SUCCESS;
}

lm_device_t *lm_gatt_desc_get_device(const lm_gatt_desc_t *desc) {
    g_assert(desc != NULL);
    return desc->device;
}

lm_gatt_char_t *lm_gatt_desc_get_char(const lm_gatt_desc_t *desc) {
    g_assert(desc != NULL);
    return desc->gatt_char;
}
