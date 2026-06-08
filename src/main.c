/*
 * PureRetro — Entry point
 *
 * Parses command-line arguments, initializes all subsystems,
 * and runs the main emulation loop.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "frontend.h"
#include "core.h"
#include "core_variables.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "log.h"

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
    fprintf(stderr, "  --system-dir <path> Directory reported via GET_SYSTEM_DIRECTORY\n");
    fprintf(stderr, "                      (overrides --portable and the default SDL pref path)\n");
    fprintf(stderr, "  --config <path>     Load key remapping configuration file\n");
    fprintf(stderr, "  --disk-index <N>    For multi-disc content: initial disk index (0-based)\n");
    fprintf(stderr, "  --lang <code>       Language reported to the core (e.g. en, ja, fr, de,\n");
    fprintf(stderr, "                      es, it, pt_br, pt_pt, ru, ko, zh_cn, zh_tw)\n");
    fprintf(stderr, "  --username <name>   Player name reported via GET_USERNAME\n");
    fprintf(stderr, "  --subsystem <ident> Load content via subsystem (e.g. sgb, bsx)\n");
    fprintf(stderr, "  --core-assets-dir <path>     Directory reported via GET_CORE_ASSETS_DIRECTORY\n");
    fprintf(stderr, "  --playlist-dir <path>        Directory reported via GET_PLAYLIST_DIRECTORY\n");
    fprintf(stderr, "  --file-browser-dir <path>    Directory reported via GET_FILE_BROWSER_START_DIRECTORY\n");
    fprintf(stderr, "  --audio-rate <Hz>    Override audio sample rate (default: core's rate)\n");
    fprintf(stderr, "  --audio-buffer-ms <ms>  Override minimum audio buffer latency (default: %d)\n",
            FRONTEND_AUDIO_BUFFER_MS);
    fprintf(stderr, "  --log-level <lvl>   debug, info (default), warn, or error\n");
    fprintf(stderr, "                      Also settable via PURERETRO_LOG environment variable.\n");
    fprintf(stderr, "  --savestate <file>  Load a savestate file after core init.\n");
}

static bool parse_lang(const char *arg, enum retro_language *out)
{
    /* Compact 2-letter / locale-style codes mapped to RETRO_LANGUAGE_*.
     * Only the most common codes are wired; extend as needed. */
    static const struct { const char *code; enum retro_language id; } map[] = {
        { "en",     RETRO_LANGUAGE_ENGLISH },
        { "en_gb",  RETRO_LANGUAGE_BRITISH_ENGLISH },
        { "ja",     RETRO_LANGUAGE_JAPANESE },
        { "fr",     RETRO_LANGUAGE_FRENCH },
        { "es",     RETRO_LANGUAGE_SPANISH },
        { "de",     RETRO_LANGUAGE_GERMAN },
        { "it",     RETRO_LANGUAGE_ITALIAN },
        { "nl",     RETRO_LANGUAGE_DUTCH },
        { "pt_br",  RETRO_LANGUAGE_PORTUGUESE_BRAZIL },
        { "pt_pt",  RETRO_LANGUAGE_PORTUGUESE_PORTUGAL },
        { "ru",     RETRO_LANGUAGE_RUSSIAN },
        { "ko",     RETRO_LANGUAGE_KOREAN },
        { "zh_tw",  RETRO_LANGUAGE_CHINESE_TRADITIONAL },
        { "zh_cn",  RETRO_LANGUAGE_CHINESE_SIMPLIFIED },
        { "eo",     RETRO_LANGUAGE_ESPERANTO },
        { "pl",     RETRO_LANGUAGE_POLISH },
        { "vi",     RETRO_LANGUAGE_VIETNAMESE },
        { "ar",     RETRO_LANGUAGE_ARABIC },
        { "el",     RETRO_LANGUAGE_GREEK },
        { "tr",     RETRO_LANGUAGE_TURKISH },
        { "sk",     RETRO_LANGUAGE_SLOVAK },
        { "fa",     RETRO_LANGUAGE_PERSIAN },
        { "he",     RETRO_LANGUAGE_HEBREW },
        { "fi",     RETRO_LANGUAGE_FINNISH },
        { "id",     RETRO_LANGUAGE_INDONESIAN },
        { "sv",     RETRO_LANGUAGE_SWEDISH },
        { "uk",     RETRO_LANGUAGE_UKRAINIAN },
        { "cs",     RETRO_LANGUAGE_CZECH },
        { "hu",     RETRO_LANGUAGE_HUNGARIAN },
        { "no",     RETRO_LANGUAGE_NORWEGIAN },
        { "ga",     RETRO_LANGUAGE_IRISH },
        { "th",     RETRO_LANGUAGE_THAI },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(arg, map[i].code) == 0) {
            *out = map[i].id;
            return true;
        }
    }
    return false;
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
        } else if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--config requires a file path\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            g_frontend.config_path = argv[i];
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
            LOG_INFO("Renderer preference: %s",
                     renderer_name(g_frontend.preferred_renderer));
        } else if (strcmp(argv[i], "--system-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--system-dir requires a path\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            free(g_frontend.system_directory);
            g_frontend.system_directory = SDL_strdup(argv[i]);
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
        } else if (strcmp(argv[i], "--disk-index") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--disk-index requires an integer argument\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            char *endptr = NULL;
            long val = strtol(argv[i], &endptr, 10);
            if (*endptr != '\0' || val < 0 || val > 255) {
                fprintf(stderr, "Invalid disk index: '%s' (expected 0-255)\n",
                        argv[i]);
                print_usage(argv[0]);
                return false;
            }
            g_frontend.initial_disk_index = (int)val;
        } else if (strcmp(argv[i], "--lang") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--lang requires a language code\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            if (!parse_lang(argv[i], &g_frontend.language)) {
                fprintf(stderr, "Unknown language code: '%s'\n", argv[i]);
                print_usage(argv[0]);
                return false;
            }
        } else if (strcmp(argv[i], "--subsystem") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--subsystem requires a subsystem identifier\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            g_frontend.subsystem_ident = argv[i];
        } else if (strcmp(argv[i], "--username") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--username requires a name\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            free(g_frontend.username);
            g_frontend.username = SDL_strdup(argv[i]);
        } else if (strcmp(argv[i], "--core-assets-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--core-assets-dir requires a path\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            free(g_frontend.core_assets_directory);
            g_frontend.core_assets_directory = SDL_strdup(argv[i]);
        } else if (strcmp(argv[i], "--playlist-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--playlist-dir requires a path\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            free(g_frontend.playlist_directory);
            g_frontend.playlist_directory = SDL_strdup(argv[i]);
        } else if (strcmp(argv[i], "--file-browser-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--file-browser-dir requires a path\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            free(g_frontend.file_browser_directory);
            g_frontend.file_browser_directory = SDL_strdup(argv[i]);
        } else if (strcmp(argv[i], "--audio-rate") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--audio-rate requires a Hz value (e.g. 48000)\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            char *endptr = NULL;
            long val = strtol(argv[i], &endptr, 10);
            if (*endptr != '\0' || val < 4000 || val > 384000) {
                fprintf(stderr, "Invalid audio rate: '%s' (expected 4000-384000)\n",
                        argv[i]);
                print_usage(argv[0]);
                return false;
            }
            g_frontend.audio_rate_override = (unsigned)val;
        } else if (strcmp(argv[i], "--audio-buffer-ms") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--audio-buffer-ms requires a value in milliseconds\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            char *endptr = NULL;
            long val = strtol(argv[i], &endptr, 10);
            if (*endptr != '\0' || val < 1 || val > 5000) {
                fprintf(stderr, "Invalid audio buffer: '%s' (expected 1-5000 ms)\n",
                        argv[i]);
                print_usage(argv[0]);
                return false;
            }
            g_frontend.audio_buffer_ms_override = (unsigned)val;
        } else if (strcmp(argv[i], "--log-level") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--log-level requires an argument (debug|info|warn|error)\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            enum log_level lvl;
            if (!log_parse_level(argv[i], &lvl)) {
                fprintf(stderr, "Invalid log level: '%s'\n", argv[i]);
                print_usage(argv[0]);
                return false;
            }
            log_set_level(lvl);
        } else if (strcmp(argv[i], "--savestate") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--savestate requires a file path\n");
                print_usage(argv[0]);
                return false;
            }
            ++i;
            g_frontend.savestate_load_path = argv[i];
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
    /* If --system-dir was explicitly given, skip automatic resolution. */
    if (g_frontend.system_directory) {
        LOG_INFO("System directory: %s (from --system-dir)",
                 g_frontend.system_directory);
        SDL_CreateDirectory(g_frontend.system_directory);
        return;
    }

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
            LOG_INFO("System directory (portable): %s",
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
        LOG_INFO("System directory: %s", g_frontend.system_directory);
        SDL_CreateDirectory(g_frontend.system_directory);
    }
    SDL_free((void *)pref);
}

static bool frontend_init(void)
{
    log_init();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    g_frontend.running = true;

    /* Set up the system directory for firmware/BIOS files.
     * Cores like Beetle PSX HW look here for scph5500.bin etc. */
    set_system_directory();

    if (!video_init("PureRetro", 640, 480)) {
        LOG_ERROR("Failed to initialize video");
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
    free(g_frontend.save_directory);
    g_frontend.save_directory = NULL;
    free(g_frontend.core_assets_directory);
    g_frontend.core_assets_directory = NULL;
    free(g_frontend.playlist_directory);
    g_frontend.playlist_directory = NULL;
    free(g_frontend.file_browser_directory);
    g_frontend.file_browser_directory = NULL;
    SDL_free(g_frontend.username);
    g_frontend.username = NULL;
    SDL_Quit();
}

static void run_loop(void)
{
    SDL_Event event;
    Uint64 prev_frame_ns = 0;

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
        if (target_frame_ns > 0 && !g_frontend.fast_forward_active) {
            Uint64 elapsed = SDL_GetTicksNS() - frame_start;
            if (elapsed < target_frame_ns)
                SDL_DelayNS(target_frame_ns - elapsed);
        }
    }
}

int main(int argc, char *argv[])
{
    memset(&g_frontend, 0, sizeof(g_frontend));
    g_frontend.initial_disk_index = -1;
    g_frontend.language = RETRO_LANGUAGE_ENGLISH;

    if (!parse_args(argc, argv))
        return EXIT_FAILURE;

    if (!frontend_init())
        return EXIT_FAILURE;

    if (!core_load(g_frontend.core_path)) {
        LOG_ERROR("Failed to load core: %s", g_frontend.core_path);
        frontend_shutdown();
        return EXIT_FAILURE;
    }

    /* Load persisted core option overrides before core_init so the core
     * sees them on its first GET_VARIABLE calls. */
    char *opt_path = core_variables_path(g_frontend.core_path,
                                          g_frontend.system_directory);
    if (opt_path)
        core_variables_load(opt_path);

    if (g_frontend.config_path) {
        if (!input_load_keymap(g_frontend.config_path)) {
            LOG_WARN("Failed to load keymap config: %s",
                     g_frontend.config_path);
        }
    }

    if (!core_init(g_frontend.content_path)) {
        LOG_ERROR("Failed to initialize core");
        free(opt_path);
        /* Tear the core down first (stops its background threads, releases
         * its own resources) before destroying the SDL / video subsystems
         * the core may still be holding pointers into. */
        core_unload();
        frontend_shutdown();
        return EXIT_FAILURE;
    }

    /* Software-only cores never call SET_HW_RENDER, so bootstrap
     * the software backend here. No-op for hardware cores. */
    if (!video_ensure_software_renderer()) {
        LOG_ERROR("Failed to initialize software renderer");
        free(opt_path);
        core_unload();
        frontend_shutdown();
        return EXIT_FAILURE;
    }

    if (g_frontend.fullscreen && g_frontend.video.window) {
        SDL_SetWindowFullscreen(g_frontend.video.window, true);
    }

    /* Log the final renderer state after core init. If the core never called
     * SET_HW_RENDER (e.g. software-only core), this still shows sw. */
    LOG_INFO("Final active renderer: %s",
             renderer_name(g_frontend.video.renderer));

    /* Warn if the user requested HW but ended up in software. This usually
     * means the core failed to load (retro_load_game returned false) or the
     * core never called SET_HW_RENDER despite being a HW core. */
    if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE &&
        g_frontend.preferred_renderer != VIDEO_RENDERER_SW &&
        g_frontend.video.renderer == VIDEO_RENDERER_SW) {
        LOG_WARN("user requested '%s' but renderer is 'sw'.",
                 renderer_name(g_frontend.preferred_renderer));
        if (!g_frontend.hw_render_requested) {
            LOG_WARN("The core never called SET_HW_RENDER. Common causes:");
            LOG_WARN("  - Missing firmware/BIOS (cores like Beetle PSX HW");
            LOG_WARN("    silently fall back to software without a valid BIOS)");
            LOG_WARN("  - Core does not support the requested renderer");
            LOG_WARN("  - Core failed to load content (check earlier errors)");
        }
    }

    /* Initialize audio now that we know the core's sample rate */
    if (!g_frontend.no_audio) {
        double rate = (g_frontend.audio_rate_override > 0)
                      ? (double)g_frontend.audio_rate_override
                      : g_av_info.timing.sample_rate;
        if (g_frontend.audio_rate_override > 0) {
            LOG_INFO("Overriding core sample rate %.2f Hz with %u Hz from --audio-rate",
                     g_av_info.timing.sample_rate,
                     g_frontend.audio_rate_override);
        }
        if (!audio_init(rate)) {
            LOG_WARN("Failed to initialize audio");
        } else if (g_frontend.audio_buffer_ms_override > 0) {
            audio_set_minimum_latency(g_frontend.audio_buffer_ms_override);
            LOG_INFO("Audio buffer minimum latency set to %u ms via --audio-buffer-ms",
                     g_frontend.audio_buffer_ms_override);
        }
    }

    /* SRAM auto-persistence. The path is derived from the content's basename
     * (without extension) under save_directory (falling back to the system
     * directory when save_directory was not set). Cores with no SRAM region
     * silently no-op. */
    {
        const char *save_dir = g_frontend.save_directory
                               ? g_frontend.save_directory
                               : g_frontend.system_directory;
        g_frontend.sram_path = core_sram_path(save_dir, g_frontend.content_path);
        if (g_frontend.sram_path)
            core_sram_load(g_frontend.sram_path);
    }

    /* Load an explicit savestate if requested via --savestate. Failure here
     * is non-fatal: the user still gets the game from a fresh state. */
    if (g_frontend.savestate_load_path)
        core_savestate_load(g_frontend.savestate_load_path);

    run_loop();

    /* Persist SRAM before tearing down the core. retro_get_memory_data
     * returns NULL after core_unload, so this must run first. */
    if (g_frontend.sram_path) {
        core_sram_save(g_frontend.sram_path);
        free(g_frontend.sram_path);
        g_frontend.sram_path = NULL;
    }

    if (g_frontend.video.hw_render_enabled) {
        video_context_destroy();
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
