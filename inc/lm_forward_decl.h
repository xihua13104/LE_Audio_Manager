/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @addtogroup LEAManager
 * @{
 * @addtogroup LEAManagerForwardDecl Forward Declaration
 * @{
 * This section provides forward declarations for opaque data structures used
 * throughout the LEA Manager. These structures are opaque handles that should
 * not be accessed directly by applications.
 */

#ifndef LM_FORWARD_DECL_H
#define LM_FORWARD_DECL_H

/**
 * @brief Opaque adapter handle.
 */
typedef struct lm_adapter lm_adapter_t;

/**
 * @brief Opaque device handle.
 */
typedef struct lm_device lm_device_t;

/**
 * @brief Opaque advertising handle.
 */
typedef struct lm_adv lm_adv_t;

/**
 * @brief Opaque agent handle.
 */
typedef struct lm_agent lm_agent_t;

/**
 * @brief Opaque player handle.
 */
typedef struct lm_player lm_player_t;

/**
 * @brief Opaque transport handle.
 */
typedef struct lm_transport lm_transport_t;

/**
 * @brief Opaque GATT server handle.
 */
typedef struct lm_gatt_server lm_gatt_server_t;

/**
 * @brief Opaque parser handle.
 */
typedef struct lm_parser lm_parser_t;

/**
 * @brief Opaque profile handle.
 */
typedef struct lm_profile lm_profile_t;

#endif //LM_FORWARD_DECL_H

/** @} */
/** @} */
