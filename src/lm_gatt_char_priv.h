/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __LM_GATT_CHAR_PRIV_H__
#define __LM_GATT_CHAR_PRIV_H__

#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_gatt.h"
#include "lm_priv.h"
#include "lm_forward_decl.h"

lm_gatt_char_t *lm_gatt_char_create(lm_device_t *device, const gchar *path);

void lm_gatt_char_destroy(lm_gatt_char_t *gatt_char);

const gchar *lm_gatt_char_get_uuid(const lm_gatt_char_t *gatt_char);

void lm_gatt_char_set_uuid(lm_gatt_char_t *gatt_char, const gchar *uuid);

void lm_gatt_char_set_mtu(lm_gatt_char_t *gatt_char, guint mtu);

lm_device_t *lm_gatt_char_get_device(const lm_gatt_char_t *gatt_char);

lm_gatt_svc_t *lm_gatt_char_get_service(const lm_gatt_char_t *gatt_char);

void lm_gatt_char_set_service(lm_gatt_char_t *gatt_char, lm_gatt_svc_t *service);

const gchar *lm_gatt_char_get_service_path(const lm_gatt_char_t *gatt_char);

void lm_gatt_char_set_service_path(lm_gatt_char_t *gatt_char, const gchar *service_path);

GList *lm_gatt_char_get_flags(const lm_gatt_char_t *gatt_char);

void lm_gatt_char_set_flags(lm_gatt_char_t *gatt_char, GList *flags);

guint lm_gatt_char_get_properties(const lm_gatt_char_t *gatt_char);

gboolean lm_gatt_char_is_notifying(const lm_gatt_char_t *gatt_char);

gboolean lm_gatt_char_supports_write(const lm_gatt_char_t *gatt_char,
                                     lm_gatt_write_type_t write_type);

gboolean lm_gatt_char_supports_read(const lm_gatt_char_t *gatt_char);

gboolean lm_gatt_char_supports_notify(const lm_gatt_char_t *gatt_char);

lm_status_t lm_gatt_char_read(lm_gatt_char_t *gatt_char);

lm_status_t lm_gatt_char_write(lm_gatt_char_t *gatt_char,
                        const GByteArray *byte_array,
                        lm_gatt_write_type_t write_type);

lm_status_t lm_gatt_char_start_notify(lm_gatt_char_t *gatt_char);

lm_status_t lm_gatt_char_stop_notify(lm_gatt_char_t *gatt_char);

void lm_gatt_char_set_notifying(lm_gatt_char_t *gatt_char, gboolean notifying);

void lm_gatt_char_add_desc(lm_gatt_char_t *gatt_char, lm_gatt_desc_t *desc);

lm_gatt_desc_t *lm_gatt_char_get_desc(const lm_gatt_char_t *gatt_char,
                                      const gchar *desc_uuid);

GList *lm_gatt_char_get_descs(const lm_gatt_char_t *gatt_char);

gchar *lm_gatt_char_to_string(const lm_gatt_char_t *gatt_char);

#endif //__LM_GATT_CHAR_PRIV_H__