/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __LM_DEVICE_PRIV_H__
#define __LM_DEVICE_PRIV_H__
#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_device.h"
#include "lm_adapter.h"
#include "lm_bearer_priv.h"
#include "lm_priv.h"

#define LM_DEVICE_BEARER_MAX                  2

#define LM_DEVICE_ADDR_STR_LEN                sizeof("XX:XX:XX:XX:XX:XX")

#define LM_DEVICE_BLUEZ_DBUS_ADDR_STR_LEN     sizeof("dev_XX_XX_XX_XX_XX_XX")

lm_device_t *lm_device_create_with_path(lm_adapter_t *adapter, const gchar *path);

void lm_device_destroy(lm_device_t *device);

void lm_device_set_bonding_state(lm_device_t *device, lm_device_bonding_state_t bonding_state);

void lm_device_update_property(lm_device_t *device, const gchar *property_name, GVariant *property_value);

void lm_device_load_properties(lm_device_t *device);

GDBusConnection *lm_device_get_dbus_conn(const lm_device_t *device);

void lm_device_set_conn_type(lm_device_t *device, lm_device_conn_type_t type);

void lm_device_reset_conn_type(lm_device_t *device, lm_device_conn_type_t type);

gboolean lm_device_has_conn_type(lm_device_t *device, lm_device_conn_type_t type);

void lm_device_add_bearer(lm_device_t *device, lm_bearer_t *bearer);

void lm_device_remove_bearer(lm_device_t *device, lm_bearer_t *bearer);

lm_bearer_t *lm_device_get_bearer(lm_device_t *device, lm_device_bearer_type_t type);

GList *lm_device_get_gatt_services(const lm_device_t *device);

lm_gatt_svc_t *lm_device_get_gatt_service(const lm_device_t *device, const gchar *service_uuid);

lm_gatt_char_t *lm_device_get_gatt_char(const lm_device_t *device, const gchar *service_uuid, const gchar *char_uuid);


#endif //__LM_DEVICE_PRIV_H__