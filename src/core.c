/*
 * PureRetro — Core management
 *
 * Dynamic loading of libretro cores, callback wiring, and
 * environment callback implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <SDL3/SDL.h>
#include "core.h"
#include "frontend.h"
#include "video.h"
#include "video_gl.h"
#include "video_vk.h"
#include "audio.h"
#include "input.h"

struct core_functions g_core;
struct retro_system_av_info g_av_info;

static SDL_SharedObject *g_core_handle = NULL;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static bool load_file(const char *path, void **out_data, size_t *out_size)
{
    FILE *fp;
    long size;
    void *data;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }

    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return false;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    data = malloc((size_t)size);
    if (!data) {
        fclose(fp);
        return false;
    }

    if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        fclose(fp);
        return false;
    }

    fclose(fp);
    *out_data = data;
    *out_size = (size_t)size;
    return true;
}

static void RETRO_CALLCONV log_stderr(enum retro_log_level level,
                                      const char *fmt, ...)
{
    va_list va;
    (void)level;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

bool core_load(const char *path)
{
    g_core_handle = SDL_LoadObject(path);
    if (!g_core_handle) {
        fprintf(stderr, "SDL_LoadObject failed: %s\n", SDL_GetError());
        return false;
    }

#define LOAD_SYM(sym)                                                     \
    do {                                                                  \
        SDL_FunctionPointer _fp = SDL_LoadFunction(g_core_handle, #sym); \
        if (!_fp) {                                                       \
            fprintf(stderr, "Failed to load symbol: %s\n", #sym);        \
            return false;                                                 \
        }                                                                 \
        memcpy(&g_core.sym, &_fp, sizeof(g_core.sym));                   \
    } while (0)

    LOAD_SYM(retro_init);
    LOAD_SYM(retro_deinit);
    LOAD_SYM(retro_api_version);
    LOAD_SYM(retro_get_system_info);
    LOAD_SYM(retro_get_system_av_info);
    LOAD_SYM(retro_set_environment);
    LOAD_SYM(retro_set_video_refresh);
    LOAD_SYM(retro_set_audio_sample);
    LOAD_SYM(retro_set_audio_sample_batch);
    LOAD_SYM(retro_set_input_poll);
    LOAD_SYM(retro_set_input_state);
    LOAD_SYM(retro_set_controller_port_device);
    LOAD_SYM(retro_reset);
    LOAD_SYM(retro_run);
    LOAD_SYM(retro_load_game);
    LOAD_SYM(retro_unload_game);
    LOAD_SYM(retro_get_region);
    LOAD_SYM(retro_get_memory_data);
    LOAD_SYM(retro_get_memory_size);

#undef LOAD_SYM

    return true;
}

void core_unload(void)
{
    if (g_core.retro_deinit)
        g_core.retro_deinit();

    if (g_core.retro_unload_game)
        g_core.retro_unload_game();

    if (g_core_handle) {
        SDL_UnloadObject(g_core_handle);
        g_core_handle = NULL;
    }

    if (g_frontend.rom_data) {
        free(g_frontend.rom_data);
        g_frontend.rom_data = NULL;
        g_frontend.rom_size = 0;
    }

    memset(&g_core, 0, sizeof(g_core));
}

bool core_init(const char *content_path)
{
    struct retro_system_info info;

    fprintf(stderr, "Initializing core (content: %s)\n",
            content_path ? content_path : "<none>");

    if (g_core.retro_api_version() != RETRO_API_VERSION) {
        fprintf(stderr, "Core API version mismatch\n");
        return false;
    }

    g_core.retro_set_environment(core_environment);
    g_core.retro_init();

    g_core.retro_get_system_info(&info);
    fprintf(stderr, "Core: %s (v%s)\n", info.library_name, info.library_version);

    g_core.retro_set_video_refresh(core_video_refresh);
    g_core.retro_set_audio_sample(core_audio_sample);
    g_core.retro_set_audio_sample_batch(core_audio_sample_batch);
    g_core.retro_set_input_poll(core_input_poll);
    g_core.retro_set_input_state(core_input_state);

    if (content_path) {
        struct retro_game_info game;
        memset(&game, 0, sizeof(game));
        game.path = content_path;

        if (!load_file(content_path, &g_frontend.rom_data, &g_frontend.rom_size)) {
            fprintf(stderr, "Failed to load content file: %s\n", content_path);
            return false;
        }

        game.data = g_frontend.rom_data;
        game.size = g_frontend.rom_size;

        fprintf(stderr, "Calling retro_load_game...\n");
        if (!g_core.retro_load_game(&game)) {
            fprintf(stderr, "retro_load_game failed (core rejected the content)\n");
            return false;
        }
        fprintf(stderr, "retro_load_game succeeded\n");
    } else {
        fprintf(stderr, "Calling retro_load_game(NULL)...\n");
        if (!g_core.retro_load_game(NULL)) {
            fprintf(stderr, "retro_load_game(NULL) failed\n");
            return false;
        }
        fprintf(stderr, "retro_load_game(NULL) succeeded\n");
    }

    g_core.retro_get_system_av_info(&g_av_info);
    fprintf(stderr, "AV: %ux%u @ %.2f Hz, audio: %.2f Hz\n",
            g_av_info.geometry.base_width,
            g_av_info.geometry.base_height,
            g_av_info.timing.fps,
            g_av_info.timing.sample_rate);

    return true;
}

void core_run(void)
{
    g_core.retro_run();
}

/* ------------------------------------------------------------------ */
/* Callbacks exposed to the core                                      */
/* ------------------------------------------------------------------ */

bool RETRO_CALLCONV core_environment(unsigned cmd, void *data)
{
    (void)data;

    /* Some cores (e.g. Beetle PSX HW) call callbacks with the experimental
     * flag OR'd in. Since we support all the experimental features the
     * project targets, strip the flag and process the base callback. */
    cmd &= ~RETRO_ENVIRONMENT_EXPERIMENTAL;

    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_ROTATION:
        /* TODO: Support screen rotation */
        return false;

    case RETRO_ENVIRONMENT_GET_OVERSCAN:
        /* Default: no overscan */
        *(bool *)data = false;
        return true;

    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;

    case RETRO_ENVIRONMENT_SET_MESSAGE: {
        const struct retro_message *msg = (const struct retro_message *)data;
        fprintf(stderr, "[CORE] %s\n", msg->msg);
        return true;
    }

    case RETRO_ENVIRONMENT_SHUTDOWN:
        g_frontend.running = false;
        return true;

    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        return false;

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = g_frontend.system_directory;
        fprintf(stderr, "Core queried system directory: %s\n",
                g_frontend.system_directory ? g_frontend.system_directory : "(null)");
        return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format fmt = *(const enum retro_pixel_format *)data;
        if (fmt != RETRO_PIXEL_FORMAT_0RGB1555 &&
            fmt != RETRO_PIXEL_FORMAT_XRGB8888 &&
            fmt != RETRO_PIXEL_FORMAT_RGB565) {
            return false;
        }
        g_frontend.video.pixel_format = fmt;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return false;

    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        return false;

    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        return video_set_hw_render((struct retro_hw_render_callback *)data);

    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: {
        int *preferred = (int *)data;
        bool result;
        switch (g_frontend.preferred_renderer) {
        case VIDEO_RENDERER_VULKAN: *preferred = RETRO_HW_CONTEXT_VULKAN;     result = true; break;
        case VIDEO_RENDERER_OPENGL: *preferred = RETRO_HW_CONTEXT_OPENGL_CORE; result = true; break;
        case VIDEO_RENDERER_SW:     *preferred = RETRO_HW_CONTEXT_NONE;       result = true; break;
        case VIDEO_RENDERER_NONE:
        default:                    result = false; break;
        }
        fprintf(stderr, "Core queried preferred HW render: %s (context=%d)\n",
                result ? "yes" : "no (no preference)", *preferred);
        return result;
    }

    case RETRO_ENVIRONMENT_GET_VARIABLE:
        return false;

    case RETRO_ENVIRONMENT_SET_VARIABLES:
        return false;

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;

    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;

    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        *(const char **)data = g_frontend.core_path;
        return true;

    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
        return false;

    case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
        return false;

    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
        *(uint64_t *)data = (1 << RETRO_DEVICE_JOYPAD);
        return true;

    case RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback *cb = (struct retro_log_callback *)data;
        cb->log = log_stderr;
        return true;
    }

    case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char **)data = NULL;
        return true;

    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = NULL;
        fprintf(stderr, "Core queried save directory: (null - core will use system directory)\n");
        return true;

    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const struct retro_system_av_info *av =
            (const struct retro_system_av_info *)data;
        g_av_info = *av;

        if (g_frontend.video.hw_render_enabled &&
            g_frontend.video.renderer == VIDEO_RENDERER_OPENGL &&
            g_frontend.video.gl) {
            video_gl_resize(g_frontend.video.gl,
                            av->geometry.max_width,
                            av->geometry.max_height);
        }
        return true;
    }

#ifdef PURERETRO_VULKAN_ENABLED
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: {
        if (g_frontend.video.renderer != VIDEO_RENDERER_VULKAN || !g_frontend.video.vk)
            return false;
        const struct retro_hw_render_interface **iface =
            (const struct retro_hw_render_interface **)data;
        *iface = (const struct retro_hw_render_interface *)&g_frontend.video.vk->hw_if;
        return true;
    }
#else
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
        return false;
#endif

    default:
        /* Log unhandled callbacks to help diagnose core compatibility issues.
         * Many cores call callbacks we don't implement; this is normal. */
        fprintf(stderr, "Unhandled env callback: %d\n", (int)cmd);
        return false;
    }
}

void RETRO_CALLCONV core_video_refresh(const void *data, unsigned width,
                                       unsigned height, size_t pitch)
{
    video_present(data, width, height, pitch);
}

void RETRO_CALLCONV core_audio_sample(int16_t left, int16_t right)
{
    int16_t samples[2] = { left, right };
    audio_push(samples, 1);
}

size_t RETRO_CALLCONV core_audio_sample_batch(const int16_t *data, size_t frames)
{
    return audio_push(data, frames);
}

void RETRO_CALLCONV core_input_poll(void)
{
    /* Input is polled in the main loop via SDL_PollEvent. */
}

int16_t RETRO_CALLCONV core_input_state(unsigned port, unsigned device,
                                        unsigned index, unsigned id)
{
    (void)index;

    if (port != 0)
        return 0;

    switch (device) {
    case RETRO_DEVICE_JOYPAD:
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
            return (int16_t)input_state_joypad_mask(port);
        return input_state_joypad(port, id);

    case RETRO_DEVICE_ANALOG:
        /* TODO: Map keyboard to analog axes */
        return 0;

    default:
        return 0;
    }
}
