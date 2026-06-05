/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

#ifndef __LM_TRANSPORT_PRIV_H__
#define __LM_TRANSPORT_PRIV_H__
#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_transport.h"
#include "lm_device.h"
#include "lm_adapter.h"

lm_transport_t *lm_transport_create(lm_device_t *device, const gchar *path);

void lm_transport_destroy(lm_transport_t *transport);

void lm_transport_update_property(lm_transport_t *transport,
                const gchar *property_name, GVariant *property_value);

lm_status_t lm_transport_select(lm_transport_t *transport);

lm_status_t lm_transport_unselect(lm_transport_t *transport);

lm_status_t lm_transport_set_links(GPtrArray *transports);

lm_status_t lm_transport_set_bcast_code(lm_transport_t *transport,
                                    lm_transport_bcast_code_t *bcode);
#endif //__LM_TRANSPORT_PRIV_H__