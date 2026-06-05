/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

#ifndef __LM_BEARER_PRIV_H__
#define __LM_BEARER_PRIV_H__
#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_device.h"
#include "lm_adapter.h"

typedef struct lm_bearer lm_bearer_t;

lm_bearer_t *lm_bearer_create(lm_device_t *device,
                    lm_device_bearer_type_t type, const gchar *device_path);

void lm_bearer_destroy(lm_bearer_t *bearer);

void lm_bearer_update_property(lm_bearer_t *bearer,
            const gchar *property_name, GVariant *property_value);

const gchar *lm_bearer_get_name(lm_bearer_t *bearer);

lm_device_bearer_type_t lm_bearer_get_type(lm_bearer_t *bearer);

const gchar *lm_bearer_type_to_name(lm_device_bearer_type_t type);

gboolean lm_bearer_is_connected(lm_bearer_t *bearer);

gboolean lm_bearer_is_paired(lm_bearer_t *bearer);

gboolean lm_bearer_is_bonded(lm_bearer_t *bearer);

lm_status_t lm_bearer_connect(lm_bearer_t *bearer);

lm_status_t lm_bearer_disconnect(lm_bearer_t *bearer);

lm_device_conn_state_t lm_bearer_get_conn_state(lm_bearer_t *bearer);

lm_device_bonding_state_t lm_bearer_get_bonding_state(lm_bearer_t *bearer);

#endif //__LM_BEARER_PRIV_H__