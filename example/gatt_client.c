/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

#include "lm.h"
#include "lm_log.h"
#include "lm_adapter.h"
#include "lm_parser.h"
#include "lm_device.h"
#include "lm_agent.h"
#include "lm_gatt_client.h"
#include "lm_gatt.h"
#include "lm_uuids.h"
#include <gio/gio.h>
#include <stdio.h>

#define TAG "gatt-client"

#define HTS_SERVICE_UUID "00001809-0000-1000-8000-00805f9b34fb"
#define TEMPERATURE_CHAR_UUID "00002a1c-0000-1000-8000-00805f9b34fb"
#define DIS_SERVICE "0000180a-0000-1000-8000-00805f9b34fb"
#define DIS_MANUFACTURER_CHAR "00002a29-0000-1000-8000-00805f9b34fb"
#define DIS_MODEL_CHAR "00002a24-0000-1000-8000-00805f9b34fb"
#define CUD_CHAR "00002901-0000-1000-8000-00805f9b34fb"

static GMainLoop *loop = NULL;
static lm_adapter_t *default_adapter = NULL;
static lm_agent_t *agent = NULL;

static lm_status_t lm_adapter_callback(lm_msg_type_t msg,
                                       __attribute__((unused)) lm_status_t status,
                                       void *buf)
{
    switch (msg) {
        case LM_ADAPTER_POWER_ON_CNF: {
            if (status == LM_STATUS_SUCCESS) {
                lm_adapter_power_on_cnf_t *cnf = (lm_adapter_power_on_cnf_t *)buf;
                lm_log_info(TAG, "adapter '%s' powered on", lm_adapter_get_path(cnf->adapter));
                lm_adapter_start_discovery(cnf->adapter);
            }
            break;
        }
        case LM_ADAPTER_POWER_OFF_CNF: {
            if (status == LM_STATUS_SUCCESS) {
                lm_adapter_power_off_cnf_t *cnf = (lm_adapter_power_off_cnf_t *)buf;
                lm_log_info(TAG, "adapter '%s' powered off", lm_adapter_get_path(cnf->adapter));
            }
            break;
        }
        case LM_ADAPTER_DISCOVERY_RESULT_IND: {
            lm_adapter_discovery_result_ind_t *ind = (lm_adapter_discovery_result_ind_t *)buf;
            lm_log_info(TAG, "found target device '%s'", lm_device_get_path(ind->device));
            gchar *s = lm_device_to_string(ind->device);
            lm_log_info(TAG, "device info: %s", s);
            g_free(s);
            lm_adapter_stop_discovery(ind->adapter);
            lm_device_connect_bearer(ind->device, LM_DEVICE_BEARER_LE);
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
            lm_log_info(TAG, "device '%s %s' connected, type %d",
                lm_device_get_name(ind->device), lm_device_get_path(ind->device), ind->type);
            break;
        }
        case LM_DEVICE_DISCONNECTED_IND: {
            lm_device_disconnected_ind_t *ind = (lm_device_disconnected_ind_t *)buf;
            lm_log_info(TAG, "device '%s %s' disconnected, type %d, reason %d",
                lm_device_get_name(ind->device), lm_device_get_path(ind->device),
                ind->type, ind->reason);
            if (lm_device_get_bonding_state(ind->device) != LM_DEVICE_BONDED)
                lm_adapter_remove_device(ind->adapter, ind->device);

            lm_adapter_start_discovery(ind->adapter);
            break;
        }
        case LM_DEVICE_REMOVED_IND: {
            break;
        }
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
            lm_log_info(TAG, "requesting passkey for '%s", lm_device_get_name(ind->device));
            lm_log_info(TAG, "Enter 6 digit pin code: ");
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

static lm_status_t lm_gatt_cli_callback(lm_msg_type_t msg,
                                        __attribute__((unused)) lm_status_t status,
                                        void* buf)
{
    switch (msg) {
    case LM_GATT_CLIENT_CHAR_READ_CNF: {
        lm_gatt_client_char_read_cnf_t *ind = (lm_gatt_client_char_read_cnf_t *)buf;
        lm_log_info(TAG, "on characteristic <%s> read", ind->char_uuid);
        lm_parser_t *parser = lm_parser_create(ind->byte_array, LM_PARSER_LITTLE_ENDIAN);
        if (g_str_equal(ind->char_uuid, DIS_MANUFACTURER_CHAR)) {
            GString *manufacturer = lm_parser_get_string(parser);
            lm_log_info(TAG, "manufacturer = %s", manufacturer->str);
            g_string_free(manufacturer, TRUE);
        } else if (g_str_equal(ind->char_uuid, DIS_MODEL_CHAR)) {
            GString *model = lm_parser_get_string(parser);
            lm_log_info(TAG, "model = %s", model->str);
            g_string_free(model, TRUE);
        }
        lm_parser_destroy(parser);
        break;
    }
    case LM_GATT_CLIENT_CHAR_WRT_CNF:
        break;
    case LM_GATT_CLIENT_DESC_READ_CNF: {
        lm_gatt_client_desc_read_cnf_t *ind = (lm_gatt_client_desc_read_cnf_t *)buf;
        lm_log_info(TAG, "on descriptor <%s> read", ind->desc_uuid);
        lm_parser_t *parser = lm_parser_create(ind->byte_array, LM_PARSER_LITTLE_ENDIAN);
        GString *parsed_string = lm_parser_get_string(parser);
        lm_log_info(TAG, "CUD %s", parsed_string->str);
        lm_parser_destroy(parser);
        break;
    }
    case LM_GATT_CLIENT_DESC_WRT_CNF:
        break;
    case LM_GATT_CLIENT_NTF_ENABLE_CNF: {
        lm_gatt_client_ntf_enable_cnf_t *ind = (lm_gatt_client_ntf_enable_cnf_t *)buf;
        lm_log_info(TAG, "char <%s> notify enable", ind->char_uuid);
        break;
    }
    case LM_GATT_CLIENT_NTF_DISABLE_CNF: {
        lm_gatt_client_ntf_disable_cnf_t *ind = (lm_gatt_client_ntf_disable_cnf_t *)buf;
        lm_log_info(TAG, "char <%s> notify disable", ind->char_uuid);
        break;
    }
    case LM_GATT_CLIENT_NTF_IND: {
        lm_gatt_client_ntf_ind_t *ind = (lm_gatt_client_ntf_ind_t *)buf;
        lm_log_info(TAG, "char <%s> notify received, len %d", ind->char_uuid, ind->byte_array->len);
        lm_parser_t *parser = lm_parser_create(ind->byte_array, LM_PARSER_LITTLE_ENDIAN);
        if (g_str_equal(ind->char_uuid, TEMPERATURE_CHAR_UUID)) {
            lm_parser_set_offset(parser, 1);
            double temperature = lm_parser_get_11073float(parser);
            lm_log_info(TAG, "temperature %.1f", temperature);

            lm_parser_set_offset(parser, 5);
            GDateTime *date_time = lm_parser_get_date_time(parser);
            if (date_time) {
                lm_log_info(TAG, "date time: %s", g_date_time_format(date_time, "%Y-%m-%d %H:%M:%S"));
                g_date_time_unref(date_time);
            }
        }
        lm_parser_destroy(parser);
        break;
    }
    case LM_GATT_CLIENT_SERVICES_RESOLVED_IND: {
        lm_gatt_client_services_resolved_ind_t *ind = (lm_gatt_client_services_resolved_ind_t *)buf;
        lm_log_info(TAG, "device '%s' GATT services resolved", lm_device_get_path(ind->device));
        lm_gatt_client_read_char(ind->device, DIS_SERVICE, DIS_MANUFACTURER_CHAR);
        lm_gatt_client_read_char(ind->device, DIS_SERVICE, DIS_MODEL_CHAR);
        lm_gatt_client_enable_notify(ind->device, HTS_SERVICE_UUID, TEMPERATURE_CHAR_UUID);
        lm_gatt_client_read_desc(ind->device, HTS_SERVICE_UUID, TEMPERATURE_CHAR_UUID, CUD_CHAR);
        break;
    }
    default:
        break;
    }

    return LM_STATUS_SUCCESS;
}

static gboolean callback(gpointer data)
{
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
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_GATT_CLI, lm_gatt_cli_callback);
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

int gatt_cli_demo(void)
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
    lm_register_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_GATT_CLI, lm_gatt_cli_callback);
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

    lm_adapter_discoverable_on(default_adapter);

    lm_adapter_connectable_on(default_adapter);

    agent = lm_agent_create(default_adapter, LM_AGENT_IO_CAPA_NO_INPUT_NO_OUTPUT);

    GPtrArray *service_uuids = g_ptr_array_new();
    g_ptr_array_add(service_uuids, LM_VENDOR_SERVICE_UUID);
    lm_adapter_set_discovery_filter(default_adapter, -100, service_uuids, NULL, 0, 0);
    g_ptr_array_free(service_uuids, TRUE);

    // Start the mainloop
    g_main_loop_run(loop);

    // Clean up mainloop
    g_main_loop_unref(loop);

    return 0;
}
