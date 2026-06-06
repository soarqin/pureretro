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
#include "core_variables.h"
#include "video.h"
#include "video_sw.h"
#include "video_gl.h"
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
    fprintf(stderr, "  --scale <N>         Integer window scale (1-16)\n");
    fprintf(stderr, "  --no-audio          Disable audio output\n");
    fprintf(stderr, "  --variable <k=v>    Override a core option variable\n");
    fprintf(stderr, "  --portable          Portable mode: use the current directory as the\n");
    fprintf(stderr, "                      config base (system files in ./system)\n");
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
        } else if (strcmp(argv[i], "--scale") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--scale requires an integer argument (1-16)\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            char *endptr = NULL;
            long val = strtol(argv[i], &endptr, 10);
            if (*endptr != '\0' || val < 1 || val > 16) {
                fprintf(stderr, "Invalid scale: '%s' (expected 1-16)\n", argv[i]);
                print_usage(argv[0]);
                return false;
            }
            g_frontend.window_scale = (unsigned)val;
        } else if (strcmp(argv[i], "--no-audio") == 0) {
            g_frontend.no_audio = true;
        } else if (strcmp(argv[i], "--portable") == 0) {
            g_frontend.portable = true;
        } else if (strcmp(argv[i], "--render") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--render requires an argument (vk, gl, or sw)\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            if (!parse_render(argv[i], &g_frontend.preferred_renderer)) {
                fprintf(stderr, "Invalid renderer: '%s' (expected vk, gl, or sw)\n",
                        argv[i]);
                print_usage(argv[0]);
                return false;
            }
            fprintf(stderr, "Renderer preference: %s\n",
                    renderer_name(g_frontend.preferred_renderer));
        } else if (strcmp(argv[i], "--variable") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--variable requires an argument (key=value)\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            const char *arg = argv[i];
            const char *eq = strchr(arg, '=');
            if (!eq) {
                fprintf(stderr, "Invalid variable syntax: '%s' (expected key=value)\n", arg);
                print_usage(argv[0]);
                return false;
            }
            size_t key_len = (size_t)(eq - arg);
            char key[256];
            if (key_len >= sizeof(key)) {
                fprintf(stderr, "Variable key too long (max %zu): '%.*s...'\n",
                        sizeof(key) - 1, (int)(sizeof(key) - 1), arg);
                print_usage(argv[0]);
                return false;
            }
            memcpy(key, arg, key_len);
            key[key_len] = '\0';
            core_variable_override(key, eq + 1);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return false;
        }
    }

    return true;
}

/* Compute and create g_frontend.system_directory. Best-effort: on failure
 * the field is left NULL and the program continues (cores that need a
 * system directory will simply error out). */
static void set_system_directory(void)
{
    if (g_frontend.portable) {
        /* Portable mode: keep all data alongside the binary's working
         * directory. The system directory is "<cwd>/system". */
        char *cwd = SDL_GetCurrentDirectory();
        if (!cwd)
            return;

        size_t cwd_len = strlen(cwd);
        /* Strip a trailing separator (e.g., "/" on Unix roots). */
        while (cwd_len > 1 &&
               (cwd[cwd_len - 1] == '/' || cwd[cwd_len - 1] == '\\'))
            cwd_len--;

        size_t total = cwd_len + 1 + strlen("system") + 1;
        g_frontend.system_directory = malloc(total);
        if (g_frontend.system_directory) {
            snprintf(g_frontend.system_directory, total,
                     "%.*s/system", (int)cwd_len, cwd);
            fprintf(stderr, "System directory (portable): %s\n",
                    g_frontend.system_directory);
            SDL_CreateDirectory(g_frontend.system_directory);
        }
        SDL_free(cwd);
        return;
    }

    /* Default: SDL_GetPrefPath returns a platform-appropriate user data
     * directory, already terminated with a separator. */
    const char *pref = SDL_GetPrefPath("pureretro", "system");
    if (!pref)
        return;

    size_t len = strlen(pref);
    g_frontend.system_directory = malloc(len + 1);
    if (g_frontend.system_directory) {
        memcpy(g_frontend.system_directory, pref, len + 1);
        fprintf(stderr, "System directory: %s\n", g_frontend.system_directory);
        SDL_CreateDirectory(g_frontend.system_directory);
    }
    SDL_free((void *)pref);
}

static bool frontend_init(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    g_frontend.running = true;

    /* Set up the system directory for firmware/BIOS files.
     * Cores like Beetle PSX HW look here for scph5500.bin etc. */
    set_system_directory();

    if (!video_init("PureRetro", 640, 480)) {
        fprintf(stderr, "Failed to initialize video\n");
        free(g_frontend.system_directory);
        g_frontend.system_directory = NULL;
        return false;
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
        Uint64 frame_start = SDL_GetTicksNS();

        /* Recompute the target frame budget every iteration so a core that
         * changes its AV info via SET_SYSTEM_AV_INFO mid-run is honoured. */
        double fps = g_av_info.timing.fps;
        Uint64 target_frame_ns = (fps > 0.0) ? (Uint64)(1000000000.0 / fps) : 0;

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

        if (target_frame_ns > 0) {
            Uint64 elapsed = SDL_GetTicksNS() - frame_start;
            if (elapsed < target_frame_ns)
                SDL_DelayNS(target_frame_ns - elapsed);
        }
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

    /* Load persisted core option overrides before core_init so the core
     * sees them on its first GET_VARIABLE calls. */
    char *opt_path = core_variables_path(g_frontend.core_path,
                                          g_frontend.system_directory);
    if (opt_path)
        core_variables_load(opt_path);

    if (!core_init(g_frontend.content_path)) {
        fprintf(stderr, "Failed to initialize core\n");
        free(opt_path);
        /* Tear the core down first (stops its background threads, releases
         * its own resources) before destroying the SDL / video subsystems
         * the core may still be holding pointers into. */
        core_unload();
        frontend_shutdown();
        return EXIT_FAILURE;
    }

    /* Create window for software cores that never called SET_HW_RENDER.
     * Hardware cores already created the window (with the correct flags)
     * inside video_set_hw_render when they selected their renderer. */
    if (!g_frontend.video.window) {
        g_frontend.video.window = SDL_CreateWindow("PureRetro", 640, 480, 0);
        if (!g_frontend.video.window) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            free(opt_path);
            core_unload();
            frontend_shutdown();
            return EXIT_FAILURE;
        }
        if (!video_sw_init(g_frontend.video.window, &g_frontend.video.sw)) {
            fprintf(stderr, "Failed to initialize software renderer\n");
            free(opt_path);
            core_unload();
            frontend_shutdown();
            return EXIT_FAILURE;
        }
        fprintf(stderr, "Created window for software renderer\n");
    }

    if (g_frontend.fullscreen && g_frontend.video.window) {
        SDL_SetWindowFullscreen(g_frontend.video.window, true);
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

    /* Resize window to integer scale if requested */
    if (g_frontend.window_scale > 0) {
        unsigned base_w = g_av_info.geometry.base_width;
        unsigned base_h = g_av_info.geometry.base_height;
        if (base_w > 0 && base_h > 0) {
            unsigned w = base_w * g_frontend.window_scale;
            unsigned h = base_h * g_frontend.window_scale;
            if (w <= FRONTEND_MAX_WIDTH && h <= FRONTEND_MAX_HEIGHT) {
                SDL_SetWindowSize(g_frontend.video.window, (int)w, (int)h);
            }
        }
    }

    /* Initialize audio now that we know the core's sample rate */
    if (!g_frontend.no_audio && !audio_init(g_av_info.timing.sample_rate)) {
        fprintf(stderr, "Warning: failed to initialize audio\n");
    }

    run_loop();

    /* For OpenGL, invoke the core's context_destroy callback while the
     * core is still loaded. The callback lives inside the core's shared
     * object; calling it after SDL_UnloadObject would segfault.
     * video_gl_context_destroy() zeros the pointer so the later call in
     * video_gl_destroy (during frontend_shutdown) is a no-op.
     * For Vulkan, keep unloading the core first so its background threads
     * stop before we tear down the VkInstance. */
    if (g_frontend.video.hw_render_enabled &&
        g_frontend.video.renderer == VIDEO_RENDERER_OPENGL &&
        g_frontend.video.gl) {
        video_gl_context_destroy(g_frontend.video.gl);
    }

    /* Persist the current disk overrides while the table is still alive. */
    if (opt_path) {
        core_variables_save(opt_path);
        free(opt_path);
    }

    core_unload();
    frontend_shutdown();

    return EXIT_SUCCESS;
}
