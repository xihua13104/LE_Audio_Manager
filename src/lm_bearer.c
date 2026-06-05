/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

#include "bluez_dbus.h"
#include "lm_device.h"
#include "lm_device_priv.h"
#include "lm_adapter.h"
#include "lm_log.h"
#include "lm_utils.h"
#include "lm_uuids.h"

#define TAG "lm_bearer"

struct lm_bearer {
    GDBusConnection *dbus_conn;
    const gchar *device_path;
    const gchar *adapter_path;
    const gchar *name;
    lm_device_bearer_type_t type;
    lm_device_t *device;
    lm_device_conn_state_t conn_state;
    lm_device_bonding_state_t bonding_state;
    gboolean connected;
    gboolean paired;
    gboolean bonded;
};

static const gchar *bearer_name_str[LM_DEVICE_BEARER_MAX] = {
    "LE",
    "BREDR"
};

lm_bearer_t *lm_bearer_create(lm_device_t *device,
                    lm_device_bearer_type_t type, const gchar *device_path)
{
    lm_bearer_t *bearer = NULL;

    g_assert(device && device_path);

    if ((bearer = g_new0(lm_bearer_t, 1)) == NULL)
        return NULL;

    bearer->dbus_conn = lm_device_get_dbus_conn(device);
    bearer->device = device;
    bearer->type = type;
    bearer->name = g_strdup(bearer_name_str[type]);
    bearer->device_path = g_strdup(device_path);

    lm_log_debug(TAG, "create '%s' bearer success", bearer->name);

    return bearer;
}

void lm_bearer_destroy(lm_bearer_t *bearer)
{
    g_assert(bearer);

    lm_log_debug(TAG, "destroy bearer '%s'", bearer->name);

    if (bearer->name)
        g_free((gpointer)bearer->name);

    if (bearer->device_path)
        g_free((gpointer)bearer->device_path);

    if (bearer->adapter_path)
        g_free((gpointer)bearer->adapter_path);

    g_free((gpointer)bearer);
}

const gchar *lm_bearer_get_name(lm_bearer_t *bearer)
{
    g_assert(bearer);

    return bearer->name;
}

lm_device_bearer_type_t lm_bearer_get_type(lm_bearer_t *bearer)
{
    g_assert(bearer);

    return bearer->type;
}

const gchar *lm_bearer_type_to_name(lm_device_bearer_type_t type)
{
    return bearer_name_str[type];
}

gboolean lm_bearer_is_connected(lm_bearer_t *bearer)
{
    g_assert(bearer);

    return bearer->connected;
}

gboolean lm_bearer_is_paired(lm_bearer_t *bearer)
{
    g_assert(bearer);

    return bearer->paired;
}

gboolean lm_bearer_is_bonded(lm_bearer_t *bearer)
{
    g_assert(bearer);

    return bearer->bonded;
}

lm_device_conn_state_t lm_bearer_get_conn_state(lm_bearer_t *bearer)
{
    g_assert(bearer);

    return bearer->conn_state;
}

static void lm_bearer_set_conn_state(lm_bearer_t *bearer, lm_device_conn_state_t state)
{
    g_assert(bearer);

    if (bearer->conn_state != state) {
        bearer->conn_state = state;
        // lm_device_bearer_conn_state_change_ind_t ind = {
        //     .device = bearer->device
        //     .type = bearer->type
        // };
        // lm_app_event_callback(LM_DEVICE_BEARER_CONN_STATE_CHANGE_IND, LM_STATUS_SUCCESS, &ind);
    }
}

lm_device_bonding_state_t lm_bearer_get_bonding_state(lm_bearer_t *bearer)
{
    g_assert(bearer);

    return bearer->bonding_state;
}

static void lm_bearer_set_bonding_state(lm_bearer_t *bearer, lm_device_bonding_state_t state)
{
    g_assert(bearer);

    if (bearer->bonding_state != state) {
        bearer->bonding_state = state;
    //     lm_device_bearer_bonding_state_change_ind_t ind = {
    //         .device = bearer->device
    //         .type = bearer->type
    //     };
    //     lm_app_event_callback(LM_DEVICE_BEARER_BONDING_STATE_CHANGE_IND, LM_STATUS_SUCCESS, &ind);
    }
}

static void lm_bearer_call_method_cb(__attribute__((unused)) GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data)
{
    lm_bearer_t *bearer = (lm_bearer_t *) user_data;
    g_assert(bearer != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(bearer->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to call bearer method (error %d '%s')", error->code, error->message);
        g_clear_error(&error);
    }
}

static void lm_bearer_call_method(lm_bearer_t *bearer,
        const gchar *method, __attribute__((unused)) GVariant *parameters)
{
    g_assert(bearer != NULL);
    g_assert(method != NULL);

    const gchar *bearers_iface[LM_DEVICE_BEARER_MAX] = {
        INTERFACE_BEARER_LE,
        INTERFACE_BEARER_BREDR
    };

    g_dbus_connection_call(bearer->dbus_conn,
                           BLUEZ_DBUS,
                           bearer->device_path,
                           bearers_iface[bearer->type],
                           method,
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_bearer_call_method_cb,
                           bearer);
}

lm_status_t lm_bearer_connect(lm_bearer_t *bearer)
{
    g_assert(bearer);

    if (bearer->connected) {
        lm_log_warn(TAG, "device '%s' with bearer '%s' is already connected",
                bearer->device_path, bearer->name);
        return LM_STATUS_SUCCESS;
    }

    lm_log_info(TAG, "connect '%s' with device '%s'", bearer->name, bearer->device_path);

    lm_bearer_set_conn_state(bearer, LM_DEVICE_CONNECTING);

    lm_bearer_call_method(bearer, BEARER_METHOD_CONNECT, NULL);

    return LM_STATUS_PENDING;
}

lm_status_t lm_bearer_disconnect(lm_bearer_t *bearer)
{
    g_assert(bearer);

    if (!bearer->connected) {
        lm_log_warn(TAG, "device '%s' with bearer '%s' is already disconnected",
                bearer->device_path, bearer->name);
        return LM_STATUS_SUCCESS;
    }

    lm_log_info(TAG, "disconnect '%s' with device '%s'", bearer->name, bearer->device_path);

    lm_bearer_set_conn_state(bearer, LM_DEVICE_DISCONNECTING);

    lm_bearer_call_method(bearer, BEARER_METHOD_DISCONNECT, NULL);

    return LM_STATUS_PENDING;
}

void lm_bearer_update_property(lm_bearer_t *bearer,
            const gchar *property_name, GVariant *property_value)
{
    if (g_str_equal(property_name, BEARER_PROPERTY_ADAPTER)) {
        if (bearer->adapter_path)
            g_free((gpointer)bearer->adapter_path);
        bearer->adapter_path = g_strdup(g_variant_get_string(property_value, NULL));
        lm_log_debug(TAG, "bearer '%s' adapter '%s'", bearer->name, bearer->adapter_path);
    } else if (g_str_equal(property_name, BEARER_PROPERTY_CONNECTED)) {
        bearer->connected = g_variant_get_boolean(property_value);
        lm_log_debug(TAG, "bearer '%s' on device '%s' connected %s",
                bearer->name, bearer->device_path, bearer->connected ? "true" : "false");

        lm_bearer_set_conn_state(bearer, bearer->connected ? LM_DEVICE_CONNECTED : LM_DEVICE_DISCONNECTED);

        if (lm_device_is_bcast_device(bearer->device))
            return;

        if (!bearer->connected)
            return;

        if (bearer->type == LM_DEVICE_BEARER_LE)
            lm_device_set_conn_type(bearer->device, LM_DEVICE_CONN_LE);
        else if (bearer->type == LM_DEVICE_BEARER_BREDR)
            lm_device_set_conn_type(bearer->device, LM_DEVICE_CONN_BREDR);

        lm_device_connected_ind_t ind = {
            .adapter = lm_device_get_adapter(bearer->device),
            .device = bearer->device,
            .type = bearer->type
        };

        lm_log_info(TAG, "device '%s' connected via '%s'", bearer->device_path, bearer->name);
        lm_app_event_callback(LM_DEVICE_CONNECTED_IND, LM_STATUS_SUCCESS, &ind);
    } else if (g_str_equal(property_name, BEARER_PROPERTY_PAIRED)) {
        bearer->paired = g_variant_get_boolean(property_value);
        lm_log_debug(TAG, "bearer '%s' on device '%s' paired %s",
                bearer->name, bearer->device_path, bearer->paired ? "true" : "false");
    } else if (g_str_equal(property_name, BEARER_PROPERTY_BONDED)) {
        bearer->bonded = g_variant_get_boolean(property_value);
        lm_log_debug(TAG, "bearer '%s' on device '%s' bonded %s",
                bearer->name, bearer->device_path, bearer->bonded ? "true" : "false");
        lm_bearer_set_bonding_state(bearer, bearer->bonded ? LM_DEVICE_BONDED : LM_DEVICE_BOND_NONE);
    }
}