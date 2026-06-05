/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

/**
 * @addtogroup LEAManager
 * @{
 * @addtogroup LEAManagerTransport Transport
 * @{
 * This section defines the APIs for managing LE Audio transports.
 * A transport represents an audio stream connection between devices, supporting
 * profiles like BAP (Basic Audio Profile) for unicast and broadcast audio.
 */

#ifndef __LM_TRANSPORT_H__
#define __LM_TRANSPORT_H__

#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"

/**
 * @defgroup LEAManagerTransport_define Define
 * @{
 */

/**
 * @brief Size of broadcast code.
 */
#define LM_TRANSPORT_BCAST_CODE_SIZE    16

/**
 * @brief Broadcast code structure.
 */
typedef struct {
    guint8 code[LM_TRANSPORT_BCAST_CODE_SIZE]; /**< 16-byte broadcast code. */
} lm_transport_bcast_code_t;

/**
 * @brief Audio location bit definitions.
 */
#define LM_TRANSPORT_AUDIO_LOCATION_MONO (0)      /**< MONO. */
#define LM_TRANSPORT_AUDIO_LOCATION_FL   (1 << 0) /**< Front left channel. */
#define LM_TRANSPORT_AUDIO_LOCATION_FR   (1 << 1) /**< Front right channel. */
#define LM_TRANSPORT_AUDIO_LOCATION_FC   (1 << 2) /**< Front center channel. */
#define LM_TRANSPORT_AUDIO_LOCATION_ALL  (0x0FFF) /**< All audio locations. */
typedef guint lm_transport_audio_location_t; /**< Type for audio location bitmask. */

/**
 * @brief Transport state change indication event.
 */
typedef struct {
    lm_transport_t *transport; /**< Pointer to the transport. */
} lm_transport_state_change_ind_t;
#define LM_TRANSPORT_STATE_CHANGE_IND       (LM_MODULE_TRANSPORT | 0x0001)

/**
 * @brief Transport QoS update indication event.
 */
typedef struct {
    lm_transport_t *transport; /**< Pointer to the transport. */
} lm_transport_qos_update_ind_t;
#define LM_TRANSPORT_QOS_UPDATE_IND         (LM_MODULE_TRANSPORT | 0x0002)

/**
 * @brief Transport volume change indication event.
 */
typedef struct {
    lm_transport_t *transport; /**< Pointer to the transport. */
} lm_transport_volume_change_ind_t;
#define LM_TRANSPORT_VOLUME_CHANGE_IND      (LM_MODULE_TRANSPORT | 0x0003)

/**
 * @}
 */

/**
 * @defgroup LEAManagerTransport_struct Struct
 * @{
 */

/**
 * @brief Quality of Service parameters for a transport.
 */
typedef struct {
    guint8  big;                      /**< Broadcast Isochronous Group (BIG) handle. */
    guint8  bis;                      /**< Broadcast Isochronous Stream (BIS) handle. */
    guint8  sync_factor;              /**< Synchronization factor. */
    guint8  packing;                  /**< Packing method. */
    guint8  framing;                   /**< Framing method. */
    guint8  encryption;               /**< Encryption status. */
    lm_transport_bcast_code_t bcode;   /**< Broadcast code. */
    guint8  options;                   /**< Additional options. */
    guint16 skip;                     /**< Skip value. */
    guint16 sync_timeout;             /**< Synchronization timeout. */
    guint8  sync_type;               /**< Synchronization type. */
    guint8  mse;                      /**< Maximum number of subevents. */
    guint16 timeout;                  /**< Connection timeout. */
    guint8  pa_sync;                  /**< Periodic advertising sync. */
    guint32 interval;                 /**< Frame interval. */
    guint16 latency;                 /**< Transport latency. */
    guint16 sdu;                      /**< Maximum SDU size. */
    guint8  phy;                      /**< PHY rate. */
    guint8  rtn;                      /**< Retransmission effort. */
    guint32 presentation_delay;      /**< Presentation delay. */
} lm_transport_qos_t;

/**
 * @}
 */

/**
 * @defgroup LEAManagerTransport_enum Enumeration
 * @{
 */

/**
 * @brief Transport state.
 */
typedef enum {
    LM_TRANSPORT_ERROR = 0,           /**< Error state. */
    LM_TRANSPORT_IDLE,               /**< Idle state. */
    LM_TRANSPORT_PENDING,           /**< Pending state. */
    LM_TRANSPORT_BROADCASTING,      /**< Broadcasting state. */
    LM_TRANSPORT_ACTIVE              /**< Active state. */
} lm_transport_state_t;

/**
 * @brief Transport profile type.
 */
typedef enum {
    LM_TRANSPORT_PROFILE_NULL = 0,           /**< No profile. */
    LM_TRANSPORT_PROFILE_A2DP_SINK,         /**< A2DP Sink profile. */
    LM_TRANSPORT_PROFILE_BAP_SINK,          /**< BAP Unicast Sink profile. */
    LM_TRANSPORT_PROFILE_BAP_BCAST_SINK,   /**< BAP Broadcast Sink profile. */
    LM_TRANSPORT_PROFILE_BAP_BCAST_SRC     /**< BAP Broadcast Source profile. */
} lm_transport_profile_t;

/**
 * @}
 */

/**
 * @defgroup LEAManagerTransport_function Function
 * @{
 */

/**
 * @brief     Get the D-Bus object path of the transport.
 * @param[in] transport Pointer to the transport.
 * @return    The D-Bus object path string.
 */
const gchar *lm_transport_get_path(lm_transport_t *transport);

/**
 * @brief     Get the UUID of the transport.
 * @param[in] transport Pointer to the transport.
 * @return    The UUID string.
 */
const gchar *lm_transport_get_uuid(lm_transport_t *transport);

/**
 * @brief     Get the device associated with the transport.
 * @param[in] transport Pointer to the transport.
 * @return    Pointer to the device.
 */
lm_device_t *lm_transport_get_device(lm_transport_t *transport);

/**
 * @brief     Get the D-Bus object path of the transport's device.
 * @param[in] transport Pointer to the transport.
 * @return    The device D-Bus object path string.
 */
const gchar *lm_transport_get_device_path(lm_transport_t *transport);

/**
 * @brief     Get the current state of the transport.
 * @param[in] transport Pointer to the transport.
 * @return    The current transport state (#lm_transport_state_t).
 */
lm_transport_state_t lm_transport_get_state(lm_transport_t *transport);

/**
 * @brief     Get the volume percentage of the transport.
 * @param[in] transport Pointer to the transport.
 * @return    The volume percentage (0.0 to 100.0).
 */
float lm_transport_get_volume_percentage(lm_transport_t *transport);

/**
 * @brief     Set the volume percentage of the transport.
 * @param[in] transport   Pointer to the transport.
 * @param[in] volume_per  The volume percentage to set (0.0 to 100.0).
 * @return    #LM_STATUS_SUCCESS if the request is successful.
 */
lm_status_t lm_transport_set_volume_percentage(lm_transport_t *transport, float volume_per);

/**
 * @brief     Get the profile type of the transport.
 * @param[in] transport Pointer to the transport.
 * @return    The profile type (#lm_transport_profile_t).
 */
lm_transport_profile_t lm_transport_get_profile(lm_transport_t *transport);

/**
 * @brief     Get the profile name of the transport.
 * @param[in] transport Pointer to the transport.
 * @return    The profile name string.
 */
const gchar *lm_transport_get_profile_name(lm_transport_t *transport);

/**
 * @brief     Get the QoS parameters of the transport.
 * @param[in] transport Pointer to the transport.
 * @return    Pointer to the QoS structure, or NULL if not available.
 */
lm_transport_qos_t *lm_transport_get_qos(lm_transport_t *transport);

/**
 * @brief     Get the audio location of the transport.
 * @param[in] transport Pointer to the transport.
 * @return    The audio location bitmask (#lm_transport_audio_location_t).
 */
lm_transport_audio_location_t lm_transport_get_audio_location(lm_transport_t *transport);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_TRANSPORT_H__
