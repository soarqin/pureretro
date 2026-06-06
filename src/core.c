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
/* Variable management (SET_VARIABLES / GET_VARIABLE)                 */
/* ------------------------------------------------------------------ */

static void variables_free(struct retro_variable **vars, size_t *count)
{
    if (!*vars)
        return;
    for (size_t i = 0; i < *count; ++i) {
        free((char *)(*vars)[i].key);
        free((char *)(*vars)[i].value);
    }
    free(*vars);
    *vars = NULL;
    *count = 0;
}

static bool variable_add(struct retro_variable **vars, size_t *count,
                         size_t *capacity, const char *key, const char *value)
{
    /* Check if key already exists: update in-place */
    for (size_t i = 0; i < *count; ++i) {
        if (strcmp((*vars)[i].key, key) == 0) {
            size_t vl = strlen(value);
            char *v = malloc(vl + 1);
            if (!v)
                return false;
            memcpy(v, value, vl + 1);
            free((char *)(*vars)[i].value);
            (*vars)[i].value = v;
            return true;
        }
    }

    if (*count >= *capacity) {
        size_t new_cap = *capacity ? *capacity * 2 : 16;
        struct retro_variable *new_arr = realloc(*vars,
                                                 new_cap * sizeof(struct retro_variable));
        if (!new_arr)
            return false;
        *vars = new_arr;
        *capacity = new_cap;
    }

    size_t kl = strlen(key);
    size_t vl = strlen(value);
    char *k = malloc(kl + 1);
    char *v = malloc(vl + 1);
    if (!k || !v) {
        free(k);
        free(v);
        return false;
    }
    memcpy(k, key, kl + 1);
    memcpy(v, value, vl + 1);

    (*vars)[*count].key = k;
    (*vars)[*count].value = v;
    (*count)++;
    return true;
}

static int variable_cmp(const void *a, const void *b)
{
    const struct retro_variable *va = (const struct retro_variable *)a;
    const struct retro_variable *vb = (const struct retro_variable *)b;
    return strcmp(va->key, vb->key);
}

static void variables_sort(struct retro_variable *vars, size_t count)
{
    if (count > 1)
        qsort(vars, count, sizeof(struct retro_variable), variable_cmp);
}

static const char *variables_find(const struct retro_variable *vars, size_t count,
                                  const char *key)
{
    if (!vars || count == 0)
        return NULL;

    struct retro_variable target = { key, NULL };
    const struct retro_variable *found =
        (const struct retro_variable *)bsearch(&target, vars, count,
                                                sizeof(struct retro_variable),
                                                variable_cmp);
    return found ? found->value : NULL;
}

/* Parse the default value from a retro_variable value string.
 * Format: "description; default|option1|option2|..." */
static const char *parse_default_value(const char *raw)
{
    if (!raw)
        return NULL;

    const char *semi = strchr(raw, ';');
    if (!semi)
        return raw; /* no description, entire string is options */

    const char *def = semi + 1;
    while (*def == ' ')
        ++def;

    if (*def == '\0')
        return NULL;

    static char buf[256];
    size_t i = 0;
    while (*def && *def != '|' && i < sizeof(buf) - 1)
        buf[i++] = *def++;
    buf[i] = '\0';

    return buf;
}

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

    variables_free(&g_frontend.variables, &g_frontend.variable_count);
    g_frontend.variable_capacity = 0;
    /* Note: overrides are intentionally kept across core reloads. */

    memset(&g_core, 0, sizeof(g_core));
}

void core_variable_override(const char *key, const char *value)
{
    if (!key || !value)
        return;
    if (!variable_add(&g_frontend.variable_overrides, &g_frontend.override_count,
                      &g_frontend.override_capacity, key, value)) {
        fprintf(stderr, "Failed to store variable override: %s=%s\n", key, value);
    } else {
        fprintf(stderr, "Variable override: %s=%s\n", key, value);
    }
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
    g_core.retro_set_video_refresh(core_video_refresh);
    g_core.retro_set_audio_sample(core_audio_sample);
    g_core.retro_set_audio_sample_batch(core_audio_sample_batch);
    g_core.retro_set_input_poll(core_input_poll);
    g_core.retro_set_input_state(core_input_state);
    g_core.retro_init();

    g_core.retro_get_system_info(&info);
    fprintf(stderr, "Core: %s (v%s)\n", info.library_name, info.library_version);

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

    /* Notify the core that the HW context is ready. This must happen after
     * retro_load_game returns so the core has finished its own setup. */
    if (g_frontend.video.hw_render_enabled && g_frontend.video.hw.context_reset) {
        fprintf(stderr, "Calling context_reset after retro_load_game...\n");
        g_frontend.video.hw.context_reset();
    }

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
        const char *fmt_name = "unknown";
        switch (fmt) {
        case RETRO_PIXEL_FORMAT_0RGB1555: fmt_name = "0RGB1555"; break;
        case RETRO_PIXEL_FORMAT_XRGB8888: fmt_name = "XRGB8888"; break;
        case RETRO_PIXEL_FORMAT_RGB565:   fmt_name = "RGB565";   break;
        case RETRO_PIXEL_FORMAT_UNKNOWN:  fmt_name = "UNKNOWN";  break;
        }
        fprintf(stderr, "Core requested pixel format: %s (%d)\n", fmt_name, (int)fmt);
        /* Accept all formats. The software renderer will handle whatever the
         * core sends. Returning false here causes cores like Beetle PSX HW
         * to skip SET_HW_RENDER entirely and fall back to software. */
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

    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = (struct retro_variable *)data;
        const char *override = variables_find(g_frontend.variable_overrides,
                                               g_frontend.override_count,
                                               var->key);
        if (override) {
            var->value = override;
            return true;
        }

        const char *raw = variables_find(g_frontend.variables,
                                          g_frontend.variable_count,
                                          var->key);
        if (!raw)
            return false;

        var->value = parse_default_value(raw);
        return var->value != NULL;
    }

    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        const struct retro_variable *vars = (const struct retro_variable *)data;
        if (!vars)
            return false;

        /* Replace the entire variable table */
        variables_free(&g_frontend.variables, &g_frontend.variable_count);
        g_frontend.variable_capacity = 0;

        bool ok = true;
        for (const struct retro_variable *v = vars; v && v->key; ++v) {
            if (!variable_add(&g_frontend.variables, &g_frontend.variable_count,
                              &g_frontend.variable_capacity, v->key, v->value)) {
                ok = false;
                break;
            }
        }
        variables_sort(g_frontend.variables, g_frontend.variable_count);
        fprintf(stderr, "Core registered %zu variables\n", g_frontend.variable_count);
        return ok;
    }

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

    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(enum retro_language *)data = RETRO_LANGUAGE_ENGLISH;
        return true;

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        return false;

    case RETRO_ENVIRONMENT_SET_VARIABLE:
        return false;

    case 43: /* SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE (base value) */
        return false;

#ifdef PURERETRO_VULKAN_ENABLED
    case 41: /* GET_HW_RENDER_INTERFACE (base value, see cmd &= ~EXPERIMENTAL above) */
        fprintf(stderr, "GET_HW_RENDER_INTERFACE: renderer=%d vk=%p\n",
                (int)g_frontend.video.renderer, (void *)g_frontend.video.vk);
        if (g_frontend.video.renderer != VIDEO_RENDERER_VULKAN || !g_frontend.video.vk)
            return false;
        {
            const struct retro_hw_render_interface **iface =
                (const struct retro_hw_render_interface **)data;
            *iface = (const struct retro_hw_render_interface *)&g_frontend.video.vk->hw_if;
        }
        fprintf(stderr, "GET_HW_RENDER_INTERFACE: returning hw_if=%p\n",
                (void *)&g_frontend.video.vk->hw_if);
        return true;
#else
    case 41: /* GET_HW_RENDER_INTERFACE (base value) */
        fprintf(stderr, "GET_HW_RENDER_INTERFACE: PURERETRO_VULKAN_ENABLED not defined\n");
        return false;
#endif

    default:
        fprintf(stderr, "Unhandled cmd: %u (0x%x)\n", cmd, cmd);
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
