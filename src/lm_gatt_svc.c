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
#include "bluez_iface.h"
#include <glib.h>
#include <gio/gio.h>

#define TAG "lm_gatt_svc"

struct lm_gatt_svc {
    lm_device_t *device; // Borrowed
    const gchar *path; // Owned
    const gchar* uuid; // Owned
    GList *chars; // Owned
};

lm_gatt_svc_t* lm_gatt_svc_create(lm_device_t *device, const gchar* path, const gchar* uuid) {
    g_assert(device != NULL);
    g_assert(path != NULL);
    g_assert(lm_utils_is_valid_uuid(uuid));

    lm_gatt_svc_t *service = g_new0(lm_gatt_svc_t, 1);
    service->device = device;
    service->path = g_strdup(path);
    service->uuid = g_strdup(uuid);
    service->chars = NULL;
    return service;
}

void lm_gatt_svc_destroy(lm_gatt_svc_t *service) {
    g_assert(service != NULL);

    g_free((gchar*) service->path);
    service->path = NULL;

    g_free((gchar*) service->uuid);
    service->uuid = NULL;

    g_list_free(service->chars);
    service->chars = NULL;

    service->device = NULL;
    g_free(service);
}

const gchar* lm_gatt_svc_get_uuid(const lm_gatt_svc_t *service) {
    g_assert(service != NULL);
    return service->uuid;
}

lm_device_t *lm_gatt_svc_get_device(const lm_gatt_svc_t *service) {
    g_assert(service != NULL);
    return service->device;
}

void lm_gatt_svc_add_char(lm_gatt_svc_t *service, lm_gatt_char_t *gatt_char) {
    g_assert(service != NULL);
    g_assert(gatt_char != NULL);

    service->chars = g_list_append(service->chars, gatt_char);
}

GList *lm_gatt_svc_get_chars(const lm_gatt_svc_t *service) {
    g_assert(service != NULL);
    return service->chars;
}

lm_gatt_char_t *lm_gatt_svc_get_char(const lm_gatt_svc_t *service, const gchar* char_uuid) {
    g_assert(service != NULL);
    g_assert(char_uuid != NULL);
    g_assert(lm_utils_is_valid_uuid(char_uuid));

    if (service->chars != NULL) {
        for (GList *iterator = service->chars; iterator; iterator = iterator->next) {
            lm_gatt_char_t *gatt_char = (lm_gatt_char_t *) iterator->data;
            if (g_str_equal(char_uuid, lm_gatt_char_get_uuid(gatt_char))) {
                return gatt_char;
            }
        }
    }
    return NULL;
}
