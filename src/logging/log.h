/*
 * PureRetro - Logging subsystem
 *
 * Loglevel-aware logging that backs both frontend internal messages and
 * the libretro core's retro_log_callback. Output goes to stderr.
 *
 * Levels (low to high): DEBUG, INFO, WARN, ERROR. Messages below the
 * active level are dropped. The active level is chosen as:
 *   1) explicit log_set_level() (e.g. from --log-level CLI)
 *   2) environment variable PURERETRO_LOG=<debug|info|warn|error>
 *   3) default LOG_LEVEL_INFO
 *
 * Format: "[HH:MM:SS.mmm] [LEVEL] [SRC] message\n"
 *   - Timestamp is wall-clock time-of-day to millisecond resolution.
 *   - SRC is "FRONTEND" for in-frontend code and "CORE" for messages
 *     forwarded from the libretro core via RETRO_ENVIRONMENT_GET_LOG_INTERFACE.
 */

#ifndef PURERETRO_LOG_H
#define PURERETRO_LOG_H

#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum log_level {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

/* Initialize the logger. Reads PURERETRO_LOG from the environment unless
 * log_set_level() has already been called. Safe to call once at startup. */
void log_init(void);

/* Set the active level explicitly. Overrides any environment value. */
void log_set_level(enum log_level lvl);

/* Get the active level (useful for cheaply gating expensive log work). */
enum log_level log_get_level(void);

/* Parse a level string ("debug"/"info"/"warn"/"error"; case-insensitive).
 * Returns true on success. */
bool log_parse_level(const char *s, enum log_level *out);

/* Emit a formatted log message. Auto-appends a newline if the format does
 * not already end with one. file/line are optional (NULL/0 to omit). */
void log_emit(enum log_level lvl, const char *src,
              const char *file, int line,
              const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 5, 6)))
#endif
    ;

/* va_list variant. Used by the libretro core log callback bridge. */
void log_emit_v(enum log_level lvl, const char *src,
                const char *file, int line,
                const char *fmt, va_list va);

#define LOG_DEBUG(...) \
    log_emit(LOG_LEVEL_DEBUG, "FRONTEND", __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  \
    log_emit(LOG_LEVEL_INFO,  "FRONTEND", __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  \
    log_emit(LOG_LEVEL_WARN,  "FRONTEND", __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) \
    log_emit(LOG_LEVEL_ERROR, "FRONTEND", __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* PURERETRO_LOG_H */
