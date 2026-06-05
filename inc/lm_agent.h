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
 * @addtogroup LEAManagerAgent Agent
 * @{
 * This section defines the APIs for managing the Bluetooth pairing agent.
 * The agent module handles authentication requests like passkey entry during pairing.
 */

#ifndef __LM_AGENT_H__
#define __LM_AGENT_H__

#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"

/**
 * @defgroup LEAManagerAgent_define Define
 * @{
 */

/**
 * @brief Agent request passkey indication event.
 */
typedef struct {
    lm_device_t *device;     /**< Pointer to the device requesting passkey. */
    guint32 passkey;        /**< Passkey to be entered. */
} lm_agent_req_passkey_ind_t;
#define LM_AGENT_REQ_PASSKEY_IND        (LM_MODULE_AGENT | 0x0001)

/**
 * @}
 */

/**
 * @defgroup LEAManagerAgent_enum Enumeration
 * @{
 */

/**
 * @brief Agent I/O capability.
 */
typedef enum {
    LM_AGENT_IO_CAPA_DISPLAY_ONLY,     /**< Display only capability. */
    LM_AGENT_IO_CAPA_DISPLAY_YES_NO,   /**< Display and Yes/No button capability. */
    LM_AGENT_IO_CAPA_KEYBOARD_ONLY,    /**< Keyboard only capability. */
    LM_AGENT_IO_CAPA_NO_INPUT_NO_OUTPUT, /**< No input/output capability. */
    LM_AGENT_IO_CAPA_KEYBOARD_DISPLAY  /**< Keyboard and display capability. */
} lm_agent_io_capability_t;

/**
 * @}
 */

/**
 * @defgroup LEAManagerAgent_function Function
 * @{
 */

/**
 * @brief     Create an agent with specified I/O capability.
 * @param[in] adapter    Pointer to the adapter.
 * @param[in] io_capability The I/O capability of the agent.
 * @return    Pointer to the created agent, or NULL on failure.
 */
lm_agent_t *lm_agent_create(lm_adapter_t *adapter, lm_agent_io_capability_t io_capability);

/**
 * @brief     Destroy an agent.
 * @param[in] agent Pointer to the agent to destroy.
 */
void lm_agent_destroy(lm_agent_t *agent);

/**
 * @brief     Get the D-Bus object path of the agent.
 * @param[in] agent Pointer to the agent.
 * @return    The D-Bus object path string.
 */
const gchar *lm_agent_get_path(const lm_agent_t *agent);

/**
 * @brief     Get the adapter associated with the agent.
 * @param[in] agent Pointer to the agent.
 * @return    Pointer to the adapter.
 */
lm_adapter_t *lm_agent_get_adapter(const lm_agent_t *agent);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_AGENT_H__
