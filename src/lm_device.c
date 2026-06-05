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
#include "lm_player.h"
#include "lm_player_priv.h"
#include "lm_transport.h"
#include "lm_transport_priv.h"
#include "lm_bearer_priv.h"
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

#define TAG "lm_device"

typedef struct {
    lm_device_t *device;
    lm_device_bearer_type_t type;
} lm_device_bearer_sub_ctx_t;

struct lm_device {
    GDBusConnection *dbus_conn; // Borrowed
    lm_adapter_t *adapter; // Borrowed
    const gchar *path; // Owned
    const gchar *name; // Owned
    const gchar *address; // Owned
    const gchar *address_type; // Owned
    const gchar *alias; // Owned
    gboolean services_resolved;
    gboolean service_discovery_started;
    gboolean paired;
    gint16 rssi;
    gboolean trusted;
    gint16 txpower;
    GHashTable *manufacturer_data; // Owned
    GHashTable *service_data; // Owned
    GList *uuids; // Owned
    guint mtu;
    GList *services_list; // Owned
    guint device_prop_changed;

    lm_device_conn_state_t conn_state;
    lm_device_bonding_state_t bonding_state;
    lm_device_conn_type_t conn_type; // Owned, indicates the connection type of the device

    guint player_prop_changed;
    guint transport_prop_changed;
    guint iface_added;
    guint iface_removed;
    guint bearers_prop_changed[LM_DEVICE_BEARER_MAX];
    guint bearers_disconnected[LM_DEVICE_BEARER_MAX];

    GHashTable *services; // Borrowed
    GHashTable *characteristics; // Borrowed
    GHashTable *descriptors; // Borrowed

    lm_player_t *active_player; // Owned, the player that is currently active on this device
    GHashTable *players;

    lm_transport_t *active_transport; // Owned, the transport that is currently active on this device
    GHashTable *transports;

    guint bcast_transport_collect_timer; // Timer for collecting remote broadcast transports
    lm_device_bcast_sync_state_t bcast_sync_state;
    lm_transport_audio_location_t audio_location;

    lm_bearer_t *bearers[LM_DEVICE_BEARER_MAX];

    /* memory self-management */
    gint ref_count;
};

typedef struct {
    lm_device_t *device;
    gchar *method;
} lm_device_method_call_ctx_t;

static const gchar *conn_state_names[] = {
    "DISCONNECTED",
    "CONNECTED",
    "CONNECTING",
    "DISCONNECTING"
};

static const gchar *bcast_sync_state_names[] = {
    "IDLE",
    "SYNCING",
    "SYNCED",
    "LOST",
    "TERMINATING"
};

static const gchar *disconn_reason_str[] = {
    "org.bluez.Reason.Unknown",
    "org.bluez.Reason.Timeout",
    "org.bluez.Reason.Local",
    "org.bluez.Reason.Remote",
    "org.bluez.Reason.Authentication",
    "org.bluez.Reason.Suspend"
};

static void lm_device_free_uuids(lm_device_t *device);
static void lm_device_free_manufacturer_data(lm_device_t *device);
static void lm_device_free_service_data(lm_device_t *device);
static void lm_device_set_conn_state(lm_device_t *device,
                                              lm_device_conn_state_t state);
static void lm_device_set_bcast_sync_state(lm_device_t *device,
                                              lm_device_bcast_sync_state_t state);
static void subscribe_device_prop_changed(lm_device_t *device);
static void unsubscribe_device_prop_changed(lm_device_t *device);
static void lm_device_extract_service(lm_device_t *device, const gchar *object_path, GVariant *properties);
static void lm_device_extract_characteristic(lm_device_t *device, const gchar *object_path, GVariant *properties);
static void lm_device_extract_descriptor(lm_device_t *device, const gchar *object_path, GVariant *properties);
static void lm_device_collect_gatt_tree(lm_device_t *device);

static lm_device_method_call_ctx_t *lm_device_method_call_ctx_create(lm_device_t *device,
                                                                const gchar *method) {
    g_assert(device != NULL);
    g_assert(method != NULL);

    lm_device_method_call_ctx_t *ctx = g_new0(lm_device_method_call_ctx_t, 1);
    ctx->device = device;
    ctx->method = g_strdup(method);

    return ctx;
}

static void lm_device_method_call_ctx_free(lm_device_method_call_ctx_t *ctx) {
    g_assert(ctx != NULL);

    if (ctx->method)
        g_free(ctx->method);

    g_free(ctx);
}

static lm_device_disconn_reason_t lm_device_string_to_disconn_reason(const gchar *str)
{
   if (!str)
        return LM_DEVICE_DISCONN_UNKNOWN;

    for (guint i = 0; i < G_N_ELEMENTS(disconn_reason_str); i++) {
        if (g_strcmp0(str, disconn_reason_str[i]) == 0) {
            return (lm_device_disconn_reason_t)i;
        }
    }
    return LM_DEVICE_DISCONN_UNKNOWN;
}

const gchar *lm_device_disconn_reason_to_string(lm_device_disconn_reason_t reason)
{
    return disconn_reason_str[reason];
}

static lm_device_bearer_sub_ctx_t *lm_device_bearer_sub_ctx_create(lm_device_t *device,
                lm_device_bearer_type_t type)
{
    g_assert(device);

    lm_device_bearer_sub_ctx_t *ctx = g_new0(lm_device_bearer_sub_ctx_t, 1);
    ctx->device = device;
    ctx->type = type;

    lm_log_debug(TAG, "'%s' bearer sub ctx create type %d", ctx->device->path, type);
    return ctx;
}

static void lm_device_bearer_sub_ctx_free(lm_device_bearer_sub_ctx_t *ctx)
{
    g_assert(ctx);

    lm_log_debug(TAG, "bearer sub ctx free type %d", ctx->type);
    g_free(ctx);
}

static lm_player_t *lm_device_find_player(lm_device_t *device, lm_player_profile_t profile)
{
    g_assert (device);

    lm_player_t *player = NULL;

    GList *all_players = g_hash_table_get_values(device->players);
    if (g_list_length(all_players) <= 0) {
        g_list_free(all_players);
        return NULL;
    }

    for (GList *iterator = all_players; iterator; iterator = iterator->next) {
        if (lm_player_get_profile((lm_player_t *) iterator->data) == profile) {
            player = (lm_player_t *) iterator->data;
            break;
        }
    }

    g_list_free(all_players);

    return player;
}

static void lm_device_update_active_player(lm_device_t *device)
{
    g_assert(device);

    lm_player_t *old_active = device->active_player;

    /* avrcp player gets the higher priority */
    lm_player_t *new_active = lm_device_find_player(device, LM_PLAYER_PROFILE_AVRCP);
    if (!new_active)
        new_active = lm_device_find_player(device, LM_PLAYER_PROFILE_MCP);

    if (old_active == new_active)
        return;

    device->active_player = new_active;

    if (device->active_player)
        lm_log_info(TAG, "active player updated to '%s'", lm_player_get_path(device->active_player));
}

static lm_transport_t *lm_device_find_transport(lm_device_t *device, lm_transport_profile_t profile)
{
    g_assert (device);

    lm_transport_t *transport = NULL;

    GList *all_trans = g_hash_table_get_values(device->transports);
    if (g_list_length(all_trans) <= 0) {
        g_list_free(all_trans);
        return NULL;
    }

    for (GList *iterator = all_trans; iterator; iterator = iterator->next) {
        if (lm_transport_get_profile((lm_transport_t *) iterator->data) == profile) {
            transport = (lm_transport_t *) iterator->data;
            break;
        }
    }

    g_list_free(all_trans);

    return transport;
}

static void lm_device_update_active_transport(lm_device_t *device)
{
    g_assert(device);

    lm_transport_t *old_active = device->active_transport;

    /* a2dp sink transport gets the higher priority */
    lm_transport_t *new_active = lm_device_find_transport(device, LM_TRANSPORT_PROFILE_A2DP_SINK);
    if (!new_active)
        new_active = lm_device_find_transport(device, LM_TRANSPORT_PROFILE_BAP_SINK);

    if (old_active == new_active)
        return;

    device->active_transport = new_active;

    if (device->active_transport)
        lm_log_info(TAG, "active transport updated to '%s' '%s'",
                    lm_transport_get_profile_name(device->active_transport),
                    lm_transport_get_path(device->active_transport));
}

static gboolean bcast_transport_collect_timer_cb(gpointer user_data)
{
    lm_status_t status = LM_STATUS_FAIL;
    lm_device_t *device = (lm_device_t *)user_data;
    lm_adapter_t *adapter = NULL;
    GList *connected_devices = NULL;
    g_assert(device);

    GPtrArray *bcast_transports = g_ptr_array_new();

    lm_device_get_transports(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK, bcast_transports);

    lm_log_info(TAG, "bcast sink transport num %d", bcast_transports->len);

    if (bcast_transports->len == 0)
        goto exit;

    lm_adapter_bcast_discovered_ind_t ind = {
        .method = LM_ADAPTER_BCAST_DISCOVERED_BY_ASSISTANT,
        .device = device,
        .bcast_transports = bcast_transports,
        .assistant = NULL
    };

    adapter = lm_device_get_adapter(device);
    lm_adapter_get_connected_devices(adapter, &connected_devices);

    if (!g_list_length(connected_devices)) {
        ind.method = LM_ADAPTER_BCAST_DISCOVERED_BY_SINK_SCAN;
    } else {
        for (GList *l = connected_devices; l; l = l->next) {
            lm_device_t *dev = (lm_device_t *)l->data;
            if (lm_device_get_conn_type(dev) & LM_DEVICE_CONN_LE) {
                ind.method = LM_ADAPTER_BCAST_DISCOVERED_BY_ASSISTANT;
                ind.assistant = dev;
                break;
            }
        }
    }

    lm_app_event_callback(LM_ADAPTER_BCAST_DISCOVERED_IND, LM_STATUS_SUCCESS, &ind);

    status = LM_STATUS_SUCCESS;

exit:
    if (bcast_transports)
        g_ptr_array_unref(bcast_transports);
    if (connected_devices)
        g_list_free(connected_devices);
    if (status == LM_STATUS_FAIL)
        return TRUE; /* FALSE:stop period timer; TRUE:keep period timer. */

    device->bcast_transport_collect_timer = 0;
    return FALSE;
}

static gboolean lm_device_is_bcast_sync_up(lm_device_t *device)
{
    GPtrArray *bcast_transports = NULL;
    GPtrArray *target =  NULL;
    lm_transport_t *transport;
    gboolean sync_up = FALSE;

    bcast_transports = g_ptr_array_new();
    lm_device_get_transports(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK, bcast_transports);
    if (!bcast_transports->len) {
        lm_log_error(TAG, "No BAP bcast sink transports on device '%s'", device->path);
        goto exit;
    }

    target =  g_ptr_array_new();
    for (guint i = 0; i < bcast_transports->len; i++) {
        transport = g_ptr_array_index(bcast_transports, i);
        if ((lm_transport_get_audio_location(transport) & device->audio_location) ||
                lm_transport_get_audio_location(transport) == LM_TRANSPORT_AUDIO_LOCATION_MONO)
            g_ptr_array_add(target, transport);
    }

    if (!target->len) {
        lm_log_error(TAG, "No matched audio location on device '%s'", device->path);
        goto exit;
    }

    for (guint i = 0; i < target->len; i++) {
        transport = g_ptr_array_index(target, i);
        if (lm_transport_get_state(transport) != LM_TRANSPORT_ACTIVE)
            goto exit;
    }

    sync_up = TRUE;

exit:
    if (bcast_transports)
        g_ptr_array_unref(bcast_transports);
    if (target)
        g_ptr_array_unref(target);

    return sync_up;
}

static void on_device_prop_changed(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender,
                                          __attribute__((unused)) const gchar *path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *signal,
                                          GVariant *parameters,
                                          void *user_data)
{
    GVariantIter *properties_changed = NULL;
    GVariantIter *properties_invalidated = NULL;
    const gchar *iface = NULL;
    const gchar *property_name = NULL;
    GVariant *property_value = NULL;

    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device != NULL);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
    g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);
    while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
        if (g_str_equal(property_name, DEVICE_PROPERTY_CONNECTED)) {
            lm_device_set_conn_state(device, g_variant_get_boolean(property_value));
            if (device->conn_state == LM_DEVICE_DISCONNECTED) {
                unsubscribe_device_prop_changed(device);
            }
        } else if (g_str_equal(property_name, DEVICE_PROPERTY_SERVICES_RESOLVED)) {
            device->services_resolved = g_variant_get_boolean(property_value);
            lm_log_debug(TAG, "ServicesResolved %s", device->services_resolved ? "true" : "false");
            if (device->services_resolved == TRUE && device->bonding_state != LM_DEVICE_BONDING) {
                lm_device_collect_gatt_tree(device);
            }

            if (device->services_resolved == FALSE && device->conn_state == LM_DEVICE_CONNECTED) {
                lm_device_set_conn_state(device, LM_DEVICE_DISCONNECTING);
            }
        } else if (g_str_equal(property_name, DEVICE_PROPERTY_PAIRED)) {
            device->paired = g_variant_get_boolean(property_value);
            lm_log_debug(TAG, "Paired %s", device->paired ? "true" : "false");
            lm_device_set_bonding_state(device, device->paired ? LM_DEVICE_BONDED : LM_DEVICE_BOND_NONE);

            // If gatt-tree has not been built yet, start building it
            if (device->services == NULL && device->services_resolved && !device->service_discovery_started) {
                lm_device_collect_gatt_tree(device);
            }
        }
    }

    if (properties_changed != NULL)
        g_variant_iter_free(properties_changed);

    if (properties_invalidated != NULL)
        g_variant_iter_free(properties_invalidated);
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
    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(oa{sa{sv}})"));
    g_variant_get(parameters, "(&oa{sa{sv}})", &object, &interfaces);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_interface_appeared, sender:%s, path:%s, interface:%s, signal:%s",
                sender_name, object, interface, signal_name);
#endif

    while (g_variant_iter_loop(interfaces, "{&s@a{sv}}", &interface_name, &properties)) {
        if (g_str_equal(interface_name, INTERFACE_MEDIA_PLAYER)) {
            // Skip this player if it is not for this device
            if (!g_str_has_prefix(object, device->path))
                continue;

            lm_log_debug(TAG, "media player '%s' added", object);

            lm_player_t *player = (lm_player_t *)g_hash_table_lookup(device->players, object);
            if (!player) {
                player = lm_player_create(device, object);
                g_hash_table_insert(device->players, g_strdup(object), player);
            }
            g_variant_iter_init(&iter, properties);
            while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
                lm_player_update_property(player, property_name, property_value);
            }
            lm_device_update_active_player(device);
        } else if (g_str_equal(interface_name, INTERFACE_MEDIA_TRANSPORT)) {
            // Skip this transport if it is not for this device
            if (!g_str_has_prefix(object, device->path))
                continue;

            lm_log_debug(TAG, "media transport '%s' added", object);

            lm_transport_t *transport = NULL;
            transport = (lm_transport_t *)g_hash_table_lookup(device->transports, object);
            if (!transport) {
                transport = lm_transport_create(device, object);
                g_hash_table_insert(device->transports, g_strdup(object), transport);
            }
            g_variant_iter_init(&iter, properties);
            while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
                lm_transport_update_property(transport, property_name, property_value);
            }
            lm_device_update_active_transport(device);

            if (lm_transport_get_profile(transport) == LM_TRANSPORT_PROFILE_BAP_BCAST_SINK) {
                /* Short PA sync complete */
                lm_log_debug(TAG, "bcast transport '%s' appeared", object);

                if (!device->bcast_transport_collect_timer) {
                    /* delay to wait all transports appear */
                    device->bcast_transport_collect_timer = g_timeout_add(100,
                                                                    bcast_transport_collect_timer_cb,
                                                                    device);
                }
            }
        } else if (g_str_equal(interface_name, INTERFACE_BEARER_LE) ||
                    g_str_equal(interface_name, INTERFACE_BEARER_BREDR)) {
            // Skip if not for this device
            if (!g_str_equal(object, device->path))
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
        }
    }

    if (interfaces)
        g_variant_iter_free(interfaces);
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

    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(oas)"));
    g_variant_get(parameters, "(&oas)", &object, &interfaces);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_interface_disappeared, sender:%s, path:%s, interface:%s, signal:%s",
               sender_name, object, interface, signal_name);
#endif

    while (g_variant_iter_loop(interfaces, "s", &interface_name)) {
        // lm_log_debug(TAG, "interface %s removed from object %s", interface_name, object);
        if (g_str_equal(interface_name, INTERFACE_MEDIA_PLAYER)) {
            // Skip this player if it is not for this device
            if (!g_str_has_prefix(object, device->path))
                continue;

            lm_log_debug(TAG, "media player '%s' removed", object);

            lm_player_t *player = (lm_player_t *)g_hash_table_lookup(device->players, object);
            if (!player)
                continue;

            g_hash_table_remove(device->players, object);
            lm_device_update_active_player(device);
        } else if (g_str_equal(interface_name, INTERFACE_MEDIA_TRANSPORT)) {
            // Skip this player if it is not for this device
            if (!g_str_has_prefix(object, device->path))
                continue;

            lm_log_debug(TAG, "media transport '%s' removed", object);

            lm_transport_t *transport = (lm_transport_t *)g_hash_table_lookup(device->transports, object);
            if (!transport)
                continue;

            lm_transport_profile_t profile = lm_transport_get_profile(transport);
            g_hash_table_remove(device->transports, object);
            lm_device_update_active_transport(device);
            if (profile == LM_TRANSPORT_PROFILE_BAP_BCAST_SINK) {
                lm_log_debug(TAG, "bcast transport '%s' disappeared", object);
                /* all BAP BCAST SINK transports disappeared */
                if (NULL == lm_device_find_transport(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK)) {
                    if (device->bcast_sync_state == LM_DEVICE_BCAST_SYNCED ||
                            device->bcast_sync_state == LM_DEVICE_BCAST_TERMINATING) {
                        lm_device_set_bcast_sync_state(device, LM_DEVICE_BCAST_LOST);
                        lm_device_bcast_sync_lost_ind_t ind = {.device = device};
                        lm_app_event_callback(LM_DEVICE_BCAST_SYNC_LOST_IND, LM_STATUS_SUCCESS, &ind);
                    } else if (device->bcast_sync_state == LM_DEVICE_BCAST_SYNCING) {
                        /* Transports were removed by BlueZ during Bcast syncing.
                         * Notify APP Bcast sync timeout.
                         */
                        lm_device_set_bcast_sync_state(device, LM_DEVICE_BCAST_IDLE);
                        lm_device_bcast_sync_up_ind_t ind = {.device = device};
                        lm_app_event_callback(LM_DEVICE_BCAST_SYNC_UP_IND, LM_STATUS_TIMEOUT, &ind);
                    }
                }
            }
        }
    }

    if (interfaces)
        g_variant_iter_free(interfaces);
}

static void on_player_prop_changed(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender,
                                          __attribute__((unused)) const gchar *path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *signal,
                                          GVariant *parameters,
                                          void *user_data)
{

    GVariantIter *properties_changed = NULL;
    GVariantIter *properties_invalidated = NULL;
    const gchar *iface = NULL;
    const gchar *property_name = NULL;
    GVariant *property_value = NULL;

    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_player_prop_changed, sender:%s, path:%s, interface:%s, signal:%s",
            sender, path, interface, signal);
#endif

    // Skip this player if it is not for this device
    if (!g_str_has_prefix(path, device->path))
        return;

    lm_player_t *player = (lm_player_t *)g_hash_table_lookup(device->players, path);
    if (!player) {
        lm_log_error(TAG, "player not found for path: '%s' on device '%s'", path, device->path);
        return;
    }

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
    g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);
    while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
        lm_player_update_property(player, property_name, property_value);
    }

    if (properties_changed)
        g_variant_iter_free(properties_changed);

    if (properties_invalidated)
        g_variant_iter_free(properties_invalidated);
}

static void on_transport_prop_changed(__attribute__((unused)) GDBusConnection *conn,
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
    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_transport_prop_changed, sender:%s, path:%s, interface:%s, signal:%s",
            sender, path, interface, signal);
#endif

    // Skip this player if it is not for this device
    if (!g_str_has_prefix(path, device->path))
        return;

    lm_transport_t *transport = (lm_transport_t *)g_hash_table_lookup(device->transports, path);
    if (!transport) {
        lm_log_error(TAG, "transport not found for path: %s on device '%s'", path, device->path);
        return;
    }

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
    g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);
    while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
        lm_transport_update_property(transport, property_name, property_value);
    }

    if (LM_TRANSPORT_PROFILE_BAP_BCAST_SINK == lm_transport_get_profile(transport)) {
        if (device->bcast_sync_state == LM_DEVICE_BCAST_SYNCING && lm_device_is_bcast_sync_up(device)) {
            lm_device_set_bcast_sync_state(device, LM_DEVICE_BCAST_SYNCED);
            lm_device_bcast_sync_up_ind_t ind = {.device = device};
            lm_app_event_callback(LM_DEVICE_BCAST_SYNC_UP_IND, LM_STATUS_SUCCESS, &ind);
        }
    }

    if (properties_changed)
        g_variant_iter_free(properties_changed);

    if (properties_invalidated)
        g_variant_iter_free(properties_invalidated);
}

static void on_bearer_prop_changed(__attribute__((unused)) GDBusConnection *conn,
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

    lm_device_bearer_sub_ctx_t *ctx = (lm_device_bearer_sub_ctx_t *) user_data;
    g_assert(ctx);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_bearer_prop_changed, sender:%s, path:%s, interface:%s, signal:%s",
            sender, path, interface, signal);
#endif

    // Skip if not for this device
    if (!g_str_equal(path, ctx->device->path))
        return;

    lm_bearer_t *bearer = ctx->device->bearers[ctx->type];
    if (!bearer) {
        lm_log_error(TAG, "bearer(%d) can not found on device '%s'", ctx->type, ctx->device->path);
        return;
    }

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
    g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);
    while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
        lm_bearer_update_property(bearer, property_name, property_value);
    }

    if (properties_changed)
        g_variant_iter_free(properties_changed);

    if (properties_invalidated)
        g_variant_iter_free(properties_invalidated);
}

static void on_bearer_disconnected(__attribute__((unused)) GDBusConnection *conn,
                                   __attribute__((unused)) const gchar *sender_name,
                                   __attribute__((unused)) const gchar *object_path,
                                   __attribute__((unused)) const gchar *interface_name,
                                   __attribute__((unused)) const gchar *signal_name,
                                   __attribute__((unused)) GVariant *parameters,
                                   gpointer user_data)
{
    const gchar *reason = NULL;
    const gchar *message = NULL;

    lm_device_bearer_sub_ctx_t *ctx = (lm_device_bearer_sub_ctx_t *) user_data;
    g_assert(ctx);

#if BLUEZ_DBUS_DEBUG
    lm_log_debug(TAG, "on_bearer_disconnected sender:%s, path:%s, interface:%s, signal:%s",
            sender_name, object_path, interface_name, signal_name);
#endif

    // Skip if not for this device
    if (!g_str_equal(object_path, ctx->device->path))
        return;

    lm_bearer_t *bearer = ctx->device->bearers[ctx->type];
    if (!bearer) {
        lm_log_error(TAG, "bearer '%s' can not found on device '%s'",
            lm_bearer_type_to_name(ctx->type), ctx->device->path);
        return;
    }

    g_variant_get(parameters, "(&s&s)", &reason, &message);

    lm_log_debug(TAG, "'%s' bearer '%s' disconnected: reason '%s', msg '%s'",
        ctx->device->path, lm_bearer_get_name(bearer), reason, message);

    if (lm_device_is_bcast_device(ctx->device)) {
        lm_log_warn(TAG, "device '%s' is bcast device, skipping disconnected indication", object_path);
        return;
    }

    if (ctx->type == LM_DEVICE_BEARER_LE)
        lm_device_reset_conn_type(ctx->device, LM_DEVICE_CONN_LE);
    else
        lm_device_reset_conn_type(ctx->device, LM_DEVICE_CONN_BREDR);

    lm_device_disconnected_ind_t ind = {
        .adapter = lm_device_get_adapter(ctx->device),
        .device = ctx->device,
        .type = ctx->type,
        .reason = lm_device_string_to_disconn_reason(reason)
    };

    lm_log_info(TAG, "device '%s' disconnected via '%s', reason '%s'", object_path, lm_bearer_get_name(bearer), reason);

    lm_app_event_callback(LM_DEVICE_DISCONNECTED_IND, LM_STATUS_SUCCESS, &ind);
}

static void subscribe_device_prop_changed(lm_device_t *device)
{
    if (!device->device_prop_changed) {
        device->device_prop_changed = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                                BLUEZ_DBUS,
                                                                INTERFACE_PROPERTIES,
                                                                PROPERTIES_SIGNAL_CHANGED,
                                                                device->path,
                                                                INTERFACE_DEVICE,
                                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                                on_device_prop_changed,
                                                                device,
                                                                NULL);
    }
}

static void unsubscribe_device_prop_changed(lm_device_t *device)
{
    if (device->device_prop_changed) {
        g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->device_prop_changed);
        device->device_prop_changed = 0;
    }
}

static void lm_device_subscribe_signal(lm_device_t *device)
{
    g_assert(device);

    const gchar *bearers_iface[LM_DEVICE_BEARER_MAX] = {
        INTERFACE_BEARER_LE,
        INTERFACE_BEARER_BREDR
    };

    if (!device->iface_added)
        device->iface_added = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                                BLUEZ_DBUS,
                                                                INTERFACE_OBJECT_MANAGER,
                                                                OBJECT_MANAGER_SIGNAL_INTERFACE_ADDED,
                                                                NULL,
                                                                NULL,
                                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                                on_interface_appeared,
                                                                device,
                                                                NULL);

    if (!device->iface_removed)
        device->iface_removed = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                                BLUEZ_DBUS,
                                                                INTERFACE_OBJECT_MANAGER,
                                                                OBJECT_MANAGER_SIGNAL_INTERFACE_REMOVED,
                                                                NULL,
                                                                NULL,
                                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                                on_interface_disappeared,
                                                                device,
                                                                NULL);

    if (!device->transport_prop_changed)
        device->transport_prop_changed = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                                BLUEZ_DBUS,
                                                                INTERFACE_PROPERTIES,
                                                                PROPERTIES_SIGNAL_CHANGED,
                                                                NULL,
                                                                INTERFACE_MEDIA_TRANSPORT,
                                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                                on_transport_prop_changed,
                                                                device,
                                                                NULL);

    if (!device->player_prop_changed)
        device->player_prop_changed = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                                BLUEZ_DBUS,
                                                                INTERFACE_PROPERTIES,
                                                                PROPERTIES_SIGNAL_CHANGED,
                                                                NULL,
                                                                INTERFACE_MEDIA_PLAYER,
                                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                                on_player_prop_changed,
                                                                device,
                                                                NULL);

    for (guint8 i = 0; i < LM_DEVICE_BEARER_MAX; i++) {
        if (!device->bearers_prop_changed[i])
            device->bearers_prop_changed[i] = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                BLUEZ_DBUS,
                                                INTERFACE_PROPERTIES,
                                                PROPERTIES_SIGNAL_CHANGED,
                                                NULL,
                                                bearers_iface[i],
                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                on_bearer_prop_changed,
                                                lm_device_bearer_sub_ctx_create(device, i),
                                                /* It is not guaranteed to be called synchronously when
                                                 * the signal is unsubscribed from, and may be called after
                                                 * connection has been destroyed.)
                                                 */
                                                (GDestroyNotify)lm_device_bearer_sub_ctx_free);
    }

    for (guint8 i = 0; i < LM_DEVICE_BEARER_MAX; i++) {
        if (!device->bearers_disconnected[i])
            device->bearers_disconnected[i] = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                BLUEZ_DBUS,
                                                bearers_iface[i],
                                                BEARER_SIGNAL_DISCONNECTED,
                                                NULL,
                                                NULL,
                                                G_DBUS_SIGNAL_FLAGS_NONE,
                                                on_bearer_disconnected,
                                                lm_device_bearer_sub_ctx_create(device, i),
                                                (GDestroyNotify)lm_device_bearer_sub_ctx_free);
    }
}

static void lm_device_unsubscribe_signal(lm_device_t *device)
{
    g_assert(device);

    unsubscribe_device_prop_changed(device);

    if (device->iface_added) {
        g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->iface_added);
        device->iface_added = 0;
    }

    if (device->iface_removed) {
        g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->iface_removed);
        device->iface_removed = 0;
    }

    if (device->transport_prop_changed) {
        g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->transport_prop_changed);
        device->transport_prop_changed = 0;
    }

    if (device->player_prop_changed) {
        g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->player_prop_changed);
        device->player_prop_changed = 0;
    }

    for (guint8 i = 0; i < LM_DEVICE_BEARER_MAX; i++) {
        if (device->bearers_prop_changed[i]) {
            g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->bearers_prop_changed[i]);
            device->bearers_prop_changed[i] = 0;
        }
    }
    for (guint8 i = 0; i < LM_DEVICE_BEARER_MAX; i++) {
        if (device->bearers_disconnected[i]) {
            g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->bearers_disconnected[i]);
            device->bearers_disconnected[i] = 0;
        }
    }
}

static void lm_device_extract_service(lm_device_t *device, const gchar *object_path, GVariant *properties) {
    g_assert(device != NULL);
    g_assert(object_path != NULL);
    g_assert(properties != NULL);

    gchar *uuid = NULL;
    const gchar *property_name;
    GVariantIter iter;
    GVariant *property_value;

    g_variant_iter_init(&iter, properties);
    while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
        if (g_str_equal(property_name, "UUID")) {
            uuid = g_strdup(g_variant_get_string(property_value, NULL));
        }
    }

    lm_gatt_svc_t *service = lm_gatt_svc_create(device, object_path, uuid);
    g_hash_table_insert(device->services, g_strdup(object_path), service);
    g_free(uuid);
}

static void lm_device_extract_characteristic(lm_device_t *device, const gchar *object_path, GVariant *properties) {
    g_assert(device != NULL);
    g_assert(object_path != NULL);
    g_assert(properties != NULL);

    lm_gatt_char_t *gatt_char = lm_gatt_char_create(device, object_path);

    const gchar *property_name;
    GVariantIter iter;
    GVariant *property_value;

    g_variant_iter_init(&iter, properties);
    while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
        if (g_str_equal(property_name, GATT_CHAR_PROPERTY_UUID)) {
            lm_gatt_char_set_uuid(gatt_char,
                                         g_variant_get_string(property_value, NULL));
        } else if (g_str_equal(property_name, GATT_CHAR_PROPERTY_SERVICE)) {
            lm_gatt_char_set_service_path(gatt_char,
                                                 g_variant_get_string(property_value, NULL));
        } else if (g_str_equal(property_name, GATT_CHAR_PROPERTY_FLAGS)) {
            lm_gatt_char_set_flags(gatt_char,
                                          lm_utils_g_variant_string_array_to_list(property_value));
        } else if (g_str_equal(property_name, GATT_CHAR_PROPERTY_NOTIFYING)) {
            lm_gatt_char_set_notifying(gatt_char,
                                              g_variant_get_boolean(property_value));
        } else if (g_str_equal(property_name, GATT_CHAR_PROPERTY_MTU)) {
            device->mtu = g_variant_get_uint16(property_value);
            lm_gatt_char_set_mtu(gatt_char, g_variant_get_uint16(property_value));
        }
    }

    // Get service and link the characteristic to the service
    lm_gatt_svc_t *service = g_hash_table_lookup(device->services,
                                           lm_gatt_char_get_service_path(gatt_char));
    if (service != NULL) {
        lm_gatt_svc_add_char(service, gatt_char);
        lm_gatt_char_set_service(gatt_char, service);
        g_hash_table_insert(device->characteristics, g_strdup(object_path), gatt_char);

        gchar *charString = lm_gatt_char_to_string(gatt_char);
        lm_log_debug(TAG, charString);
        g_free(charString);
    } else {
        lm_log_error(TAG, "could not find service %s",
                  lm_gatt_char_get_service_path(gatt_char));
    }
}

static void lm_device_extract_descriptor(lm_device_t *device, const gchar *object_path, GVariant *properties) {
    g_assert(device != NULL);
    g_assert(object_path != NULL);
    g_assert(properties != NULL);

    lm_gatt_desc_t *descriptor = lm_gatt_desc_create(device, object_path);

    const gchar *property_name;
    GVariantIter iter;
    GVariant *property_value;
    g_variant_iter_init(&iter, properties);
    while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
        if (g_str_equal(property_name, "UUID")) {
            lm_gatt_desc_set_uuid(descriptor, g_variant_get_string(property_value, NULL));
        } else if (g_str_equal(property_name, "Characteristic")) {
            lm_gatt_desc_set_char_path(descriptor,
                                          g_variant_get_string(property_value, NULL));
        } else if (g_str_equal(property_name, "Flags")) {
            lm_gatt_desc_set_flags(descriptor, lm_utils_g_variant_string_array_to_list(property_value));
        }
    }

    // Look up characteristic
    lm_gatt_char_t *gatt_char = g_hash_table_lookup(device->characteristics,
                                                         lm_gatt_desc_get_char_path(descriptor));
    if (gatt_char != NULL) {
        lm_gatt_char_add_desc(gatt_char, descriptor);
        lm_gatt_desc_set_char(descriptor, gatt_char);
        g_hash_table_insert(device->descriptors, g_strdup(object_path), descriptor);

        const gchar *descString = lm_gatt_desc_to_string(descriptor);
        lm_log_debug(TAG, descString);
        g_free((gchar *) descString);
    } else {
        lm_log_error(TAG, "could not find characteristic %s",
                  lm_gatt_desc_get_char_path(descriptor));
    }
}

static void lm_device_collect_gatt_tree_cb(__attribute__((unused)) GObject *source_object,
                                               GAsyncResult *res,
                                               gpointer user_data) {

    GError *error = NULL;
    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device != NULL);

    GVariant *result = g_dbus_connection_call_finish(device->dbus_conn, res, &error);

    if (result == NULL) {
        lm_log_error(TAG, "Unable to get result for GetManagedObjects");
        if (error != NULL) {
            lm_log_error(TAG, "call failed (error %d: %s)", error->code, error->message);
            g_clear_error(&error);
            return;
        }
    }

    GVariantIter *iter;
    const gchar *object_path;
    GVariant *ifaces_and_properties;
    if (result) {
        if (device->services != NULL) {
            g_hash_table_destroy(device->services);
        }
        device->services = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                 g_free, (GDestroyNotify) lm_gatt_svc_destroy);

        if (device->characteristics != NULL) {
            g_hash_table_destroy(device->characteristics);
        }
        device->characteristics = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                        g_free, (GDestroyNotify) lm_gatt_char_destroy);

        if (device->descriptors != NULL) {
            g_hash_table_destroy(device->descriptors);
        }
        device->descriptors = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                    g_free, (GDestroyNotify) lm_gatt_desc_destroy);

        g_assert(g_str_equal(g_variant_get_type_string(result), "(a{oa{sa{sv}}})"));
        g_variant_get(result, "(a{oa{sa{sv}}})", &iter);
        while (g_variant_iter_loop(iter, "{&o@a{sa{sv}}}", &object_path, &ifaces_and_properties)) {
            if (g_str_has_prefix(object_path, device->path)) {
                const gchar *interface_name;
                GVariant *properties;
                GVariantIter iter2;
                g_variant_iter_init(&iter2, ifaces_and_properties);
                while (g_variant_iter_loop(&iter2, "{&s@a{sv}}", &interface_name, &properties)) {
                    if (g_str_equal(interface_name, INTERFACE_GATT_SERVICE)) {
                        lm_device_extract_service(device, object_path, properties);
                    } else if (g_str_equal(interface_name, INTERFACE_GATT_CHAR)) {
                        lm_device_extract_characteristic(device, object_path, properties);
                    } else if (g_str_equal(interface_name, INTERFACE_GATT_DESC)) {
                        lm_device_extract_descriptor(device, object_path, properties);
                    }
                }
            }
        }

        if (iter != NULL) {
            g_variant_iter_free(iter);
        }
        g_variant_unref(result);
    }

    if (device->services_list != NULL) {
        g_list_free(device->services_list);
    }
    device->services_list = g_hash_table_get_values(device->services);

    lm_log_debug(TAG, "found %d services", g_list_length(device->services_list));

    lm_gatt_client_services_resolved_ind_t ind = {
        .device = device
    };
    lm_app_event_callback(LM_GATT_CLIENT_SERVICES_RESOLVED_IND, LM_STATUS_SUCCESS, &ind);
}

static void lm_device_collect_gatt_tree(lm_device_t *device) {
    g_assert(device != NULL);

    device->service_discovery_started = TRUE;
    g_dbus_connection_call(device->dbus_conn,
                           BLUEZ_DBUS,
                           "/",
                           INTERFACE_OBJECT_MANAGER,
                           OBJECT_MANAGER_METHOD_GET_MANAGED_OBJECTS,
                           NULL,
                           G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_device_collect_gatt_tree_cb,
                           device);
}

lm_device_t *lm_device_create_with_path(lm_adapter_t *adapter, const gchar *path)
{
    lm_device_t *device;

    g_assert(adapter && path);

    if ((device = g_new0(lm_device_t, 1)) == NULL)
        return NULL;

    device->adapter = adapter;
    device->dbus_conn = lm_adapter_get_dbus_conn(adapter);
    device->adapter = adapter;
    device->rssi = -255;
    device->txpower = -255;
    device->mtu = 23;
    device->path = g_strdup(path);

    // device->address = g_strdup_printf("%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
    //     addr.b[5], addr.b[4], addr.b[3], addr.b[2], addr.b[1], addr.b[0]);
    device->players = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                (GDestroyNotify) lm_player_destroy);
    device->transports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                (GDestroyNotify) lm_transport_destroy);
    for (guint8 i = 0; i < LM_DEVICE_BEARER_MAX; i++)
        device->bearers[i] = NULL;

    lm_device_subscribe_signal(device);

    lm_log_debug(TAG, "create device '%s'", path);
    return device;
}

void lm_device_destroy(lm_device_t *device)
{
    g_assert(device);

    lm_log_debug(TAG, "destroy device '%s'", device->path);

    if (lm_device_is_bcast_device(device))
        lm_log_info(TAG, "destroy bcast device '%s'", device->path);

    lm_device_removed_ind_t ind = {
        .adapter = device->adapter,
        .device = device
    };
    lm_app_event_callback(LM_DEVICE_REMOVED_IND, LM_STATUS_SUCCESS, &ind);

    lm_device_unsubscribe_signal(device);

    if (device->path)
        g_free((gpointer)device->path);

    if (device->name)
        g_free((gpointer)device->name);

    if (device->address)
        g_free((gpointer)device->address);

    if (device->address_type)
        g_free((gpointer)device->address_type);

    if (device->alias)
        g_free((gpointer)device->alias);

    if (device->transports)
        g_hash_table_destroy(device->transports);

    if (device->players)
        g_hash_table_destroy(device->players);

    if (device->bcast_transport_collect_timer) {
        g_source_remove(device->bcast_transport_collect_timer);
        device->bcast_transport_collect_timer = 0;
    }

    for (guint8 i = 0; i < LM_DEVICE_BEARER_MAX; i++) {
        if (device->bearers[i])
            lm_bearer_destroy(device->bearers[i]);
    }

    lm_device_free_manufacturer_data(device);

    lm_device_free_uuids(device);

    lm_device_free_service_data(device);

    g_free((gpointer)device);
}

lm_device_t *lm_device_lookup_by_path(lm_adapter_t *adapter, const gchar *path)
{
    g_assert(adapter && path);
    return (lm_device_t *)g_hash_table_lookup(lm_adapter_get_device_cache(adapter), path);
}

static void lm_device_load_properties_cb(__attribute__((unused)) GObject *source_object,
                                                      GAsyncResult *res,
                                                      gpointer user_data) {

    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device != NULL);

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_finish(device->dbus_conn, res, &error);

    if (error != NULL) {
        lm_log_error(TAG, "failed to call '%s' (error %d: %s)", "GetAll", error->code, error->message);
        g_clear_error(&error);
    }

    if (result != NULL) {
        GVariantIter *iter = NULL;
        const gchar *property_name = NULL;
        GVariant *property_value = NULL;

        g_assert(g_str_equal(g_variant_get_type_string(result), "(a{sv})"));
        g_variant_get(result, "(a{sv})", &iter);
        while (g_variant_iter_loop(iter, "{&sv}", &property_name, &property_value)) {
            lm_device_update_property(device, property_name, property_value);
        }

        if (iter != NULL) {
            g_variant_iter_free(iter);
        }
        g_variant_unref(result);
    }
}

void lm_device_load_properties(lm_device_t *device) {
    g_dbus_connection_call(device->dbus_conn,
                           BLUEZ_DBUS,
                           device->path,
                           INTERFACE_PROPERTIES,
                           PROPERTIES_METHOD_GET_ALL,
                           g_variant_new("(s)", INTERFACE_DEVICE),
                           G_VARIANT_TYPE("(a{sv})"),
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_device_load_properties_cb,
                           device);
}

static void lm_device_free_uuids(lm_device_t *device)
{
    if (device->uuids != NULL) {
        g_list_free_full(device->uuids, g_free);
        device->uuids = NULL;
    }
}

static void lm_device_free_manufacturer_data(lm_device_t *device)
{
    g_assert(device != NULL);

    if (device->manufacturer_data != NULL) {
        g_hash_table_destroy(device->manufacturer_data);
        device->manufacturer_data = NULL;
    }
}

static void lm_device_free_service_data(lm_device_t *device)
{
    g_assert(device != NULL);

    if (device->service_data != NULL) {
        g_hash_table_destroy(device->service_data);
        device->service_data = NULL;
    }
}

static void lm_device_set_conn_state(lm_device_t *device,
                                              lm_device_conn_state_t state)
{
    lm_device_conn_state_t old_state = device->conn_state;

    if (old_state == state)
        return;

    device->conn_state = state;

    lm_log_info(TAG, "'%s' conn state '%s' -> '%s'",
            device->path, conn_state_names[old_state], conn_state_names[state]);

    lm_device_conn_state_change_ind_t ind = {
        .adapter = device->adapter,
        .device = device
    };
    lm_app_event_callback(LM_DEVICE_CONN_STATE_CHANGE_IND, LM_STATUS_SUCCESS, &ind);
}

static void lm_device_set_bcast_sync_state(lm_device_t *device,
                                              lm_device_bcast_sync_state_t state)
{
    lm_device_bcast_sync_state_t old_state = device->bcast_sync_state;

    if (old_state == state)
        return;

    device->bcast_sync_state = state;

    lm_log_info(TAG, "'%s' bcast sync state '%s' -> '%s'",
            device->path,
            bcast_sync_state_names[old_state],
            bcast_sync_state_names[state]);
}

void lm_device_set_bonding_state(lm_device_t *device, lm_device_bonding_state_t bonding_state) {
    g_assert(device != NULL);

    device->bonding_state = bonding_state;
}

gchar *lm_device_to_string(const lm_device_t *device) {
    g_assert(device != NULL);

    // First build up uuids string
    GString *uuids = g_string_new("[");
    if (g_list_length(device->uuids) > 0) {
        for (GList *iterator = device->uuids; iterator; iterator = iterator->next) {
            g_string_append_printf(uuids, "%s, ", (gchar *) iterator->data);
        }
        g_string_truncate(uuids, uuids->len - 2);
    }
    g_string_append(uuids, "]");

    // Build up manufacturer data string
    GString *manufacturer_data = g_string_new("[");
    if (device->manufacturer_data != NULL && g_hash_table_size(device->manufacturer_data) > 0) {
        GHashTableIter iter;
        int *key;
        gpointer value;
        g_hash_table_iter_init(&iter, device->manufacturer_data);
        while (g_hash_table_iter_next(&iter, (gpointer) &key, &value)) {
            GByteArray *byte_array = (GByteArray *) value;
            GString *byteArrayString = lm_utils_g_byte_array_as_hex(byte_array);
            gint keyInt = *key;
            g_string_append_printf(manufacturer_data, "%04X -> %s, ", keyInt, byteArrayString->str);
            g_string_free(byteArrayString, TRUE);
        }
        g_string_truncate(manufacturer_data, manufacturer_data->len - 2);
    }
    g_string_append(manufacturer_data, "]");

    // Build up service data string
    GString *service_data = g_string_new("[");
    if (device->service_data != NULL && g_hash_table_size(device->service_data) > 0) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, device->service_data);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            GByteArray *byte_array = (GByteArray *) value;
            GString *byteArrayString = lm_utils_g_byte_array_as_hex(byte_array);
            g_string_append_printf(service_data, "%s -> %s, ", (gchar *) key, byteArrayString->str);
            g_string_free(byteArrayString, TRUE);
        }
        g_string_truncate(service_data, service_data->len - 2);
    }
    g_string_append(service_data, "]");

    gchar *result = g_strdup_printf(
            "device{name='%s', address='%s', address_type=%s, rssi=%d, uuids=%s, manufacturer_data=%s, service_data=%s, paired=%s, txpower=%d path='%s' }",
            device->name,
            device->address,
            device->address_type,
            device->rssi,
            uuids->str,
            manufacturer_data->str,
            service_data->str,
            device->paired ? "true" : "false",
            device->txpower,
            device->path
    );

    g_string_free(uuids, TRUE);
    g_string_free(manufacturer_data, TRUE);
    g_string_free(service_data, TRUE);
    return result;
}

lm_device_conn_state_t lm_device_get_conn_state(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->conn_state;
}

lm_device_conn_state_t lm_device_get_bearer_conn_state(const lm_device_t *device,
                lm_device_bearer_type_t type)
{
    g_assert(device);

    if (!device->bearers[type]) {
        lm_log_error(TAG, "there is no '%s' bearer on device '%s'",
                lm_bearer_type_to_name(type), device->path);
        return LM_STATUS_NOT_READY;
    }

    return lm_bearer_get_conn_state(device->bearers[type]);
}

const gchar *lm_device_conn_state_name(const lm_device_t *device) {
    g_assert(device != NULL);
    return conn_state_names[device->conn_state];
}

const gchar *lm_device_conn_state_to_string(lm_device_conn_state_t state)
{
    return conn_state_names[state];
}

const gchar *lm_device_get_address(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->address;
}

void lm_device_set_address(lm_device_t *device, const gchar *address) {
    g_assert(device != NULL);
    g_assert(address != NULL);

    g_free((gchar *) device->address);
    device->address = g_strdup(address);
}

const gchar *lm_device_get_address_type(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->address_type;
}

void lm_device_set_address_type(lm_device_t *device, const gchar *address_type) {
    g_assert(device != NULL);
    g_assert(address_type != NULL);

    g_free((gchar *) device->address_type);
    device->address_type = g_strdup(address_type);
}

const gchar *lm_device_get_alias(lm_device_t *device) {
    g_assert(device != NULL);
    return device->alias;
}

void lm_device_set_alias(lm_device_t *device, const gchar *alias) {
    g_assert(device != NULL);
    g_assert(alias != NULL);

    g_free((gchar *) device->alias);
    device->alias = g_strdup(alias);
}

const gchar *lm_device_get_name(lm_device_t *device) {
    g_assert(device != NULL);
    return device->name;
}

void lm_device_set_name(lm_device_t *device, const gchar *name) {
    g_assert(device != NULL);
    g_assert(name != NULL);
    g_assert(strlen(name) > 0);

    g_free((gchar *) device->name);
    device->name = g_strdup(name);
}

const gchar *lm_device_get_path(lm_device_t *device) {
    g_assert(device != NULL);
    return device->path;
}

void lm_device_set_path(lm_device_t *device, const gchar *path) {
    g_assert(device != NULL);
    g_assert(path != NULL);

    g_free((gchar *) device->path);
    device->path = g_strdup(path);
}

gboolean lm_device_get_paired(lm_device_t *device) {
    g_assert(device != NULL);
    return device->paired;
}

void lm_device_set_paired(lm_device_t *device, gboolean paired) {
    g_assert(device != NULL);
    device->paired = paired;
    lm_device_set_bonding_state(device, paired ? LM_DEVICE_BONDED : LM_DEVICE_BOND_NONE);
}

gint16 lm_device_get_rssi(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->rssi;
}

void lm_device_set_rssi(lm_device_t *device, gint16 rssi) {
    g_assert(device != NULL);
    device->rssi = rssi;
}

gboolean lm_device_get_trusted(lm_device_t *device) {
    g_assert(device != NULL);
    return device->trusted;
}

void lm_device_set_trusted(lm_device_t *device, gboolean trusted) {
    g_assert(device != NULL);
    device->trusted = trusted;
}

gint16 lm_device_get_txpower(lm_device_t *device) {
    g_assert(device != NULL);
    return device->txpower;
}

void lm_device_set_txpower(lm_device_t *device, gint16 txpower) {
    g_assert(device != NULL);
    device->txpower = txpower;
}

GList *lm_device_get_uuids(lm_device_t *device) {
    g_assert(device != NULL);
    return device->uuids;
}

void lm_device_set_uuids(lm_device_t *device, GList *uuids) {
    g_assert(device != NULL);

    lm_device_free_uuids(device);
    device->uuids = uuids;
}

GHashTable *lm_device_get_manufacturer_data(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->manufacturer_data;
}

void lm_device_set_manufacturer_data(lm_device_t *device, GHashTable *manufacturer_data) {
    g_assert(device != NULL);

    lm_device_free_manufacturer_data(device);
    device->manufacturer_data = manufacturer_data;
}

GHashTable *lm_device_get_service_data(const lm_device_t *device)
{
    g_assert(device != NULL);
    return device->service_data;
}

void lm_device_set_service_data(lm_device_t *device, GHashTable *service_data) {
    g_assert(device != NULL);

    lm_device_free_service_data(device);
    device->service_data = service_data;
}

GDBusConnection *lm_device_get_dbus_conn(const lm_device_t *device)
{
    g_assert(device != NULL);
    return device->dbus_conn;
}

lm_device_bonding_state_t lm_device_get_bonding_state(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->bonding_state;
}

lm_device_bonding_state_t lm_device_get_bearer_bonding_state(const lm_device_t *device,
                lm_device_bearer_type_t type)
{
    g_assert(device);

    if (!device->bearers[type]) {
        lm_log_error(TAG, "there is no '%s' bearer on device '%s'",
                lm_bearer_type_to_name(type), device->path);
        return LM_STATUS_NOT_READY;
    }

    return lm_bearer_get_bonding_state(device->bearers[type]);
}

lm_adapter_t *lm_device_get_adapter(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->adapter;
}

guint lm_device_get_mtu(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->mtu;
}

gboolean lm_device_has_service(const lm_device_t *device, const gchar *service_uuid) {
    g_assert(device != NULL);
    g_assert(g_uuid_string_is_valid(service_uuid));

    if (device->uuids != NULL && g_list_length(device->uuids) > 0) {
        for (GList *iterator = device->uuids; iterator; iterator = iterator->next) {
            if (g_str_equal(service_uuid, (gchar *) iterator->data)) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

void lm_device_update_property(lm_device_t *device, const gchar *property_name, GVariant *property_value) {
    // lm_log_debug(TAG, "%s property_name:%s",  __func__, property_name);
    if (g_str_equal(property_name, DEVICE_PROPERTY_ADDRESS)) {
        lm_device_set_address(device, g_variant_get_string(property_value, NULL));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_ADDRESS_TYPE)) {
        lm_device_set_address_type(device, g_variant_get_string(property_value, NULL));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_ALIAS)) {
        lm_device_set_alias(device, g_variant_get_string(property_value, NULL));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_CONNECTED)) {
            lm_device_set_conn_state(device,
                g_variant_get_boolean(property_value) ? LM_DEVICE_CONNECTED : LM_DEVICE_DISCONNECTED);
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_NAME)) {
        lm_device_set_name(device, g_variant_get_string(property_value, NULL));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_PAIRED)) {
        lm_device_set_paired(device, g_variant_get_boolean(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_RSSI)) {
        lm_device_set_rssi(device, g_variant_get_int16(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_TRUSTED)) {
        lm_device_set_trusted(device, g_variant_get_boolean(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_TXPOWER)) {
        lm_device_set_txpower(device, g_variant_get_int16(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_UUIDS)) {
        lm_device_set_uuids(device, lm_utils_g_variant_string_array_to_list(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_MANUFACTURER_DATA)) {
        GVariantIter *iter;
        g_variant_get(property_value, "a{qv}", &iter);

        GVariant *array;
        guint16 key;
        GHashTable *manufacturer_data = g_hash_table_new_full(g_int_hash, g_int_equal,
                                                              g_free, (GDestroyNotify) lm_utils_byte_array_free);
        while (g_variant_iter_loop(iter, "{qv}", &key, &array)) {
            gsize data_length = 0;
            guint8 *data = (guint8 *) g_variant_get_fixed_array(array, &data_length, sizeof(guint8));
            GByteArray *byte_array = g_byte_array_sized_new((guint) data_length);
            g_byte_array_append(byte_array, data, (guint) data_length);

            int *key_copy = g_new0 (gint, 1);
            *key_copy = key;

            g_hash_table_insert(manufacturer_data, key_copy, byte_array);
        }
        lm_device_set_manufacturer_data(device, manufacturer_data);
        g_variant_iter_free(iter);
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_SERVICE_DATA)) {
        GVariantIter *iter;
        g_variant_get(property_value, "a{sv}", &iter);

        GVariant *array;
        gchar *key;

        GHashTable *service_data = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                         g_free, (GDestroyNotify) lm_utils_byte_array_free);
        while (g_variant_iter_loop(iter, "{sv}", &key, &array)) {
            gsize data_length = 0;
            guint8 *data = (guint8 *) g_variant_get_fixed_array(array, &data_length, sizeof(guint8));
            GByteArray *byte_array = g_byte_array_sized_new((guint) data_length);
            g_byte_array_append(byte_array, data, (guint) data_length);

            gchar *key_copy = g_strdup(key);

            g_hash_table_insert(service_data, key_copy, byte_array);
        }
        lm_device_set_service_data(device, service_data);
        g_variant_iter_free(iter);
    }
}

lm_player_t *lm_device_get_active_player(lm_device_t *device)
{
    g_assert(device != NULL);

    return device->active_player;
}

lm_transport_t *lm_device_get_active_transport(lm_device_t *device)
{
    g_assert(device != NULL);

    return device->active_transport;
}

gboolean lm_device_is_bcast_device(lm_device_t *device)
{
    g_assert(device);

    if (device->uuids && g_list_length(device->uuids) > 0) {
        for (GList *iterator = device->uuids; iterator; iterator = iterator->next) {
            if (g_str_equal((gchar *) iterator->data, BCAST_AUDIO_AUNOUNCEMENT_SERVICE_UUID)) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

lm_device_conn_type_t lm_device_get_conn_type(const lm_device_t *device)
{
    g_assert(device);

    return device->conn_type;
}

void lm_device_set_conn_type(lm_device_t *device, lm_device_conn_type_t type)
{
    g_assert(device);

    if (!(device->conn_type & type)) {
        device->conn_type |= type;
        lm_log_debug(TAG, "device '%s' new type set: 0x%x 0x%x", device->path, type, device->conn_type);
    }
}

void lm_device_reset_conn_type(lm_device_t *device, lm_device_conn_type_t type)
{
    g_assert(device);

    if (device->conn_type & type) {
        device->conn_type &= ~type;
        lm_log_debug(TAG, "device '%s' type reset: 0x%x 0x%x", device->path, type, device->conn_type);
    }
}

gboolean lm_device_has_conn_type(lm_device_t *device, lm_device_conn_type_t type)
{
    g_assert(device);
    return (device->conn_type & type) != 0;
}

static void lm_device_call_method_cb(__attribute__((unused)) GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data)
{
    lm_device_method_call_ctx_t *ctx = (lm_device_method_call_ctx_t *) user_data;
    lm_device_t *device = ctx->device;
    g_assert(device != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(device->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to call device method '%s' (error %d '%s')",
                        ctx->method, error->code, error->message);
        g_clear_error(&error);
        if (g_str_equal(ctx->method, DEVICE_METHOD_CONNECT)) {
            lm_device_set_conn_state(device, LM_DEVICE_DISCONNECTED);
        } else if (g_str_equal(ctx->method, DEVICE_METHOD_DISCONNECT)) {
            lm_device_set_conn_state(device, LM_DEVICE_CONNECTED);
        } else if (g_str_equal(ctx->method, DEVICE_METHOD_PAIR)) {
            lm_device_set_bonding_state(device, LM_DEVICE_BOND_NONE);
        }
    }

    lm_device_method_call_ctx_free(ctx);
}

static lm_status_t lm_device_call_method(lm_device_t *device,
        const gchar *method, __attribute__((unused)) GVariant *parameters)
{
    g_assert(device != NULL);
    g_assert(method != NULL);

    g_dbus_connection_call(device->dbus_conn,
                           BLUEZ_DBUS,
                           device->path,
                           INTERFACE_DEVICE,
                           method,
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_device_call_method_cb,
                           lm_device_method_call_ctx_create(device, method));

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_device_pair(lm_device_t *device) {
    g_assert(device != NULL);
    g_assert(device->path != NULL);

    lm_log_debug(TAG, "pairing device '%s'", device->address);

    if (LM_DEVICE_DISCONNECTED == device->conn_state) {
        device->conn_state = LM_DEVICE_CONNECTING;
    }

    subscribe_device_prop_changed(device);
    lm_device_set_bonding_state(device, LM_DEVICE_BONDING);
    return lm_device_call_method(device, DEVICE_METHOD_PAIR, NULL);
}

lm_status_t lm_device_connect(lm_device_t *device)
{
    g_assert(device);

    if (LM_DEVICE_CONNECTED == device->conn_state) {
        lm_log_warn(TAG, "device already connected with type 0x%x", device->conn_type);
        return LM_STATUS_SUCCESS;
    }

    lm_device_set_conn_state(device, LM_DEVICE_CONNECTING);
    subscribe_device_prop_changed(device);
    return lm_device_call_method(device, DEVICE_METHOD_CONNECT, NULL);
}

lm_status_t lm_device_disconnect(lm_device_t *device)
{
    g_assert(device);

    if (LM_DEVICE_DISCONNECTED == device->conn_state) {
        lm_log_warn(TAG, "device already disconnected");
        return LM_STATUS_SUCCESS;
    }

    lm_device_set_conn_state(device, LM_DEVICE_DISCONNECTING);
    return lm_device_call_method(device, DEVICE_METHOD_DISCONNECT, NULL);
}

lm_status_t lm_device_connect_bearer(lm_device_t *device, lm_device_bearer_type_t type)
{
    g_assert(device);

    if (!device->bearers[type]) {
        lm_log_error(TAG, "there is no '%s' bearer on device '%s'",
                lm_bearer_type_to_name(type), device->path);
        return LM_STATUS_NOT_READY;
    }

    subscribe_device_prop_changed(device);
    return lm_bearer_connect(device->bearers[type]);
}

lm_status_t lm_device_disconnect_bearer(lm_device_t *device, lm_device_bearer_type_t type)
{
    g_assert(device);

    if (!device->bearers[type]) {
        lm_log_error(TAG, "there is no '%s' bearer on device '%s'",
                lm_bearer_type_to_name(type), device->path);
        return LM_STATUS_NOT_READY;
    }

    return lm_bearer_disconnect(device->bearers[type]);
}

gboolean lm_device_is_bcast_encrypted(lm_device_t *device)
{
    GPtrArray *bcast_transports = NULL;
    lm_transport_t *transport;
    gboolean encrypted = FALSE;

    bcast_transports = g_ptr_array_new();
    lm_device_get_transports(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK, bcast_transports);
    if (!bcast_transports->len) {
        lm_log_error(TAG, "No broadcast transports available");
        goto exit;
    }

    for (guint i = 0; i < bcast_transports->len; i++) {
        transport = (lm_transport_t *)g_ptr_array_index(bcast_transports, i);
            lm_transport_qos_t *qos = lm_transport_get_qos(transport);
            if (qos->encryption) {
                encrypted = TRUE;
                goto exit;
            }
    }

exit:
    if (bcast_transports)
        g_ptr_array_unref(bcast_transports);

    return encrypted;
}

lm_device_bcast_sync_state_t lm_device_get_bcast_sync_state(lm_device_t *device)
{
    g_assert(device);

    return device->bcast_sync_state;
}

lm_status_t lm_device_start_sync_bcast(lm_device_t *device,
                lm_transport_audio_location_t location,
                lm_transport_bcast_code_t *bcode)
{
    lm_transport_t *transport;
    GPtrArray *bcast_transports = NULL;
    GPtrArray *selected_transports = NULL;
    lm_status_t status = LM_STATUS_FAIL;
    gboolean bcode_required = FALSE;
    lm_transport_bcast_code_t empty_bcode = {0};

    g_assert(device);

    lm_log_info(TAG, "Start syncing broadcast with device '%s', location mask 0x%x",
                                                        device->path, location);

    if (device->bcast_sync_state != LM_DEVICE_BCAST_IDLE &&
            device->bcast_sync_state != LM_DEVICE_BCAST_LOST) {
        lm_log_error(TAG, "invalid sync state %d", device->bcast_sync_state);
        return LM_STATUS_FAIL;
    }

    bcast_transports = g_ptr_array_new();
    lm_device_get_transports(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK, bcast_transports);
    if (!bcast_transports->len) {
        lm_log_error(TAG, "No broadcast transports available");
        goto exit;
    }

    selected_transports = g_ptr_array_new();
    for (guint i = 0; i < bcast_transports->len; i++) {
        transport = (lm_transport_t *)g_ptr_array_index(bcast_transports, i);
        if (lm_transport_get_state(transport) != LM_TRANSPORT_IDLE) {
            lm_log_error(TAG, "transport '%s' is not ready to sync",
                                                lm_transport_get_path(transport));
            goto exit;
        }
        if ((lm_transport_get_audio_location(transport) & location) ||
            lm_transport_get_audio_location(transport) == LM_TRANSPORT_AUDIO_LOCATION_MONO) {
            lm_transport_qos_t *qos = lm_transport_get_qos(transport);
            if (qos->encryption &&
                    !memcmp(&qos->bcode, &empty_bcode, sizeof(lm_transport_bcast_code_t)))
                bcode_required = TRUE;

            g_ptr_array_add(selected_transports, transport);
        }
    }

    if (!selected_transports->len) {
        lm_log_error(TAG, "No transport matches audio location mask 0x%x", location);
        goto exit;
    }

    if (bcode_required) {
        if (!bcode || !memcmp(bcode, &empty_bcode, sizeof(lm_transport_bcast_code_t))) {
            lm_log_error(TAG, "empty broadcast code");
            goto exit;
        }
    }

    /* Set links for all selected transports */
    lm_transport_set_links(selected_transports);

    /* Select all transports */
    for (guint i = 0; i < selected_transports->len; i++) {
        transport = g_ptr_array_index(selected_transports, i);
        if (bcode_required)
            lm_transport_set_bcast_code(transport, bcode);

        lm_transport_select(transport);
    }

    device->audio_location = location;

    lm_device_set_bcast_sync_state(device, LM_DEVICE_BCAST_SYNCING);
    status = LM_STATUS_SUCCESS;

exit:
    if (bcast_transports)
        g_ptr_array_unref(bcast_transports);
    if (selected_transports)
        g_ptr_array_unref(selected_transports);
    return status;
}

lm_status_t lm_device_stop_sync_bcast(lm_device_t *device)
{
    GPtrArray *bcast_transports = NULL;

    g_assert(device);

    lm_log_info(TAG, "Stop syncing broadcast with device '%s'", device->path);

    if (device->bcast_sync_state != LM_DEVICE_BCAST_SYNCED) {
        lm_log_error(TAG, "invalid sync state %d", device->bcast_sync_state);
        return LM_STATUS_FAIL;
    }

    bcast_transports = g_ptr_array_new();

    lm_device_get_transports(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK, bcast_transports);
    if (bcast_transports->len == 0) {
        g_ptr_array_unref(bcast_transports);
        lm_log_error(TAG, "No broadcast transports available");
        return LM_STATUS_FAIL;
    }

    g_ptr_array_unref(bcast_transports);

    lm_device_set_bcast_sync_state(device, LM_DEVICE_BCAST_TERMINATING);
    return lm_adapter_remove_device(device->adapter, device);
}

void lm_device_get_transports(lm_device_t *device, lm_transport_profile_t profile, GPtrArray *array)
{
    g_assert(device != NULL);
    g_assert(array != NULL);
    GHashTableIter hash_iter;
    gpointer key, value;

    g_ptr_array_set_size(array, 0);

    g_hash_table_iter_init(&hash_iter, device->transports);
    while (g_hash_table_iter_next(&hash_iter, &key, &value)) {
        lm_transport_t *transport = (lm_transport_t *)value;
        if (lm_transport_get_profile(transport) == profile)
            g_ptr_array_add(array, transport);
    }
}

void lm_device_add_bearer(lm_device_t *device, lm_bearer_t *bearer)
{
    lm_device_bearer_type_t type;

    g_assert(device && bearer);

    type = lm_bearer_get_type(bearer);
    if (device->bearers[type]) {
        lm_bearer_destroy(device->bearers[type]);
        device->bearers[type] = NULL;
    }

    lm_log_debug(TAG, "'%s' add bearer '%s'", device->path, lm_bearer_get_name(bearer));
    device->bearers[type] = bearer;
}

void lm_device_remove_bearer(lm_device_t *device, lm_bearer_t *bearer)
{
    lm_device_bearer_type_t type;

    g_assert(device && bearer);

    type = lm_bearer_get_type(bearer);
    if (!device->bearers[type])
        return;

    lm_log_debug(TAG, "'%s' remove bearer '%s'", device->path, lm_bearer_get_name(bearer));
    lm_bearer_destroy(device->bearers[type]);
    device->bearers[type] = NULL;
}

lm_bearer_t *lm_device_get_bearer(lm_device_t *device, lm_device_bearer_type_t type)
{
    g_assert(device);

    return device->bearers[type];
}

GList *lm_device_get_gatt_services(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->services_list;
}

lm_gatt_svc_t *lm_device_get_gatt_service(const lm_device_t *device, const gchar *service_uuid) {
    g_assert(device != NULL);
    g_assert(service_uuid != NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(service_uuid), NULL);

    if (device->services_list != NULL) {
        for (GList *iterator = device->services_list; iterator; iterator = iterator->next) {
            lm_gatt_svc_t *service = (lm_gatt_svc_t *) iterator->data;
            if (g_str_equal(service_uuid, lm_gatt_svc_get_uuid(service))) {
                return service;
            }
        }
    }

    return NULL;
}

lm_gatt_char_t *lm_device_get_gatt_char(const lm_device_t *device, const gchar *service_uuid, const gchar *char_uuid) {
    g_assert(device != NULL);
    g_assert(service_uuid != NULL);
    g_assert(char_uuid != NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(service_uuid), NULL);
    g_return_val_if_fail(g_uuid_string_is_valid(char_uuid), NULL);

    lm_gatt_svc_t *service = lm_device_get_gatt_service(device, service_uuid);
    if (service != NULL) {
        return lm_gatt_svc_get_char(service, char_uuid);
    }

    return NULL;
}