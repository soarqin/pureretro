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

    /* Set up the system directory for firmware/BIOS files.
     * SDL_GetPrefPath returns a platform-appropriate user data directory.
     * Cores like Beetle PSX HW look here for scph5500.bin etc. */
    const char *pref = SDL_GetPrefPath("pureretro", "system");
    if (pref) {
        /* SDL_GetPrefPath already appends the app name ("system") and a
         * trailing separator. Copy it into our own buffer. */
        size_t len = strlen(pref);
        g_frontend.system_directory = malloc(len + 1);
        if (g_frontend.system_directory) {
            memcpy(g_frontend.system_directory, pref, len + 1);
            fprintf(stderr, "System directory: %s\n", g_frontend.system_directory);
            /* Create the directory if it doesn't exist (best-effort) */
            SDL_CreateDirectory(g_frontend.system_directory);
        }
        SDL_free((void *)pref);
    }

    if (!video_init("PureRetro", 640, 480)) {
        fprintf(stderr, "Failed to initialize video\n");
        free(g_frontend.system_directory);
        g_frontend.system_directory = NULL;
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
    free(g_frontend.system_directory);
    g_frontend.system_directory = NULL;
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
        if (!g_frontend.hw_render_requested) {
            fprintf(stderr, "  The core never called SET_HW_RENDER. Common causes:\n");
            fprintf(stderr, "  - Missing firmware/BIOS (cores like Beetle PSX HW\n");
            fprintf(stderr, "    silently fall back to software without a valid BIOS)\n");
            fprintf(stderr, "  - Core does not support the requested renderer\n");
            fprintf(stderr, "  - Core failed to load content (check earlier errors)\n");
        }
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
