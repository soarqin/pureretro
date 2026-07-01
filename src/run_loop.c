/*
 * PureRetro — Main emulation loop (see run_loop.h).
 */

#define _POSIX_C_SOURCE 200809L

#include "run_loop.h"

#include "audio.h"
#include "core.h"
#include "frontend.h"
#include "input.h"
#include "video.h"

#include <SDL3/SDL.h>

void run_loop(void)
{
    SDL_Event event;
    Uint64 prev_frame_ns = 0;

    while (g_frontend.running) {
        Uint64 frame_start = SDL_GetTicksNS();

        /* Recompute the target frame budget every iteration so a core that
         * changes its AV info via SET_SYSTEM_AV_INFO mid-run is honoured. */
        double fps = g_av_info.timing.fps;
        /* Sanity-clamp fps: absurdly high values produce target_frame_ns=0
         * and spin the CPU at 100%; zero/negative fps also degenerate. */
        if (fps <= 0.0)
            fps = 60.0;
        else if (fps > 1000.0)
            fps = 1000.0;
        Uint64 target_frame_ns = (Uint64)(1000000000.0 / fps);

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                g_frontend.running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                input_process_event(&event);
                /* Frontend hotkey interception: only fires when the core has
                 * NOT registered a keyboard callback. Cores that take
                 * full keyboard input (DOSBox, computer emulators) need
                 * unimpeded ESC/F11 — otherwise the user can't type ESC
                 * in their guest OS. Exit those cores via window close
                 * (SDL_EVENT_QUIT). */
                if (event.type == SDL_EVENT_KEY_DOWN &&
                    !g_frontend.keyboard_callback.callback) {
                    if (event.key.key == SDLK_F11) {
                        g_frontend.fullscreen = !g_frontend.fullscreen;
                        SDL_SetWindowFullscreen(g_frontend.video.window,
                                                g_frontend.fullscreen);
                    } else if (event.key.key == SDLK_ESCAPE) {
                        g_frontend.running = false;
                    }
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                video_process_event(&event);
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                g_frontend.window_minimized = true;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_SHOWN:
                g_frontend.window_minimized = false;
                break;
            default:
                break;
            }
        }

        /* While minimized: skip core_run/audio entirely and sleep a bit so
         * we don't peg the CPU. Still pump events. */
        if (g_frontend.window_minimized) {
            SDL_DelayNS(16000000ULL);
            continue;
        }

        input_poll();
        audio_notify_buffer_status();

        /* Invoke the core's frame-time callback (SET_FRAME_TIME_CALLBACK)
         * with the actual microseconds since the previous frame, or with
         * `reference` on the first frame and when the delta is implausible
         * (negative due to clock skew, or absurdly large after a pause). */
        if (g_frontend.frame_time_callback) {
            retro_usec_t delta;
            if (prev_frame_ns == 0) {
                delta = g_frontend.frame_time_reference;
            } else {
                Uint64 d_ns = frame_start - prev_frame_ns;
                /* Cap at 1 second to keep the core's timer from blowing up
                 * after a pause / window-drag stall. */
                if (d_ns > 1000000000ULL)
                    delta = g_frontend.frame_time_reference;
                else
                    delta = (retro_usec_t)(d_ns / 1000ULL);
            }
            g_frontend.frame_time_callback(delta);
        }
        prev_frame_ns = frame_start;

        core_run();

        /* Skip the frame-pacing delay when the core has requested
         * fast-forward via SET_FASTFORWARDING_OVERRIDE. */
        if (!g_frontend.fast_forward_active) {
            Uint64 elapsed = SDL_GetTicksNS() - frame_start;
            if (elapsed < target_frame_ns)
                SDL_DelayNS(target_frame_ns - elapsed);
        }
    }
}
