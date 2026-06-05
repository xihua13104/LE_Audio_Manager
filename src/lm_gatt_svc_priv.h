/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __LM_GATT_SVC_PRIV_H__
#define __LM_GATT_SVC_PRIV_H__

#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_priv.h"

lm_gatt_svc_t* lm_gatt_svc_create(lm_device_t *device, const gchar* path, const gchar* uuid);

void lm_gatt_svc_destroy(lm_gatt_svc_t *service);

const gchar* lm_gatt_svc_get_uuid(const lm_gatt_svc_t *service);

lm_device_t *lm_gatt_svc_get_device(const lm_gatt_svc_t *service);

void lm_gatt_svc_add_char(lm_gatt_svc_t *service, lm_gatt_char_t *gatt_char);

GList *lm_gatt_svc_get_chars(const lm_gatt_svc_t *service);

lm_gatt_char_t *lm_gatt_svc_get_char(const lm_gatt_svc_t *service, const gchar* char_uuid);

#endif //__LM_GATT_SVC_PRIV_H__