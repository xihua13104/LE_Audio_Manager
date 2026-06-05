/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */


#ifndef __LM_UTILS_H__
#define __LM_UTILS_H__

#include "lm.h"
#include "lm_log.h"
#include <glib.h>
#include <gio/gio.h>

gint lm_utils_dbus_bluez_object_path_to_hci_dev_id(const gchar *path);

gchar *lm_utils_variant_sanitize_object_path(gchar *path);

gboolean lm_utils_variant_validate_value(GVariant *value, const GVariantType *type,
        const gchar *name);

GSource *lm_utils_io_create_watch_full(GIOChannel *channel, gint priority,
        GIOCondition cond, GIOFunc func, void *userdata, GDestroyNotify notify);

GString *lm_utils_g_byte_array_as_hex(const GByteArray *byteArray);

GList *lm_utils_g_variant_string_array_to_list(GVariant *value);

GByteArray *lm_utils_g_variant_get_byte_array(GVariant *variant);

void lm_utils_byte_array_free(GByteArray *byteArray);

gchar* lm_utils_random_string(gsize length);

gboolean lm_utils_is_valid_uuid(const gchar *uuid);

gchar *lm_utils_path_to_address(const gchar *path);

#endif //__LM_UTILS_H__