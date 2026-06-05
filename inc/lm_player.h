/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

/**
 * @addtogroup LEAManager
 * @{
 * @addtogroup LEAManagerPlayer Player
 * @{
 * This section defines the APIs for managing media players through AVRCP/MCP profiles.
 * The player module provides functionality to control media playback, get track information,
 * and monitor player status on remote devices.
 */

#ifndef __LM_PLAYER_H__
#define __LM_PLAYER_H__

#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_type.h"

/**
 * @defgroup LEAManagerPlayer_define Define
 * @{
 */

/**
 * @brief Player status change indication event.
 */
typedef struct {
    lm_player_t *player; /**< Pointer to the player. */
} lm_player_status_change_ind_t;
#define LM_PLAYER_STATUS_CHANGE_IND        (LM_MODULE_PLAYER | 0x0001)

/**
 * @brief Player track update indication event.
 */
typedef struct {
    lm_player_t *player; /**< Pointer to the player. */
} lm_player_track_update_ind_t;
#define LM_PLAYER_TRACK_UPDATE_IND         (LM_MODULE_PLAYER | 0x0002)

/**
 * @}
 */

/**
 * @defgroup LEAManagerPlayer_enum Enumeration
 * @{
 */

/**
 * @brief Player status.
 */
typedef enum {
    LM_PLAYER_PLAYING,          /**< Player is playing. */
    LM_PLAYER_STOPPED,          /**< Player is stopped. */
    LM_PLAYER_PAUSED,           /**< Player is paused. */
    LM_PLAYER_FORWARD_SEEK,     /**< Player is seeking forward. */
    LM_PLAYER_REVERSE_SEEK,     /**< Player is seeking reverse. */
    LM_PLAYER_ERROR             /**< Player is in error state. */
} lm_player_status_t;

/**
 * @brief Player profile type.
 */
typedef enum {
    LM_PLAYER_PROFILE_NULL = 0, /**< No profile. */
    LM_PLAYER_PROFILE_AVRCP,    /**< AVRCP profile. */
    LM_PLAYER_PROFILE_MCP       /**< MCP (Media Control Profile). */
} lm_player_profile_t;

/**
 * @}
 */

/**
 * @defgroup LEAManagerPlayer_struct Struct
 * @{
 */

/**
 * @brief Player track information structure.
 */
typedef struct {
    gchar *title;            /**< Track title. */
    gchar *artist;           /**< Track artist. */
    gchar *album;            /**< Album name. */
    gchar *general_name;    /**< General name. */
    guint32 number_of_tracks; /**< Total number of tracks. */
    guint32 track_number;    /**< Current track number. */
    guint32 duration;        /**< Track duration in milliseconds. */
    gchar *image_handle;     /**< Album art image handle. */
} lm_player_track_t;

/**
 * @}
 */

/**
 * @defgroup LEAManagerPlayer_function Function
 * @{
 */

/**
 * @brief     Get the current status of the player.
 * @param[in] player Pointer to the player.
 * @return    The player status (#lm_player_status_t).
 */
lm_player_status_t lm_player_get_status(lm_player_t *player);

/**
 * @brief     Get the current playback position.
 * @param[in] player Pointer to the player.
 * @return    The position in milliseconds.
 */
guint32 lm_player_get_position(lm_player_t *player);

/**
 * @brief     Get the player name.
 * @param[in] player Pointer to the player.
 * @return    The player name string.
 */
gchar *lm_player_get_name(lm_player_t *player);

/**
 * @brief     Get the player type.
 * @param[in] player Pointer to the player.
 * @return    The player type string.
 */
gchar *lm_player_get_type(lm_player_t *player);

/**
 * @brief     Get the D-Bus object path of the player.
 * @param[in] player Pointer to the player.
 * @return    The D-Bus object path string.
 */
gchar *lm_player_get_path(lm_player_t *player);

/**
 * @brief     Get the current track information.
 * @param[in] player Pointer to the player.
 * @return    Pointer to the track information structure.
 */
lm_player_track_t *lm_player_get_track(lm_player_t *player);

/**
 * @brief     Start playback.
 * @param[in] player Pointer to the player.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_player_play(lm_player_t *player);

/**
 * @brief     Pause playback.
 * @param[in] player Pointer to the player.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_player_pause(lm_player_t *player);

/**
 * @brief     Stop playback.
 * @param[in] player Pointer to the player.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_player_stop(lm_player_t *player);

/**
 * @brief     Skip to next track.
 * @param[in] player Pointer to the player.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_player_next(lm_player_t *player);

/**
 * @brief     Skip to previous track.
 * @param[in] player Pointer to the player.
 * @return    #LM_STATUS_SUCCESS if the request is sent successfully.
 */
lm_status_t lm_player_previous(lm_player_t *player);

/**
 * @brief     Get the player profile type.
 * @param[in] player Pointer to the player.
 * @return    The profile type (#lm_player_profile_t).
 */
lm_player_profile_t lm_player_get_profile(lm_player_t *player);

/**
 * @brief     Get the device associated with the player.
 * @param[in] player Pointer to the player.
 * @return    Pointer to the device.
 */
lm_device_t *lm_player_get_device(lm_player_t *player);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_PLAYER_H__
