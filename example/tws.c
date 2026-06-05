/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

#include "lm.h"
#include "lm_log.h"
#include "lm_adapter.h"
#include "lm_adv.h"
#include "lm_device.h"
#include "lm_agent.h"
#include "lm_player.h"
#include "lm_transport.h"
#include "lm_uuids.h"
#include <stdio.h>

#define TAG "tws"

static GMainLoop *loop = NULL;
static lm_adapter_t *default_adapter = NULL;
static lm_adv_t *adv = NULL;
static lm_agent_t *agent = NULL;

static lm_status_t lm_adapter_callback(lm_msg_type_t msg,
                                       __attribute__((unused)) lm_status_t status,
                                       void *buf)
{
    switch (msg) {
        case LM_ADAPTER_POWER_ON_CNF: {
            if (status == LM_STATUS_SUCCESS) {
                lm_adapter_power_on_cnf_t *cnf = (lm_adapter_power_on_cnf_t *)buf;
                lm_log_debug(TAG, "adapter '%s' powered on", lm_adapter_get_path(cnf->adapter));
            }
            break;
        }
        case LM_ADAPTER_POWER_OFF_CNF: {
            if (status == LM_STATUS_SUCCESS) {
                lm_adapter_power_off_cnf_t *cnf = (lm_adapter_power_off_cnf_t *)buf;
                lm_log_debug(TAG, "adapter '%s' powered off", lm_adapter_get_path(cnf->adapter));
            }
            break;
        }
        default:
            break;
    }

    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_device_callback(lm_msg_type_t msg,
                                      __attribute__((unused)) lm_status_t status,
                                      void *buf)
{
    switch (msg) {
        case LM_DEVICE_CONNECTED_IND: {
            lm_device_connected_ind_t *ind = (lm_device_connected_ind_t *)buf;
            lm_log_debug(TAG, "device '%s %s' connected, type %d",
                lm_device_get_name(ind->device), lm_device_get_path(ind->device), ind->type);
            if (ind->type == LM_DEVICE_BEARER_LE)
                lm_adapter_stop_adv(ind->adapter, adv);
            break;
        }
        case LM_DEVICE_DISCONNECTED_IND: {
            lm_device_disconnected_ind_t *ind = (lm_device_disconnected_ind_t *)buf;
            lm_log_debug(TAG, "device '%s %s' disconnected, type %d, reason %d",
                lm_device_get_name(ind->device), lm_device_get_path(ind->device),
                ind->type, ind->reason);
            if (ind->type == LM_DEVICE_BEARER_LE)
                lm_adapter_start_adv(ind->adapter, adv);
            break;
        }
        case LM_DEVICE_REMOVED_IND:
            break;
        default:
            break;
    }

    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_agent_callback(lm_msg_type_t msg,
                                     __attribute__((unused))lm_status_t status,
                                     void *buf)
{
    switch (msg) {
        case LM_AGENT_REQ_PASSKEY_IND: {
            lm_agent_req_passkey_ind_t *ind = (lm_agent_req_passkey_ind_t *)buf;
            lm_log_debug(TAG, "requesting passkey for '%s", lm_device_get_name(ind->device));
            lm_log_debug(TAG, "Enter 6 digit pin code: ");
            int result = fscanf(stdin, "%d", &ind->passkey);
            if (result != 1) {
                lm_log_error(TAG, "didn't read a pin code");
            }
            break;
        }
        default:
            break;
    }

    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_player_callback(__attribute__((unused)) lm_msg_type_t msg,
                                      __attribute__((unused)) lm_status_t status,
                                      __attribute__((unused)) void* buf)
{
    switch (msg) {
    case LM_PLAYER_STATUS_CHANGE_IND:
        break;
    case LM_PLAYER_TRACK_UPDATE_IND:
        break;
    default:
        break;
    }
    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_transport_callback(__attribute__((unused)) lm_msg_type_t msg,
                                        __attribute__((unused)) lm_status_t status,
                                        __attribute__((unused)) void* buf)
{
    switch (msg) {
    case LM_TRANSPORT_STATE_CHANGE_IND:
        break;
    case LM_TRANSPORT_QOS_UPDATE_IND:
        break;
    case LM_TRANSPORT_VOLUME_CHANGE_IND:
        break;
    default:
        break;
    }
    return LM_STATUS_SUCCESS;
}

static gboolean callback(gpointer data)
{
    if (adv) {
        if (lm_adapter_is_advertising(default_adapter))
            lm_adapter_stop_adv(default_adapter, adv);
        lm_adv_destroy(adv);
        adv = NULL;
    }

    if (agent) {
        lm_agent_destroy(agent);
        agent = NULL;
    }

    if (default_adapter) {
        lm_adapter_destroy(default_adapter);
        default_adapter = NULL;
    }
    /* unsubscribe callbacks */
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_ADAPTER, lm_adapter_callback);
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_DEVICE, lm_device_callback);
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_AGENT, lm_agent_callback);
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_PLAYER, lm_player_callback);
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_TRANSPORT, lm_transport_callback);
    lm_deinit();
    g_main_loop_quit((GMainLoop *) data);

    return FALSE;
}

static void cleanup_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        lm_log_error(TAG, "received signo:%d", signo);
        callback(loop);
        lm_log_info(TAG, "cleanup completed, exiting...");
    }
}

int tws_demo(void)
{
    guint timeout_ms = 0;

    // Setup signal handler
    if (signal(SIGINT, cleanup_handler) == SIG_ERR)
        lm_log_error(TAG, "can't catch SIGINT");
    else if (signal(SIGTERM, cleanup_handler) == SIG_ERR)
        lm_log_error(TAG, "can't catch SIGTERM");

    loop = g_main_loop_new(NULL, FALSE);

    lm_log_enabled(TRUE);
    lm_log_set_level(LM_LOG_INFO);
    lm_init();

    /* subscribe callbacks */
    lm_register_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_ADAPTER, lm_adapter_callback);
    lm_register_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_DEVICE, lm_device_callback);
    lm_register_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_AGENT, lm_agent_callback);
    lm_register_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_PLAYER, lm_player_callback);
    lm_register_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_TRANSPORT, lm_transport_callback);

    /* wait default appear */
    do {
        default_adapter = lm_adapter_get_default();
        if (default_adapter) {
            break;
        }
        g_usleep(1000 * 1000);
        timeout_ms++;
    } while (NULL == default_adapter && timeout_ms < 5);

    if (!default_adapter) {
        lm_log_error(TAG, "get default adapter timeout!");
        g_assert(0 && "can not found default adapter");
    }

    /* wait default adapter power on */
    if (!lm_adapter_is_power_on(default_adapter))
        lm_adapter_power_on(default_adapter);

    timeout_ms = 0;
    while (!lm_adapter_is_power_on(default_adapter) && timeout_ms < 5) {
        g_usleep(1000 * 1000);
        timeout_ms++;
    }

    if (!lm_adapter_is_power_on(default_adapter)) {
        lm_log_error(TAG, "default adapter is not powered on!");
        g_assert(0 && "wait default adapter power on timeout");
    }

    if (!lm_adapter_get_alias(default_adapter))
        lm_adapter_set_alias(default_adapter, "lm_le_tws");

    agent = lm_agent_create(default_adapter, LM_AGENT_IO_CAPA_NO_INPUT_NO_OUTPUT);

    /* setup advertisement */
    adv = lm_adv_create();
    lm_adv_set_local_name(adv, lm_adapter_get_alias(default_adapter));
    lm_adv_set_appearance(adv, 0x0840);
    lm_adv_set_discoverable(adv, TRUE);
    lm_adv_set_secondary_channel(adv, LM_ADV_SC_1M);
    lm_adv_set_rsi(adv);

    GPtrArray *adv_service_uuids = g_ptr_array_new();
    g_ptr_array_add(adv_service_uuids, (gpointer)BCAST_AUDIO_SCAN_SERVICE_UUID);
    g_ptr_array_add(adv_service_uuids, (gpointer)PUBLISHED_AUDIO_CAP_SERVICE_UUID);
    g_ptr_array_add(adv_service_uuids, (gpointer)VOLUME_CONTROL_SERVICE_UUID);
    g_ptr_array_add(adv_service_uuids, (gpointer)MICROPHONE_CONTROL_SERVICE_UUID);
    g_ptr_array_add(adv_service_uuids, (gpointer)COMMON_AUDIO_SERVICE_UUID);
    lm_adv_set_services(adv, adv_service_uuids);
    g_ptr_array_free(adv_service_uuids, TRUE);

    GByteArray *ascs_data_array = g_byte_array_new();
    // 0x00:Announcement Type
    // 0xFF,0x0F:Available sink contexts
    // 0x43,0x02:Available source context
    guint8 ascs_data[] = {0x00, 0xFF, 0x0F, 0x43, 0x02, 0x00};
    g_byte_array_append(ascs_data_array, ascs_data, sizeof(ascs_data));
    lm_adv_set_service_data(adv,
            AUDIO_STREAM_CONTROL_SERVICE_UUID, ascs_data_array);
    g_byte_array_free(ascs_data_array, TRUE);

    GByteArray *tmas_data_array = g_byte_array_new();
    //0x2A: call terminal, unicast media receiver, broadcast media receiver
    guint8 tmas_data[] = {0x2A, 0x00};
    g_byte_array_append(tmas_data_array, tmas_data, sizeof(tmas_data));
    lm_adv_set_service_data(adv,
            TELEPHONY_MEDIA_AUDIO_SERVICE_UUID, tmas_data_array);
    g_byte_array_free(tmas_data_array, TRUE);

    lm_adapter_start_adv(default_adapter, adv);
    lm_log_debug(TAG, "adv name '%s'", lm_adapter_get_alias(default_adapter));

    // Start the mainloop
    g_main_loop_run(loop);

    // Clean up mainloop
    g_main_loop_unref(loop);

    return 0;
}
