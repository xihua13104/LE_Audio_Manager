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

#define TAG "broadcaster"

static GMainLoop *loop = NULL;
static lm_adapter_t *default_adapter = NULL;

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
        case LM_ADAPTER_LOCAL_BCAST_TRANSPORT_STATE_CHANGE_IND: {
            lm_adapter_local_bcast_transport_state_change_ind_t *ind = (lm_adapter_local_bcast_transport_state_change_ind_t *)buf;
            lm_log_info(TAG, "local bcast transport state %d",
                lm_transport_get_state(ind->transport));
            if (lm_transport_get_state(ind->transport) == LM_TRANSPORT_ACTIVE) {
                lm_log_info(TAG, "bcast active '%s'", lm_transport_get_path(ind->transport));
            } else if (lm_transport_get_state(ind->transport) == LM_TRANSPORT_IDLE) {
                lm_log_info(TAG, "bcast idle '%s'", lm_transport_get_path(ind->transport));
            }
            break;
        }
        default:
            break;
    }

    return LM_STATUS_SUCCESS;
}

static gboolean callback(gpointer data)
{
    if (default_adapter) {
        lm_adapter_destroy(default_adapter);
        default_adapter = NULL;
    }
    /* unsubscribe callbacks */
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_ADAPTER, lm_adapter_callback);
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

int broadcaster_demo(void)
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

    /* wait default adapter appear */
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
        lm_adapter_set_alias(default_adapter, "lm_broadcaster");

    lm_adapter_discoverable_off(default_adapter);

    lm_adapter_connectable_off(default_adapter);

    // Start the mainloop
    g_main_loop_run(loop);

    // Clean up mainloop
    g_main_loop_unref(loop);

    return 0;
}
