/*
 * PureRetro — Main emulation loop.
 *
 * run_loop() pumps SDL events, honors frontend hotkeys (F11/ESC) when the
 * core has not claimed the keyboard, invokes the core's frame-time
 * callback with real deltas, calls core_run(), and paces the frame budget
 * from g_av_info.timing.fps (skipped under fast-forward or minimize).
 */

#ifndef PURERETRO_RUN_LOOP_H
#define PURERETRO_RUN_LOOP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Block until g_frontend.running turns false (SDL_QUIT / ESC / SHUTDOWN
 * env callback). Reads/writes g_frontend and g_av_info only. */
void run_loop(void);

#ifdef __cplusplus
}
#endif

#endif /* PURERETRO_RUN_LOOP_H */
