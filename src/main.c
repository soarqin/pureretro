/*
 * PureRetro — Entry point
 *
 * Parses command-line arguments, initializes all subsystems,
 * and runs the main emulation loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "frontend.h"
#include "core.h"
#include "video.h"
#include "audio.h"
#include "input.h"

/* Global frontend state */
struct frontend_state g_frontend;

static void print_usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s <core> <content> [options]\n", argv0);
    fprintf(stderr, "  <core>     Path to the libretro core (e.g., nestopia_libretro.so)\n");
    fprintf(stderr, "  <content>  Path to the game ROM or content file\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --fullscreen, -f    Start in fullscreen mode\n");
    fprintf(stderr, "  --render <api>      Hint preferred renderer: vk, gl, or sw\n");
    fprintf(stderr, "                      (Core may still choose a different renderer.)\n");
}

static bool parse_render(const char *arg, enum video_renderer *out)
{
    if (strcmp(arg, "vk") == 0) {
        *out = VIDEO_RENDERER_VULKAN;
        return true;
    }
    if (strcmp(arg, "gl") == 0) {
        *out = VIDEO_RENDERER_OPENGL;
        return true;
    }
    if (strcmp(arg, "sw") == 0) {
        *out = VIDEO_RENDERER_SW;
        return true;
    }
    return false;
}

static bool parse_args(int argc, char *argv[])
{
    int i;

    if (argc < 2) {
        print_usage(argv[0]);
        return false;
    }

    g_frontend.core_path = argv[1];

    if (argc >= 3 && argv[2][0] != '-') {
        g_frontend.content_path = argv[2];
        i = 3;
    } else {
        g_frontend.content_path = NULL;
        i = 2;
    }

    for (; i < argc; ++i) {
        if (strcmp(argv[i], "--fullscreen") == 0 ||
            strcmp(argv[i], "-f") == 0) {
            g_frontend.fullscreen = true;
        } else if (strcmp(argv[i], "--render") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--render requires an argument (vk, gl, or sw)\n");
                print_usage(argv[0]);
                return false;
            }
            if (!parse_render(argv[++i], &g_frontend.preferred_renderer)) {
                fprintf(stderr, "Invalid renderer: '%s' (expected vk, gl, or sw)\n",
                        argv[i]);
                print_usage(argv[0]);
                return false;
            }
            fprintf(stderr, "Renderer preference: %s\n",
                    renderer_name(g_frontend.preferred_renderer));
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return false;
        }
    }

    return true;
}

static bool frontend_init(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    g_frontend.running = true;

    if (!video_init("PureRetro", 640, 480)) {
        fprintf(stderr, "Failed to initialize video\n");
        return false;
    }

    if (g_frontend.fullscreen && g_frontend.video.window) {
        SDL_SetWindowFullscreen(g_frontend.video.window, true);
    }

    /* Audio will be initialized after the core loads and reports its sample rate. */

    return true;
}

static void frontend_shutdown(void)
{
    audio_shutdown();
    video_shutdown();
    SDL_Quit();
}

static void run_loop(void)
{
    SDL_Event event;

    while (g_frontend.running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                g_frontend.running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                input_process_event(&event);
                if (event.type == SDL_EVENT_KEY_DOWN) {
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
                video_process_event(&event);
                break;
            default:
                break;
            }
        }

        input_poll();
        core_run();
    }
}

int main(int argc, char *argv[])
{
    memset(&g_frontend, 0, sizeof(g_frontend));

    if (!parse_args(argc, argv))
        return EXIT_FAILURE;

    if (!frontend_init())
        return EXIT_FAILURE;

    if (!core_load(g_frontend.core_path)) {
        fprintf(stderr, "Failed to load core: %s\n", g_frontend.core_path);
        frontend_shutdown();
        return EXIT_FAILURE;
    }

    if (!core_init(g_frontend.content_path)) {
        fprintf(stderr, "Failed to initialize core\n");
        core_unload();
        frontend_shutdown();
        return EXIT_FAILURE;
    }

    /* Log the final renderer state after core init. If the core never called
     * SET_HW_RENDER (e.g. software-only core), this still shows sw. */
    fprintf(stderr, "Final active renderer: %s\n",
            renderer_name(g_frontend.video.renderer));

    /* Warn if the user requested HW but ended up in software. This usually
     * means the core failed to load (retro_load_game returned false) or the
     * core never called SET_HW_RENDER despite being a HW core. */
    if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE &&
        g_frontend.preferred_renderer != VIDEO_RENDERER_SW &&
        g_frontend.video.renderer == VIDEO_RENDERER_SW) {
        fprintf(stderr, "  WARNING: user requested '%s' but renderer is 'sw'.\n",
                renderer_name(g_frontend.preferred_renderer));
        fprintf(stderr, "  This usually means the core failed to load. Check\n");
        fprintf(stderr, "  for 'retro_load_game failed' or other errors above.\n");
    }

    /* Initialize audio now that we know the core's sample rate */
    if (!audio_init(g_av_info.timing.sample_rate)) {
        fprintf(stderr, "Warning: failed to initialize audio\n");
    }

    run_loop();

    core_unload();
    frontend_shutdown();

    return EXIT_SUCCESS;
}
