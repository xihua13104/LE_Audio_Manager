/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

#ifndef __LM_PRIV_H__
#define __LM_PRIV_H__
#include "lm.h"

typedef struct lm_gatt_desc lm_gatt_desc_t;
typedef struct lm_gatt_svc lm_gatt_svc_t;
typedef struct lm_gatt_char lm_gatt_char_t;

lm_status_t lm_app_event_callback(lm_msg_type_t msg, lm_status_t status, void *buf);

GDBusConnection *lm_get_gdbus_connection(void);

lm_status_t lm_get_audio_location_config(lm_transport_profile_t profile,
                        lm_transport_audio_location_t *location);

#endif //__LM_PRIV_H__