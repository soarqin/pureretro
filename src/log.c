/*
 * PureRetro - Logging subsystem implementation.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <SDL3/SDL.h>
#include "log.h"

static enum log_level g_level = LOG_LEVEL_INFO;
static bool g_level_explicit = false;

static const char *level_name(enum log_level lvl)
{
    switch (lvl) {
    case LOG_LEVEL_DEBUG: return "DEBUG";
    case LOG_LEVEL_INFO:  return "INFO ";
    case LOG_LEVEL_WARN:  return "WARN ";
    case LOG_LEVEL_ERROR: return "ERROR";
    }
    return "?????";
}

bool log_parse_level(const char *s, enum log_level *out)
{
    if (!s || !out)
        return false;
    if (strcasecmp(s, "debug") == 0) { *out = LOG_LEVEL_DEBUG; return true; }
    if (strcasecmp(s, "info")  == 0) { *out = LOG_LEVEL_INFO;  return true; }
    if (strcasecmp(s, "warn")  == 0 ||
        strcasecmp(s, "warning") == 0) { *out = LOG_LEVEL_WARN;  return true; }
    if (strcasecmp(s, "error") == 0) { *out = LOG_LEVEL_ERROR; return true; }
    return false;
}

void log_init(void)
{
    if (g_level_explicit)
        return;
    const char *env = getenv("PURERETRO_LOG");
    enum log_level parsed;
    if (env && log_parse_level(env, &parsed))
        g_level = parsed;
}

void log_set_level(enum log_level lvl)
{
    g_level = lvl;
    g_level_explicit = true;
}

enum log_level log_get_level(void)
{
    return g_level;
}

/* Compose "HH:MM:SS.mmm" from local wall-clock time. SDL's tick clock would
 * give us a strictly monotonic value, but wall-clock makes log lines easier
 * to correlate with external events (e.g. file timestamps, dmesg). */
static void format_timestamp(char *buf, size_t cap)
{
    time_t now = time(NULL);
    struct tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    /* Milliseconds come from SDL's tick counter since we lack a portable
     * sub-second wall-clock without pulling in extra headers per-platform. */
    Uint64 ms = SDL_GetTicks() % 1000;
    snprintf(buf, cap, "%02d:%02d:%02d.%03u",
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (unsigned)ms);
}

void log_emit_v(enum log_level lvl, const char *src,
                const char *file, int line,
                const char *fmt, va_list va)
{
    if (lvl < g_level)
        return;
    if (!fmt)
        return;

    char ts[16];
    format_timestamp(ts, sizeof(ts));

    /* Use a single fprintf per line where practical so concurrent threads
     * are less likely to interleave their output. */
    char msg[2048];
    int n = vsnprintf(msg, sizeof(msg), fmt, va);
    if (n < 0)
        return;

    /* Strip a trailing newline so our own '\n' below is the only one. Some
     * cores helpfully include one in their format string. */
    if (n > 0 && (size_t)n < sizeof(msg) && msg[n - 1] == '\n')
        msg[n - 1] = '\0';

    if (file && line > 0 && lvl <= LOG_LEVEL_DEBUG) {
        /* DEBUG includes source location to aid hunting issues; INFO+
         * stays clean for normal use. */
        fprintf(stderr, "[%s] [%s] [%s] %s (%s:%d)\n",
                ts, level_name(lvl), src ? src : "?", msg, file, line);
    } else {
        fprintf(stderr, "[%s] [%s] [%s] %s\n",
                ts, level_name(lvl), src ? src : "?", msg);
    }
    fflush(stderr);
}

void log_emit(enum log_level lvl, const char *src,
              const char *file, int line,
              const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    log_emit_v(lvl, src, file, line, fmt, va);
    va_end(va);
}
