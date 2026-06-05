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
#include "bluez_iface.h"
#include <glib.h>
#include <gio/gio.h>

#define TAG "lm_gatt_srv"

struct lm_gatt_server {
    gchar *path;
    lm_adapter_t *adapter;
    guint registration_id;
    GDBusConnection *dbus_conn;
    GHashTable *services;
};

typedef struct lm_local_service {
    gchar *path;
    gchar *uuid;
    guint registration_id;
    GHashTable *characteristics;
    lm_gatt_server_t *gatt_server;
} lm_local_service_t;

typedef struct lm_local_char {
    gchar *path;
    gchar *uuid;
    gchar *service_path;
    gchar *service_uuid;
    guint registration_id;
    GByteArray *value;
    lm_gatt_property_t prop;
    GList *flags;
    gboolean notifying;
    GHashTable *descriptors;
    lm_gatt_server_t *gatt_server;
} lm_local_char_t;

typedef struct lm_local_desc {
    gchar *path;
    gchar *uuid;
    gchar *char_path;
    gchar *char_uuid;
    gchar *service_uuid;
    guint registration_id;
    GByteArray *value;
    lm_gatt_property_t prop;
    GList *flags;
    lm_gatt_server_t *gatt_server;
} lm_local_desc_t;

typedef struct {
    lm_status_t status;
    const gchar *bluez_err;
} lm_status_bluez_err_map_t;

static const lm_status_bluez_err_map_t err_map[] = {
    {LM_STATUS_GATT_SRV_FAILED, "org.bluez.Error.Failed"},
    {LM_STATUS_GATT_SRV_REJECTED, "org.bluez.Error.Rejected"},
    {LM_STATUS_GATT_SRV_INPROGRESS, "org.bluez.Error.InProgress"},
    {LM_STATUS_GATT_SRV_NOT_PERMITTED, "org.bluez.Error.NotPermitted"},
    {LM_STATUS_GATT_SRV_INVALID_VALUE_LEN, "org.bluez.Error.InvalidValueLength"},
    {LM_STATUS_GATT_SRV_NOT_AUTHORIZED, "org.bluez.Error.NotAuthorized"},
    {LM_STATUS_GATT_SRV_NOT_SUPPORTED, "org.bluez.Error.NotSupported"},
};

static const gchar *lm_gatt_status_to_bluez_error(lm_status_t status)
{
    for (guint i = 0; i < G_N_ELEMENTS(err_map); i++) {
        if (status == err_map[i].status)
            return err_map[i].bluez_err;
    }

    return NULL;
}

static void lm_local_desc_free(lm_local_desc_t *local_desc)
{
    g_assert(local_desc != NULL);

    lm_log_debug(TAG, "freeing descriptor %s", local_desc->path);

    if (local_desc->registration_id != 0) {
        gboolean result = g_dbus_connection_unregister_object(local_desc->gatt_server->dbus_conn,
                                                              local_desc->registration_id);
        if (!result) {
            lm_log_error(TAG, "error: could not unregister descriptor %s", local_desc->path);
        }
        local_desc->registration_id = 0;
    }

    if (local_desc->value != NULL) {
        g_byte_array_free(local_desc->value, TRUE);
        local_desc->value = NULL;
    }

    g_free(local_desc->path);
    local_desc->path = NULL;

    g_free(local_desc->char_path);
    local_desc->char_path = NULL;

    g_free(local_desc->uuid);
    local_desc->uuid = NULL;

    g_free(local_desc->char_uuid);
    local_desc->char_uuid = NULL;

    g_free(local_desc->service_uuid);
    local_desc->service_uuid = NULL;

    if (local_desc->flags != NULL) {
        g_list_free_full(local_desc->flags, g_free);
        local_desc->flags = NULL;
    }

    g_free(local_desc);
}

static void lm_local_char_free(lm_local_char_t *local_char)
{
    g_assert(local_char != NULL);

    lm_log_debug(TAG, "freeing char %s", local_char->path);

    if (local_char->descriptors != NULL) {
        g_hash_table_destroy(local_char->descriptors);
        local_char->descriptors = NULL;
    }

    if (local_char->registration_id != 0) {
        gboolean result = g_dbus_connection_unregister_object(local_char->gatt_server->dbus_conn,
                                                              local_char->registration_id);
        if (!result) {
            lm_log_error(TAG, "error: could not unregister service %s", local_char->path);
        }
        local_char->registration_id = 0;
    }

    if (local_char->value != NULL) {
        g_byte_array_free(local_char->value, TRUE);
        local_char->value = NULL;
    }

    g_free(local_char->path);
    local_char->path = NULL;

    g_free(local_char->uuid);
    local_char->uuid = NULL;

    g_free(local_char->service_uuid);
    local_char->service_uuid = NULL;

    g_free(local_char->service_path);
    local_char->service_path = NULL;

    if (local_char->flags != NULL) {
        g_list_free_full(local_char->flags, g_free);
        local_char->flags = NULL;
    }

    g_free(local_char);
}

static void lm_local_service_free(lm_local_service_t *local_service)
{
    g_assert(local_service != NULL);

    lm_log_debug(TAG, "freeing service %s", local_service->path);

    if (local_service->characteristics != NULL) {
        g_hash_table_destroy(local_service->characteristics);
        local_service->characteristics = NULL;
    }

    if (local_service->registration_id != 0) {
        gboolean result = g_dbus_connection_unregister_object(local_service->gatt_server->dbus_conn,
                                                              local_service->registration_id);
        if (!result) {
            lm_log_error(TAG, "error: could not unregister service %s", local_service->path);
        }
        local_service->registration_id = 0;
    }

    g_free(local_service->path);
    local_service->path = NULL;

    g_free(local_service->uuid);
    local_service->uuid = NULL;

    g_free(local_service);
}

typedef struct {
    gchar *device;
    guint16 mtu;
    guint16 offset;
    gchar *link_type;
} lm_read_options_t;

void read_options_free(lm_read_options_t *options) {
    if (options->link_type != NULL) g_free(options->link_type);
    if (options->device != NULL) g_free(options->device);
    g_free(options);
}

static lm_read_options_t *parse_read_options(GVariant *params) {
    g_assert(g_str_equal(g_variant_get_type_string(params), "(a{sv})"));
    lm_read_options_t *options = g_new0(lm_read_options_t, 1);

    GVariantIter *options_variant;
    g_variant_get(params, "(a{sv})", &options_variant);

    GVariant *property_value;
    gchar *property_name;
    while (g_variant_iter_loop(options_variant, "{&sv}", &property_name, &property_value)) {
        if (g_str_equal(property_name, "offset")) {
            options->offset = g_variant_get_uint16(property_value);
        } else if (g_str_equal(property_name, "mtu")) {
            options->mtu = g_variant_get_uint16(property_value);
        } else if (g_str_equal(property_name, "device")) {
            options->device = lm_utils_path_to_address(g_variant_get_string(property_value, NULL));
        } else if (g_str_equal(property_name, "link")) {
            options->link_type = g_strdup(g_variant_get_string(property_value, NULL));
        }
    }
    g_variant_iter_free(options_variant);

    lm_log_debug(TAG, "read with offset=%u, mtu=%u, link=%s, device=%s", (unsigned int) options->offset,
              (unsigned int) options->mtu, options->link_type, options->device);

    return options;
}

typedef struct {
    gchar *write_type;
    gchar *device;
    guint16 mtu;
    guint16 offset;
    gchar *link_type;
} lm_write_options_t;

void write_options_free(lm_write_options_t *options) {
    if (options->link_type != NULL) g_free(options->link_type);
    if (options->device != NULL) g_free(options->device);
    if (options->write_type != NULL) g_free(options->write_type);
    g_free(options);
}

static lm_write_options_t *parse_write_options(GVariant *options_variant) {
    g_assert(g_str_equal(g_variant_get_type_string(options_variant), "a{sv}"));
    lm_write_options_t *options = g_new0(lm_write_options_t, 1);

    GVariantIter iter;
    g_variant_iter_init(&iter, options_variant);
    GVariant *property_value;
    gchar *property_name;
    while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
        if (g_str_equal(property_name, "offset")) {
            options->offset = g_variant_get_uint16(property_value);
        } else if (g_str_equal(property_name, "type")) {
            options->write_type = g_strdup(g_variant_get_string(property_value, NULL));
        } else if (g_str_equal(property_name, "mtu")) {
            options->mtu = g_variant_get_uint16(property_value);
        } else if (g_str_equal(property_name, "device")) {
            options->device = lm_utils_path_to_address(g_variant_get_string(property_value, NULL));
        } else if (g_str_equal(property_name, "link")) {
            options->link_type = g_strdup(g_variant_get_string(property_value, NULL));
        }
    }

    lm_log_debug(TAG, "write with offset=%u, mtu=%u, link=%s, device=%s", (unsigned int) options->offset,
              (unsigned int) options->mtu, options->link_type, options->device);

    return options;
}

static void add_char_path(__attribute__((unused)) gpointer key,
                            gpointer value, gpointer userdata) {
    lm_local_char_t *local_char = (lm_local_char_t *) value;
    g_variant_builder_add((GVariantBuilder *) userdata, "o", local_char->path);
}

static GVariant *lm_local_service_get_char(const lm_local_service_t *local_service) {
    g_assert(local_service != NULL);

    GVariantBuilder *characteristics_builder = g_variant_builder_new(G_VARIANT_TYPE("ao"));
    g_hash_table_foreach(local_service->characteristics, add_char_path, characteristics_builder);
    GVariant *result = g_variant_builder_end(characteristics_builder);
    g_variant_builder_unref(characteristics_builder);
    return result;
}

static void add_desc_path(__attribute__((unused)) gpointer key,
                        gpointer value, gpointer userdata) {
    lm_local_desc_t *local_desc = (lm_local_desc_t *) value;
    g_variant_builder_add((GVariantBuilder *) userdata, "o", local_desc->path);
}

static GVariant *lm_local_char_get_descs(const lm_local_char_t *local_char) {
    g_assert(local_char != NULL);

    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("ao"));
    g_hash_table_foreach(local_char->descriptors, add_desc_path, builder);
    GVariant *result = g_variant_builder_end(builder);
    g_variant_builder_unref(builder);
    return result;
}

static GVariant *lm_local_charget_flags(const lm_local_char_t *local_char) {
    g_assert(local_char != NULL);

    GVariantBuilder *flags_builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
    for (GList *iterator = local_char->flags; iterator; iterator = iterator->next) {
        g_variant_builder_add(flags_builder, "s", (gchar *) iterator->data);
    }
    GVariant *result = g_variant_builder_end(flags_builder);
    g_variant_builder_unref(flags_builder);
    return result;
}

static GVariant *lm_local_desc_get_flags(const lm_local_desc_t *local_desc) {
    g_assert(local_desc != NULL);

    GVariantBuilder *flags_builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
    for (GList *iterator = local_desc->flags; iterator; iterator = iterator->next) {
        g_variant_builder_add(flags_builder, "s", (gchar *) iterator->data);
    }
    GVariant *result = g_variant_builder_end(flags_builder);
    g_variant_builder_unref(flags_builder);
    return result;
}

static void add_descriptors(GVariantBuilder *builder,
                            lm_local_char_t *local_char) {
    // NOTE that the CCCD is automatically added by Bluez so no need to add it.
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, local_char->descriptors);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        lm_local_desc_t *local_desc = (lm_local_desc_t *) value;
        lm_log_debug(TAG, "adding %s", local_desc->path);

        GVariantBuilder *descriptors_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sa{sv}}"));
        GVariantBuilder *desc_properties_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

        GByteArray *byte_array = local_desc->value;
        GVariant *byteArrayVariant = NULL;
        if (byte_array != NULL) {
            byteArrayVariant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, byte_array->data,
                                                         byte_array->len, sizeof(guint8));
            g_variant_builder_add(desc_properties_builder, "{sv}", "Value", byteArrayVariant);
        }
        g_variant_builder_add(desc_properties_builder, "{sv}", "UUID",
                              g_variant_new_string(local_desc->uuid));
        g_variant_builder_add(desc_properties_builder, "{sv}", "Characteristic",
                              g_variant_new("o", local_desc->char_path));
        g_variant_builder_add(desc_properties_builder, "{sv}", "Flags",
                              lm_local_desc_get_flags(local_desc));

        // Add the descriptor to result
        g_variant_builder_add(descriptors_builder, "{sa{sv}}", INTERFACE_GATT_DESC,
                              desc_properties_builder);
        g_variant_builder_unref(desc_properties_builder);
        g_variant_builder_add(builder, "{oa{sa{sv}}}", local_desc->path, descriptors_builder);
        g_variant_builder_unref(descriptors_builder);
    }
}

static void add_characteristics(GVariantBuilder *builder, lm_local_service_t *local_service) {
    // Build service characteristics
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, local_service->characteristics);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        lm_local_char_t *local_char = (lm_local_char_t *) value;
        lm_log_debug(TAG, "adding %s", local_char->path);

        GVariantBuilder *characteristic_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sa{sv}}"));
        GVariantBuilder *char_properties_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

        // Build char properties
        GByteArray *byte_array = local_char->value;
        GVariant *byteArrayVariant = NULL;
        if (byte_array != NULL) {
            byteArrayVariant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, byte_array->data,
                                                         byte_array->len, sizeof(guint8));
            g_variant_builder_add(char_properties_builder, "{sv}", "Value", byteArrayVariant);
        }
        g_variant_builder_add(char_properties_builder, "{sv}", "UUID",
                              g_variant_new_string(local_char->uuid));
        g_variant_builder_add(char_properties_builder, "{sv}", "Service",
                              g_variant_new("o", local_service->path));
        g_variant_builder_add(char_properties_builder, "{sv}", "Flags",
                              lm_local_charget_flags(local_char));
        g_variant_builder_add(char_properties_builder, "{sv}", "Notifying",
                              g_variant_new("b", local_char->notifying));
        g_variant_builder_add(char_properties_builder, "{sv}", "Descriptors",
                              lm_local_char_get_descs(local_char));

        // Add the char to result
        g_variant_builder_add(characteristic_builder, "{sa{sv}}", INTERFACE_GATT_CHAR,
                              char_properties_builder);
        g_variant_builder_unref(char_properties_builder);
        g_variant_builder_add(builder, "{oa{sa{sv}}}", local_char->path, characteristic_builder);
        g_variant_builder_unref(characteristic_builder);

        add_descriptors(builder, local_char);
    }
}

static void add_services(lm_gatt_server_t *gatt_server, GVariantBuilder *builder) {
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, gatt_server->services);
    while (g_hash_table_iter_next(&iter, (gpointer) &key, &value)) {
        lm_local_service_t *local_service = (lm_local_service_t *) value;
        lm_log_debug(TAG, "adding %s", local_service->path);
        GVariantBuilder *service_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sa{sv}}"));

        // Build service properties
        GVariantBuilder *service_properties_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(service_properties_builder, "{sv}", "UUID",
                              g_variant_new_string((gchar *) key));
        g_variant_builder_add(service_properties_builder, "{sv}", "Primary",
                              g_variant_new_boolean(TRUE));
        g_variant_builder_add(service_properties_builder, "{sv}", "Characteristics",
                              lm_local_service_get_char(local_service));

        // Add the service to result
        g_variant_builder_add(service_builder, "{sa{sv}}", INTERFACE_GATT_SERVICE, service_properties_builder);
        g_variant_builder_unref(service_properties_builder);
        g_variant_builder_add(builder, "{oa{sa{sv}}}", local_service->path, service_builder);
        g_variant_builder_unref(service_builder);
        add_characteristics(builder, local_service);
    }
}

static void lm_gatt_server_method_call(__attribute__((unused)) GDBusConnection *conn,
                                       __attribute__((unused)) const gchar *sender,
                                       __attribute__((unused)) const gchar *path,
                                       __attribute__((unused)) const gchar *interface,
                                       __attribute__((unused)) const gchar *method,
                                       __attribute__((unused)) GVariant *params,
                                       __attribute__((unused)) GDBusMethodInvocation *invocation,
                                                  void *userdata) {

    lm_gatt_server_t *gatt_server = (lm_gatt_server_t *) userdata;
    g_assert(gatt_server != NULL);

    if (g_str_equal(method, OBJECT_MANAGER_METHOD_GET_MANAGED_OBJECTS)) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        if (gatt_server->services != NULL && g_hash_table_size(gatt_server->services) > 0) {
            add_services(gatt_server, builder);
        }
        GVariant *result = g_variant_builder_end(builder);
        g_variant_builder_unref(builder);

        g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&result, 1));
    }
}

static const GDBusInterfaceVTable gatt_server_method_table = {
        .method_call = lm_gatt_server_method_call,
};

void lm_gatt_server_publish(lm_gatt_server_t *gatt_server, const lm_adapter_t *adapter) {
    g_assert(gatt_server != NULL);
    g_assert(adapter != NULL);

    GError *error = NULL;

    gatt_server->registration_id = g_dbus_connection_register_object(gatt_server->dbus_conn,
                                                                     gatt_server->path,
                                                                     (GDBusInterfaceInfo*)&freedesktop_dbus_object_manager_interface,
                                                                     &gatt_server_method_table,
                                                                     gatt_server,
                                                                     NULL,
                                                                     &error);
    if (error != NULL) {
        lm_log_error(TAG, "publish gatt server failed: %s", error->message);
        g_clear_error(&error);
        return;
    }

    lm_log_debug(TAG, "successfully published gatt server");
}

lm_gatt_server_t *lm_gatt_server_create(lm_adapter_t *adapter) {
    g_assert(adapter != NULL);
    gchar* random_str = lm_utils_random_string(4);

    lm_gatt_server_t *gatt_server = g_new0(lm_gatt_server_t, 1);
    gatt_server->dbus_conn = lm_adapter_get_dbus_conn(adapter);
    gatt_server->path = g_strdup_printf("%s/lmgattsrv_%s", lm_adapter_get_path(adapter), random_str);
    gatt_server->services = g_hash_table_new_full(g_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  (GDestroyNotify) lm_local_service_free);

    lm_gatt_server_publish(gatt_server, adapter);

    g_free(random_str);
    return gatt_server;
}

void lm_gatt_server_destroy(lm_gatt_server_t *gatt_server) {
    g_assert(gatt_server != NULL);

    lm_log_debug(TAG, "freeing gatt_server %s", gatt_server->path);

    if (gatt_server->services != NULL) {
        g_hash_table_destroy(gatt_server->services);
        gatt_server->services = NULL;
    }

    if (gatt_server->registration_id != 0) {
        gboolean result = g_dbus_connection_unregister_object(gatt_server->dbus_conn, gatt_server->registration_id);
        if (!result) {
            lm_log_error(TAG, "error: could not unregister gatt_server %s", gatt_server->path);
        }
        gatt_server->registration_id = 0;
    }

    if (gatt_server->path != NULL) {
        g_free(gatt_server->path);
        gatt_server->path = NULL;
    }

    g_free(gatt_server);
}

static const GDBusInterfaceVTable service_table = {};

lm_status_t lm_gatt_server_add_service(lm_gatt_server_t *gatt_server, const gchar *service_uuid) {
    g_return_val_if_fail (gatt_server != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), LM_STATUS_INVALID_ARGS);

    GError *error = NULL;

    lm_local_service_t *local_service = g_new0(lm_local_service_t, 1);
    local_service->uuid = g_strdup(service_uuid);
    local_service->gatt_server = gatt_server;
    local_service->characteristics = g_hash_table_new_full(
            g_str_hash,
            g_str_equal,
            g_free,
            (GDestroyNotify) lm_local_char_free);
    local_service->path = g_strdup_printf(
            "%s/service%d",
            gatt_server->path,
            g_hash_table_size(gatt_server->services));
    g_hash_table_insert(gatt_server->services, g_strdup(service_uuid), local_service);

    local_service->registration_id = g_dbus_connection_register_object(gatt_server->dbus_conn,
                                                                      local_service->path,
                                                                      (GDBusInterfaceInfo*)&bluez_gatt_service1_interface,
                                                                      &service_table,
                                                                      local_service,
                                                                      NULL,
                                                                      &error);
    if (error) {
        lm_log_error(TAG, "failed to publish local service");
        lm_log_error(TAG, "Error %s", error->message);
        g_hash_table_remove(gatt_server->services, service_uuid);
        lm_local_service_free(local_service);
        g_clear_error(&error);
        return LM_STATUS_FAIL;
    }

    lm_log_debug(TAG, "successfully published local service %s", service_uuid);
    return LM_STATUS_SUCCESS;
}

static lm_local_service_t *lm_gatt_server_get_service(const lm_gatt_server_t *gatt_server, const gchar *service_uuid) {
    g_return_val_if_fail (gatt_server != NULL, NULL);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), NULL);

    return g_hash_table_lookup(gatt_server->services, service_uuid);
}

static GList *prop_to_flags(const lm_gatt_property_t prop) {
    GList *list = NULL;

    if (prop & LM_GATT_PROP_READ) {
        list = g_list_append(list, g_strdup("read"));
    }
    if (prop & LM_GATT_PROP_WRITE_WITHOUT_RESP) {
        list = g_list_append(list, g_strdup("write-without-response"));
    }
    if (prop & LM_GATT_PROP_WRITE) {
        list = g_list_append(list, g_strdup("write"));
    }
    if (prop & LM_GATT_PROP_NOTIFY) {
        list = g_list_append(list, g_strdup("notify"));
    }
    if (prop & LM_GATT_PROP_INDICATE) {
        list = g_list_append(list, g_strdup("indicate"));
    }
    if (prop & LM_GATT_PROP_ENCRYPT_READ) {
        list = g_list_append(list, g_strdup("encrypt-read"));
    }
    if (prop & LM_GATT_PROP_ENCRYPT_WRITE) {
        list = g_list_append(list, g_strdup("encrypt-write"));
    }
    if (prop & LM_GATT_PROP_ENCRYPT_NOTIFY) {
        list = g_list_append(list, g_strdup("encrypt-notify"));
    }
    if (prop & LM_GATT_PROP_ENCRYPT_INDICATE) {
        list = g_list_append(list, g_strdup("encrypt-indicate"));
    }
    if (prop & LM_GATT_PROP_ENCRYPT_AUTH_READ) {
        list = g_list_append(list, g_strdup("encrypt-authenticated-read"));
    }
    if (prop & LM_GATT_PROP_ENCRYPT_AUTH_WRITE) {
        list = g_list_append(list, g_strdup("encrypt-authenticated-write"));
    }
    if (prop & LM_GATT_PROP_ENCRYPT_AUTH_NOTIFY) {
        list = g_list_append(list, g_strdup("encrypt-authenticated-notify"));
    }
    if (prop & LM_GATT_PROP_ENCRYPT_AUTH_INDICATE) {
        list = g_list_append(list, g_strdup("encrypt-authenticated-indicate"));
    }
    if (prop & LM_GATT_PROP_SECURE_READ) {
        list = g_list_append(list, g_strdup("secure-read"));
    }
    if (prop & LM_GATT_PROP_SECURE_WRITE) {
        list = g_list_append(list, g_strdup("secure-write"));
    }
    if (prop & LM_GATT_PROP_SECURE_NOTIFY) {
        list = g_list_append(list, g_strdup("secure-notify"));
    }
    if (prop & LM_GATT_PROP_SECURE_INDICATE) {
        list = g_list_append(list, g_strdup("secure-indicate"));
    }

    return list;
}

static lm_status_t lm_char_set_value(const lm_gatt_server_t *gatt_server, lm_local_char_t *local_char,
                                         GByteArray *byte_array)
{
    g_return_val_if_fail (gatt_server != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (local_char != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (byte_array != NULL, LM_STATUS_INVALID_ARGS);

    GString *byteArrayStr = lm_utils_g_byte_array_as_hex(byte_array);
    lm_log_debug(TAG, "set value <%s> to <%s>", byteArrayStr->str, local_char->uuid);
    g_string_free(byteArrayStr, TRUE);

    if (local_char->value != NULL) {
        g_byte_array_free(local_char->value, TRUE);
    }

    // Copy the byte array contents to the char's value
    GByteArray *newByteArray = g_byte_array_sized_new(byte_array->len);
    g_byte_array_append(newByteArray, byte_array->data, byte_array->len);
    local_char->value = newByteArray;

    lm_gatt_server_char_updated_ind_t ind = {
        .gatt_server = local_char->gatt_server,
        .service_uuid = local_char->service_uuid,
        .char_uuid = local_char->uuid,
        .byte_array = local_char->value
    };
    lm_app_event_callback(LM_GATT_SERVER_CHAR_UPDATED_IND, LM_STATUS_SUCCESS, &ind);

    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_desc_set_value(const lm_gatt_server_t *gatt_server, lm_local_desc_t *descriptor,
                                     GByteArray *byte_array)
{
    g_return_val_if_fail (gatt_server != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (descriptor != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (byte_array != NULL, LM_STATUS_INVALID_ARGS);

    GString *byteArrayStr = lm_utils_g_byte_array_as_hex(byte_array);
    lm_log_debug(TAG, "set value <%s> to <%s>", byteArrayStr->str, descriptor->uuid);
    g_string_free(byteArrayStr, TRUE);

    if (descriptor->value != NULL) {
        g_byte_array_free(descriptor->value, TRUE);
    }

    // Copy the byte array contents to the descriptor's value
    GByteArray *newByteArray = g_byte_array_sized_new(byte_array->len);
    g_byte_array_append(newByteArray, byte_array->data, byte_array->len);
    descriptor->value = newByteArray;

    lm_gatt_server_desc_updated_ind_t ind = {
        .gatt_server = descriptor->gatt_server,
        .service_uuid = descriptor->service_uuid,
        .char_uuid = descriptor->char_uuid,
        .desc_uuid = descriptor->uuid,
        .byte_array = descriptor->value
    };
    lm_app_event_callback(LM_GATT_SERVER_DESC_UPDATED_IND, LM_STATUS_SUCCESS, &ind);

    return LM_STATUS_SUCCESS;
}

static lm_local_char_t *get_local_char(const lm_gatt_server_t *gatt_server, const gchar *service_uuid,
                                                     const gchar *char_uuid) {

    g_return_val_if_fail (gatt_server != NULL, NULL);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), NULL);
    g_return_val_if_fail (lm_utils_is_valid_uuid(char_uuid), NULL);

    lm_local_service_t *service = lm_gatt_server_get_service(gatt_server, service_uuid);
    if (service != NULL) {
        return g_hash_table_lookup(service->characteristics, char_uuid);
    }
    return NULL;
}

static lm_local_desc_t *get_local_desc(const lm_gatt_server_t *gatt_server, const gchar *service_uuid,
                                             const gchar *char_uuid, const gchar *desc_uuid) {

    g_return_val_if_fail (gatt_server != NULL, NULL);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), NULL);
    g_return_val_if_fail (lm_utils_is_valid_uuid(char_uuid), NULL);
    g_return_val_if_fail (lm_utils_is_valid_uuid(desc_uuid), NULL);

    lm_local_char_t *local_char = get_local_char(gatt_server, service_uuid, char_uuid);
    if (local_char != NULL) {
        return g_hash_table_lookup(local_char->descriptors, desc_uuid);
    }
    return NULL;
}

static void lm_descriptor_method_call(__attribute__((unused)) GDBusConnection *conn,
                                      __attribute__((unused)) const gchar *sender,
                                      __attribute__((unused)) const gchar *path,
                                      __attribute__((unused)) const gchar *interface,
                                      __attribute__((unused)) const gchar *method,
                                      __attribute__((unused)) GVariant *params,
                                      __attribute__((unused)) GDBusMethodInvocation *invocation,
                                      __attribute__((unused)) void *userdata) {

    lm_local_desc_t *local_desc = (lm_local_desc_t *) userdata;
    g_assert(local_desc != NULL);

    lm_gatt_server_t *gatt_server = local_desc->gatt_server;
    g_assert(gatt_server != NULL);

    if (g_str_equal(method, GATT_DESC_METHOD_READ_VALUE)) {
        lm_read_options_t *options = parse_read_options(params);

        lm_log_debug(TAG, "read descriptor <%s> by %s", local_desc->uuid, options->device);

        const gchar *result = NULL;
        lm_status_t status;
        lm_gatt_server_desc_read_ind_t ind = {
            .gatt_server = local_desc->gatt_server,
            .client_addr = options->device,
            .service_uuid = local_desc->service_uuid,
            .char_uuid = local_desc->char_uuid,
            .desc_uuid = local_desc->uuid
        };
        status = lm_app_event_callback(LM_GATT_SERVER_DESC_READ_IND, LM_STATUS_SUCCESS, &ind);
        read_options_free(options);

        result = lm_gatt_status_to_bluez_error(status);
        if (result) {
            g_dbus_method_invocation_return_dbus_error(invocation, result, "read descriptor error");
            lm_log_debug(TAG, "read descriptor error");
            return;
        }

        if (local_desc->value != NULL) {
            GVariant *resultVariant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                                                local_desc->value->data,
                                                                local_desc->value->len,
                                                                sizeof(guint8));
            g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&resultVariant, 1));
        } else {
            g_dbus_method_invocation_return_dbus_error(invocation,
                        lm_gatt_status_to_bluez_error(LM_STATUS_GATT_SRV_FAILED),
                        "no value for descriptor");
        }
    } else if (g_str_equal(method, GATT_DESC_METHOD_WRITE_VALUE)) {
        g_assert(g_str_equal(g_variant_get_type_string(params), "(aya{sv})"));
        GVariant *valueVariant, *options_variant;

        // Get the options
        g_variant_get(params, "(@ay@a{sv})", &valueVariant, &options_variant);
        lm_write_options_t *options = parse_write_options(options_variant);
        g_variant_unref(options_variant);

        // Get the byte array to be written
        GByteArray *byte_array = lm_utils_g_variant_get_byte_array(valueVariant);

        lm_log_debug(TAG, "write descriptor <%s> by %s", local_desc->uuid, options->device);

        // Allow gatt_server to accept/reject the desc value before setting it
        const gchar *result = NULL;
        lm_status_t status;
        lm_gatt_server_desc_wrt_ind_t ind = {
            .gatt_server = local_desc->gatt_server,
            .client_addr = options->device,
            .service_uuid = local_desc->service_uuid,
            .char_uuid = local_desc->char_uuid,
            .desc_uuid = local_desc->uuid,
            .byte_array = byte_array
        };
        status = lm_app_event_callback(LM_GATT_SERVER_DESC_WRT_IND, LM_STATUS_SUCCESS, &ind);
        write_options_free(options);

        result = lm_gatt_status_to_bluez_error(status);
        if (result) {
            g_dbus_method_invocation_return_dbus_error(invocation, result, "write error");
            lm_log_error(TAG, "write error");
            g_variant_unref(valueVariant);
            if (byte_array != NULL) {
                g_byte_array_free(byte_array, FALSE);
            }
            return;
        }

        lm_desc_set_value(gatt_server, local_desc, byte_array);

        if (byte_array != NULL) {
            g_byte_array_free(byte_array, FALSE);
        }

        g_variant_unref(valueVariant);

        g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    }
}

static const GDBusInterfaceVTable descriptor_table = {
        .method_call = lm_descriptor_method_call,
};

lm_status_t lm_gatt_server_add_desc(lm_gatt_server_t *gatt_server, const gchar *service_uuid,
                                    const gchar *char_uuid, const gchar *desc_uuid, lm_gatt_property_t prop) {
    g_return_val_if_fail (gatt_server != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), LM_STATUS_INVALID_ARGS);

    lm_local_char_t *local_char = get_local_char(gatt_server, service_uuid, char_uuid);
    if (local_char == NULL) {
        g_critical("char %s does not exist", char_uuid);
        return LM_STATUS_INVALID_ARGS;
    }

    GError *error = NULL;
    lm_local_desc_t *local_desc = g_new0(lm_local_desc_t, 1);
    local_desc->uuid = g_strdup(desc_uuid);
    local_desc->gatt_server = gatt_server;
    local_desc->char_path = g_strdup(local_char->path);
    local_desc->char_uuid = g_strdup(char_uuid);
    local_desc->service_uuid = g_strdup(service_uuid);
    local_desc->flags = prop_to_flags(prop);
    local_desc->path = g_strdup_printf("%s/desc%d",
                                            local_char->path,
                                            g_hash_table_size(local_char->descriptors));
    g_hash_table_insert(local_char->descriptors, g_strdup(desc_uuid), local_desc);

    // Register descriptor
    local_desc->registration_id = g_dbus_connection_register_object(gatt_server->dbus_conn,
                                                                         local_desc->path,
                                                                         (GDBusInterfaceInfo*)&bluez_gatt_descriptor1_interface,
                                                                         &descriptor_table,
                                                                         local_desc,
                                                                         NULL,
                                                                         &error);
    if (error) {
        lm_log_error(TAG, "failed to publish local descriptor");
        lm_log_error(TAG, "Error %s", error->message);
        g_clear_error(&error);
        g_hash_table_remove(local_char->descriptors, desc_uuid);
        return LM_STATUS_FAIL;
    }

    lm_log_debug(TAG, "successfully published local descriptor %s", desc_uuid);
    return LM_STATUS_SUCCESS;
}

lm_status_t lm_gatt_server_set_char_value(const lm_gatt_server_t *gatt_server, const gchar *service_uuid,
                                    const gchar *char_uuid, GByteArray *byte_array) {

    g_return_val_if_fail (gatt_server != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (service_uuid != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (char_uuid != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (byte_array != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(char_uuid), LM_STATUS_INVALID_ARGS);

    lm_local_char_t *local_char = get_local_char(gatt_server, service_uuid, char_uuid);
    if (local_char == NULL) {
        g_critical("%s: local char with uuid %s does not exist", G_STRFUNC, char_uuid);
        return LM_STATUS_INVALID_ARGS;
    }

    return lm_char_set_value(gatt_server, local_char, byte_array);
}

lm_status_t lm_gatt_server_set_desc_value(const lm_gatt_server_t *gatt_server, const gchar *service_uuid,
                                    const gchar *char_uuid, const gchar *desc_uuid, GByteArray *byte_array) {

    g_return_val_if_fail (gatt_server != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (service_uuid != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (char_uuid != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (byte_array != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(char_uuid), LM_STATUS_INVALID_ARGS);

    lm_local_desc_t *descriptor = get_local_desc(gatt_server, service_uuid, char_uuid, desc_uuid);
    if (descriptor == NULL) {
        g_critical("%s: char with uuid %s does not exist", G_STRFUNC, char_uuid);
        return LM_STATUS_FAIL;
    }

    return lm_desc_set_value(gatt_server, descriptor, byte_array);
}

GByteArray *lm_gatt_server_get_char_value(const lm_gatt_server_t *gatt_server, const gchar *service_uuid,
                                            const gchar *char_uuid) {

    g_return_val_if_fail (gatt_server != NULL, NULL);
    g_return_val_if_fail (service_uuid != NULL, NULL);
    g_return_val_if_fail (char_uuid != NULL, NULL);
    g_return_val_if_fail (g_uuid_string_is_valid(service_uuid), NULL);
    g_return_val_if_fail (g_uuid_string_is_valid(char_uuid), NULL);

    lm_local_char_t *local_char = get_local_char(gatt_server, service_uuid, char_uuid);
    if (local_char != NULL) {
        return local_char->value;
    }
    return NULL;
}

static void lm_characteristic_method_call(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender,
                                          __attribute__((unused)) const gchar *path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *method,
                                          __attribute__((unused)) GVariant *params,
                                          __attribute__((unused)) GDBusMethodInvocation *invocation,
                                          __attribute__((unused)) void *userdata) {

    lm_local_char_t *local_char = (lm_local_char_t *) userdata;
    g_assert(local_char != NULL);

    lm_gatt_server_t *gatt_server = local_char->gatt_server;
    g_assert(gatt_server != NULL);

    if (g_str_equal(method, GATT_CHAR_METHOD_READ_VALUE)) {
        lm_log_debug(TAG, "read <%s>", local_char->uuid);
        lm_read_options_t *options = parse_read_options(params);

        // Allow gatt_server to accept/reject the local char value before setting it
        const gchar *result = NULL;
        lm_status_t status;
        lm_gatt_server_char_read_ind_t ind = {
            .gatt_server = local_char->gatt_server,
            .client_addr = options->device,
            .service_uuid = local_char->service_uuid,
            .char_uuid = local_char->uuid,
            .mtu = options->mtu,
            .offset = options->offset
        };
        status = lm_app_event_callback(LM_GATT_SERVER_CHAR_READ_IND, LM_STATUS_SUCCESS, &ind);
        read_options_free(options);

        result = lm_gatt_status_to_bluez_error(status);
        if (result) {
            g_dbus_method_invocation_return_dbus_error(invocation, result, "read local char error");
            lm_log_error(TAG, "read local char error '%s'", result);
            return;
        }

        // TODO deal with the offset & mtu parameter
        if (local_char->value != NULL) {
            GVariant *resultVariant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                                                local_char->value->data,
                                                                local_char->value->len,
                                                                sizeof(guint8));
            g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&resultVariant, 1));
        } else {
            g_dbus_method_invocation_return_dbus_error(invocation,
                        lm_gatt_status_to_bluez_error(LM_STATUS_GATT_SRV_FAILED),
                        "no value");
        }
    } else if (g_str_equal(method, GATT_CHAR_METHOD_WRITE_VALUE)) {
        g_assert(g_str_equal(g_variant_get_type_string(params), "(aya{sv})"));
        GVariant *valueVariant, *options_variant;

        // Get the write options
        g_variant_get(params, "(@ay@a{sv})", &valueVariant, &options_variant);
        lm_write_options_t *options = parse_write_options(options_variant);
        g_variant_unref(options_variant);

        // Get the byte array to be written
        GByteArray *byte_array = lm_utils_g_variant_get_byte_array(valueVariant);

        lm_log_debug(TAG, "write <%s>", local_char->uuid);

        // Allow gatt_server to accept/reject the local char value before setting it
        const gchar *result = NULL;
        lm_status_t status;
        lm_gatt_server_char_wrt_ind_t ind = {
            .gatt_server = local_char->gatt_server,
            .client_addr = options->device,
            .service_uuid = local_char->service_uuid,
            .char_uuid = local_char->uuid,
            .byte_array = byte_array,
            .mtu = options->mtu,
            .offset = options->offset
        };
        status = lm_app_event_callback(LM_GATT_SERVER_CHAR_WRT_IND, LM_STATUS_SUCCESS, &ind);
        write_options_free(options);

        result = lm_gatt_status_to_bluez_error(status);
        if (result) {
            g_dbus_method_invocation_return_dbus_error(invocation, result, "write error");
            lm_log_debug(TAG, "write error");
            return;
        }

        // TODO deal with offset and mtu
        lm_char_set_value(gatt_server, local_char, byte_array);

        if (byte_array != NULL) {
            g_byte_array_free(byte_array, FALSE);
        }

        g_variant_unref(valueVariant);

        g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    } else if (g_str_equal(method, GATT_CHAR_METHOD_START_NOTIFY)) {
        lm_log_debug(TAG, "start notify <%s>", local_char->uuid);

        local_char->notifying = TRUE;
        g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));

        lm_gatt_server_char_ntf_enabled_ind_t ind = {
            .gatt_server = local_char->gatt_server,
            .service_uuid = local_char->service_uuid,
            .char_uuid = local_char->uuid
        };
        lm_app_event_callback(LM_GATT_SERVER_CHAR_NTF_ENABLED_IND, LM_STATUS_SUCCESS, &ind);
    } else if (g_str_equal(method, GATT_CHAR_METHOD_STOP_NOTIFY)) {
        lm_log_debug(TAG, "stop notify <%s>", local_char->uuid);

        local_char->notifying = FALSE;
        g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));

        lm_gatt_server_char_ntf_disabled_ind_t ind = {
            .gatt_server = local_char->gatt_server,
            .service_uuid = local_char->service_uuid,
            .char_uuid = local_char->uuid
        };
        lm_app_event_callback(LM_GATT_SERVER_CHAR_NTF_DISABLED_IND, LM_STATUS_SUCCESS, &ind);
    } else if (g_str_equal(method, GATT_CHAR_METHOD_CONFIRM)) {
        lm_log_debug(TAG, "indication confirmed <%s>", local_char->uuid);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    }
}

static GVariant *characteristic_get_property(
                                      __attribute__((unused)) GDBusConnection *dbus_conn,
                                      __attribute__((unused)) const gchar *sender,
                                      __attribute__((unused)) const gchar *object_path,
                                      __attribute__((unused)) const gchar *interface_name,
                                      __attribute__((unused)) const gchar *property_name,
                                      __attribute__((unused)) GError **error,
                                      gpointer user_data) {

    lm_log_debug(TAG, "local char get property : %s", property_name);
    lm_local_char_t *local_char = (lm_local_char_t *) user_data;
    g_assert(local_char != NULL);

    GVariant *ret = NULL;
    if (g_str_equal(property_name, GATT_CHAR_PROPERTY_UUID)) {
        ret = g_variant_new_string(local_char->uuid);
    } else if (g_str_equal(property_name, GATT_CHAR_PROPERTY_SERVICE)) {
        ret = g_variant_new_object_path(local_char->path);
    } else if (g_str_equal(property_name, GATT_CHAR_PROPERTY_FLAGS)) {
        ret = lm_local_charget_flags(local_char);
    } else if (g_str_equal(property_name, GATT_CHAR_PROPERTY_NOTIFYING)) {
        ret = g_variant_new_boolean(local_char->notifying);
    } else if (g_str_equal(property_name, GATT_CHAR_PROPERTY_VALUE)) {
        ret = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, local_char->value->data, local_char->value->len,
                                        sizeof(guint8));
    }
    return ret;
}

static const GDBusInterfaceVTable characteristic_table = {
        .method_call = lm_characteristic_method_call,
        .get_property = characteristic_get_property
};

lm_status_t lm_gatt_server_add_char(lm_gatt_server_t *gatt_server, const gchar *service_uuid,
                                        const gchar *char_uuid, lm_gatt_property_t prop)
{

    g_return_val_if_fail (gatt_server != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(char_uuid), LM_STATUS_INVALID_ARGS);

    lm_local_service_t *local_service = lm_gatt_server_get_service(gatt_server, service_uuid);
    if (local_service == NULL) {
        g_critical("service %s does not exist", service_uuid);
        return LM_STATUS_INVALID_ARGS;
    }

    GError *error = NULL;
    lm_local_char_t *local_char = g_new0(lm_local_char_t, 1);
    local_char->service_uuid = g_strdup(service_uuid);
    local_char->service_path = g_strdup(local_service->path);
    local_char->uuid = g_strdup(char_uuid);
    local_char->prop = prop;
    local_char->flags = prop_to_flags(prop);
    local_char->value = NULL;
    local_char->gatt_server = gatt_server;
    local_char->path = g_strdup_printf("%s/gchar%d",
                                           local_service->path,
                                           g_hash_table_size(local_service->characteristics));
    local_char->descriptors = g_hash_table_new_full(
            g_str_hash,
            g_str_equal,
            g_free,
            (GDestroyNotify) lm_local_desc_free);
    g_hash_table_insert(local_service->characteristics, g_strdup(char_uuid), local_char);

    // Register char
    local_char->registration_id = g_dbus_connection_register_object(gatt_server->dbus_conn,
                                                                        local_char->path,
                                                                        (GDBusInterfaceInfo*)&bluez_gatt_characteristic1_interface,
                                                                        &characteristic_table,
                                                                        local_char,
                                                                        NULL,
                                                                        &error);

    if (error) {
        lm_log_error(TAG, "failed to publish local char");
        lm_log_error(TAG, "Error %s", error->message);
        g_clear_error(&error);
        g_hash_table_remove(local_service->characteristics, char_uuid);
        return LM_STATUS_FAIL;
    }

    lm_log_debug(TAG, "successfully published local char %s", char_uuid);
    return LM_STATUS_SUCCESS;
}

const gchar *lm_gatt_server_get_path(const lm_gatt_server_t *gatt_server)
{
    g_assert(gatt_server != NULL);
    return gatt_server->path;
}

lm_status_t lm_gatt_server_send_notify(const lm_gatt_server_t *gatt_server, const gchar *service_uuid,
    const gchar *char_uuid, const GByteArray *byte_array)
{
    g_return_val_if_fail (gatt_server != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (byte_array != NULL, LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), LM_STATUS_INVALID_ARGS);
    g_return_val_if_fail (lm_utils_is_valid_uuid(char_uuid), LM_STATUS_INVALID_ARGS);

    lm_local_char_t *local_char = get_local_char(gatt_server, service_uuid, char_uuid);
    if (local_char == NULL) {
        g_critical("%s: local char %s does not exist", G_STRFUNC, service_uuid);
        return LM_STATUS_INVALID_ARGS;
    }

    GVariant *valueVariant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                                       byte_array->data,
                                                       byte_array->len,
                                                       sizeof(guint8));
    GVariantBuilder *properties_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(properties_builder, "{sv}", "Value", valueVariant);
    GVariantBuilder *invalidated_builder = g_variant_builder_new(G_VARIANT_TYPE("as"));

    GError *error = NULL;
    gboolean result = g_dbus_connection_emit_signal(gatt_server->dbus_conn,
                                                    NULL,
                                                    local_char->path,
                                                    INTERFACE_PROPERTIES,
                                                    PROPERTIES_SIGNAL_CHANGED,
                                                    g_variant_new("(sa{sv}as)",
                                                    INTERFACE_GATT_CHAR,
                                                    properties_builder, invalidated_builder),
                                                    &error);

    g_variant_builder_unref(invalidated_builder);
    g_variant_builder_unref(properties_builder);

    if (result != TRUE) {
        if (error != NULL) {
            lm_log_error(TAG, "error emitting signal: %s", error->message);
            g_clear_error(&error);
        }
        return LM_STATUS_FAIL;
    }

    GString *byteArrayStr = lm_utils_g_byte_array_as_hex(byte_array);
    lm_log_debug(TAG, "notified <%s> on <%s>", byteArrayStr->str, local_char->uuid);
    g_string_free(byteArrayStr, TRUE);
    return LM_STATUS_SUCCESS;
}

gboolean lm_gatt_server_is_notify_enabled(const lm_gatt_server_t *gatt_server, const gchar *service_uuid,
                                            const gchar *char_uuid) {
    g_return_val_if_fail (gatt_server != NULL, FALSE);
    g_return_val_if_fail (lm_utils_is_valid_uuid(service_uuid), FALSE);
    g_return_val_if_fail (lm_utils_is_valid_uuid(char_uuid), FALSE);

    lm_local_char_t *local_char = get_local_char(gatt_server, service_uuid, char_uuid);
    if (local_char == NULL) {
        g_critical("%s: local char %s does not exist", G_STRFUNC, service_uuid);
        return FALSE;
    }

    return local_char->notifying;
}
