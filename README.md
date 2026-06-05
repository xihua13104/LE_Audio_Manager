# bluez_inc_enhance
This repository is an enhancement to bluez_inc(https://github.com/weliem/bluez_inc), adding support for Classic BT and partial support for LE audio.

## 1. Introduction

Initially, I used bluez_inc to develop LE audio, so I named this library LEA Manager(LE Audio Manager). However, as the project's requirements expanded, Classic BT was also added. Therefore, you'll see everything prefixed with "lm".

LEA Manager is a C library that provides a clean, high-level interface to the BlueZ D-Bus API for both LE Audio and Classic/BLE applications.

The library abstracts the complexity of D-Bus communication and allows applications to manage Bluetooth audio devices using simple C APIs.

LEA Manager supports:

* LE Audio profiles and broadcast audio
* Classic Bluetooth audio profiles
* BLE/Classic device management
* GATT client/server operations
* Media playback and transport control
* Bluetooth pairing and authentication

---

## 2. Supported Features

### LE Audio

* BAP (Basic Audio Profile)
* BAP Broadcast (Broadcast Audio / BIS)
* PACS (Published Audio Capabilities Service)
* ASCS (Audio Stream Control Service)
* BASS (Broadcast Audio Scan Service)
* VCS (Volume Control Service)
* CAS (Common Audio Service)

### Classic / BLE

* A2DP
* AVRCP
* Classic Profiles
* GATT Server
* GATT Client
* Device Discovery
* Pairing and Bonding

---

## 3. Architecture

```text
+------------------+     +------------------+     +------------------+
|   Application    | <-> |   LEA Manager    | <-> |   BlueZ D-Bus    |
+------------------+     +------------------+     +------------------+
                                |
          +---------------------+---------------------+
          |                     |                     |
    +-----+-----+         +-----+-----+         +-----+-----+
    |   Adapter |         |   Device  |         |GATT Client|
    +-----------+         +-----------+         +-----------+
    |  LE Adv   |         |   Player  |         |GATT Server|
    +-----------+         +-----------+         +-----------+
    |   Agent   |         | Transport |         |  Parser   |
    +-----------+         +-----------+         +-----------+
```

---

## 4. Dependencies

Required runtime and development dependencies:

* GLib 2.0
* Gio / GDBus
* BlueZ 5.x or later
* libbluetooth


Install on Ubuntu / Debian:

```bash
sudo apt install -y libglib2.0-dev libbluetooth-dev
```

### LE Audio Requirements

LE Audio functionality requires:

* BlueZ 5.85 or later
* Experimental BlueZ features enabled
* Linux kernel 6.12 or later with ISO socket support
* Bluetooth controller firmware with LE Audio support
* Pipewire

Start BlueZ with:

```bash
bluetoothd --experimental
```

---

## 5. Programming Model

All LEA Manager APIs are asynchronous.

API return values only indicate whether the request was accepted. Actual completion or failure is reported later through callbacks.

Example:

```c
lm_adapter_power_on(adapter);
```

The function returns immediately. The adapter is not ready until the following event is received:

```c
LM_ADAPTER_POWER_ON_CNF
```

Similarly:

* `lm_device_connect()` completes when a device-connected event is received
* `lm_adapter_start_discovery()` completes when discovery starts and devices are reported
* `lm_device_pair()` completes when pairing-related events are received

### GLib Main Loop Requirement

Callbacks are executed in the GLib main loop context.

Applications must create and run a main loop:

```c
GMainLoop *loop = g_main_loop_new(NULL, FALSE);
g_main_loop_run(loop);
```

Do not block inside callbacks.

---

## 6. Typical Application Flow

```text
Application Startup
    ↓
lm_init()
    ↓
Register callbacks
    ↓
Get default adapter
    ↓
Power on adapter
    ↓
Wait for LM_ADAPTER_POWER_ON_CNF
    ↓
Start discovery / advertising / pairing / connection
```

---

## 7. Initialization

```c
#include "lm.h"
#include "lm_log.h"
#include "lm_adapter.h"
#include "lm_device.h"

#define TAG "myapp"

static lm_status_t lm_adapter_callback(lm_msg_type_t msg,
                                       lm_status_t status,
                                       void *buf)
{
    switch (msg) {
    case LM_ADAPTER_POWER_ON_CNF: {
        if (status == LM_STATUS_SUCCESS) {
            lm_adapter_power_on_cnf_t *cnf = buf;
            lm_log_info(TAG, "adapter '%s' powered on",
                        lm_adapter_get_path(cnf->adapter));
        }
        break;
    }

    case LM_ADAPTER_POWER_OFF_CNF: {
        if (status == LM_STATUS_SUCCESS) {
            lm_adapter_power_off_cnf_t *cnf = buf;
            lm_log_info(TAG, "adapter '%s' powered off",
                        lm_adapter_get_path(cnf->adapter));
        }
        break;
    }

    default:
        break;
    }

    return LM_STATUS_SUCCESS;
}

int main(void)
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    lm_log_enabled(TRUE);
    lm_log_set_level(LM_LOG_DEBUG);

    lm_init();

    lm_register_callback(
        LM_CALLBACK_TYPE_APP_EVENT,
        MODULE_MASK_ADAPTER,
        lm_adapter_callback);

    lm_adapter_t *adapter = lm_adapter_get_default();

    if (!lm_adapter_is_power_on(adapter)) {
        lm_status_t ret = lm_adapter_power_on(adapter);
        if (ret != LM_STATUS_SUCCESS) {
            lm_log_error(TAG, "failed to request power on: %d", ret);
        }
    }

    g_main_loop_run(loop);

    lm_deinit();
    g_main_loop_unref(loop);

    return 0;
}
```

---

## 8. Callback Registration

Register callbacks using `lm_register_callback()`:

```c
lm_register_callback(
    LM_CALLBACK_TYPE_APP_EVENT,
    MODULE_MASK_ADAPTER | MODULE_MASK_DEVICE,
    my_callback);
```

### Module Masks

| Mask                    | Description                |
| ----------------------- | -------------------------- |
| `MODULE_MASK_ADAPTER`   | Adapter events             |
| `MODULE_MASK_DEVICE`    | Device events              |
| `MODULE_MASK_TRANSPORT` | Transport events           |
| `MODULE_MASK_PLAYER`    | Media player events        |
| `MODULE_MASK_PROFILE`   | Classic profile events     |
| `MODULE_MASK_AGENT`     | Pairing and authentication |
| `MODULE_MASK_GATT_SRV`  | GATT server events         |
| `MODULE_MASK_GATT_CLI`  | GATT client events         |

### Callback Behavior

* A callback receives all events whose module matches the registered mask
* Multiple callbacks may be registered for the same module
* A single callback may register multiple module masks
* Callback invocation order is unspecified

Example:

```c
lm_register_callback(
    LM_CALLBACK_TYPE_APP_EVENT,
    MODULE_MASK_ADAPTER,
    adapter_callback);

lm_register_callback(
    LM_CALLBACK_TYPE_APP_EVENT,
    MODULE_MASK_DEVICE,
    device_callback);
```

---

## 9. Device Discovery and Connection

Start discovery after the adapter has been powered on.

```c
lm_adapter_t *adapter = lm_adapter_get_default();

lm_status_t ret = lm_adapter_start_discovery(adapter);
if (ret != LM_STATUS_SUCCESS) {
    lm_log_error(TAG, "failed to start discovery: %d", ret);
}
```

### Discovery Filter

`lm_adapter_set_discovery_filter()` allows the application to limit which devices are reported during discovery.

```c
void lm_adapter_set_discovery_filter(
    lm_adapter_t *adapter,
    gint16 rssi_threshold,
    const GPtrArray *service_uuids,
    const gchar *pattern,
    guint max_devices,
    guint timeout);
```

| Parameter        | Description                                                                                                 |
| ---------------- | ----------------------------------------------------------------------------------------------------------- |
| `adapter`        | Bluetooth adapter returned by `lm_adapter_get_default()`                                                    |
| `rssi_threshold` | Minimum RSSI value. Devices with weaker signal are ignored. Valid range: `-127` to `20` dBm                 |
| `service_uuids`  | Optional list of Bluetooth service UUIDs. Only devices advertising at least one of these UUIDs are reported |
| `pattern`        | Optional device name filter                                                                                 |
| `max_devices`    | Maximum number of matching devices to report. `0` means unlimited                                           |
| `timeout`        | Discovery timeout in milliseconds. `0` means no timeout                                                     |

The configured filter is applied before devices are reported through `LM_ADAPTER_DISCOVERY_RESULT_IND`.

Calling `lm_adapter_set_discovery_filter()` replaces any previously configured filter.

#### RSSI Filter Example

Only report nearby devices stronger than `-60 dBm`:

```c
lm_adapter_set_discovery_filter(adapter,
                                -60,
                                NULL,
                                NULL,
                                0,
                                0);
```

Typical RSSI values:

| RSSI        | Approximate Distance |
| ----------- | -------------------- |
| `-40 dBm`   | Very close           |
| `-60 dBm`   | Nearby room          |
| `-80 dBm`   | Far away             |
| `< -90 dBm` | Weak / unreliable    |

#### Service UUID Filter Example

Only discover devices advertising specific Bluetooth services.

Example: discover LE Audio devices advertising BASS and PACS.

```c
GPtrArray *uuids = g_ptr_array_new();

g_ptr_array_add(uuids, BCAST_AUDIO_SCAN_SERVICE_UUID);
g_ptr_array_add(uuids, PUBLISHED_AUDIO_CAP_SERVICE_UUID);

lm_adapter_set_discovery_filter(adapter,
                                -80,
                                uuids,
                                NULL,
                                0,
                                0);

g_ptr_array_free(uuids, TRUE);
```

You may also use raw UUID strings:

```c
GPtrArray *uuids = g_ptr_array_new();

g_ptr_array_add(uuids, "0000180f-0000-1000-8000-00805f9b34fb"); // Battery Service
g_ptr_array_add(uuids, "0000110b-0000-1000-8000-00805f9b34fb"); // Audio Sink

lm_adapter_set_discovery_filter(adapter,
                                -80,
                                uuids,
                                NULL,
                                0,
                                0);

g_ptr_array_free(uuids, TRUE);
```

All UUID strings must be valid Bluetooth UUIDs.

#### Device Name Pattern Example

Only report devices whose name matches a specific pattern:

```c
lm_adapter_set_discovery_filter(adapter,
                                -80,
                                NULL,
                                "MySpeaker",
                                0,
                                0);
```

Examples:

| Pattern       | Matching Device Names                      |
| ------------- | ------------------------------------------ |
| `"MySpeaker"` | `MySpeaker`, `MySpeaker-L`, `MySpeaker123` |
| `"LEA"`       | `LEA_Speaker`, `LEA_Left`                  |
| `"Earbud"`    | `MyEarbud`, `Earbud-R`                     |

#### Maximum Device Count Example

Stop discovery after a fixed number of matching devices:

```c
lm_adapter_set_discovery_filter(adapter,
                                -80,
                                NULL,
                                NULL,
                                5,
                                0);
```

Only the first 5 matching devices are reported.

#### Discovery Timeout Example

Automatically stop discovery after 5 seconds:

```c
lm_adapter_set_discovery_filter(adapter,
                                -80,
                                NULL,
                                NULL,
                                0,
                                5000);
```

#### Combined Filter Example

Find up to 3 nearby LE Audio speakers whose name begins with `"MySpeaker"` and advertise PACS:

```c
GPtrArray *uuids = g_ptr_array_new();
g_ptr_array_add(uuids, PUBLISHED_AUDIO_CAP_SERVICE_UUID);

lm_adapter_set_discovery_filter(adapter,
                                -65,
                                uuids,
                                "MySpeaker",
                                3,
                                10000);

lm_adapter_start_discovery(adapter);

g_ptr_array_free(uuids, TRUE);
```

This configuration means:

* RSSI stronger than `-65 dBm`
* Device name begins with `"MySpeaker"`
* Device advertises PACS
* Stop after 3 devices
* Timeout after 10 seconds

When the timeout expires or the maximum number of devices is reached, LEA Manager automatically stops discovery
and emits `LM_ADAPTER_DISCOVERY_COMPLETE_IND`.

### Discovery Callback Example

```c
static lm_status_t lm_adapter_callback(lm_msg_type_t msg,
                                       lm_status_t status,
                                       void *buf)
{
    switch (msg) {

    case LM_ADAPTER_DISCOVERY_RESULT_IND: {
        lm_adapter_discovery_result_ind_t *ind = buf;

        const char *name = lm_device_get_name(ind->device);

        lm_log_info(TAG,
                    "device found: %s (%s)",
                    name ? name : "unknown",
                    lm_device_get_address(ind->device));

        if (name && strstr(name, "TargetDevice")) {
            lm_adapter_stop_discovery(ind->adapter);
            lm_device_connect(ind->device);
        }
        break;
    }

    case LM_ADAPTER_DISCOVERY_COMPLETE_IND:
        lm_log_info(TAG, "discovery complete");
        break;

    default:
        break;
    }

    return LM_STATUS_SUCCESS;
}
```

`lm_device_connect()` only requests the connection. The connection is complete when a device event `LM_DEVICE_CONNECTED_IND` is received.

---

## 10. Broadcast Device Discovery and Synchronization

```c
static lm_transport_bcast_code_t bcode = {
    'a', 'm', 'l', 'b', 'i', 's',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
```

```c
lm_adapter_start_discovery(adapter);
```

```c
static lm_status_t lm_adapter_callback(lm_msg_type_t msg,
                                       lm_status_t status,
                                       void *buf)
{
    switch (msg) {
    case LM_ADAPTER_BCAST_DISCOVERED_IND: {
        lm_adapter_bcast_discovered_ind_t *ind = buf;

        lm_device_start_sync_bcast(
            ind->device,
            LM_TRANSPORT_AUDIO_LOCATION_FL |
            LM_TRANSPORT_AUDIO_LOCATION_FR,
            &bcode);
        break;
    }

    case LM_DEVICE_BCAST_SYNC_UP_IND: {
        lm_device_bcast_sync_up_ind_t *ind = buf;
        lm_log_info(TAG,
                    "broadcast sync established: %s",
                    lm_device_get_path(ind->device));
        break;
    }

    case LM_DEVICE_BCAST_SYNC_LOST_IND: {
        lm_device_bcast_sync_lost_ind_t *ind = buf;
        lm_log_warn(TAG,
                    "broadcast sync lost: %s",
                    lm_device_get_path(ind->device));
        break;
    }

    default:
        break;
    }

    return LM_STATUS_SUCCESS;
}
```

---

## 11. Advertising as a Unicast Sink

```c
lm_adv_t *adv = lm_adv_create();

lm_adv_set_local_name(adv, "MySpeaker");
lm_adv_set_appearance(adv, 0x0840);
lm_adv_set_discoverable(adv, TRUE);
lm_adv_set_secondary_channel(adv, LM_ADV_SC_1M);

GPtrArray *uuids = g_ptr_array_new();
g_ptr_array_add(uuids, BCAST_AUDIO_SCAN_SERVICE_UUID);
g_ptr_array_add(uuids, PUBLISHED_AUDIO_CAP_SERVICE_UUID);
g_ptr_array_add(uuids, VOLUME_CONTROL_SERVICE_UUID);

lm_adv_set_services(adv, uuids);
g_ptr_array_free(uuids, TRUE);

lm_status_t ret = lm_adapter_start_adv(adapter, adv);
if (ret != LM_STATUS_SUCCESS) {
    lm_log_error(TAG, "failed to start advertising: %d", ret);
}
```

---

## 12. Media Player Control

```c
lm_player_t *player = lm_device_get_active_player(device);

if (player) {
    lm_player_play(player);
    lm_player_pause(player);
    lm_player_stop(player);
    lm_player_next(player);
    lm_player_previous(player);

    lm_player_status_t status = lm_player_get_status(player);
    lm_player_track_t *track = lm_player_get_track(player);
    guint32 position = lm_player_get_position(player);
}
```

---

## 13. Transport Volume Control

```c
lm_transport_t *transport = lm_device_get_active_transport(device);

if (transport) {
    lm_transport_state_t state = lm_transport_get_state(transport);
    lm_transport_profile_t profile = lm_transport_get_profile(transport);
    lm_transport_qos_t *qos = lm_transport_get_qos(transport);

    float volume = lm_transport_get_volume_percentage(transport);

    lm_transport_set_volume_percentage(transport, 75.0f);
}
```

---

## 14. GATT Server Example

```c
lm_gatt_server_t *server = lm_gatt_server_create(adapter);

lm_gatt_server_add_service(server, MY_SERVICE_UUID);

lm_gatt_server_add_char(server,
                        MY_SERVICE_UUID,
                        MY_CHAR_UUID,
                        LM_GATT_PROP_READ | LM_GATT_PROP_NOTIFY);

lm_adapter_register_gatt_server(adapter, server);

guint8 value[] = {0x01, 0x02, 0x03};

GByteArray *data = g_byte_array_new();
g_byte_array_append(data, value, sizeof(value));

lm_gatt_server_send_notify(server,
                           MY_SERVICE_UUID,
                           MY_CHAR_UUID,
                           data);

g_byte_array_free(data, TRUE);
```

---

## 15. Pairing and Authentication

```c
lm_agent_t *agent = lm_agent_create(
    adapter,
    LM_AGENT_IO_CAPA_DISPLAY_YES_NO);
```

```c
static int passkey = 1234;
static lm_status_t lm_agent_callback(lm_msg_type_t msg,
                                     lm_status_t status,
                                     void *buf)
{
    switch (msg) {
    case LM_AGENT_REQ_PASSKEY_IND: {
        lm_agent_req_passkey_ind_t *ind = buf;

        lm_log_info(TAG,
                    "passkey requested for %s",
                    lm_device_get_name(ind->device));

        ind->passkey = passkey;
        break;
    }

    default:
        break;
    }

    return LM_STATUS_SUCCESS;
}
```

```c
lm_register_callback(
    LM_CALLBACK_TYPE_APP_EVENT,
    MODULE_MASK_AGENT,
    lm_agent_callback);

lm_device_pair(device);
```

---

## 16. Event Message Format

All events use `lm_msg_type_t`:

```text
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| module id (5 bits) |         event id (27 bits)             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

The module ID identifies the source module. The event ID identifies the specific event.

Examples:

* `LM_ADAPTER_POWER_ON_CNF`
* `LM_DEVICE_CONNECTED_IND`
* `LM_TRANSPORT_STATE_CHANGED_IND`

---

## 17. Error Handling

Most APIs return `lm_status_t`.

Typical values include:

| Status                    | Meaning                        |
| ------------------------- | ------------------------------ |
| `LM_STATUS_SUCCESS`       | Request accepted               |
| `LM_STATUS_BUSY`          | Operation already in progress  |
| `LM_STATUS_INVALID_STATE` | Adapter or device is not ready |
| `LM_STATUS_NOT_SUPPORTED` | Feature not supported          |
| `LM_STATUS_FAILED`        | Generic failure                |

Example:

```c
lm_status_t ret = lm_adapter_start_discovery(adapter);

if (ret == LM_STATUS_BUSY) {
    lm_log_warn(TAG, "discovery already running");
}
```

---

## 18. Object Ownership

Objects returned by LEA Manager are owned by the library unless explicitly documented otherwise.

Applications must NOT free:

* `lm_device_t *`
* `lm_transport_t *`
* `lm_player_t *`

Applications must free:

* `lm_adapter_t *` return by lm_adapter_get_default() with lm_adapter_destroy()
* `lm_agent_t *` created by the application
* `lm_adv_t *` created by the application
* `lm_gatt_server_t *` created by the application
* `GList *` returned by list APIs
* Temporary `GPtrArray *` created by the application
* Temporary `GByteArray *` created by the application
* `GMainLoop *`

Example:

```c
GList *devices = NULL;
lm_adapter_get_connected_devices(adapter, &devices);

/* use devices */

g_list_free(devices);
```

Do not free the individual `lm_device_t *` elements inside the list.

---

## 19. Thread Safety

LEA Manager is not thread-safe unless otherwise documented.

Rules:

* Use all APIs from the same GLib main loop thread
* Do not access LEA Manager objects concurrently from multiple threads
* Do not block inside callbacks
* Move long-running work to worker threads or deferred tasks

---

## 20. Logging

```c
lm_log_enabled(TRUE);
lm_log_set_level(LM_LOG_DEBUG);

lm_log_set_filename("myapp.log", 65536, 10);

lm_log_debug(TAG, "debug message");
lm_log_info(TAG, "info message");
lm_log_warn(TAG, "warning message");
lm_log_error(TAG, "error message");
```

Supported log levels:

* `LM_LOG_DEBUG`
* `LM_LOG_INFO`
* `LM_LOG_WARN`
* `LM_LOG_ERROR`

---

## 21. Cleanup and Shutdown

Before application exit:

```c
g_main_loop_quit(loop);
lm_deinit();
g_main_loop_unref(loop);
```

Applications should stop active discovery, advertising, and audio synchronization before shutdown.

---

## 22. Common Pitfalls

1. Forgetting to call `lm_init()`
2. Not running the GLib main loop
3. Assuming APIs are synchronous
4. Starting discovery before adapter power-on completes
5. Blocking inside callbacks
6. Using the wrong module mask when registering callbacks
7. Freeing `lm_device_t *` or `lm_transport_t *`
8. Accessing objects from multiple threads
9. Forgetting to enable BlueZ experimental mode for LE Audio

---

## 23. FAQ

### Q: Why do I not receive any events?

Possible causes:

* `lm_register_callback()` was not called
* Wrong module mask was used
* GLib main loop is not running

### Q: Why does discovery fail?

Ensure:

* Adapter is powered on
* `LM_ADAPTER_POWER_ON_CNF` has been received
* Discovery is not already running

### Q: Why does LE Audio not work?

Ensure:

* BlueZ is started with `--experimental`
* Kernel and controller support ISO / LE Audio
* Broadcast code and audio location are correct

### Q: Can I use LEA Manager from multiple threads?

No. Use a single GLib main loop thread.

---

## 24. Example Applications

The `example/` directory contains complete sample applications:

| Example         | Description                               |
| --------------- | ----------------------------------------- |
| `broadcaster.c` | Broadcast source role                     |
| `speaker.c`     | Audio sink with broadcast synchronization |
| `tws.c`         | True Wireless Stereo device by LE Audio   |
| `gatt_server.c` | GATT server example                       |
| `gatt_client.c` | GATT client example                       |

Build:

```bash
make
```

Run:

```bash
./lea_manager broadcaster
./lea_manager speaker
./lea_manager tws
./lea_manager gatt-server
./lea_manager gatt-client
```

---

## 25. API Reference

See the header files under `inc/`:

| Header             | Description                              |
| ------------------ | ---------------------------------------- |
| `lm.h`             | Initialization and callback registration |
| `lm_adapter.h`     | Bluetooth adapter management             |
| `lm_device.h`      | Remote device management                 |
| `lm_transport.h`   | Audio transport and QoS                  |
| `lm_player.h`      | Media player control                     |
| `lm_profile.h`     | Classic BT profile                       |
| `lm_adv.h`         | Advertising                              |
| `lm_agent.h`       | Pairing agent                            |
| `lm_gatt_server.h` | GATT server APIs                         |
| `lm_gatt_client.h` | GATT client APIs                         |
| `lm_uuids.h`       | Standard Bluetooth UUIDs                 |
| `lm_log.h`         | Logging utilities                        |
