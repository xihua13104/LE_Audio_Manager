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
#include "lm_adv.h"
#include "lm_device.h"
#include "lm_agent.h"
#include "lm_gatt_server.h"
#include "lm_uuids.h"
#include <gio/gio.h>
#include <stdio.h>

#define TAG "gatt-server"

#define HTS_SERVICE_UUID "00001809-0000-1000-8000-00805f9b34fb"
#define TEMPERATURE_CHAR_UUID "00002a1c-0000-1000-8000-00805f9b34fb"
#define CUD_CHAR "00002901-0000-1000-8000-00805f9b34fb"

#define TEMP_MESURE_TIMER_LEN    1000 /* ms */

typedef struct {
    guint16 year;
    guint8  month;
    guint8  day;
    guint8  hour;
    guint8  minute;
    guint8  second;
} __attribute__((packed)) hts_timestamp_t;

typedef struct {
    guint8 flags;
    guint32 temp_value;
    hts_timestamp_t ts;
    guint8 temp_type;
} __attribute__((packed)) hts_temperature_char_value_t;

static GMainLoop *loop = NULL;
static lm_adapter_t *default_adapter = NULL;
static lm_adv_t *adv = NULL;
static lm_agent_t *agent = NULL;
static lm_gatt_server_t *gatt_server = NULL;
static guint temp_mesure_timer = 0;
static hts_temperature_char_value_t temperature = {
    .flags = 0x06,
    .temp_value = 0xff00016A, /* 36.2 degree Celsius */
    .ts = {
        .year = 2024,
        .month = 3,
        .day = 14,
        .hour = 15,
        .minute = 9,
        .second = 26
    },
    .temp_type = 0x01 /* armpit */
};

static lm_status_t lm_adapter_callback(lm_msg_type_t msg,
                                       __attribute__((unused)) lm_status_t status,
                                       void *buf)
{
    switch (msg) {
        case LM_ADAPTER_POWER_ON_CNF: {
            if (status == LM_STATUS_SUCCESS) {
                lm_adapter_power_on_cnf_t *cnf = (lm_adapter_power_on_cnf_t *)buf;
                lm_log_info(TAG, "adapter '%s' powered on", lm_adapter_get_path(cnf->adapter));
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
            if (ind->type == LM_DEVICE_BEARER_LE && adv)
                lm_adapter_stop_adv(ind->adapter, adv);
            break;
        }
        case LM_DEVICE_DISCONNECTED_IND: {
            lm_device_disconnected_ind_t *ind = (lm_device_disconnected_ind_t *)buf;
            lm_log_info(TAG, "device '%s %s' disconnected, type %d, reason %d",
                lm_device_get_name(ind->device), lm_device_get_path(ind->device),
                ind->type, ind->reason);
            if (ind->type == LM_DEVICE_BEARER_LE && adv)
                lm_adapter_start_adv(ind->adapter, adv);
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

static gboolean temp_mesure_timer_cb(gpointer user_data)
{
    lm_gatt_server_t *gatt_server = (lm_gatt_server_t *)user_data;

    temperature.ts.second += 1;
    /* For simplicity, only handling second and minute change */
    if (temperature.ts.second >= 60) {
        temperature.ts.second = 0;
        temperature.ts.minute += 1;
    }
    temperature.temp_value += 1; /* increase 0.1 degree Celsius */

    if (lm_gatt_server_is_notify_enabled(gatt_server, HTS_SERVICE_UUID, TEMPERATURE_CHAR_UUID)) {
        GByteArray *byteArray = g_byte_array_sized_new(sizeof(temperature));
        g_byte_array_append(byteArray, (guint8 *)&temperature, sizeof(temperature));
        lm_gatt_server_send_notify(gatt_server, HTS_SERVICE_UUID, TEMPERATURE_CHAR_UUID, byteArray);
        g_byte_array_free(byteArray, TRUE);
    }

    return TRUE; /* keep period timer */
}

static lm_status_t lm_gatt_srv_callback(lm_msg_type_t msg,
                                        __attribute__((unused)) lm_status_t status,
                                        void* buf)
{
    switch (msg) {
    case LM_GATT_SERVER_CHAR_READ_IND: {
        lm_gatt_server_char_read_ind_t *ind = (lm_gatt_server_char_read_ind_t *)buf;
        lm_log_info(TAG, "char read request on uuid <%s> from '%s'", ind->char_uuid, ind->client_addr);
        if (g_str_equal(ind->service_uuid, HTS_SERVICE_UUID) &&
                    g_str_equal(ind->char_uuid, TEMPERATURE_CHAR_UUID)) {
            GByteArray *byteArray = g_byte_array_sized_new(sizeof(temperature));
            g_byte_array_append(byteArray, (guint8 *)&temperature, sizeof(temperature));
            lm_gatt_server_set_char_value(gatt_server, ind->service_uuid, ind->char_uuid, byteArray);
            g_byte_array_free(byteArray, TRUE);
        }
        break;
    }
    case LM_GATT_SERVER_CHAR_WRT_IND: {
        lm_gatt_server_char_wrt_ind_t *ind = (lm_gatt_server_char_wrt_ind_t *)buf;
        lm_log_info(TAG, "char write request on uuid <%s> from '%s'", ind->char_uuid, ind->client_addr);
        lm_log_info(TAG, "write with value: ");
        lm_log_hex_dump_ba(LM_LOG_INFO, TAG, ind->byte_array);
        break;
    }
    case LM_GATT_SERVER_CHAR_UPDATED_IND: {
        lm_gatt_server_char_updated_ind_t *ind = (lm_gatt_server_char_updated_ind_t *)buf;
        lm_log_info(TAG, "char <%s> updated to: ", ind->char_uuid);
        lm_log_hex_dump_ba(LM_LOG_INFO, TAG, ind->byte_array);
        break;
    }
    case LM_GATT_SERVER_CHAR_NTF_ENABLED_IND: {
        lm_gatt_server_char_ntf_enabled_ind_t *ind = (lm_gatt_server_char_ntf_enabled_ind_t *)buf;
        lm_log_info(TAG, "notify enabled on char uuid <%s>", ind->char_uuid);
        if (!temp_mesure_timer) {
            temp_mesure_timer = g_timeout_add(TEMP_MESURE_TIMER_LEN,
                                            temp_mesure_timer_cb, (void *)ind->gatt_server);
        }
        break;
    }
    case LM_GATT_SERVER_CHAR_NTF_DISABLED_IND: {
        lm_gatt_server_char_ntf_disabled_ind_t *ind = (lm_gatt_server_char_ntf_disabled_ind_t *)buf;
        lm_log_info(TAG, "notify disabled on char uuid <%s>", ind->char_uuid);
        if (temp_mesure_timer) {
            g_source_remove(temp_mesure_timer);
            temp_mesure_timer = 0;
        }
        break;
    }
    case LM_GATT_SERVER_DESC_READ_IND: {
        lm_gatt_server_desc_read_ind_t *ind = (lm_gatt_server_desc_read_ind_t *)buf;
        lm_log_info(TAG, "desc read request on uuid <%s>", ind->desc_uuid);
        break;
    }
    case LM_GATT_SERVER_DESC_WRT_IND: {
        lm_gatt_server_desc_wrt_ind_t *ind = (lm_gatt_server_desc_wrt_ind_t *)buf;
        lm_log_info(TAG, "desc write request on uuid <%s>", ind->desc_uuid);
        break;
    }
    case LM_GATT_SERVER_DESC_UPDATED_IND: {
        lm_gatt_server_desc_updated_ind_t *ind = (lm_gatt_server_desc_updated_ind_t *)buf;
        lm_log_info(TAG, "desc <%s> updated to: ", ind->desc_uuid);
        lm_log_hex_dump_ba(LM_LOG_INFO, TAG, ind->byte_array);
        break;
    }
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

    if (temp_mesure_timer) {
        g_source_remove(temp_mesure_timer);
        temp_mesure_timer = 0;
    }

    if (gatt_server) {
        lm_adapter_unregister_gatt_server(default_adapter, gatt_server);
        lm_gatt_server_destroy(gatt_server);
        gatt_server = NULL;
    }

    if (default_adapter) {
        lm_adapter_destroy(default_adapter);
        default_adapter = NULL;
    }
    /* unsubscribe callbacks */
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_ADAPTER, lm_adapter_callback);
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_DEVICE, lm_device_callback);
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_AGENT, lm_agent_callback);
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_GATT_SRV, lm_gatt_srv_callback);
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

int gatt_srv_demo(void)
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
    lm_register_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_GATT_SRV, lm_gatt_srv_callback);
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
        lm_adapter_set_alias(default_adapter, "lm_speaker");

    lm_adapter_discoverable_on(default_adapter);

    lm_adapter_connectable_on(default_adapter);

    agent = lm_agent_create(default_adapter, LM_AGENT_IO_CAPA_NO_INPUT_NO_OUTPUT);

    /* setup advertisement */
    adv = lm_adv_create();
    lm_adv_set_local_name(adv, lm_adapter_get_alias(default_adapter));
    lm_adv_set_appearance(adv, 0x0840);
    lm_adv_set_discoverable(adv, TRUE);
    lm_adv_set_secondary_channel(adv, LM_ADV_SC_1M);

    GPtrArray *adv_service_uuids = g_ptr_array_new();
    g_ptr_array_add(adv_service_uuids, (gpointer)LM_VENDOR_SERVICE_UUID);
    g_ptr_array_add(adv_service_uuids, (gpointer)HTS_SERVICE_UUID);
    lm_adv_set_services(adv, adv_service_uuids);
    g_ptr_array_free(adv_service_uuids, TRUE);

    lm_adapter_start_adv(default_adapter, adv);
    lm_log_info(TAG, "adv name '%s'", lm_adapter_get_alias(default_adapter));

    gatt_server = lm_gatt_server_create(default_adapter);
    lm_gatt_server_add_service(gatt_server, HTS_SERVICE_UUID);
    lm_gatt_server_add_char(gatt_server,
                            HTS_SERVICE_UUID,
                            TEMPERATURE_CHAR_UUID,
                            LM_GATT_PROP_NOTIFY | LM_GATT_PROP_READ | LM_GATT_PROP_WRITE);
    lm_gatt_server_add_desc(gatt_server,
                            HTS_SERVICE_UUID,
                            TEMPERATURE_CHAR_UUID,
                            CUD_CHAR,
                            LM_GATT_PROP_READ | LM_GATT_PROP_WRITE);

    const guint8 cud[] = "hello there";
    GByteArray *cudArray = g_byte_array_sized_new(sizeof(cud));
    g_byte_array_append(cudArray, cud, sizeof(cud));
    lm_gatt_server_set_desc_value(gatt_server, HTS_SERVICE_UUID, TEMPERATURE_CHAR_UUID, CUD_CHAR, cudArray);

    lm_adapter_register_gatt_server(default_adapter, gatt_server);
    // Start the mainloop
    g_main_loop_run(loop);

    // Clean up mainloop
    g_main_loop_unref(loop);

    return 0;
}
