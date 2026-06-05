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
#include "lm_device.h"
#include "lm_device_priv.h"
#include "lm_adv.h"
#include "bluez_dbus.h"
#include "lm_log.h"
#include "lm.h"
#include "lm_utils.h"
#include "lm_uuids.h"
#include "lm_transport.h"
#include "lm_transport_priv.h"
#include "lm_bearer_priv.h"
#include "lm_gatt_server.h"
#include <glib.h>
#include <gio/gio.h>
#include <stdio.h>

#define TAG "lm_adapter"

typedef struct {
    gint16 rssi;
    GPtrArray *services;
    const gchar *pattern;
    guint max_devices;
    guint timeout;
} lm_adapter_discovery_filter_t;

struct lm_adapter {
    GDBusConnection *dbus_conn;  // Borrowed
    const gchar *path; // Owned
    const gchar *address; // Owned
    const gchar *alias; //Owned
    gboolean powered;
    gboolean discoverable;
    gboolean connectable;
    gboolean discovering;
    gboolean advertising; //Owned

    lm_adapter_power_state_t power_state;
    lm_adapter_discovery_state_t discovery_state;
    lm_adapter_discovery_filter_t *discovery_filter;
    guint discovery_timer_id;
    guint discovery_devices_found;

    guint device_prop_changed;
    guint adapter_prop_changed;
    guint iface_added;
    guint iface_removed;

    void *user_data; // Borrowed
    GHashTable *device_cache; // Owned

    lm_adv_t *adv; // Borrowed

    GHashTable *local_bis_src_transports;
    guint local_bis_src_transport_prop_changed;
    //lm_agent_t *agent;

    /* memory self-management */
    gint ref_count;
};

typedef struct {
    lm_adapter_t *adapter;
    gchar *method;
} lm_adapter_method_call_ctx_t;

static const gchar *g_power_state_name[] = {
    "on",
    "off",
    "off-enabling",
    "on-disabling",
    "off-blocked"
};

static const gchar *g_discovery_state_name[] = {
    "stopped",
    "starting",
    "started",
    "stopping"
};

#define LM_ADAPTER_GET_DISCOVERY_STATE_NAME(adapter) \
                g_discovery_state_name[(adapter)->discovery_state]


static void on_local_bis_src_transport_prop_changed(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender,
                                          __attribute__((unused)) const gchar *path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *signal,
                                          GVariant *parameters,
                                          void *user_data);

static lm_adapter_method_call_ctx_t *lm_adapter_method_call_ctx_create(lm_adapter_t *adapter,
                                                                const gchar *method) {
    g_assert(adapter != NULL);
    g_assert(method != NULL);

    lm_adapter_method_call_ctx_t *ctx = g_new0(lm_adapter_method_call_ctx_t, 1);
    ctx->adapter = adapter;
    ctx->method = g_strdup(method);
    return ctx;
}

static void lm_adapter_method_call_ctx_free(lm_adapter_method_call_ctx_t *ctx) {
    g_assert(ctx != NULL);

    if (ctx->method)
        g_free(ctx->method);

    g_free(ctx);
}

static lm_adapter_power_state_t lm_adapter_get_power_state_from_name(const gchar *power_state) {
    for (guint i = 0; i < G_N_ELEMENTS(g_power_state_name); i++) {
        if (g_strcmp0(power_state, g_power_state_name[i]) == 0) {
            return (lm_adapter_power_state_t)i;
        }
    }
    return LM_ADAPTER_POWER_OFF;
}

static void lm_adapter_free_discovery_filter(lm_adapter_t *adapter) {
    g_assert(adapter);

    if (!adapter->discovery_filter)
        return;

    if (adapter->discovery_filter->services) {
        for (guint i = 0; i < adapter->discovery_filter->services->len; i++) {
            gchar *uuid_filter = g_ptr_array_index(adapter->discovery_filter->services, i);
            g_free(uuid_filter);
        }
        g_ptr_array_free(adapter->discovery_filter->services, TRUE);
        adapter->discovery_filter->services = NULL;
    }
    if (adapter->discovery_filter->pattern) {
        g_free((gchar *) adapter->discovery_filter->pattern);
        adapter->discovery_filter->pattern = NULL;
    }
}

static void lm_adapter_update_property(lm_adapter_t *adapter,
                                       const gchar *property_name,
                                       GVariant *property_value)
{
    g_assert(adapter);
    g_assert(property_name);
    g_assert(property_value);

    // lm_log_debug(TAG, "%s property_name:%s",  __func__, property_name);

    if (g_str_equal(property_name, ADAPTER_PROPERTY_ADDRESS)) {
        if (adapter->address)
            g_free((void *)adapter->address);
        adapter->address = g_strdup(g_variant_get_string(property_value, NULL));
        lm_log_debug(TAG, "adapter '%s' addr '%s'", adapter->path, adapter->address);
    } else if (g_str_equal(property_name, ADAPTER_PROPERTY_POWERED)) {
        adapter->powered = g_variant_get_boolean(property_value);
        lm_log_debug(TAG, "adapter '%s' powered %s", adapter->path, adapter->powered ? "true" : "false");
    } else if(g_str_equal(property_name, ADAPTER_PROPERTY_POWER_STATE)) {
        const gchar *power_state_name = g_variant_get_string(property_value, NULL);
        g_assert(power_state_name);
        lm_log_info(TAG, "adapter '%s' power state changed to '%s'", adapter->path, power_state_name);
        adapter->power_state = lm_adapter_get_power_state_from_name(power_state_name);
        if (adapter->power_state == LM_ADAPTER_POWER_ON) {
            lm_adapter_power_on_cnf_t cnf = {
                .adapter = adapter
            };
            lm_app_event_callback(LM_ADAPTER_POWER_ON_CNF, LM_STATUS_SUCCESS, &cnf);
        } else if (adapter->power_state == LM_ADAPTER_POWER_OFF) {
            lm_adapter_power_off_cnf_t cnf = {
                .adapter = adapter
            };
            lm_app_event_callback(LM_ADAPTER_POWER_OFF_CNF, LM_STATUS_SUCCESS, &cnf);
        }
    } else if (g_str_equal(property_name, ADAPTER_PROPERTY_DISCOVERING)) {
        adapter->discovering = g_variant_get_boolean(property_value);
        lm_log_debug(TAG, "adapter '%s' discovering %s", adapter->path, adapter->discovering ? "true" : "false");
    } else if (g_str_equal(property_name, ADAPTER_PROPERTY_DISCOVERABLE)) {
        adapter->discoverable = g_variant_get_boolean(property_value);
        lm_log_debug(TAG, "adapter '%s' discoverable %s", adapter->path, adapter->discoverable ? "true" : "false");
    } else if (g_str_equal(property_name, ADAPTER_PROPERTY_CONNECTABLE)) {
        adapter->connectable = g_variant_get_boolean(property_value);
        lm_log_debug(TAG, "adapter '%s' connectable %s", adapter->path, adapter->connectable ? "true" : "false");
    } else if (g_str_equal(property_name, ADAPTER_PROPERTY_ALIAS)) {
        if (adapter->alias)
            g_free((void *)adapter->alias);
        adapter->alias = g_strdup(g_variant_get_string(property_value, NULL));
        lm_log_debug(TAG, "adapter '%s' alias '%s'", adapter->path, adapter->alias);
    }
}

static gboolean matches_discovery_filter(lm_adapter_t *adapter, lm_device_t *device)
{
    g_assert(adapter != NULL);
    g_assert(device != NULL);

    if (!adapter->discovery_filter)
        return TRUE;

    if (lm_device_get_rssi(device) < adapter->discovery_filter->rssi) {
        // lm_log_error(TAG, "device '%s' rejected (RSSI: %d)", lm_device_get_path(device), lm_device_get_rssi(device));
        return FALSE;
    }

    const gchar *pattern = adapter->discovery_filter->pattern;
    if (pattern != NULL && lm_device_get_name(device) != NULL) {
        if (!(g_str_has_prefix(lm_device_get_name(device), pattern) ||
              g_str_has_prefix(lm_device_get_address(device), pattern))) {
            lm_log_error(TAG, "device '%s' rejected (Name/Address does not match pattern '%s')",
                         lm_device_get_path(device), pattern);
            return FALSE;
        }
    }

    GPtrArray *services_filter = adapter->discovery_filter->services;
    if (services_filter != NULL) {
        guint count = services_filter->len;
        if (count == 0)
            return TRUE;

        for (guint i = 0; i < count; i++) {
            const gchar *uuid_filter = g_ptr_array_index(services_filter, i);
            if (lm_device_has_service(device, uuid_filter)) {
                return TRUE;
            }
        }
        return FALSE;
    }
    return TRUE;
}

static void deliver_discovery_result(lm_adapter_t *adapter, lm_device_t *device) {
    g_assert(adapter != NULL);
    g_assert(device != NULL);

    if (lm_device_get_conn_state(device) == LM_DEVICE_DISCONNECTED) {
        // Double check if the device matches the discovery filter
        if (!matches_discovery_filter(adapter, device))
            return;

        lm_adapter_discovery_result_ind_t ind = {
            .adapter = adapter,
            .device = device
        };
        lm_app_event_callback(LM_ADAPTER_DISCOVERY_RESULT_IND, LM_STATUS_SUCCESS, &ind);
        if (adapter->discovery_filter && adapter->discovery_filter->max_devices) {
            adapter->discovery_devices_found++;
            if (adapter->discovery_devices_found >= adapter->discovery_filter->max_devices) {
                lm_log_info(TAG, "Max devices found(%d), stopping discovery",
                    adapter->discovery_devices_found);

                lm_adapter_stop_discovery(adapter);

                lm_adapter_discovery_complete_ind_t complete_ind = {
                    .adapter = adapter
                };
                lm_app_event_callback(LM_ADAPTER_DISCOVERY_COMPLETE_IND,
                                    LM_STATUS_SUCCESS,
                                    (void *)&complete_ind);
            }
        }
    }
}

static void on_interface_disappeared(__attribute__((unused)) GDBusConnection *conn,
                                             __attribute__((unused)) const gchar *sender_name,
                                             __attribute__((unused)) const gchar *object_path,
                                             __attribute__((unused)) const gchar *interface,
                                             __attribute__((unused)) const gchar *signal_name,
                                             GVariant *parameters,
                                             gpointer user_data) {
    GVariantIter *interfaces = NULL;
    const gchar *object = NULL;
    const gchar *interface_name = NULL;

    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(oas)"));
    g_variant_get(parameters, "(&oas)", &object, &interfaces);

    while (g_variant_iter_loop(interfaces, "s", &interface_name)) {
        if (g_str_equal(interface_name, INTERFACE_DEVICE)) {
            lm_device_t *device = lm_device_lookup_by_path(adapter, object);
            if (device) {
                lm_log_debug(TAG, "device '%s' removed", object);
                g_hash_table_remove(adapter->device_cache, object);
            }
        } else if (g_str_equal(interface_name, INTERFACE_MEDIA_TRANSPORT)) {
            if (!g_str_has_prefix(object, adapter->path))
                continue;
            if (!g_hash_table_lookup(adapter->local_bis_src_transports, object))
                continue;

            lm_log_info(TAG, "bis source transport '%s' removed", object);
            g_hash_table_remove(adapter->local_bis_src_transports, object);
        }
    }

    if (interfaces)
        g_variant_iter_free(interfaces);
}

static void on_interface_appeared(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender_name,
                                          __attribute__((unused)) const gchar *object_path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *signal_name,
                                          GVariant *parameters,
                                          gpointer user_data) {
    GVariantIter *interfaces = NULL;
    const gchar *object = NULL;
    const gchar *interface_name = NULL;
    GVariant *properties = NULL;
    gchar *property_name = NULL;
    GVariantIter iter;
    GVariant *property_value = NULL;
    lm_device_t *new_device = NULL;
    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(oa{sa{sv}})"));
    g_variant_get(parameters, "(&oa{sa{sv}})", &object, &interfaces);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_interface_appeared, sender:%s, path:%s, interface:%s, signal:%s",
               sender_name, object, interface_name, signal_name);
#endif

    while (g_variant_iter_loop(interfaces, "{&s@a{sv}}", &interface_name, &properties)) {

        lm_log_debug(TAG, "interface '%s' appeared on object '%s'", interface_name, object);

        if (g_str_equal(interface_name, INTERFACE_DEVICE)) {
            // Skip this device if it is not for this adapter
            if (!g_str_has_prefix(object, adapter->path))
                continue;

            if (g_hash_table_contains(adapter->device_cache, object))
                continue;

            lm_device_t *device = lm_device_create_with_path(adapter, object);

            g_variant_iter_init(&iter, properties);
            while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
                lm_device_update_property(device, property_name, property_value);
            }

            g_hash_table_insert(adapter->device_cache,
                                g_strdup(lm_device_get_path(device)),
                                device);

            new_device = device;
        } else if (g_str_equal(interface_name, INTERFACE_BEARER_LE) ||
                    g_str_equal(interface_name, INTERFACE_BEARER_BREDR)) {
            if (!g_str_has_prefix(object, adapter->path))
                continue;

            lm_device_t *device = lm_device_lookup_by_path(adapter, object);
            if (!device)
                continue;

            lm_device_bearer_type_t type = g_str_equal(interface_name, INTERFACE_BEARER_LE) ? LM_DEVICE_BEARER_LE : LM_DEVICE_BEARER_BREDR;
            if (lm_device_get_bearer(device, type))
                continue;
            lm_bearer_t *bearer = lm_bearer_create(device, type, object);
            g_variant_iter_init(&iter, properties);
            while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
                lm_bearer_update_property(bearer, property_name, property_value);
            }
            lm_device_add_bearer(device, bearer);
        } else if (g_str_equal(interface_name, INTERFACE_MEDIA_TRANSPORT)) {
            // Skip this transport if it is not for this adapter
            if (!g_str_has_prefix(object, adapter->path))
                continue;
            if (g_hash_table_contains(adapter->local_bis_src_transports, object))
                continue;

            // lm_log_info(TAG, "media transport '%s' added on adapter", object);

            lm_transport_t *transport = lm_transport_create(NULL, object);
            g_variant_iter_init(&iter, properties);
            while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
                lm_transport_update_property(transport, property_name, property_value);
            }
            if (g_str_equal(lm_transport_get_uuid(transport), BCAST_AUDIO_AUNOUNCEMENT_SERVICE_UUID)) {
                g_hash_table_insert(adapter->local_bis_src_transports, g_strdup(lm_transport_get_path(transport)), transport);
                lm_log_info(TAG, "bis source transport '%s' added", object);
            } else {
                lm_transport_destroy(transport);
                // lm_log_warn(TAG, "unknown transport '%s'", object);
            }
        }
    }

    if (adapter->discovery_state == LM_ADAPTER_DISCOVERY_STARTED &&
        new_device != NULL &&
        lm_device_get_conn_state(new_device) == LM_DEVICE_DISCONNECTED) {
        deliver_discovery_result(adapter, new_device);
    }

    if (interfaces)
        g_variant_iter_free(interfaces);
}

static void on_device_prop_changed(__attribute__((unused)) GDBusConnection *conn,
                                         __attribute__((unused)) const gchar *sender,
                                         const gchar *path,
                                         __attribute__((unused)) const gchar *interface,
                                         __attribute__((unused)) const gchar *signal,
                                         GVariant *parameters,
                                         void *user_data) {
    GVariantIter *properties_changed = NULL;
    GVariantIter *properties_invalidated = NULL;
    const gchar *iface = NULL;
    const gchar *property_name = NULL;
    GVariant *property_value = NULL;

    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_device_prop_changed, sender:%s, path:%s, interface:%s, signal:%s",
               sender, path, interface, signal);
#endif

    lm_device_t *device = lm_device_lookup_by_path(adapter, path);
    if (device == NULL) {
        if (g_str_has_prefix(path, adapter->path)) {
            device = lm_device_create_with_path(adapter, path);
            g_hash_table_insert(adapter->device_cache, g_strdup(lm_device_get_path(device)), device);
            lm_log_warn(TAG, "new added device with path '%s'", path);
            lm_device_load_properties(device);
        }
    } else {
        gboolean is_dis_result = FALSE;
        lm_log_debug(TAG, "device prop change with path '%s'", path);
        g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
        g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);
        while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
            lm_device_update_property(device, property_name, property_value);
            if (g_str_equal(property_name, DEVICE_PROPERTY_RSSI) ||
                g_str_equal(property_name, DEVICE_PROPERTY_MANUFACTURER_DATA) ||
                g_str_equal(property_name, DEVICE_PROPERTY_SERVICE_DATA)) {
                is_dis_result = TRUE;
            }
        }
        if (adapter->discovery_state == LM_ADAPTER_DISCOVERY_STARTED && is_dis_result) {
            deliver_discovery_result(adapter, device);
        }
    }

    if (properties_changed)
        g_variant_iter_free(properties_changed);

    if (properties_invalidated)
        g_variant_iter_free(properties_invalidated);
}


static void on_adapter_prop_changed(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender,
                                          __attribute__((unused)) const gchar *path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *signal,
                                          GVariant *parameters,
                                          void *user_data) {

    GVariantIter *properties_changed = NULL;
    GVariantIter *properties_invalidated = NULL;
    const gchar *iface = NULL;
    const gchar *property_name = NULL;
    GVariant *property_value = NULL;

    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
    g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_adapter_prop_changed sender:%s, path:%s, interface:%s, signal:%s",
         sender, path, interface, signal);
#endif

    while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
        lm_adapter_update_property(adapter, property_name, property_value);
    }

    if (properties_changed)
        g_variant_iter_free(properties_changed);

    if (properties_invalidated)
        g_variant_iter_free(properties_invalidated);
}

static void on_local_bis_src_transport_prop_changed(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender,
                                          __attribute__((unused)) const gchar *path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *signal,
                                          GVariant *parameters,
                                          void *user_data) {

    GVariantIter *properties_changed = NULL;
    GVariantIter *properties_invalidated = NULL;
    const gchar *iface = NULL;
    const gchar *property_name = NULL;
    GVariant *property_value = NULL;
    lm_transport_t *transport = NULL;

    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
    g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_local_bis_src_transport_prop_changed sender:%s, path:%s, interface:%s, signal:%s",
        sender, path, interface, signal);
#endif

    if (!g_str_has_prefix(path, adapter->path))
        return;

    transport = (lm_transport_t *)g_hash_table_lookup(adapter->local_bis_src_transports, path);
    if (!transport)
        return;

    while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
        lm_transport_update_property(transport, property_name, property_value);
        if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_STATE)) {
            lm_adapter_local_bcast_transport_state_change_ind_t ind = {
                .adapter = adapter,
                .transport = transport
            };
            lm_app_event_callback(LM_ADAPTER_LOCAL_BCAST_TRANSPORT_STATE_CHANGE_IND,
                                        LM_STATUS_SUCCESS, &ind);
        }
    }

    if (properties_changed)
        g_variant_iter_free(properties_changed);

    if (properties_invalidated)
        g_variant_iter_free(properties_invalidated);
}

static void lm_adapter_subscribe_signal(lm_adapter_t *adapter)
{
    g_assert(adapter);
    g_assert(adapter->dbus_conn);
    g_assert(adapter->path);

    adapter->adapter_prop_changed = g_dbus_connection_signal_subscribe(adapter->dbus_conn,
                                                            BLUEZ_DBUS,
                                                            INTERFACE_PROPERTIES,
                                                            PROPERTIES_SIGNAL_CHANGED,
                                                            adapter->path,
                                                            INTERFACE_ADAPTER,
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            on_adapter_prop_changed,
                                                            adapter,
                                                            NULL);

    adapter->iface_added = g_dbus_connection_signal_subscribe(adapter->dbus_conn,
                                                              BLUEZ_DBUS,
                                                              INTERFACE_OBJECT_MANAGER,
                                                              OBJECT_MANAGER_SIGNAL_INTERFACE_ADDED,
                                                              NULL,
                                                              NULL,
                                                              G_DBUS_SIGNAL_FLAGS_NONE,
                                                              on_interface_appeared,
                                                              adapter,
                                                              NULL);

    adapter->iface_removed = g_dbus_connection_signal_subscribe(adapter->dbus_conn,
                                                            BLUEZ_DBUS,
                                                            INTERFACE_OBJECT_MANAGER,
                                                            OBJECT_MANAGER_SIGNAL_INTERFACE_REMOVED,
                                                            NULL,
                                                            NULL,
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            on_interface_disappeared,
                                                            adapter,
                                                            NULL);

    adapter->device_prop_changed = g_dbus_connection_signal_subscribe(adapter->dbus_conn,
                                                            BLUEZ_DBUS,
                                                            INTERFACE_PROPERTIES,
                                                            PROPERTIES_SIGNAL_CHANGED,
                                                            NULL,
                                                            INTERFACE_DEVICE,
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            on_device_prop_changed,
                                                            adapter,
                                                            NULL);

    adapter->local_bis_src_transport_prop_changed = g_dbus_connection_signal_subscribe(
                                                            adapter->dbus_conn,
                                                            BLUEZ_DBUS,
                                                            INTERFACE_PROPERTIES,
                                                            PROPERTIES_SIGNAL_CHANGED,
                                                            NULL,
                                                            INTERFACE_MEDIA_TRANSPORT,
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            on_local_bis_src_transport_prop_changed,
                                                            adapter,
                                                            NULL);
}

static void lm_adapter_unsubscribe_signal(lm_adapter_t *adapter)
{
    g_assert(adapter);

    g_dbus_connection_signal_unsubscribe(adapter->dbus_conn, adapter->device_prop_changed);
    adapter->device_prop_changed = 0;
    g_dbus_connection_signal_unsubscribe(adapter->dbus_conn, adapter->adapter_prop_changed);
    adapter->adapter_prop_changed = 0;
    g_dbus_connection_signal_unsubscribe(adapter->dbus_conn, adapter->iface_added);
    adapter->iface_added = 0;
    g_dbus_connection_signal_unsubscribe(adapter->dbus_conn, adapter->iface_removed);
    adapter->iface_removed = 0;
    g_dbus_connection_signal_unsubscribe(adapter->dbus_conn, adapter->local_bis_src_transport_prop_changed);
    adapter->local_bis_src_transport_prop_changed = 0;
}

static lm_adapter_t *lm_adapter_get_adapter_by_path(GPtrArray *adapters, const gchar *path) {
    g_assert(adapters != NULL);
    g_assert(path != NULL);
    g_assert(strlen(path) > 0);

    for (guint i = 0; i < adapters->len; i++) {
        lm_adapter_t *adapter = g_ptr_array_index(adapters, i);
        const gchar *adapter_path = lm_adapter_get_path(adapter);
        if (g_str_has_prefix(path, adapter_path)) {
            return adapter;
        }
    }
    return NULL;
}

static lm_adapter_t *lm_adapter_create(GDBusConnection *connection, const gchar *path) {
    g_assert(connection);
    g_assert(path);
    g_assert(strlen(path) > 0);

    lm_adapter_t *adapter = g_new0(lm_adapter_t, 1);
    adapter->dbus_conn = connection;
    adapter->path = g_strdup(path);
    adapter->alias = NULL;
    adapter->address = NULL;
    adapter->power_state = LM_ADAPTER_POWER_OFF;
    adapter->discovery_state = LM_ADAPTER_DISCOVERY_STOPPED;
    adapter->discovery_filter = NULL;
    adapter->device_cache = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                  g_free, (GDestroyNotify) lm_device_destroy);
    adapter->local_bis_src_transports = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                  g_free, (GDestroyNotify) lm_transport_destroy);
    adapter->user_data = NULL;

    lm_adapter_subscribe_signal(adapter);
    return adapter;
}

void lm_adapter_destroy(lm_adapter_t *adapter)
{
    g_assert(adapter);

    lm_log_info(TAG, "destroy adapter '%s'", adapter->path);

    lm_adapter_unsubscribe_signal(adapter);

    if (adapter->discovery_filter)
        lm_adapter_free_discovery_filter(adapter);
    if (adapter->path)
        g_free((void *)adapter->path);
    if (adapter->address)
        g_free((void *)adapter->address);
    if (adapter->alias)
        g_free((void *)adapter->alias);
    g_hash_table_destroy(adapter->device_cache);
    g_hash_table_destroy(adapter->local_bis_src_transports);
    g_free(adapter);
}

static GPtrArray *lm_adapter_find_all(GDBusConnection *dbus_conn) {
    g_assert(dbus_conn);

    GPtrArray *adapter_array = g_ptr_array_new();
    lm_log_info(TAG, "finding adapter");

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(dbus_conn,
                                                   BLUEZ_DBUS,
                                                   "/",
                                                   INTERFACE_OBJECT_MANAGER,
                                                   OBJECT_MANAGER_METHOD_GET_MANAGED_OBJECTS,
                                                   NULL,
                                                   G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                                                   G_DBUS_CALL_FLAGS_NONE,
                                                   BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                                                   NULL,
                                                   &error);

    if (result) {
        GVariantIter *iter;
        const gchar *object_path;
        GVariant *ifaces_and_properties;

        g_assert(g_str_equal(g_variant_get_type_string(result), "(a{oa{sa{sv}}})"));
        g_variant_get(result, "(a{oa{sa{sv}}})", &iter);
        while (g_variant_iter_loop(iter, "{&o@a{sa{sv}}}", &object_path, &ifaces_and_properties)) {
            const gchar *interface_name;
            GVariant *properties;
            GVariantIter iter2;

            lm_log_debug(TAG, "obj_path '%s'", object_path);
            g_variant_iter_init(&iter2, ifaces_and_properties);
            while (g_variant_iter_loop(&iter2, "{&s@a{sv}}", &interface_name, &properties)) {
                if (g_str_equal(interface_name, INTERFACE_ADAPTER)) {
                    lm_adapter_t *adapter = lm_adapter_create(dbus_conn, object_path);
                    lm_log_info(TAG, "found adapter '%s'", object_path);

                    gchar *property_name;
                    GVariantIter iter3;
                    GVariant *property_value;
                    g_variant_iter_init(&iter3, properties);
                    while (g_variant_iter_loop(&iter3, "{&sv}", &property_name, &property_value)) {
                        lm_adapter_update_property(adapter, property_name, property_value);
                    }
                    g_ptr_array_add(adapter_array, adapter);
                } else if (g_str_equal(interface_name, INTERFACE_DEVICE)) {
                    lm_adapter_t *adapter = lm_adapter_get_adapter_by_path(adapter_array, object_path);
                    lm_device_t *device = lm_device_create_with_path(adapter, object_path);
                    g_hash_table_insert(adapter->device_cache, g_strdup(lm_device_get_path(device)), device);

                    gchar *property_name;
                    GVariantIter iter4;
                    GVariant *property_value;
                    g_variant_iter_init(&iter4, properties);
                    while (g_variant_iter_loop(&iter4, "{&sv}", &property_name, &property_value)) {
                        lm_device_update_property(device, property_name, property_value);
                    }
                    lm_log_info(TAG, "found device '%s' '%s'", object_path, lm_device_get_name(device));
                } else if (g_str_equal(interface_name, INTERFACE_MEDIA_TRANSPORT)) {
                    lm_adapter_t *adapter = lm_adapter_get_adapter_by_path(adapter_array, object_path);
                    lm_transport_t *transport = lm_transport_create(NULL, object_path);

                    gchar *property_name;
                    GVariantIter iter5;
                    GVariant *property_value;
                    g_variant_iter_init(&iter5, properties);
                    while (g_variant_iter_loop(&iter5, "{&sv}", &property_name, &property_value)) {
                        lm_transport_update_property(transport, property_name, property_value);
                    }
                    if (g_str_equal(lm_transport_get_uuid(transport), BCAST_AUDIO_AUNOUNCEMENT_SERVICE_UUID)) {
                        g_hash_table_insert(adapter->local_bis_src_transports, g_strdup(lm_transport_get_path(transport)), transport);
                        lm_log_info(TAG, "found bis source transport '%s'", object_path);
                    } else {
                        lm_transport_destroy(transport);
                        lm_log_warn(TAG, "unknown transport '%s'", object_path);
                    }
                } else if (g_str_equal(interface_name, INTERFACE_BEARER_LE) ||
                            g_str_equal(interface_name, INTERFACE_BEARER_BREDR)) {
                    lm_adapter_t *adapter = lm_adapter_get_adapter_by_path(adapter_array, object_path);

                    lm_device_t *device = lm_device_lookup_by_path(adapter, object_path);
                    if (!device)
                        continue;

                    lm_device_bearer_type_t type = g_str_equal(interface_name, INTERFACE_BEARER_LE) ? LM_DEVICE_BEARER_LE : LM_DEVICE_BEARER_BREDR;
                    lm_bearer_t *bearer = lm_bearer_create(device, type, object_path);
                    gchar *property_name;
                    GVariantIter iter6;
                    GVariant *property_value;

                    g_variant_iter_init(&iter6, properties);
                    while (g_variant_iter_loop(&iter6, "{&sv}", &property_name, &property_value)) {
                        lm_bearer_update_property(bearer, property_name, property_value);
                    }
                    lm_device_add_bearer(device, bearer);
                    lm_log_info(TAG, "found bearer '%s' on device '%s'", lm_bearer_get_name(bearer), lm_device_get_name(device));
                }
            }
        }

        if (iter) {
            g_variant_iter_free(iter);
        }
        g_variant_unref(result);
    }

    if (error) {
        lm_log_error(TAG, "Error GetManagedObjects: %s", error->message);
        g_clear_error(&error);
    }

    lm_log_info(TAG, "found %d adapter", adapter_array->len);

    return adapter_array;
}

lm_adapter_t *lm_adapter_get_default(void)
{
    lm_adapter_t *adapter = NULL;
    GDBusConnection *dbus_conn = lm_get_gdbus_connection();
    if (!dbus_conn) {
        lm_log_error(TAG, "no dbus connection, please call lm_init() first!");
        return NULL;
    }
    GPtrArray *adapters = lm_adapter_find_all(dbus_conn);
    if (adapters && adapters->len > 0) {
        adapter = g_ptr_array_index(adapters, 0);
        for (guint i = 1; i < adapters->len; i++) {
            lm_adapter_destroy(g_ptr_array_index(adapters, i));
        }
        g_ptr_array_free(adapters, TRUE);
    }

    return adapter;
}

static void lm_adapter_set_property_cb(__attribute__((unused)) GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data) {
    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(adapter->dbus_conn, res, &error);
    if (value) {
        g_variant_unref(value);
    }

    if (error) {
        lm_log_error(TAG, "failed to set adapter property (error %d: %s)", error->code, error->message);
        g_clear_error(&error);
    }
}

static lm_status_t lm_adapter_set_property(lm_adapter_t *adapter, const gchar *property, GVariant *value) {
    g_assert(adapter);
    g_assert(property);
    g_assert(value);

    g_dbus_connection_call(adapter->dbus_conn,
                           BLUEZ_DBUS,
                           adapter->path,
                           INTERFACE_PROPERTIES,
                           PROPERTIES_METHOD_SET,
                           g_variant_new("(ssv)", INTERFACE_ADAPTER, property, value),
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_adapter_set_property_cb,
                           adapter);
    return LM_STATUS_SUCCESS;
}

gboolean lm_adapter_is_power_on(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->powered;
}

lm_adapter_power_state_t lm_adapter_get_power_state(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->power_state;
}

lm_status_t lm_adapter_power_on(lm_adapter_t *adapter)
{
    g_assert(adapter);
    if (adapter->powered) {
        lm_log_warn(TAG, "lm_adapter_t '%s' is already powered on", adapter->path);
        return LM_STATUS_SUCCESS;
    }

    return lm_adapter_set_property(adapter, ADAPTER_PROPERTY_POWERED, g_variant_new("b", TRUE));
}

lm_status_t lm_adapter_power_off(lm_adapter_t *adapter)
{
    g_assert(adapter);
    if (!adapter->powered) {
        lm_log_warn(TAG, "lm_adapter_t '%s' is already powered off", adapter->path);
        return LM_STATUS_SUCCESS;
    }

    return lm_adapter_set_property(adapter, ADAPTER_PROPERTY_POWERED, g_variant_new("b", FALSE));
}

lm_adapter_discovery_state_t lm_adapter_get_discovery_state(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->discovery_state;
}

static gboolean lm_adapter_discovery_timeout_cb(gpointer user_data)
{
    lm_adapter_t *adapter = (lm_adapter_t *)user_data;
    g_assert(adapter != NULL);

    lm_log_info(TAG, "adapter '%s' discovery timeout reached(%d s)",
            adapter->path, adapter->discovery_filter->timeout);

    lm_adapter_stop_discovery(adapter);

    lm_adapter_discovery_complete_ind_t complete_ind = {
        .adapter = adapter
    };
    lm_app_event_callback(LM_ADAPTER_DISCOVERY_COMPLETE_IND,
                        LM_STATUS_SUCCESS,
                        (void *)&complete_ind);

    adapter->discovery_timer_id = 0;

    adapter->discovery_devices_found = 0;

    return FALSE;
}

static void lm_adapter_set_discovery_state(lm_adapter_t *adapter,
                    lm_adapter_discovery_state_t discovery_state)
{
    g_assert(adapter != NULL);

    if (adapter->discovery_state == discovery_state)
        return;

    adapter->discovery_state = discovery_state;
    lm_log_info(TAG, "adapter '%s' discovery state changed to '%s'",
                 adapter->path, LM_ADAPTER_GET_DISCOVERY_STATE_NAME(adapter));

    lm_adapter_discovery_state_change_ind_t ind = {
        .adapter = adapter
    };
    lm_app_event_callback(LM_ADAPTER_DISCOVERY_STATE_CHANGE_IND,
                          LM_STATUS_SUCCESS,
                          (void *)&ind);

    switch (discovery_state) {
        case LM_ADAPTER_DISCOVERY_STARTING:
            break;
        case LM_ADAPTER_DISCOVERY_STARTED:
            if (!adapter->discovery_timer_id && adapter->discovery_filter &&
                adapter->discovery_filter->timeout > 0) {
                adapter->discovery_timer_id = g_timeout_add_seconds(
                    adapter->discovery_filter->timeout,
                    lm_adapter_discovery_timeout_cb,
                    adapter);
            }
            break;
        case LM_ADAPTER_DISCOVERY_STOPPING:
            break;
        case LM_ADAPTER_DISCOVERY_STOPPED:
            if (adapter->discovery_timer_id) {
                g_source_remove(adapter->discovery_timer_id);
                adapter->discovery_timer_id = 0;
            }
            adapter->discovery_devices_found = 0;
            break;
        default:
            break;
    }
}

static void lm_adapter_start_discovery_cb(__attribute__((unused)) GObject *source_object,
                                             GAsyncResult *res,
                                             gpointer user_data) {

    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter != NULL);

    GError *error = NULL;

    lm_log_debug(TAG, "%s", __func__);
    GVariant *value = g_dbus_connection_call_finish(adapter->dbus_conn, res, &error);

    if (error != NULL) {
        lm_log_error(TAG, "failed to call '%s' (error %d: %s)", ADAPTER_METHOD_START_DISCOVERY, error->code, error->message);
        const gchar *code_str = g_dbus_error_get_remote_error(error);
        if (g_strcmp0(code_str, "org.bluez.Error.InProgress") == 0)
            lm_adapter_set_discovery_state(adapter, LM_ADAPTER_DISCOVERY_STARTED);
        else
            adapter->discovery_state = LM_ADAPTER_DISCOVERY_STOPPED;
        g_clear_error(&error);
    } else {
        lm_adapter_set_discovery_state(adapter, LM_ADAPTER_DISCOVERY_STARTED);
    }

    if (value != NULL) {
        g_variant_unref(value);
    }
}

lm_status_t lm_adapter_start_discovery(lm_adapter_t *adapter)
{
    g_assert(adapter);

    if (adapter->discovery_state == LM_ADAPTER_DISCOVERY_STARTED) {
        return LM_STATUS_SUCCESS;
    } else if (adapter->discovery_state != LM_ADAPTER_DISCOVERY_STOPPED) {
        lm_log_warn(TAG, "adapter '%s' can not start discovery in state:'%s'",
            adapter->path, LM_ADAPTER_GET_DISCOVERY_STATE_NAME(adapter));
        return LM_STATUS_FAIL;
    }

    lm_adapter_set_discovery_state(adapter, LM_ADAPTER_DISCOVERY_STARTING);
    g_dbus_connection_call(adapter->dbus_conn,
                            BLUEZ_DBUS,
                            adapter->path,
                            INTERFACE_ADAPTER,
                            ADAPTER_METHOD_START_DISCOVERY,
                            NULL,
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                            NULL,
                            (GAsyncReadyCallback) lm_adapter_start_discovery_cb,
                            adapter);

    return LM_STATUS_SUCCESS;
}

static void lm_adapter_stop_discovery_cb(__attribute__((unused)) GObject *source_object,
                                            GAsyncResult *res,
                                            gpointer user_data) {
    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(adapter->dbus_conn, res, &error);

    if (error != NULL) {
        lm_log_error(TAG, "failed to call '%s' (error %d: %s)", ADAPTER_METHOD_STOP_DISCOVERY, error->code, error->message);
        const gchar *code_str = g_dbus_error_get_remote_error(error);
        if (g_strcmp0(code_str, "org.bluez.Error.InProgress") == 0)
            lm_adapter_set_discovery_state(adapter, LM_ADAPTER_DISCOVERY_STOPPED);
        else
            adapter->discovery_state = LM_ADAPTER_DISCOVERY_STARTED;
        g_clear_error(&error);
    } else {
        lm_adapter_set_discovery_state(adapter, LM_ADAPTER_DISCOVERY_STOPPED);
    }

    if (value != NULL)
        g_variant_unref(value);
}

lm_status_t lm_adapter_stop_discovery(lm_adapter_t *adapter)
{
    g_assert(adapter);

    if (adapter->discovery_state == LM_ADAPTER_DISCOVERY_STOPPED) {
        return LM_STATUS_SUCCESS;
    } else if (adapter->discovery_state != LM_ADAPTER_DISCOVERY_STARTED) {
        lm_log_warn(TAG, "adapter '%s' can not stop discovery in state:'%s'",
            adapter->path, LM_ADAPTER_GET_DISCOVERY_STATE_NAME(adapter));
        return LM_STATUS_FAIL;
    }
    lm_adapter_set_discovery_state(adapter, LM_ADAPTER_DISCOVERY_STOPPING);
    g_dbus_connection_call(adapter->dbus_conn,
                            BLUEZ_DBUS,
                            adapter->path,
                            INTERFACE_ADAPTER,
                            ADAPTER_METHOD_STOP_DISCOVERY,
                            NULL,
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                            NULL,
                            (GAsyncReadyCallback) lm_adapter_stop_discovery_cb,
                            adapter);
    return LM_STATUS_SUCCESS;
}

static void lm_adapter_call_method_cb(__attribute__((unused)) GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data)
{
    lm_adapter_method_call_ctx_t *ctx = (lm_adapter_method_call_ctx_t *) user_data;
    lm_adapter_t *adapter = ctx->adapter;
    g_assert(adapter != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(adapter->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to call adapter method '%s', error '%s'", ctx->method, error->message);
        g_clear_error(&error);
    }

    lm_adapter_method_call_ctx_free(ctx);
}

static void lm_adapter_call_method(lm_adapter_t *adapter, const gchar *method, GVariant *parameters)
{
    g_assert(adapter != NULL);
    g_assert(method != NULL);

    g_dbus_connection_call(adapter->dbus_conn,
                           BLUEZ_DBUS,
                           adapter->path,
                           INTERFACE_ADAPTER,
                           method,
                           parameters,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_adapter_call_method_cb,
                           lm_adapter_method_call_ctx_create(adapter, method));
}

void lm_adapter_set_discovery_filter(lm_adapter_t *adapter,
                                            gint16 rssi_threshold,
                                            const GPtrArray *service_uuids,
                                            const gchar *pattern,
                                            guint max_devices,
                                            guint timeout)
{
    g_assert(adapter != NULL);
    g_assert(rssi_threshold >= -127);
    g_assert(rssi_threshold <= 20);

    if (adapter->discovery_filter) {
        lm_adapter_free_discovery_filter(adapter);
    }
    adapter->discovery_filter = g_new0(lm_adapter_discovery_filter_t, 1);
    g_assert(adapter->discovery_filter != NULL);
    adapter->discovery_filter->services = g_ptr_array_new();
    adapter->discovery_filter->rssi = rssi_threshold;
    adapter->discovery_filter->pattern = g_strdup(pattern);
    adapter->discovery_filter->max_devices = max_devices;
    adapter->discovery_filter->timeout = timeout;

    GVariantBuilder *arguments = g_variant_builder_new(G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(arguments, "{sv}", "Transport", g_variant_new_string("le"));
    g_variant_builder_add(arguments, "{sv}", "RSSI", g_variant_new_int16(rssi_threshold));
    g_variant_builder_add(arguments, "{sv}", "DuplicateData", g_variant_new_boolean(FALSE));
    g_variant_builder_add(arguments, "{sv}", "Discoverable", g_variant_new_boolean(FALSE));

    if (pattern != NULL) {
        g_variant_builder_add(arguments, "{sv}", "Pattern", g_variant_new_string(pattern));
    }

    if (service_uuids != NULL && service_uuids->len > 0) {
        GVariantBuilder *uuids = g_variant_builder_new(G_VARIANT_TYPE_STRING_ARRAY);
        for (guint i = 0; i < service_uuids->len; i++) {
            gchar *uuid = g_ptr_array_index(service_uuids, i);
            g_assert(g_uuid_string_is_valid(uuid));
            g_variant_builder_add(uuids, "s", uuid);
            g_ptr_array_add(adapter->discovery_filter->services, g_strdup(uuid));
        }
        g_variant_builder_add(arguments, "{sv}", DEVICE_PROPERTY_UUIDS, g_variant_builder_end(uuids));
        g_variant_builder_unref(uuids);
    }

    GVariant *filter = g_variant_builder_end(arguments);
    g_variant_builder_unref(arguments);
    lm_adapter_call_method(adapter,
                            ADAPTER_METHOD_SET_DISCOVERY_FILTER,
                            g_variant_new_tuple(&filter, 1));
}

void lm_adapter_clear_discovery_filter(lm_adapter_t *adapter)
{
    g_assert(adapter);
    if (adapter->discovery_filter) {
        lm_adapter_free_discovery_filter(adapter);
        adapter->discovery_filter = NULL;
    }
    lm_adapter_call_method(adapter, ADAPTER_METHOD_SET_DISCOVERY_FILTER, NULL);
}

lm_status_t lm_adapter_discoverable_on(lm_adapter_t *adapter)
{
    g_assert(adapter);
    if (adapter->discoverable) {
        lm_log_warn(TAG, "adapter '%s' is already discoverable", adapter->path);
        return LM_STATUS_SUCCESS;
    }

    return lm_adapter_set_property(adapter, ADAPTER_PROPERTY_DISCOVERABLE,
                                        g_variant_new("b", TRUE));
}

lm_status_t lm_adapter_discoverable_off(lm_adapter_t *adapter)
{
    g_assert(adapter);
    if (!adapter->discoverable) {
        lm_log_warn(TAG, "adapter '%s' is already undiscoverable", adapter->path);
        return LM_STATUS_SUCCESS;
    }

    return lm_adapter_set_property(adapter, ADAPTER_PROPERTY_DISCOVERABLE,
                                        g_variant_new("b", FALSE));
}

gboolean lm_adapter_is_discoverable(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->discoverable;
}

lm_status_t lm_adapter_connectable_on(lm_adapter_t *adapter)
{
    g_assert(adapter);
    if (adapter->connectable) {
        lm_log_warn(TAG, "adapter '%s' is already connectable", adapter->path);
        return LM_STATUS_SUCCESS;
    }

    return lm_adapter_set_property(adapter, ADAPTER_PROPERTY_CONNECTABLE,
                                        g_variant_new("b", TRUE));
}

lm_status_t lm_adapter_connectable_off(lm_adapter_t *adapter)
{
    g_assert(adapter);
    if (!adapter->connectable) {
        lm_log_warn(TAG, "adapter '%s' is already unconnectable", adapter->path);
        return LM_STATUS_SUCCESS;
    }

    return lm_adapter_set_property(adapter, ADAPTER_PROPERTY_CONNECTABLE,
                                        g_variant_new("b", FALSE));
}

gboolean lm_adapter_is_connectable(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->connectable;
}

const gchar *lm_adapter_get_path(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->path;
}

lm_status_t lm_adapter_set_alias(lm_adapter_t *adapter, const gchar *alias)
{
    g_assert(adapter);
    g_assert(alias);

    return lm_adapter_set_property(adapter, ADAPTER_PROPERTY_ALIAS,
                                        g_variant_new("s", alias));
}

const gchar *lm_adapter_get_alias(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->alias;
}

const gchar *lm_adapter_get_address(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->address;
}

GHashTable *lm_adapter_get_device_cache(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->device_cache;
}

void lm_adapter_foreach_device(lm_adapter_t *adapter,
                lm_adapter_device_func_t func,
                void *user_data)
{
    g_assert (adapter != NULL);
    g_assert (func != NULL);
    GList *all_devices, *l;

    all_devices = g_hash_table_get_values(adapter->device_cache);
    if (!all_devices)
        return;

    for (l = all_devices; l; l = l->next)
        func(l->data, user_data);

    g_list_free(all_devices);
}

static void foreach_connected_device(lm_device_t *device, void *user_data)
{
    g_assert(user_data != NULL);
    GList **out_list = (GList **)user_data;

    if (lm_device_get_conn_state(device) == LM_DEVICE_CONNECTED &&
        !lm_device_is_bcast_device(device)) {
        *out_list = g_list_append(*out_list, device);
    }
}

void lm_adapter_get_connected_devices(lm_adapter_t *adapter, GList **out_list)
{
    g_assert(adapter != NULL);
    g_assert(out_list != NULL);

    lm_adapter_foreach_device(adapter, foreach_connected_device, out_list);
}

static void foreach_paired_device(lm_device_t *device, void *user_data)
{
    g_assert(user_data != NULL);
    GList **out_list = (GList **)user_data;

    if (lm_device_get_bonding_state(device) == LM_DEVICE_BONDED &&
        !lm_device_is_bcast_device(device)) {
        *out_list = g_list_append(*out_list, device);
    }
}

void lm_adapter_get_paired_devices(lm_adapter_t *adapter, GList **out_list)
{
    g_assert(adapter != NULL);
    g_assert(out_list != NULL);

    lm_adapter_foreach_device(adapter, foreach_paired_device, out_list);
}

static void foreach_bcast_source_device(lm_device_t *device, void *user_data)
{
    g_assert(user_data != NULL);
    GList **out_list = (GList **)user_data;

    if (lm_device_is_bcast_device(device))
        *out_list = g_list_append(*out_list, device);
}

void lm_adapter_get_bcast_source_devices(lm_adapter_t *adapter, GList **out_list)
{
    g_assert(adapter != NULL);
    g_assert(out_list != NULL);

    lm_adapter_foreach_device(adapter,
                              foreach_bcast_source_device,
                              out_list);
}

GDBusConnection *lm_adapter_get_dbus_conn(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->dbus_conn;
}

static void lm_start_adv_cb(__attribute__((unused)) GObject *source_object,
                                               GAsyncResult *res,
                                               gpointer user_data) {
    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(adapter->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to register advertisement (error %d: %s)", error->code, error->message);
        g_clear_error(&error);
    } else {
        lm_log_info(TAG, "started advertising ('%s')", lm_adv_get_local_name(adapter->adv));
        adapter->advertising = TRUE;
    }
}

lm_status_t lm_adapter_start_adv(lm_adapter_t *adapter, lm_adv_t *adv)
{
    g_assert(adapter != NULL);
    g_assert(adv != NULL);

    if (adapter->adv) {
        lm_log_warn(TAG, "adapter '%s' has already started adv", adapter->path);
        return LM_STATUS_BUSY;
    }

    lm_status_t status = lm_adv_register(adv);
    if (LM_STATUS_SUCCESS != status) {
        lm_log_error(TAG, "register adv fail %d", status);
        return status;
    }

    adapter->adv = adv;

    g_dbus_connection_call(adapter->dbus_conn,
                        BLUEZ_DBUS,
                        adapter->path,
                        INTERFACE_ADV_MANAGER,
                        ADV_MANAGER_METHOD_REGISTER,
                        g_variant_new("(oa{sv})", lm_adv_get_path(adv), NULL),
                        NULL,
                        G_DBUS_CALL_FLAGS_NONE,
                        BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                        NULL,
                        (GAsyncReadyCallback) lm_start_adv_cb,
                        adapter);

    return LM_STATUS_SUCCESS;
}

static void lm_stop_adv_cb(__attribute__((unused)) GObject *source_object,
                                               GAsyncResult *res,
                                               gpointer user_data) {
    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(adapter->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to unregister advertisement (error %d: %s)", error->code, error->message);
        g_clear_error(&error);
    } else {
        lm_adv_unregister(adapter->adv);
        lm_log_info(TAG, "stopped advertising");
        adapter->adv = NULL;
        adapter->advertising = FALSE;
    }
}

lm_status_t lm_adapter_stop_adv(lm_adapter_t *adapter, lm_adv_t *adv)
{
    g_assert(adapter != NULL);
    g_assert(adv != NULL);

    if (!adapter->adv) {
        lm_log_error(TAG, "not advertising");
        return LM_STATUS_FAIL;
    }

    g_dbus_connection_call(adapter->dbus_conn,
                        BLUEZ_DBUS,
                        adapter->path,
                        INTERFACE_ADV_MANAGER,
                        ADV_MANAGER_METHOD_UNREGISTER,
                        g_variant_new("(o)", lm_adv_get_path(adv)),
                        NULL,
                        G_DBUS_CALL_FLAGS_NONE,
                        BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                        NULL,
                        (GAsyncReadyCallback) lm_stop_adv_cb,
                        adapter);

    return LM_STATUS_SUCCESS;
}

gboolean lm_adapter_is_advertising(lm_adapter_t *adapter)
{
    g_assert(adapter);
    return adapter->advertising;
}

lm_status_t lm_adapter_remove_device(lm_adapter_t *adapter, lm_device_t *device)
{
    g_assert(device);
    g_assert (adapter);

    lm_log_debug(TAG, "removing '%s' '%s'", lm_device_get_name(device), lm_device_get_address(device));
    lm_adapter_call_method(adapter, ADAPTER_METHOD_REMOVE_DEVICE,
                                      g_variant_new("(o)", lm_device_get_path(device)));
    return LM_STATUS_SUCCESS;
}

void lm_adapter_get_local_bcast_source_transports(lm_adapter_t *adapter, GPtrArray *array)
{
    g_assert(adapter != NULL);
    g_assert(array != NULL);
    GHashTableIter hash_iter;
    gpointer key, value;

    g_ptr_array_set_size(array, 0);

    g_hash_table_iter_init(&hash_iter, adapter->local_bis_src_transports);
    while (g_hash_table_iter_next(&hash_iter, &key, &value)) {
        lm_transport_t *transport = (lm_transport_t *)value;
        if (transport)
            g_ptr_array_add(array, transport);
    }
}

static void lm_register_gatt_srv_cb(__attribute__((unused)) GObject *source_object,
                                           GAsyncResult *res,
                                           gpointer user_data) {
    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(adapter->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to register gatt server (error %d: %s)", error->code, error->message);
        g_clear_error(&error);
    } else {
         lm_log_debug(TAG, "successfully registered gatt server");
    }
}

lm_status_t lm_adapter_register_gatt_server(lm_adapter_t *adapter, lm_gatt_server_t *gatt_server)
{
    g_assert(adapter != NULL);
    g_assert(gatt_server != NULL);

    g_dbus_connection_call(adapter->dbus_conn,
                           BLUEZ_DBUS,
                           adapter->path,
                           INTERFACE_GATT_MANAGER,
                           GATT_MANAGER_METHOD_REG_APP,
                           g_variant_new("(oa{sv})", lm_gatt_server_get_path(gatt_server), NULL),
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_register_gatt_srv_cb, adapter);

    return LM_STATUS_SUCCESS;
}

static void lm_unregister_gatt_srv_cb(__attribute__((unused)) GObject *source_object,
                                             GAsyncResult *res,
                                             gpointer user_data)
{
    lm_adapter_t *adapter = (lm_adapter_t *) user_data;
    g_assert(adapter != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(adapter->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
         lm_log_error(TAG, "failed to unregister gatt server (error %d: %s)", error->code, error->message);
        g_clear_error(&error);
    } else {
         lm_log_debug(TAG, "successfully unregistered gatt server");
    }
}

lm_status_t lm_adapter_unregister_gatt_server(lm_adapter_t *adapter, lm_gatt_server_t *gatt_server)
{
    g_assert(adapter != NULL);
    g_assert(gatt_server != NULL);

    g_dbus_connection_call(adapter->dbus_conn,
                           BLUEZ_DBUS,
                           adapter->path,
                           INTERFACE_GATT_MANAGER,
                           GATT_MANAGER_METHOD_UREG_APP,
                           g_variant_new("(o)", lm_gatt_server_get_path(gatt_server)),
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_unregister_gatt_srv_cb, adapter);

    return LM_STATUS_SUCCESS;
}