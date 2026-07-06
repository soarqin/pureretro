/*
 * PureRetro — Frontend lifecycle (init / shutdown).
 *
 * Owns SDL init, log init, system-directory resolution, and window/renderer
 * bootstrap. Higher-level orchestration (core load, SRAM, savestate,
 * run loop) lives in main.c.
 */

#ifndef PURERETRO_FRONTEND_LIFECYCLE_H
#define PURERETRO_FRONTEND_LIFECYCLE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up SDL, the logger, the system directory, and the video window.
 * Reads/writes only g_frontend. Returns false on hard failure. */
bool frontend_init(void);

/* Tear down audio, video, SDL, and free heap-owned frontend paths.
 * Idempotent: safe to call after a partial frontend_init failure. */
void frontend_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PURERETRO_FRONTEND_LIFECYCLE_H */
