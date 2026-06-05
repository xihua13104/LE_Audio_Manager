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
#include "lm_gatt_server.h"
#include "lm_gatt_svc_priv.h"
#include "lm_gatt_char_priv.h"
#include "lm_gatt_desc_priv.h"
#include "lm_gatt_client.h"
#include "bluez_iface.h"
#include <glib.h>
#include <gio/gio.h>

#define TAG "lm_gatt_char"

struct lm_gatt_char {
    lm_device_t *device; // Borrowed
    lm_gatt_svc_t *service; // Borrowed
    GDBusConnection *connection; // Borrowed
    const gchar *path; // Owned
    const gchar *uuid; // Owned
    const gchar *service_path; // Owned
    gboolean notifying;
    GList *flags; // Owned
    guint properties;
    GList *descs; // Owned
    guint mtu;

    guint gatt_char_prop_changed;
};

typedef struct lm_write_data {
    GVariant *value;
    lm_gatt_char_t *gatt_char;
} lm_write_data_t;

lm_gatt_char_t *lm_gatt_char_create(lm_device_t *device, const gchar *path) {
    g_assert(device != NULL);
    g_assert(path != NULL);
    g_assert(strlen(path) > 0);

    lm_gatt_char_t *gatt_char = g_new0(lm_gatt_char_t, 1);
    gatt_char->device = device;
    gatt_char->connection = lm_device_get_dbus_conn(device);
    gatt_char->path = g_strdup(path);
    gatt_char->mtu = 23;
    return gatt_char;
}

void lm_gatt_char_destroy(lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);

    if (gatt_char->gatt_char_prop_changed != 0) {
        g_dbus_connection_signal_unsubscribe(gatt_char->connection, gatt_char->gatt_char_prop_changed);
        gatt_char->gatt_char_prop_changed = 0;
    }

    if (gatt_char->flags != NULL) {
        g_list_free_full(gatt_char->flags, g_free);
        gatt_char->flags = NULL;
    }

    if (gatt_char->descs != NULL) {
        g_list_free(gatt_char->descs);
        gatt_char->descs = NULL;
    }

    g_free((gchar *) gatt_char->uuid);
    gatt_char->uuid = NULL;

    g_free((gchar *) gatt_char->path);
    gatt_char->path = NULL;

    g_free((gchar *) gatt_char->service_path);
    gatt_char->service_path = NULL;

    gatt_char->device = NULL;
    gatt_char->connection = NULL;
    gatt_char->service = NULL;
    g_free(gatt_char);
}

gchar *lm_gatt_char_to_string(const lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);

    GString *flags = g_string_new("[");
    if (g_list_length(gatt_char->flags) > 0) {
        for (GList *iterator = gatt_char->flags; iterator; iterator = iterator->next) {
            g_string_append_printf(flags, "%s, ", (gchar *) iterator->data);
        }
        g_string_truncate(flags, flags->len - 2);
    }
    g_string_append(flags, "]");

    gchar *result = g_strdup_printf(
            "lm_gatt_char_t{uuid='%s', flags='%s', properties=%d, service_uuid='%s, mtu=%d'}",
            gatt_char->uuid,
            flags->str,
            gatt_char->properties,
            lm_gatt_svc_get_uuid(gatt_char->service),
            gatt_char->mtu);

    g_string_free(flags, TRUE);
    return result;
}

static void lm_gatt_char_read_cb(__attribute__((unused)) GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data) {
    GError *error = NULL;
    GByteArray *byte_array = NULL;
    GVariant *innerArray = NULL;
    lm_status_t status = LM_STATUS_FAIL;
    lm_gatt_char_t *gatt_char = (lm_gatt_char_t *) user_data;
    g_assert(gatt_char != NULL);

    GVariant *value = g_dbus_connection_call_finish(gatt_char->connection, res, &error);
    if (value != NULL) {
        g_assert(g_str_equal(g_variant_get_type_string(value), "(ay)"));
        innerArray = g_variant_get_child_value(value, 0);
        byte_array = lm_utils_g_variant_get_byte_array(innerArray);
    }

    lm_gatt_client_char_read_cnf_t cnf = {
        .device = gatt_char->device,
        .service_uuid = lm_gatt_svc_get_uuid(gatt_char->service),
        .char_uuid = gatt_char->uuid,
        .byte_array = byte_array
    };

    if (error) {
        lm_log_debug(TAG, "failed to call '%s' (error %d: %s)", GATT_CHAR_METHOD_READ_VALUE, error->code,
                  error->message);
        g_clear_error(&error);
    } else {
        status = LM_STATUS_SUCCESS;
    }

    lm_app_event_callback(LM_GATT_CLIENT_CHAR_READ_CNF, status, &cnf);

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

lm_status_t lm_gatt_char_read(lm_gatt_char_t *gatt_char)
{
    g_assert(gatt_char != NULL);

    if (!lm_gatt_char_supports_read(gatt_char)) {
        lm_log_error(TAG, "gatt char <%s> is not readable on device '%s'",
                                        gatt_char->uuid, lm_device_get_path(gatt_char->device));
        return LM_STATUS_FAIL;
    }

    lm_log_debug(TAG, "reading <%s>", gatt_char->uuid);

    guint16 offset = 0;
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(builder, "{sv}", "offset", g_variant_new_uint16(offset));
    GVariant *options = g_variant_builder_end(builder);
    g_variant_builder_unref(builder);

    g_dbus_connection_call(gatt_char->connection,
                           BLUEZ_DBUS,
                           gatt_char->path,
                           INTERFACE_GATT_CHAR,
                           GATT_CHAR_METHOD_READ_VALUE,
                           g_variant_new("(@a{sv})", options),
                           G_VARIANT_TYPE("(ay)"),
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_gatt_char_read_cb,
                           gatt_char);

    return LM_STATUS_SUCCESS;
}

static void lm_gatt_char_write_cb(__attribute__((unused)) GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data) {
    lm_write_data_t *write_data = (lm_write_data_t*) user_data;
    lm_gatt_char_t *gatt_char = write_data->gatt_char;
    lm_status_t status = LM_STATUS_FAIL;
    g_assert(gatt_char != NULL);

    GByteArray *byte_array = NULL;
    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(gatt_char->connection, res, &error);

    if (write_data->value != NULL) {
        byte_array = lm_utils_g_variant_get_byte_array(write_data->value);
    }

    lm_gatt_client_char_wrt_cnf_t cnf = {
        .device = gatt_char->device,
        .service_uuid = lm_gatt_svc_get_uuid(gatt_char->service),
        .char_uuid = gatt_char->uuid,
        .byte_array = byte_array
    };

    if (error) {
        lm_log_debug(TAG, "failed to call '%s' (error %d: %s)", GATT_CHAR_METHOD_WRITE_VALUE, error->code,
                  error->message);
        g_clear_error(&error);
    } else {
        status = LM_STATUS_SUCCESS;
    }

    lm_app_event_callback(LM_GATT_CLIENT_CHAR_WRT_CNF, status, &cnf);

    if (byte_array != NULL) {
        g_byte_array_free(byte_array, FALSE);
    }
    g_variant_unref(write_data->value);
    g_free(write_data);

    if (value != NULL) {
        g_variant_unref(value);
    }
}

lm_status_t lm_gatt_char_write(lm_gatt_char_t *gatt_char, const GByteArray *byte_array, lm_gatt_write_type_t write_type) {
    g_assert(gatt_char != NULL);
    g_assert(byte_array != NULL);
    g_assert(byte_array->len > 0);

    if (!lm_gatt_char_supports_write(gatt_char, write_type)) {
        lm_log_error(TAG, "gatt char <%s> is not writable on device '%s'",
                                        gatt_char->uuid, lm_device_get_path(gatt_char->device));
        return LM_STATUS_FAIL;
    }

    GString *byteArrayStr = lm_utils_g_byte_array_as_hex(byte_array);
    lm_log_debug(TAG, "writing <%s> to <%s>", byteArrayStr->str, gatt_char->uuid);
    g_string_free(byteArrayStr, TRUE);

    GVariant *value = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, byte_array->data, byte_array->len, sizeof(guint8));

    lm_write_data_t *write_data = g_new0(lm_write_data_t, 1);
    write_data->value = g_variant_ref(value);
    write_data->gatt_char = gatt_char;

    guint16 offset = 0;
    const gchar *writeTypeString = write_type == LM_GATT_WRITE_WITH_RESPONSE ? "request" : "command";
    GVariantBuilder *optionsBuilder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(optionsBuilder, "{sv}", "offset", g_variant_new_uint16(offset));
    g_variant_builder_add(optionsBuilder, "{sv}", "type", g_variant_new_string(writeTypeString));
    GVariant *options = g_variant_builder_end(optionsBuilder);
    g_variant_builder_unref(optionsBuilder);

    g_dbus_connection_call(gatt_char->connection,
                           BLUEZ_DBUS,
                           gatt_char->path,
                           INTERFACE_GATT_CHAR,
                           GATT_CHAR_METHOD_WRITE_VALUE,
                           g_variant_new("(@ay@a{sv})", value, options),
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_gatt_char_write_cb,
                           write_data);

    return LM_STATUS_SUCCESS;
}

static void lm_gatt_signal_characteristic_changed(__attribute__((unused)) GDBusConnection *conn,
                                                        __attribute__((unused)) const gchar *sender,
                                                        __attribute__((unused)) const gchar *path,
                                                        __attribute__((unused)) const gchar *interface,
                                                        __attribute__((unused)) const gchar *signal,
                                                        GVariant *parameters,
                                                        void *user_data) {

    lm_gatt_char_t *gatt_char = (lm_gatt_char_t *) user_data;
    g_assert(gatt_char != NULL);

    GVariantIter *properties_changed = NULL;
    GVariantIter *properties_invalidated = NULL;
    const gchar *iface = NULL;
    const gchar *property_name = NULL;
    GVariant *property_value = NULL;

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
    g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);
    while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
        if (g_str_equal(property_name, GATT_CHAR_PROPERTY_NOTIFYING)) {
            gatt_char->notifying = g_variant_get_boolean(property_value);
            lm_log_debug(TAG, "notifying %s <%s>", gatt_char->notifying ? "true" : "false", gatt_char->uuid);
            if (gatt_char->notifying) {
                lm_gatt_client_ntf_enable_cnf_t cnf = {
                    .device = gatt_char->device,
                    .service_uuid = lm_gatt_svc_get_uuid(gatt_char->service),
                    .char_uuid = gatt_char->uuid
                };
                lm_app_event_callback(LM_GATT_CLIENT_NTF_ENABLE_CNF, LM_STATUS_SUCCESS, &cnf);
            } else {
                lm_gatt_client_ntf_disable_cnf_t cnf = {
                    .device = gatt_char->device,
                    .service_uuid = lm_gatt_svc_get_uuid(gatt_char->service),
                    .char_uuid = gatt_char->uuid
                };
                lm_app_event_callback(LM_GATT_CLIENT_NTF_DISABLE_CNF, LM_STATUS_SUCCESS, &cnf);

                if (gatt_char->gatt_char_prop_changed != 0) {
                    g_dbus_connection_signal_unsubscribe(gatt_char->connection,
                                                         gatt_char->gatt_char_prop_changed);
                    gatt_char->gatt_char_prop_changed = 0;
                }
            }
        } else if (g_str_equal(property_name, GATT_CHAR_PROPERTY_VALUE)) {
            GByteArray *byte_array = lm_utils_g_variant_get_byte_array(property_value);
            GString *result = lm_utils_g_byte_array_as_hex(byte_array);
            lm_log_debug(TAG, "notification <%s> on <%s>", result->str, gatt_char->uuid);
            g_string_free(result, TRUE);

            lm_gatt_client_ntf_ind_t ind = {
                .device = gatt_char->device,
                .service_uuid = lm_gatt_svc_get_uuid(gatt_char->service),
                .char_uuid = gatt_char->uuid,
                .byte_array = byte_array
            };
            lm_app_event_callback(LM_GATT_CLIENT_NTF_IND, LM_STATUS_SUCCESS, &ind);
            g_byte_array_free(byte_array, FALSE);
        }
    }

    if (properties_changed != NULL) {
        g_variant_iter_free(properties_changed);
    }
    if (properties_invalidated != NULL) {
        g_variant_iter_free(properties_invalidated);
    }
}

static void lm_gatt_char_start_notify_cb(__attribute__((unused)) GObject *source_object,
                                               GAsyncResult *res,
                                               gpointer user_data) {

    lm_gatt_char_t *gatt_char = (lm_gatt_char_t *) user_data;
    g_assert(gatt_char != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(gatt_char->connection, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_debug(TAG, "failed to call '%s' (error %d: %s)", GATT_CHAR_METHOD_START_NOTIFY, error->code,
                  error->message);
        lm_gatt_client_ntf_enable_cnf_t cnf = {
            .device = gatt_char->device,
            .service_uuid = lm_gatt_svc_get_uuid(gatt_char->service),
            .char_uuid = gatt_char->uuid
        };
        lm_app_event_callback(LM_GATT_CLIENT_NTF_ENABLE_CNF, LM_STATUS_FAIL, &cnf);
        g_clear_error(&error);
    }
}

static void register_for_properties_changed_signal(lm_gatt_char_t *gatt_char) {
    if (gatt_char->gatt_char_prop_changed == 0) {
        gatt_char->gatt_char_prop_changed = g_dbus_connection_signal_subscribe(gatt_char->connection,
                                                                                         BLUEZ_DBUS,
                                                                                         "org.freedesktop.DBus.Properties",
                                                                                         "PropertiesChanged",
                                                                                         gatt_char->path,
                                                                                         INTERFACE_GATT_CHAR,
                                                                                         G_DBUS_SIGNAL_FLAGS_NONE,
                                                                                         lm_gatt_signal_characteristic_changed,
                                                                                         gatt_char,
                                                                                         NULL);
    }
}

lm_status_t lm_gatt_char_start_notify(lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);

    if (!lm_gatt_char_supports_notify(gatt_char)) {
        lm_log_error(TAG, "gatt char <%s> does not support notify/indicate on device '%s'",
                                        gatt_char->uuid, lm_device_get_path(gatt_char->device));
        return LM_STATUS_FAIL;
    }

    lm_log_debug(TAG, "start notify for <%s>", gatt_char->uuid);
    register_for_properties_changed_signal(gatt_char);

    g_dbus_connection_call(gatt_char->connection,
                           BLUEZ_DBUS,
                           gatt_char->path,
                           INTERFACE_GATT_CHAR,
                           GATT_CHAR_METHOD_START_NOTIFY,
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_gatt_char_start_notify_cb,
                           gatt_char);

    return LM_STATUS_SUCCESS;
}

static void lm_gatt_char_stop_notify_cb(__attribute__((unused)) GObject *source_object,
                    GAsyncResult *res, gpointer user_data) {
    lm_gatt_char_t *gatt_char = (lm_gatt_char_t *) user_data;
    g_assert(gatt_char != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(gatt_char->connection, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_debug(TAG, "failed to call '%s' (error %d: %s)", GATT_CHAR_METHOD_STOP_NOTIFY, error->code,
                  error->message);
        lm_gatt_client_ntf_disable_cnf_t cnf = {
            .device = gatt_char->device,
            .service_uuid = lm_gatt_svc_get_uuid(gatt_char->service),
            .char_uuid = gatt_char->uuid
        };
        lm_app_event_callback(LM_GATT_CLIENT_NTF_DISABLE_CNF, LM_STATUS_FAIL, &cnf);
        g_clear_error(&error);
    }
}

lm_status_t lm_gatt_char_stop_notify(lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);

    if (!lm_gatt_char_supports_notify(gatt_char)) {
        lm_log_error(TAG, "gatt char <%s> does not support notify/indicate on device '%s'",
                                        gatt_char->uuid, lm_device_get_path(gatt_char->device));
        return LM_STATUS_FAIL;
    }

    g_dbus_connection_call(gatt_char->connection,
                           BLUEZ_DBUS,
                           gatt_char->path,
                           INTERFACE_GATT_CHAR,
                           GATT_CHAR_METHOD_STOP_NOTIFY,
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_gatt_char_stop_notify_cb,
                           gatt_char);

    return LM_STATUS_SUCCESS;
}

void lm_gatt_char_set_notifying(lm_gatt_char_t *gatt_char, gboolean notifying) {
    g_assert(gatt_char != NULL);
    gatt_char->notifying = notifying;

    if (gatt_char->notifying) {
        register_for_properties_changed_signal(gatt_char);
    }
}

const gchar *lm_gatt_char_get_uuid(const lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);
    return gatt_char->uuid;
}

void lm_gatt_char_set_uuid(lm_gatt_char_t *gatt_char, const gchar *uuid) {
    g_assert(gatt_char != NULL);
    g_assert(uuid != NULL);

    g_free((gchar *) gatt_char->uuid);
    gatt_char->uuid = g_strdup(uuid);
}

void lm_gatt_char_set_mtu(lm_gatt_char_t *gatt_char, guint mtu) {
    g_assert(gatt_char != NULL);
    gatt_char->mtu = mtu;
}

lm_device_t *lm_gatt_char_get_device(const lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);
    return gatt_char->device;
}

lm_gatt_svc_t *lm_gatt_char_get_service(const lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);
    return gatt_char->service;
}

void lm_gatt_char_set_service(lm_gatt_char_t *gatt_char, lm_gatt_svc_t *service) {
    g_assert(gatt_char != NULL);
    g_assert(service != NULL);
    gatt_char->service = service;
}

const gchar *lm_gatt_char_get_service_path(const lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);
    return gatt_char->service_path;
}

void lm_gatt_char_set_service_path(lm_gatt_char_t *gatt_char, const gchar *service_path) {
    g_assert(gatt_char != NULL);
    g_assert(service_path != NULL);

    if (gatt_char->service_path != NULL) {
        g_free((gchar *) gatt_char->service_path);
    }
    gatt_char->service_path = g_strdup(service_path);
}

GList *lm_gatt_char_get_flags(const lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);
    return gatt_char->flags;
}

static guint lm_gatt_char_flags_to_int(GList *flags) {
    guint result = 0;
    if (g_list_length(flags) > 0) {
        for (GList *iterator = flags; iterator; iterator = iterator->next) {
            gchar *property = (gchar *) iterator->data;
            if (g_str_equal(property, "broadcast")) {
                result += LM_GATT_PROP_BROADCAST;
            } else if (g_str_equal(property, "read")) {
                result += LM_GATT_PROP_READ;
            } else if (g_str_equal(property, "write-without-response")) {
                result += LM_GATT_PROP_WRITE_WITHOUT_RESP;
            } else if (g_str_equal(property, "write")) {
                result += LM_GATT_PROP_WRITE;
            } else if (g_str_equal(property, "notify")) {
                result += LM_GATT_PROP_NOTIFY;
            } else if (g_str_equal(property, "indicate")) {
                result += LM_GATT_PROP_INDICATE;
            } else if (g_str_equal(property, "authenticated-signed-writes")) {
                result += LM_GATT_PROP_AUTH;
            } else if (g_str_equal(property, "encrypt-read")) {
                result += LM_GATT_PROP_ENCRYPT_READ;
            } else if (g_str_equal(property, "encrypt-write")) {
                result += LM_GATT_PROP_ENCRYPT_WRITE;
            } else if (g_str_equal(property, "encrypt-notify")) {
                result += LM_GATT_PROP_ENCRYPT_NOTIFY;
            } else if (g_str_equal(property, "encrypt-indicate")) {
                result += LM_GATT_PROP_ENCRYPT_INDICATE;
            }
        }
    }
    return result;
}

void lm_gatt_char_set_flags(lm_gatt_char_t *gatt_char, GList *flags) {
    g_assert(gatt_char != NULL);
    g_assert(flags != NULL);

    if (gatt_char->flags != NULL) {
        g_list_free_full(gatt_char->flags, g_free);
    }
    gatt_char->flags = flags;
    gatt_char->properties = lm_gatt_char_flags_to_int(flags);
}

guint lm_gatt_char_get_properties(const lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);
    return gatt_char->properties;
}

gboolean lm_gatt_char_is_notifying(const lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);
    return gatt_char->notifying;
}

gboolean lm_gatt_char_supports_write(const lm_gatt_char_t *gatt_char, lm_gatt_write_type_t write_type) {
    if (write_type == LM_GATT_WRITE_WITH_RESPONSE) {
        return (gatt_char->properties & LM_GATT_PROP_WRITE) > 0;
    } else {
        return (gatt_char->properties & LM_GATT_PROP_WRITE_WITHOUT_RESP) > 0;
    }
}

gboolean lm_gatt_char_supports_read(const lm_gatt_char_t *gatt_char) {
    return (gatt_char->properties & LM_GATT_PROP_READ) > 0;
}

gboolean lm_gatt_char_supports_notify(const lm_gatt_char_t *gatt_char) {
    return ((gatt_char->properties & LM_GATT_PROP_INDICATE) > 0 ||
            (gatt_char->properties & LM_GATT_PROP_NOTIFY) > 0);
}

void lm_gatt_char_add_desc(lm_gatt_char_t *gatt_char, lm_gatt_desc_t *desc) {
    g_assert(gatt_char != NULL);
    g_assert(desc != NULL);

    gatt_char->descs = g_list_append(gatt_char->descs, desc);
}

lm_gatt_desc_t *lm_gatt_char_get_desc(const lm_gatt_char_t *gatt_char, const gchar* desc_uuid) {
    g_assert(gatt_char != NULL);
    g_assert(lm_utils_is_valid_uuid(desc_uuid));

    if (gatt_char->descs != NULL) {
        for (GList *iterator = gatt_char->descs; iterator; iterator = iterator->next) {
            lm_gatt_desc_t *desc = (lm_gatt_desc_t *) iterator->data;
            if (g_str_equal(desc_uuid, lm_gatt_desc_get_uuid(desc))) {
                return desc;
            }
        }
    }
    return NULL;
}

GList *lm_gatt_char_get_descs(const lm_gatt_char_t *gatt_char) {
    g_assert(gatt_char != NULL);
    return gatt_char->descs;
}

