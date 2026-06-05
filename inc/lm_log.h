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
 * @addtogroup LEAManagerLog Log
 * @{
 * This section defines the logging APIs for the LEA Manager.
 * The log module provides functions for outputting debug, info, warning, and error messages.
 */

#ifndef __LM_LOG_H__
#define __LM_LOG_H__

#include <glib.h>

/**
 * @defgroup LEAManagerLog_define Define
 * @{
 */

/**
 * @brief Log level type.
 */
typedef enum {
    LM_LOG_DEBUG = 0, /**< Debug level. */
    LM_LOG_INFO = 1,  /**< Info level. */
    LM_LOG_WARN = 2,  /**< Warning level. */
    LM_LOG_ERROR = 3  /**< Error level. */
} lm_log_level_t;

/**
 * @brief Debug log macro.
 * @param[in] tag    Log tag/identifier.
 * @param[in] format Printf-style format string.
 */
#define lm_log_debug(tag, format, ...) lm_log_at_level(LM_LOG_DEBUG, tag, format, ##__VA_ARGS__)

/**
 * @brief Info log macro.
 * @param[in] tag    Log tag/identifier.
 * @param[in] format Printf-style format string.
 */
#define lm_log_info(tag, format, ...)  lm_log_at_level(LM_LOG_INFO, tag, format, ##__VA_ARGS__)

/**
 * @brief Warning log macro.
 * @param[in] tag    Log tag/identifier.
 * @param[in] format Printf-style format string.
 */
#define lm_log_warn(tag, format, ...)  lm_log_at_level(LM_LOG_WARN, tag, format,  ##__VA_ARGS__)

/**
 * @brief Error log macro.
 * @param[in] tag    Log tag/identifier.
 * @param[in] format Printf-style format string.
 */
#define lm_log_error(tag, format, ...) lm_log_at_level(LM_LOG_ERROR, tag, format, ##__VA_ARGS__)

/**
 * @brief Hex dump macro for GByteArray.
 * @param[in] level Log level.
 * @param[in] tag   Log tag.
 * @param[in] ba    GByteArray to dump.
 */
#define lm_log_hex_dump_ba(level, tag, ba) lm_log_hex_dump(level, tag, (ba)->data, (ba)->len)

/**
 * @}
 */

/**
 * @defgroup LEAManagerLog_callback Callback
 * @{
 */

/**
 * @brief Log event callback function type.
 * @param[in] level   Log level.
 * @param[in] tag     Log tag.
 * @param[in] message Log message string.
 */
typedef void (*lm_log_event_callback_t)(lm_log_level_t level, const char *tag, const char *message);

/**
 * @}
 */

/**
 * @defgroup LEAManagerLog_function Function
 * @{
 */

/**
 * @brief     Log a message at the specified level.
 * @param[in] level   Log level.
 * @param[in] tag     Log tag.
 * @param[in] format  Printf-style format string.
 */
void lm_log_at_level(lm_log_level_t level, const char* tag, const char *format, ...);

/**
 * @brief     Set the minimum log level.
 * @param[in] level   Minimum log level to output.
 */
void lm_log_set_level(lm_log_level_t level);

/**
 * @brief     Set log file output.
 * @param[in] filename   Log file name.
 * @param[in] max_size    Maximum file size before rotation.
 * @param[in] max_files  Maximum number of log files to keep.
 */
void lm_log_set_filename(const char* filename, unsigned long max_size, unsigned int max_files);

/**
 * @brief     Set a custom log handler callback.
 * @param[in] callback  Log event callback function.
 */
void lm_log_set_handler(lm_log_event_callback_t callback);

/**
 * @brief     Enable or disable logging.
 * @param[in] enabled   TRUE to enable, FALSE to disable.
 */
void lm_log_enabled(gboolean enabled);

/**
 * @brief     Dump hex data to log.
 * @param[in] level  Log level.
 * @param[in] tag    Log tag.
 * @param[in] data  Pointer to data buffer.
 * @param[in] len   Length of data buffer.
 */
void lm_log_hex_dump(lm_log_level_t level, const gchar *tag, const guint8 *data, gsize len);

/**
 * @}
 */

/** @} */
/** @} */

#endif //__LM_LOG_H__
