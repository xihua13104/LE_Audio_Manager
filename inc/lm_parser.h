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
 * @addtogroup LEAManagerParser Parser
 * @{
 * This section defines the APIs for parsing binary data structures.
 * The parser module provides utilities for extracting various data types
 * from byte arrays in a specified byte order.
 */

#ifndef __LM_PARSER_H__
#define __LM_PARSER_H__

#include "lm_forward_decl.h"

/**
 * @defgroup LEAManagerParser_enum Enumeration
 * @{
 */

/**
 * @brief Parser byte order.
 */
typedef enum {
    LM_PARSER_LITTLE_ENDIAN = 0, /**< Little endian byte order. */
    LM_PARSER_BIG_ENDIAN = 1     /**< Big endian byte order. */
} lm_parser_byte_order_t;

/**
 * @}
 */

/**
 * @defgroup LEAManagerParser_function Function
 * @{
 */

/**
 * @brief     Create a parser for the given byte array.
 * @param[in] bytes  Pointer to the byte array to parse.
 * @param[in] order  Byte order (#lm_parser_byte_order_t).
 * @return    Pointer to the created parser, or NULL on failure.
 */
lm_parser_t *lm_parser_create(const GByteArray *bytes, lm_parser_byte_order_t order);

/**
 * @brief     Set the parser offset.
 * @param[in] parser Pointer to the parser.
 * @param[in] offset New offset position.
 */
void lm_parser_set_offset(lm_parser_t *parser, guint offset);

/**
 * @brief     Destroy a parser.
 * @param[in] parser Pointer to the parser to destroy.
 */
void lm_parser_destroy(lm_parser_t *parser);

/**
 * @brief     Read an unsigned 8-bit integer.
 * @param[in] parser Pointer to the parser.
 * @return    The read value.
 */
guint8 lm_parser_get_uint8(lm_parser_t *parser);

/**
 * @brief     Read a signed 8-bit integer.
 * @param[in] parser Pointer to the parser.
 * @return    The read value.
 */
gint8 lm_parser_get_sint8(lm_parser_t *parser);

/**
 * @brief     Read an unsigned 16-bit integer.
 * @param[in] parser Pointer to the parser.
 * @return    The read value.
 */
guint16 lm_parser_get_uint16(lm_parser_t *parser);

/**
 * @brief     Read a signed 16-bit integer.
 * @param[in] parser Pointer to the parser.
 * @return    The read value.
 */
gint16 lm_parser_get_sint16(lm_parser_t *parser);

/**
 * @brief     Read an unsigned 24-bit integer.
 * @param[in] parser Pointer to the parser.
 * @return    The read value.
 */
guint32 lm_parser_get_uint24(lm_parser_t *parser);

/**
 * @brief     Read an unsigned 32-bit integer.
 * @param[in] parser Pointer to the parser.
 * @return    The read value.
 */
guint32 lm_parser_get_uint32(lm_parser_t *parser);

/**
 * @brief     Read a short float (16-bit IEEE 11073 format).
 * @param[in] parser Pointer to the parser.
 * @return    The read value as double.
 */
double lm_parser_get_sfloat(lm_parser_t *parser);

/**
 * @brief     Read a 32-bit IEEE 11073 float.
 * @param[in] parser Pointer to the parser.
 * @return    The read value as double.
 */
double lm_parser_get_11073float(lm_parser_t *parser);

/**
 * @brief     Read a IEEE 754 half-precision float (16-bit).
 * @param[in] parser Pointer to the parser.
 * @return    The read value as double.
 */
double lm_parser_get_754half(lm_parser_t *parser);

/**
 * @brief     Read a IEEE 754 single-precision float (32-bit).
 * @param[in] parser Pointer to the parser.
 * @return    The read value as double.
 */
double lm_parser_get_754float(lm_parser_t *parser);

/**
 * @brief     Read a date/time value.
 * @param[in] parser Pointer to the parser.
 * @return    Pointer to the parsed GDateTime, or NULL on failure.
 */
GDateTime* lm_parser_get_date_time(lm_parser_t *parser);

/**
 * @brief     Read a string value.
 * @param[in] parser Pointer to the parser.
 * @return    Pointer to the parsed GString, or NULL on failure.
 */
GString *lm_parser_get_string(lm_parser_t *parser);
/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_PARSER_H__
