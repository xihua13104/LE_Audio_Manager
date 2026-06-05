/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

#include "bluez_dbus.h"
#include "bluez_iface.h"
#include "lm_profile.h"
#include "lm_device_priv.h"
#include "lm_log.h"
#include "lm.h"
#include "lm_utils.h"
#include <gio/gio.h>
#include <glib.h>

#define TAG "lm_profile"

struct lm_profile {
    GDBusConnection *dbus_conn;
    lm_adapter_t *adapter;
    gchar *path;
    gchar *uuid;
    gchar *name;
    gchar *service_record;
    guint16 channel;
    guint16 psm;
    guint16 version;
    guint16 features;
    gboolean require_auth;
    gboolean require_authorize;
    gboolean auto_connect;
    guint registration_id;
    void *user_data;
};

static void lm_profile_method_call( __attribute__((unused)) GDBusConnection *conn,
                                    __attribute__((unused)) const gchar *sender,
                                    __attribute__((unused)) const gchar *path,
                                    __attribute__((unused)) const gchar *interface,
                                   const gchar *method,
                                   GVariant *params,
                                   GDBusMethodInvocation *invocation,
                                   gpointer userdata)
{
    lm_profile_t *profile = userdata;

    g_assert(profile != NULL);
    lm_log_debug(TAG, "profile method: %s", method);

    if (g_str_equal(method, PROFILE_METHOD_RELEASE)) {
        lm_log_info(TAG, "profile released");
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method, PROFILE_METHOD_NEW_CONNECTION)) {
        gchar *device_path = NULL;
        gint fd = -1;
        GVariant *fd_properties = NULL;
        guint16 version = 0;
        guint16 features = 0;

        g_variant_get(params, "(oha{sv})", &device_path, &fd, &fd_properties);

        if (fd_properties != NULL) {
            GVariantIter iter;
            const gchar *key;
            GVariant *value;

            g_variant_iter_init(&iter, fd_properties);
            while (g_variant_iter_loop(&iter, "{&sv}",&key, &value)) {
                if (g_str_equal(key, "Version"))
                    version = g_variant_get_uint16(value);
                else if (g_str_equal(key, "Features"))
                    features = g_variant_get_uint16(value);
            }
        }
        lm_log_info(TAG, "new connection with '%s' fd=%d version=0x%04x features=0x%04x",
                        device_path, fd, version, features);

        lm_device_t *device = lm_device_lookup_by_path(profile->adapter, device_path);
        if (device) {
            lm_profile_connection_ind_t ind = {
                .profile = profile,
                .device = device,
                .fd = fd,
                .version = version,
                .features = features
            };
            lm_app_event_callback(LM_PROFILE_CONNECTION_IND, LM_STATUS_SUCCESS, &ind);
        } else
            lm_log_error(TAG, "unknown device '%s' for new connection", device_path);

        g_free(device_path);

        if (fd_properties != NULL)
            g_variant_unref(fd_properties);

        g_dbus_method_invocation_return_value(invocation, NULL);
    } else {
        lm_log_error(TAG, "unknown profile method: %s", method);
        g_dbus_method_invocation_return_dbus_error(
                        invocation,
                        "org.bluez.Error.NotSupported",
                        "Method not supported");
    }
}

static const GDBusInterfaceVTable profile_method_table = {
    .method_call = lm_profile_method_call,
};

static lm_status_t lm_profile_register_object(lm_profile_t *profile)
{
    GError *error = NULL;

    profile->registration_id =
        g_dbus_connection_register_object(
                        profile->dbus_conn,
                        profile->path,
                        (GDBusInterfaceInfo *)
                        &bluez_profile1_interface,
                        &profile_method_table,
                        profile,
                        NULL,
                        &error);

    if (error != NULL) {
        lm_log_error(TAG, "register profile object failed: %s", error->message);
        g_clear_error(&error);
        return LM_STATUS_FAIL;
    }

    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_profile_unregister_object(lm_profile_t *profile)
{
    if (profile->registration_id) {
        g_dbus_connection_unregister_object(
                        profile->dbus_conn,
                        profile->registration_id);
        profile->registration_id = 0;
    }
    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_profilemgr_method_call(GDBusConnection *dbus_conn,
                                          const gchar *method,
                                          GVariant *params)
{
    GError *error = NULL;

    GVariant *result =
        g_dbus_connection_call_sync(
                        dbus_conn,
                        BLUEZ_DBUS,
                        "/org/bluez",
                        INTERFACE_PROFILE_MANAGER,
                        method,
                        params,
                        NULL,
                        G_DBUS_CALL_FLAGS_NONE,
                        BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                        NULL,
                        &error);

    if (result)
        g_variant_unref(result);

    if (error != NULL) {
        lm_log_error(TAG, "ProfileManager call failed '%s': %s", method, error->message);
        g_clear_error(&error);
        return LM_STATUS_FAIL;
    }

    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_profilemgr_register_profile(lm_profile_t *profile)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    if (profile->name) {
        g_variant_builder_add(&builder,
                              "{sv}",
                              "Name",
                              g_variant_new_string(profile->name));
    }

    if (profile->channel) {
        g_variant_builder_add(&builder,
                            "{sv}",
                            "Channel",
                            g_variant_new_uint16(profile->channel));
    }

    if (profile->psm) {
        g_variant_builder_add(&builder,
                            "{sv}",
                            "PSM",
                            g_variant_new_uint16(profile->psm));
    }

    if (profile->version) {
        g_variant_builder_add(&builder,
                            "{sv}",
                            "Version",
                            g_variant_new_uint16(profile->version));
    }

    if (profile->features) {
        g_variant_builder_add(&builder,
                            "{sv}",
                            "Features",
                            g_variant_new_uint16(profile->features));
    }

    g_variant_builder_add(&builder,
                          "{sv}",
                          "RequireAuthentication",
                          g_variant_new_boolean(
                              profile->require_auth));

    g_variant_builder_add(&builder,
                          "{sv}",
                          "RequireAuthorization",
                          g_variant_new_boolean(
                              profile->require_authorize));

    g_variant_builder_add(&builder,
                          "{sv}",
                          "AutoConnect",
                          g_variant_new_boolean(
                              profile->auto_connect));

    if (profile->service_record) {
        g_variant_builder_add(&builder,
                              "{sv}",
                              "ServiceRecord",
                              g_variant_new_string(
                                  profile->service_record));
    }

    return lm_profilemgr_method_call(
                    profile->dbus_conn,
                    PROFILE_MANAGER_METHOD_REGISTER_PROFILE,
                    g_variant_new("(osa{sv})",
                                  profile->path,
                                  profile->uuid,
                                  &builder));
}

static lm_status_t lm_profilemgr_unregister_profile(lm_profile_t *profile)
{
    return lm_profilemgr_method_call(
                    profile->dbus_conn,
                    PROFILE_MANAGER_METHOD_UNREGISTER_PROFILE,
                    g_variant_new("(o)", profile->path));
}

lm_profile_t *lm_profile_create(lm_adapter_t *adapter, const gchar *uuid)
{
    g_assert(adapter != NULL);
    g_assert(uuid != NULL);
    g_assert(lm_utils_is_valid_uuid(uuid));

    gchar *random_str = NULL;
    lm_profile_t *profile = g_new0(lm_profile_t, 1);

    profile->dbus_conn = lm_adapter_get_dbus_conn(adapter);
    profile->adapter = adapter;
    random_str = lm_utils_random_string(4);
    profile->path = g_strdup_printf("/org/bluez/lmprofile_%s", random_str);
    profile->uuid = g_strdup(uuid);
    g_free(random_str);

    return profile;
}

void lm_profile_destroy(lm_profile_t *profile)
{
    g_assert(profile != NULL);

    if (profile->path)
        g_free(profile->path);

    if (profile->uuid)
        g_free(profile->uuid);

    if (profile->name)
        g_free(profile->name);

    if (profile->service_record)
        g_free(profile->service_record);

    profile->dbus_conn = NULL;
    profile->adapter = NULL;
    profile->user_data = NULL;

    g_free(profile);
}

void lm_profile_set_name(lm_profile_t *profile, const gchar *name)
{
    g_assert(profile != NULL);

    if (profile->name)
        g_free(profile->name);

    profile->name = g_strdup(name);
}

const gchar *lm_profile_get_name(lm_profile_t *profile)
{
    g_assert(profile != NULL);

    return profile->name;
}

void lm_profile_set_channel(lm_profile_t *profile, guint16 channel)
{
    g_assert(profile != NULL);

    profile->channel = channel;
}

void lm_profile_set_psm(lm_profile_t *profile, guint16 psm)
{
    g_assert(profile != NULL);

    profile->psm = psm;
}

void lm_profile_set_version(lm_profile_t *profile, guint16 version)
{
    g_assert(profile != NULL);

    profile->version = version;
}

void lm_profile_set_features(lm_profile_t *profile, guint16 features)
{
    g_assert(profile != NULL);

    profile->features = features;
}

void lm_profile_set_require_auth(lm_profile_t *profile, gboolean enable)
{
    g_assert(profile != NULL);

    profile->require_auth = enable;
}

void lm_profile_set_require_authorize(lm_profile_t *profile, gboolean enable)
{
    g_assert(profile != NULL);

    profile->require_authorize = enable;
}

void lm_profile_set_auto_connect(lm_profile_t *profile, gboolean enable)
{
    g_assert(profile != NULL);

    profile->auto_connect = enable;
}

void lm_profile_set_service_record(lm_profile_t *profile, const gchar *record)
{
    g_assert(profile != NULL);

    if (profile->service_record)
        g_free(profile->service_record);

    profile->service_record = g_strdup(record);
}

void lm_profile_set_user_data(lm_profile_t *profile, void *user_data)
{
    g_assert(profile != NULL);

    profile->user_data = user_data;
}

const gchar *lm_profile_get_path(lm_profile_t *profile)
{
    g_assert(profile != NULL);

    return profile->path;
}

lm_status_t lm_profile_register(lm_profile_t *profile)
{
    g_assert(profile != NULL);

    if (lm_profile_register_object(profile) != LM_STATUS_SUCCESS)
        return LM_STATUS_FAIL;

    return lm_profilemgr_register_profile(profile);
}

lm_status_t lm_profile_unregister(lm_profile_t *profile)
{
    g_assert(profile != NULL);

    lm_profile_unregister_object(profile);

    return lm_profilemgr_unregister_profile(profile);
}

lm_adapter_t *lm_profile_get_adapter(lm_profile_t *profile)
{
    g_assert(profile != NULL);

    return profile->adapter;
}