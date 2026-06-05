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
 * @addtogroup LEAManagerGATT GATT Common
 * @{
 * This section defines the common GATT types and properties used by both
 * GATT server and GATT client modules.
 *
 * GATT (Generic Attribute Profile) defines how data is organized and exchanged
 * in a BLE connection. Data is organized into Services, which contain
 * Characteristics, which may contain Descriptors.
 */

#ifndef __LM_GATT_H__
#define __LM_GATT_H__

#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"

/**
 * @defgroup LEAManagerGATT_define Define
 * @{
 */

/**
 * @brief GATT property bit field.
 * Reference: Core SPEC 4.1 page 2183 (Table 3.5: GATT Char Properties
 * bit field) defines how the gatt char Value can be used, or how the
 * gatt descriptors (see Section 3.3.3 - page 2184) can be accessed.
 * In the core spec, regular properties are included in the gatt_char
 * declaration, and the extended properties are defined as descriptor.
 */
#define LM_GATT_PROP_BROADCAST              (1 << 0)   /**< Broadcast property. */
#define LM_GATT_PROP_READ                   (1 << 1)   /**< Read property. */
#define LM_GATT_PROP_WRITE_WITHOUT_RESP     (1 << 2)   /**< Write Without Response property. */
#define LM_GATT_PROP_WRITE                  (1 << 3)   /**< Write property. */
#define LM_GATT_PROP_NOTIFY                 (1 << 4)   /**< Notify property. */
#define LM_GATT_PROP_INDICATE               (1 << 5)   /**< Indicate property. */
#define LM_GATT_PROP_AUTH                   (1 << 6)   /**< Authenticated Signed Writes property. */
#define LM_GATT_PROP_EXT_PROP               (1 << 7)   /**< Extended Properties descriptor. */
#define LM_GATT_PROP_ENCRYPT_READ           (1 << 8)   /**< Encrypt read access. */
#define LM_GATT_PROP_ENCRYPT_WRITE          (1 << 9)   /**< Encrypt write access. */
#define LM_GATT_PROP_ENCRYPT_NOTIFY         (1 << 10)  /**< Encrypt notify access. */
#define LM_GATT_PROP_ENCRYPT_INDICATE       (1 << 11)  /**< Encrypt indicate access. */
#define LM_GATT_PROP_ENCRYPT_AUTH_READ      (1 << 12)  /**< Encrypt authenticated read access. */
#define LM_GATT_PROP_ENCRYPT_AUTH_WRITE     (1 << 13)  /**< Encrypt authenticated write access. */
#define LM_GATT_PROP_ENCRYPT_AUTH_NOTIFY    (1 << 14)  /**< Encrypt authenticated notify access. */
#define LM_GATT_PROP_ENCRYPT_AUTH_INDICATE  (1 << 15)  /**< Encrypt authenticated indicate access. */
#define LM_GATT_PROP_SECURE_READ            (1 << 16)  /**< Secure read access (MITM required). */
#define LM_GATT_PROP_SECURE_WRITE           (1 << 17)  /**< Secure write access (MITM required). */
#define LM_GATT_PROP_SECURE_NOTIFY          (1 << 18)  /**< Secure notify access (MITM required). */
#define LM_GATT_PROP_SECURE_INDICATE        (1 << 19)  /**< Secure indicate access (MITM required). */

/**
 * @brief GATT property type.
 */
typedef guint lm_gatt_property_t;

/**
 * @}
 */

/**
 * @defgroup LEAManagerGATT_enum Enumeration
 * @{
 */

/**
 * @brief GATT write type.
 */
typedef enum {
    LM_GATT_WRITE_WITH_RESPONSE = 0,       /**< Write with response. */
    LM_GATT_WRITE_WITHOUT_RESPONSE = 1    /**< Write without response. */
} lm_gatt_write_type_t;

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_GATT_H__
