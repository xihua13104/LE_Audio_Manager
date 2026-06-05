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
#include "lm_gatt_server.h"
#include "lm_uuids.h"
#include "lm_profile.h"
#include <gio/gio.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <glib-unix.h>

#define TAG "speaker"

#define HTS_SERVICE_UUID "00001809-0000-1000-8000-00805f9b34fb"
#define TEMPERATURE_CHAR_UUID "00002a1c-0000-1000-8000-00805f9b34fb"
#define CUD_CHAR "00002901-0000-1000-8000-00805f9b34fb"
#define IAP2_PROFILE_UUID "00000000-deca-fade-deca-deafdecacaff"

#define TEMP_MESURE_TIMER_LEN    1000 /* ms */

static GMainLoop *loop = NULL;
static lm_adapter_t *default_adapter = NULL;
static lm_adv_t *adv = NULL;
static lm_agent_t *agent = NULL;
static lm_device_t *active_bis_src_dev = NULL;
static lm_device_t *bis_assistant = NULL;
static lm_profile_t *iap2_profile = NULL;
static GList *found_bis_src_devs = NULL;
static gint iap2_fd = -1;
static lm_transport_bcast_code_t bcode = { {'l', 'm', 'b', 'i', 's'} };

static const gchar *iap2_service_record =
"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"
"<record>"
    /*
     * Service Class ID List
     */
    "<attribute id=\"0x0001\">"
        "<sequence>"
            "<uuid value=\"00000000-deca-fade-deca-deafdecacaff\"/>"
        "</sequence>"
    "</attribute>"

    /*
     * Protocol Descriptor List
     * RFCOMM channel 23
     */
    "<attribute id=\"0x0004\">"
        "<sequence>"

            /* L2CAP */
            "<sequence>"
                "<uuid value=\"0x0100\"/>"
            "</sequence>"

            /* RFCOMM */
            "<sequence>"
                "<uuid value=\"0x0003\"/>"
                "<uint8 value=\"0x17\"/>"
            "</sequence>"

        "</sequence>"
    "</attribute>"

    /*
     * Browse Group List
     */
    "<attribute id=\"0x0005\">"
        "<sequence>"
            "<uuid value=\"0x1002\"/>"
        "</sequence>"
    "</attribute>"

    /*
     * Bluetooth Profile Descriptor List
     */
    "<attribute id=\"0x0009\">"
        "<sequence>"
            "<sequence>"
                "<uuid value=\"00000000-deca-fade-deca-deafdecacaff\"/>"
                "<uint16 value=\"0x0100\"/>"
            "</sequence>"
        "</sequence>"
    "</attribute>"

    /*
     * Service Name
     */
    "<attribute id=\"0x0100\">"
        "<text value=\"iAP2\"/>"
    "</attribute>"

    "<attribute id=\"0xF000\">"
        "<sequence>"
            "<uint16 value=\"0x0001\"/>"
        "</sequence>"
    "</attribute>"

"</record>";

static lm_device_t *find_device_with_max_rssi(GList *device_list)
{
    GList *l;
    lm_device_t *max_rssi_dev = NULL;
    gint16 max_rssi = G_MININT16;

    for (l = device_list; l != NULL; l = l->next) {
        lm_device_t *dev = l->data;
        gint16 rssi = lm_device_get_rssi(dev);
        lm_log_debug(TAG, "device '%s' rssi %d", lm_device_get_path(dev), rssi);
        if (rssi > max_rssi) {
            max_rssi = rssi;
            max_rssi_dev = dev;
        }
    }

    return max_rssi_dev;
}

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
        case LM_ADAPTER_BCAST_DISCOVERED_IND: {
            lm_adapter_bcast_discovered_ind_t *ind = (lm_adapter_bcast_discovered_ind_t *)buf;
            lm_log_info(TAG, "found remote bcast device '%s'", lm_device_get_path(ind->device));
            char *s = lm_device_to_string(ind->device);
            lm_log_info(TAG, "device info: %s", s);
            g_free(s);

            GList *uuids = lm_device_get_uuids(ind->device);
            if (!g_list_find_custom(uuids, LM_VENDOR_SERVICE_UUID, (GCompareFunc)g_strcmp0) &&
                !g_list_find_custom(uuids, BCAST_AUDIO_AUNOUNCEMENT_SERVICE_UUID, (GCompareFunc)g_strcmp0)) {
                lm_log_info(TAG, "unknown bcast device '%s'", lm_device_get_path(ind->device));
                break;
            }
            /* prevent sync multiple bcast devices */
            if (active_bis_src_dev) {
                lm_log_warn(TAG, "active bcast device found: %s",
                                            lm_device_get_path(active_bis_src_dev));
                break;
            }

            if (ind->method == LM_ADAPTER_BCAST_DISCOVERED_BY_SINK_SCAN) {
                lm_log_info(TAG, "bcast found by local scan");
                if (!g_list_find(found_bis_src_devs, ind->device))
                    found_bis_src_devs = g_list_append(found_bis_src_devs, ind->device);
                if (LM_ADAPTER_DISCOVERY_STOPPED == lm_adapter_get_discovery_state(
                    lm_device_get_adapter(ind->device))) {
                    lm_log_info(TAG, "bcast found after discovery stopped");
                    lm_device_t *device = find_device_with_max_rssi(found_bis_src_devs);
                    if (device) {
                        lm_log_info(TAG, "device with max RSSI(%d) found: %s",
                            lm_device_get_rssi(device), lm_device_get_path(device));
                        lm_device_start_sync_bcast(device,
                                                LM_TRANSPORT_AUDIO_LOCATION_FL |
                                                LM_TRANSPORT_AUDIO_LOCATION_FR,
                                                &bcode);
                    }
                }
            } else if (ind->method == LM_ADAPTER_BCAST_DISCOVERED_BY_ASSISTANT) {
                bis_assistant = ind->assistant;
                lm_log_info(TAG, "bcast device found by assistant '%s'",
                                            lm_device_get_path(ind->assistant));
                lm_device_start_sync_bcast(ind->device,
                                                LM_TRANSPORT_AUDIO_LOCATION_FL |
                                                LM_TRANSPORT_AUDIO_LOCATION_FR,
                                                NULL);
            }
            break;
        }
        case LM_ADAPTER_DISCOVERY_COMPLETE_IND: {
            lm_log_info(TAG, "adapter discovery complete");
            /* prevent sync multiple bcast devices */
            if (active_bis_src_dev) {
                lm_log_warn(TAG, "active bcast device found: %s",
                                    lm_device_get_path(active_bis_src_dev));
                break;
            }

            lm_device_t *device = find_device_with_max_rssi(found_bis_src_devs);
            if (device) {
                lm_log_info(TAG, "device with max RSSI(%d) found: %s",
                    lm_device_get_rssi(device), lm_device_get_path(device));
                lm_device_start_sync_bcast(device,
                                                LM_TRANSPORT_AUDIO_LOCATION_FL |
                                                LM_TRANSPORT_AUDIO_LOCATION_FR,
                                                &bcode);
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
            if (ind->type == LM_DEVICE_BEARER_LE && adv)
                lm_adapter_stop_adv(ind->adapter, adv);
            break;
        }
        case LM_DEVICE_DISCONNECTED_IND: {
            lm_device_disconnected_ind_t *ind = (lm_device_disconnected_ind_t *)buf;
            lm_log_debug(TAG, "device '%s %s' disconnected, type %d, reason %d",
                lm_device_get_name(ind->device), lm_device_get_path(ind->device),
                ind->type, ind->reason);
            if (ind->type == LM_DEVICE_BEARER_LE && adv)
                lm_adapter_start_adv(ind->adapter, adv);
            break;
        }
        case LM_DEVICE_BCAST_SYNC_UP_IND: {
            lm_device_bcast_sync_up_ind_t *ind = (lm_device_bcast_sync_up_ind_t *)buf;
            lm_log_info(TAG, "'%s' bcast sync up, status 0x%x",
                        lm_device_get_path(ind->device), status);
            if (status == LM_STATUS_SUCCESS)
                active_bis_src_dev = ind->device;
            else {
                g_list_free(found_bis_src_devs);
                found_bis_src_devs = NULL;
            }
            break;
        }
        case LM_DEVICE_BCAST_SYNC_LOST_IND: {
            lm_device_bcast_sync_lost_ind_t *ind = (lm_device_bcast_sync_lost_ind_t *)buf;
            lm_log_info(TAG, "'%s' bcast sync lost", lm_device_get_path(ind->device));
            /* clean cached devices found by discovery, free node only, because BlueZ
             * will remove temporary devices automatically.
             */
            g_list_free(found_bis_src_devs);
            found_bis_src_devs = NULL;

            active_bis_src_dev = NULL;
            break;
        }
        case LM_DEVICE_REMOVED_IND: {
            lm_device_removed_ind_t *ind = (lm_device_removed_ind_t *)buf;
            if (found_bis_src_devs) {
                found_bis_src_devs = g_list_remove(found_bis_src_devs, ind->device);
            }
            if (ind->device == active_bis_src_dev)
                active_bis_src_dev = NULL;
            else if (ind->device == bis_assistant)
                bis_assistant = NULL;
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

static void hex_dump(const uint8_t *buf, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0)
            printf(" ");
    }

    if (len % 16)
        printf(" ");
}

static gboolean iap2_io_cb(__attribute__((unused)) int fd,
                           __attribute__((unused)) GIOCondition cond,
                           __attribute__((unused)) gpointer data)
{
    uint8_t buf[1024];

    if (fd != iap2_fd) {
        lm_log_error(TAG, "unexpected fd %d, expected %d", fd, iap2_fd);
        return FALSE;
    }

    if (cond & (G_IO_HUP | G_IO_ERR)) {
        lm_log_info(TAG, "iAP2 disconnected");
        close(iap2_fd);
        iap2_fd = -1;
        return FALSE;
    }

    int ret = read(iap2_fd, buf, sizeof(buf));

    if (ret > 0) {
        lm_log_info(TAG, "iAP2 RX %d bytes", ret);
        hex_dump(buf, ret);
    } else if (ret <= 0) {
        lm_log_info(TAG, "iAP2 closed");
        close(iap2_fd);
        iap2_fd = -1;
        return FALSE;
    }

    return TRUE;
}

static void iap2_test_write(void)
{
    uint8_t detect_pkt[] = { 0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10 };

    if (iap2_fd < 0) {
        lm_log_info(TAG, "iAP2 not connected");
        return;
    }

    int ret = write(iap2_fd, detect_pkt, sizeof(detect_pkt));
    if (ret < 0) {
        lm_log_info(TAG, "iAP2 write failed: %s",
               strerror(errno));
    } else {
        lm_log_info(TAG, "iAP2 TX %d bytes", ret);
        hex_dump(detect_pkt, sizeof(detect_pkt));
    }
}

static lm_status_t lm_profile_callback(lm_msg_type_t msg,
                    __attribute__((unused)) lm_status_t status,
                    void* buf)
{
    switch (msg) {
    case LM_PROFILE_CONNECTION_IND: {
        lm_profile_connection_ind_t *ind = (lm_profile_connection_ind_t *)buf;
        lm_log_info(TAG, "profile '%s' connected on device '%s', fd %d, version %d, features %08x",
            lm_profile_get_name(ind->profile), lm_device_get_path(ind->device), ind->fd, ind->version, ind->features);
        iap2_fd = ind->fd;
        g_unix_fd_add(iap2_fd, G_IO_IN | G_IO_HUP | G_IO_ERR, iap2_io_cb, NULL);
        /*
         * test write
         */
        iap2_test_write();
        break;
    }
    case LM_PROFILE_DISCONNECTION_IND: {
        lm_profile_disconnection_ind_t *ind = (lm_profile_disconnection_ind_t *)buf;
        lm_log_info(TAG, "profile '%s' disconnected from device '%s'",
            lm_profile_get_name(ind->profile), lm_device_get_path(ind->device));
        if (iap2_fd >= 0) {
            close(iap2_fd);
            iap2_fd = -1;
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

    if (iap2_profile) {
        lm_profile_unregister(iap2_profile);
        lm_profile_destroy(iap2_profile);
        iap2_profile = NULL;
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
    lm_unregister_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_PROFILE, lm_profile_callback);
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

int speaker_demo(void)
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
    lm_register_callback(LM_CALLBACK_TYPE_APP_EVENT, MODULE_MASK_PROFILE, lm_profile_callback);
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

    iap2_profile = lm_profile_create(default_adapter, IAP2_PROFILE_UUID);
    lm_profile_set_name(iap2_profile, "iAP2");
    lm_profile_set_channel(iap2_profile, 23);
    lm_profile_set_service_record(iap2_profile, iap2_service_record);
    lm_profile_register(iap2_profile);

    // Start the mainloop
    g_main_loop_run(loop);

    // Clean up mainloop
    g_main_loop_unref(loop);

    return 0;
}
