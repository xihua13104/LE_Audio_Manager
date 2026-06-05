/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

/**
 * @addtogroup LEAManager
 * @{
 * @addtogroup LEAManagerUUIDs UUIDs
 * @{
 * This section defines the standard Bluetooth service UUIDs used in LE Audio.
 * These UUIDs are used for GATT service discovery and identification.
 */

#ifndef __LM_UUIDS__
#define __LM_UUIDS__

/**
 * @defgroup LEAManagerUUIDs_define Define
 * @{
 */

/**
 * @brief Null service UUID.
 */
#define NULL_SERVICE_UUID                     "00000000-0000-0000-0000-000000000000"

/**
 * @brief Vendor service UUID.
 */
#define LM_VENDOR_SERVICE_UUID               "0000fba0-0000-1000-8000-00805f9b34fb"

/**
 * @brief Basic Audio Announcement Service UUID (BASS).
 */
#define BASIC_AUDIO_AUNOUNCEMENT_SERVICE_UUID "00001851-0000-1000-8000-00805f9b34fb"

/**
 * @brief Broadcast Audio Announcement Service UUID (BAAS).
 */
#define BCAST_AUDIO_AUNOUNCEMENT_SERVICE_UUID "00001852-0000-1000-8000-00805f9b34fb"

/**
 * @brief Sink PAC (Published Audio Capabilities) Service UUID.
 */
#define SINK_PAC_SERVICE_UUID                 "00002bc9-0000-1000-8000-00805f9b34fb"

/**
 * @brief Audio Sink Service UUID (A2DP Sink).
 */
#define AUDIO_SINK_SERVICE_UUID               "0000110b-0000-1000-8000-00805f9b34fb"

/**
 * @brief Audio Stream Control Service UUID (ASCS).
 */
#define AUDIO_STREAM_CONTROL_SERVICE_UUID     "0000184e-0000-1000-8000-00805f9b34fb"

/**
 * @brief Broadcast Audio Scan Service UUID (BASS).
 */
#define BCAST_AUDIO_SCAN_SERVICE_UUID         "0000184f-0000-1000-8000-00805f9b34fb"

/**
 * @brief Published Audio Capabilities Service UUID (PACS).
 */
#define PUBLISHED_AUDIO_CAP_SERVICE_UUID      "00001850-0000-1000-8000-00805f9b34fb"

/**
 * @brief Volume Control Service UUID (VCS).
 */
#define VOLUME_CONTROL_SERVICE_UUID           "00001844-0000-1000-8000-00805f9b34fb"

/**
 * @brief Microphone Control Service UUID (MCS).
 */
#define MICROPHONE_CONTROL_SERVICE_UUID       "0000184d-0000-1000-8000-00805f9b34fb"

/**
 * @brief Telephony and Media Audio Service UUID (TMAS).
 */
#define TELEPHONY_MEDIA_AUDIO_SERVICE_UUID    "00001855-0000-1000-8000-00805f9b34fb"

/**
 * @brief Common Audio Service UUID (CAS).
 */
#define COMMON_AUDIO_SERVICE_UUID             "00001853-0000-1000-8000-00805f9b34fb"

/**
 * @brief Coordinated Set Identification Service UUID (CSIS).
 */
#define COORDINATED_SET_ID_SERVICE_UUID       "00001846-0000-1000-8000-00805f9b34fb"

/**
 * @brief Set Identity Resolving Key Characteristic UUID.
 */
#define SET_ID_RESOLVING_KEY_CHAR_UUID        "00002b84-0000-1000-8000-00805f9b34fb"

/**
 * @brief Coordinated Set Size Characteristic UUID.
 */
#define COORDINATED_SET_SIZE_CHAR_UUID        "00002b85-0000-1000-8000-00805f9b34fb"

/**
 * @brief Set Member Lock Characteristic UUID.
 */
#define SET_MEMBER_LOCK_CHAR_UUID             "00002b86-0000-1000-8000-00805f9b34fb"

/**
 * @brief Set Member Rank Characteristic UUID.
 */
#define SET_MEMBER_RANK_CHAR_UUID             "00002b87-0000-1000-8000-00805f9b34fb"

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_UUIDS__
