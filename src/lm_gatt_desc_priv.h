/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __LM_GATT_DESC_PRIV_H__
#define __LM_GATT_DESC_PRIV_H__

#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_priv.h"

lm_gatt_desc_t *lm_gatt_desc_create(lm_device_t *device, const gchar *path);

void lm_gatt_desc_destroy(lm_gatt_desc_t *desc);

const gchar *lm_gatt_desc_to_string(const lm_gatt_desc_t *desc);

void lm_gatt_desc_set_uuid(lm_gatt_desc_t *desc, const gchar *uuid);

void lm_gatt_desc_set_char_path(lm_gatt_desc_t *desc, const gchar *path);

const gchar *lm_gatt_desc_get_char_path(const lm_gatt_desc_t *desc);

const gchar *lm_gatt_desc_get_uuid(const lm_gatt_desc_t *desc);

void lm_gatt_desc_set_char(lm_gatt_desc_t *desc, lm_gatt_char_t *gatt_char);

void lm_gatt_desc_set_flags(lm_gatt_desc_t *desc, GList *flags);

lm_status_t lm_gatt_desc_read(lm_gatt_desc_t *desc);

lm_status_t lm_gatt_desc_write(lm_gatt_desc_t *desc, const GByteArray *byte_array);

lm_device_t *lm_gatt_desc_get_device(const lm_gatt_desc_t *desc);

lm_gatt_char_t *lm_gatt_desc_get_char(const lm_gatt_desc_t *desc);

#endif //__LM_GATT_DESC_PRIV_H__