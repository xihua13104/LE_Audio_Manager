/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

#include "lm_log.h"
#include <glib.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/stat.h>

#define BUFFER_SIZE     1024
#define MAX_FILE_SIZE   1024 * 64
#define HEX_DUMP_MAX_BYTES 1024
#define MAX_LOGS        5

static struct {
    gboolean enabled;
    lm_log_level_t level;
    FILE *fout;
    gchar filename[256];
    gulong maxFileSize;
    guint maxFiles;
    size_t currentSize;
    lm_log_event_callback_t logCallback;
} log_settings = {TRUE, LM_LOG_DEBUG, NULL, "", MAX_FILE_SIZE, MAX_LOGS, 0, NULL};

static const gchar *log_level_names[] = {
    [LM_LOG_DEBUG] = "[D]",
    [LM_LOG_INFO] = "[I]",
    [LM_LOG_WARN]  = "[W]",
    [LM_LOG_ERROR]  = "[E]"
};

void lm_log_set_level(lm_log_level_t level) {
    log_settings.level = level;
}

void lm_log_set_handler(lm_log_event_callback_t callback) {
    log_settings.logCallback = callback;
}

void lm_log_enabled(gboolean enabled) {
    log_settings.enabled = enabled;
}

static void open_log_file(void) {
    log_settings.fout = fopen(log_settings.filename, "a");
    if (log_settings.fout == NULL) {
        log_settings.fout = stdout;
        return;
    }

    struct stat finfo;
    fstat(fileno(log_settings.fout), &finfo);
    log_settings.currentSize = (size_t) finfo.st_size;
}

void lm_log_set_filename(const gchar *filename, gulong max_size, guint max_files) {
    g_assert(filename != NULL);
    g_assert(strlen(filename) > 0);

    log_settings.maxFileSize = max_size ? max_size : MAX_FILE_SIZE;
    log_settings.maxFiles = max_files ? max_files : MAX_LOGS;
    strncpy(log_settings.filename, filename, sizeof(log_settings.filename) - 1);
    open_log_file();
}

/**
 * Get the current UTC time in milliseconds since epoch
 * @return
 */
static long long current_timestamp_in_millis(void) {
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000; // calculate milliseconds
    return milliseconds;
}

/**
 * Returns a string representation of the current time year-month-day hours:minutes:seconds
 * @return newly allocated string, must be freed using g_free()
 */
static gchar *current_time_string(void) {
    GDateTime *now = g_date_time_new_now_local();
    gchar *time_string = g_date_time_format(now, "%F %R:%S");
    g_date_time_unref(now);

    gchar *result = g_strdup_printf("%s:%03lld", time_string, current_timestamp_in_millis() % 1000);
    g_free(time_string);
    return result;
}

static void log_log(const gchar *tag, const gchar *level, const gchar *message) {
    gchar *timestamp = current_time_string();
    int bytes_written;
    if ((bytes_written = fprintf(log_settings.fout, "%s %s [%s] %s\n", timestamp, level, tag, message)) > 0) {
        log_settings.currentSize += (guint) bytes_written;
        fflush(log_settings.fout);
    }

    g_free(timestamp);
}

static gchar *get_log_name(int index) {
    if (index > 0) {
        return g_strdup_printf("%s.%d", log_settings.filename, index);
    } else {
        return g_strdup(log_settings.filename);
    }
}

static gboolean fileExists(const gchar *filename) {
    FILE *fp;

    if ((fp = fopen(filename, "r")) == NULL) {
        return FALSE;
    } else {
        fclose(fp);
        return TRUE;
    }
}

static void rotate_log_files(void) {
    for (int i = (int) log_settings.maxFiles; i > 0; i--) {
        gchar *src = get_log_name(i - 1);
        gchar *dst = get_log_name(i);
        if (fileExists(dst)) {
            remove(dst);
        }

        if (fileExists(src)) {
            rename(src, dst);
        }

        g_free(src);
        g_free(dst);
    }
}

static void rotate_log_file_if_needed(void) {
    if ((log_settings.currentSize < log_settings.maxFileSize) ||
        log_settings.fout == stdout || log_settings.logCallback != NULL)
        return;

    g_assert(log_settings.fout != NULL);
    fclose(log_settings.fout);
    rotate_log_files();
    open_log_file();
}

void lm_log_at_level(lm_log_level_t level, const gchar *tag, const gchar *format, ...) {
    // Init fout to stdout if needed
    if (log_settings.fout == NULL && log_settings.logCallback == NULL) {
        log_settings.fout = stdout;
    }

    rotate_log_file_if_needed();

    if (log_settings.level <= level && log_settings.enabled) {
        gchar buf[BUFFER_SIZE];
        va_list arg;
        va_start(arg, format);
        g_vsnprintf(buf, BUFFER_SIZE, format, arg);
        if (log_settings.logCallback) {
            log_settings.logCallback(level, tag, buf);
        } else {
            log_log(tag, log_level_names[level], buf);
        }
        va_end(arg);
    }
}

void lm_log_hex_dump(lm_log_level_t level,
                     const gchar *tag,
                     const guint8 *data,
                     gsize len)
{
    if (!data || len == 0) {
        lm_log_at_level(level, tag, "HEX DUMP: <empty>");
        return;
    }

    gsize dump_len = len;
    gboolean truncated = FALSE;

    if (dump_len > HEX_DUMP_MAX_BYTES) {
        dump_len = HEX_DUMP_MAX_BYTES;
        truncated = TRUE;
    }

    /* each byte "XX " -> 3 chars */
    GString *hex = g_string_sized_new(dump_len * 3 + 32);

    for (gsize i = 0; i < dump_len; i++) {
        g_string_append_printf(hex, "%02X ", data[i]);
    }

    if (truncated) {
        g_string_append(hex, "... ");
    }

    lm_log_at_level(level,
                    tag,
                    "HEX DUMP (%zu bytes): %s",
                    len,
                    hex->str);

    g_string_free(hex, TRUE);
}


