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
#include "core_variables_parse.h"
#include "frontend.h"
#include "video.h"
#include "audio.h"
#include "input.h"
#include "vfs.h"
#include "log.h"

struct core_functions g_core;
struct retro_system_av_info g_av_info;

static SDL_SharedObject *g_core_handle = NULL;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void controller_ports_clear(void)
{
    for (unsigned p = 0; p < g_frontend.controller_port_count; ++p) {
        struct controller_port_info *port = &g_frontend.controller_ports[p];
        for (unsigned i = 0; i < port->num_types; ++i)
            free((char *)port->types[i].desc);
        free(port->types);
        port->types = NULL;
        port->num_types = 0;
    }
    g_frontend.controller_port_count = 0;
}

/* Apply --disk-index after the core has registered a disk control interface.
 * The libretro contract permits set_image_index() at any time after init. */
static void disk_control_apply_initial_index(void)
{
    if (!g_frontend.has_disk_control)
        return;
    if (g_frontend.initial_disk_index < 0)
        return;

    const struct retro_disk_control_ext_callback *d = &g_frontend.disk_control;
    if (!d->get_num_images || !d->set_eject_state || !d->set_image_index) {
        LOG_WARN("--disk-index ignored: core missing required disk callbacks");
        return;
    }

    unsigned num = d->get_num_images();
    unsigned idx = (unsigned)g_frontend.initial_disk_index;
    if (idx >= num) {
        LOG_WARN("--disk-index %u out of range (core reports %u images)",
                 idx, num);
        return;
    }

    /* Standard eject-set-insert dance, mirroring how real frontends switch
     * disks. Failures are warned but non-fatal: the core may simply have
     * the requested disk already loaded. */
    if (!d->set_eject_state(true))
        LOG_WARN("disk set_eject_state(true) failed");
    if (!d->set_image_index(idx))
        LOG_WARN("disk set_image_index(%u) failed", idx);
    if (!d->set_eject_state(false))
        LOG_WARN("disk set_eject_state(false) failed");

    LOG_INFO("Disk index set to %u (of %u)", idx, num);
}

static bool load_file(const char *path, void **out_data, size_t *out_size)
{
    FILE *fp;
    long size;
    void *data;

    fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("Failed to open file: %s", path);
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }

    size = ftell(fp);
    if (size <= 0) {
        if (size < 0)
            LOG_ERROR("ftell failed for: %s", path);
        else
            LOG_ERROR("Refusing to load empty file: %s", path);
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

static void RETRO_CALLCONV core_log_bridge(enum retro_log_level level,
                                           const char *fmt, ...)
{
    enum log_level lvl;
    switch (level) {
    case RETRO_LOG_DEBUG: lvl = LOG_LEVEL_DEBUG; break;
    case RETRO_LOG_INFO:  lvl = LOG_LEVEL_INFO;  break;
    case RETRO_LOG_WARN:  lvl = LOG_LEVEL_WARN;  break;
    case RETRO_LOG_ERROR: lvl = LOG_LEVEL_ERROR; break;
    default:              lvl = LOG_LEVEL_INFO;  break;
    }
    va_list va;
    va_start(va, fmt);
    log_emit_v(lvl, "CORE", NULL, 0, fmt, va);
    va_end(va);
}

bool core_load(const char *path)
{
    g_core_handle = SDL_LoadObject(path);
    if (!g_core_handle) {
        LOG_ERROR("SDL_LoadObject failed: %s", SDL_GetError());
        return false;
    }

#define LOAD_SYM(sym)                                                     \
    do {                                                                  \
        SDL_FunctionPointer _fp = SDL_LoadFunction(g_core_handle, #sym); \
        if (!_fp) {                                                       \
            LOG_ERROR("Failed to load symbol: %s", #sym);                \
            SDL_UnloadObject(g_core_handle);                              \
            g_core_handle = NULL;                                         \
            memset(&g_core, 0, sizeof(g_core));                           \
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
    /* libretro lifecycle: retro_unload_game() must be called before
     * retro_deinit(). Cores like PPSSPP spawn background threads during
     * retro_load_game(); retro_unload_game() is responsible for stopping
     * them. Calling retro_deinit() first would tear down global state
     * while those threads are still running, leading to use-after-free
     * or null-pointer crashes (e.g. in System_AudioPushSamples). */
    if (g_core.retro_unload_game)
        g_core.retro_unload_game();

    if (g_core.retro_deinit)
        g_core.retro_deinit();

    if (g_core_handle) {
        SDL_UnloadObject(g_core_handle);
        g_core_handle = NULL;
    }

    if (g_frontend.rom_data) {
        free(g_frontend.rom_data);
        g_frontend.rom_data = NULL;
        g_frontend.rom_size = 0;
    }

    core_options_table_clear(&g_frontend.core_options);
    variable_table_clear(&g_frontend.disk_overrides);
    variable_table_clear(&g_frontend.cli_overrides);

    controller_ports_clear();
    memset(&g_frontend.disk_control, 0, sizeof(g_frontend.disk_control));
    g_frontend.has_disk_control = false;

    memset(&g_core, 0, sizeof(g_core));
}

bool core_init(const char *content_path)
{
    struct retro_system_info info;

    LOG_INFO("Initializing core (content: %s)",
             content_path ? content_path : "<none>");

    if (g_core.retro_api_version() != RETRO_API_VERSION) {
        LOG_ERROR("Core API version mismatch");
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
    LOG_INFO("Core: %s (v%s)", info.library_name, info.library_version);

    if (content_path) {
        struct retro_game_info game;
        memset(&game, 0, sizeof(game));
        game.path = content_path;

        if (!load_file(content_path, &g_frontend.rom_data, &g_frontend.rom_size)) {
            LOG_ERROR("Failed to load content file: %s", content_path);
            return false;
        }

        game.data = g_frontend.rom_data;
        game.size = g_frontend.rom_size;

        LOG_INFO("Calling retro_load_game...");
        if (!g_core.retro_load_game(&game)) {
            LOG_ERROR("retro_load_game failed (core rejected the content)");
            /* Release the ROM buffer immediately so a caller that bails out
             * without invoking core_unload does not leak it. core_unload()
             * also handles this case, so a double-free is avoided by
             * nulling the pointers below. */
            free(g_frontend.rom_data);
            g_frontend.rom_data = NULL;
            g_frontend.rom_size = 0;
            return false;
        }
        LOG_INFO("retro_load_game succeeded");
    } else {
        LOG_INFO("Calling retro_load_game(NULL)...");
        if (!g_core.retro_load_game(NULL)) {
            LOG_ERROR("retro_load_game(NULL) failed");
            return false;
        }
        LOG_INFO("retro_load_game(NULL) succeeded");
    }

    g_core.retro_get_system_av_info(&g_av_info);
    LOG_INFO("AV: %ux%u @ %.2f Hz, audio: %.2f Hz",
             g_av_info.geometry.base_width,
             g_av_info.geometry.base_height,
             g_av_info.timing.fps,
             g_av_info.timing.sample_rate);

    /* For HW cores the window was created during SET_HW_RENDER before
     * AV info was available. Resize it now that we know the real resolution. */
    if (g_frontend.video.hw_render_enabled && g_frontend.video.window) {
        video_resize_window_to_geometry();
    }

    /* Notify the core that the HW context is ready. This must happen after
     * retro_load_game returns so the core has finished its own setup. */
    if (g_frontend.video.hw_render_enabled && g_frontend.video.hw.context_reset) {
        LOG_INFO("Calling context_reset after retro_load_game...");
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

/* Helper: returns true if data is non-NULL; otherwise logs and returns false. */
static bool require_data(unsigned cmd, const void *data)
{
    if (!data) {
        LOG_WARN("core_environment: NULL data for cmd %u (0x%x)", cmd, cmd);
        return false;
    }
    return true;
}

static bool add_options_from_v1_defs(const struct retro_core_option_definition *defs)
{
    bool ok = true;
    for (const struct retro_core_option_definition *def = defs; def && def->key; ++def) {
        const char *values[RETRO_NUM_CORE_OPTION_VALUES_MAX + 1];
        size_t val_count = 0;
        for (size_t i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++i) {
            if (!def->values[i].value)
                break;
            values[val_count++] = def->values[i].value;
        }
        values[val_count] = NULL;
        if (!core_options_table_add(&g_frontend.core_options,
                                    def->key, def->desc, def->info,
                                    values, def->default_value)) {
            ok = false;
            break;
        }
    }
    return ok;
}

static bool add_options_from_v2_defs(const struct retro_core_option_v2_definition *defs)
{
    bool ok = true;
    for (const struct retro_core_option_v2_definition *def = defs; def && def->key; ++def) {
        const char *values[RETRO_NUM_CORE_OPTION_VALUES_MAX + 1];
        size_t val_count = 0;
        for (size_t i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++i) {
            if (!def->values[i].value)
                break;
            values[val_count++] = def->values[i].value;
        }
        values[val_count] = NULL;
        if (!core_options_table_add(&g_frontend.core_options,
                                    def->key, def->desc, def->info,
                                    values, def->default_value)) {
            ok = false;
            break;
        }
    }
    return ok;
}

static size_t seed_disk_overrides_from_defaults(void)
{
    size_t seeded = 0;
    size_t total = core_options_table_count(&g_frontend.core_options);
    for (size_t i = 0; i < total; ++i) {
        const struct core_option *opt =
            core_options_table_at(&g_frontend.core_options, i);
        if (variable_table_get(&g_frontend.disk_overrides, opt->key))
            continue;
        if (variable_table_set(&g_frontend.disk_overrides, opt->key, opt->default_value))
            seeded++;
    }
    return seeded;
}

bool RETRO_CALLCONV core_environment(unsigned cmd, void *data)
{
    /* Some cores (e.g. Beetle PSX HW) call callbacks with the experimental
     * flag OR'd in. Since we support all the experimental features the
     * project targets, strip the flag and process the base callback. The
     * raw value is kept for diagnostics. */
    const unsigned raw_cmd = cmd;
    cmd &= ~RETRO_ENVIRONMENT_EXPERIMENTAL;

    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_ROTATION:
        /* TODO: Support screen rotation */
        return false;

    case RETRO_ENVIRONMENT_GET_OVERSCAN:
        /* Default: no overscan */
        if (!require_data(cmd, data))
            return false;
        *(bool *)data = false;
        return true;

    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        if (!require_data(cmd, data))
            return false;
        *(bool *)data = true;
        return true;

    case RETRO_ENVIRONMENT_SET_MESSAGE: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_message *msg = (const struct retro_message *)data;
        LOG_INFO("[CORE] %s", msg->msg);
        return true;
    }

    case RETRO_ENVIRONMENT_SHUTDOWN:
        g_frontend.running = false;
        return true;

    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: {
        if (!require_data(cmd, data))
            return false;
        unsigned level = *(const unsigned *)data;
        LOG_INFO("Core performance level hint: %u", level);
        return true;
    }

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (!require_data(cmd, data))
            return false;
        *(const char **)data = g_frontend.system_directory;
        LOG_DEBUG("Core queried system directory: %s",
                  g_frontend.system_directory ? g_frontend.system_directory : "(null)");
        return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        if (!require_data(cmd, data))
            return false;
        enum retro_pixel_format fmt = *(const enum retro_pixel_format *)data;
        const char *fmt_name = "unknown";
        switch (fmt) {
        case RETRO_PIXEL_FORMAT_0RGB1555: fmt_name = "0RGB1555"; break;
        case RETRO_PIXEL_FORMAT_XRGB8888: fmt_name = "XRGB8888"; break;
        case RETRO_PIXEL_FORMAT_RGB565:   fmt_name = "RGB565";   break;
        case RETRO_PIXEL_FORMAT_UNKNOWN:  fmt_name = "UNKNOWN";  break;
        }
        LOG_INFO("Core requested pixel format: %s (%d)", fmt_name, (int)fmt);
        /* Accept all formats. The software renderer will handle whatever the
         * core sends. Returning false here causes cores like Beetle PSX HW
         * to skip SET_HW_RENDER entirely and fall back to software. */
        g_frontend.video.pixel_format = fmt;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return false;

    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_keyboard_callback *cb =
            (const struct retro_keyboard_callback *)data;
        g_frontend.keyboard_callback = *cb;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_disk_control_callback *legacy =
            (const struct retro_disk_control_callback *)data;
        /* The legacy struct's first 7 fields are layout-identical to the
         * ext struct. memcpy those and leave the ext-only fields NULL
         * (memset guarantees set_initial_image/get_image_path/get_image_label). */
        memset(&g_frontend.disk_control, 0,
               sizeof(g_frontend.disk_control));
        memcpy(&g_frontend.disk_control, legacy,
               sizeof(struct retro_disk_control_callback));
        g_frontend.has_disk_control = true;
        LOG_INFO("Core registered legacy disk control interface");
        disk_control_apply_initial_index();
        return true;
    }

    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        if (!require_data(cmd, data))
            return false;
        return video_set_hw_render((struct retro_hw_render_callback *)data);

    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: {
        if (!require_data(cmd, data))
            return false;
        int *preferred = (int *)data;
        bool result;
        switch (g_frontend.preferred_renderer) {
        case VIDEO_RENDERER_VULKAN: *preferred = RETRO_HW_CONTEXT_VULKAN;     result = true; break;
        case VIDEO_RENDERER_OPENGL: *preferred = RETRO_HW_CONTEXT_OPENGL_CORE; result = true; break;
        case VIDEO_RENDERER_SW:     *preferred = RETRO_HW_CONTEXT_NONE;       result = true; break;
        case VIDEO_RENDERER_NONE:
        default:                    result = false; break;
        }
        LOG_DEBUG("Core queried preferred HW render: %s (context=%d)",
                  result ? "yes" : "no (no preference)", *preferred);
        return result;
    }

    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        if (!require_data(cmd, data))
            return false;
        struct retro_variable *var = (struct retro_variable *)data;
        if (!var->key)
            return false;

        const char *override = variable_table_get(&g_frontend.cli_overrides,
                                                  var->key);
        if (!override)
            override = variable_table_get(&g_frontend.disk_overrides, var->key);
        if (override) {
            var->value = override;
            return true;
        }

        const struct core_option *opt =
            core_options_table_get(&g_frontend.core_options, var->key);
        if (!opt)
            return false;

        var->value = opt->current_value ? opt->current_value : opt->default_value;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        const struct retro_variable *vars = (const struct retro_variable *)data;
        if (!vars)
            return false;

        core_options_table_clear(&g_frontend.core_options);

        bool ok = true;
        for (const struct retro_variable *v = vars; v && v->key; ++v) {
            char desc[256];
            core_var_parse_description(v->value, desc, sizeof(desc));

            char def[256];
            core_var_parse_default(v->value, def, sizeof(def));

            const char *choices = core_var_choices_begin(v->value);
            const char *values[64];
            size_t val_count = 0;

            if (choices) {
                const char *p = choices;
                while (*p) {
                    if (val_count >= 63) {
                        ok = false;
                        break;
                    }
                    const char *end = p;
                    while (*end && *end != '|')
                        ++end;
                    size_t len = (size_t)(end - p);
                    char *choice = malloc(len + 1);
                    if (!choice) {
                        ok = false;
                        break;
                    }
                    memcpy(choice, p, len);
                    choice[len] = '\0';
                    values[val_count++] = choice;
                    if (*end == '|')
                        ++end;
                    p = end;
                }
            }

            if (!ok) {
                for (size_t i = 0; i < val_count; ++i)
                    free((char *)values[i]);
                break;
            }
            values[val_count] = NULL;

            if (!core_options_table_add(&g_frontend.core_options,
                                        v->key, desc, NULL, values, def)) {
                ok = false;
            }

            for (size_t i = 0; i < val_count; ++i)
                free((char *)values[i]);

            if (!ok)
                break;
        }

        if (!ok)
            core_options_table_clear(&g_frontend.core_options);

        size_t seeded = 0;
        size_t total = core_options_table_count(&g_frontend.core_options);
        for (size_t i = 0; i < total; ++i) {
            const struct core_option *opt =
                core_options_table_at(&g_frontend.core_options, i);
            if (variable_table_get(&g_frontend.disk_overrides, opt->key))
                continue;
            if (variable_table_set(&g_frontend.disk_overrides,
                                   opt->key, opt->default_value))
                seeded++;
        }

        LOG_INFO("Core registered %zu variables (%zu seeded from defaults)",
                 total, seeded);
        return ok;
    }

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (!require_data(cmd, data))
            return false;
        *(bool *)data = false;
        return true;

    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;

    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
        if (!require_data(cmd, data))
            return false;
        *(const char **)data = g_frontend.core_path;
        return true;

    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
        return false;

    case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
        return false;

    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        if (!require_data(cmd, data))
            return false;
        *(bool *)data = true;
        return true;

    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
        if (!require_data(cmd, data))
            return false;
        *(uint64_t *)data = (1 << RETRO_DEVICE_JOYPAD);
        return true;

    case RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        if (!require_data(cmd, data))
            return false;
        struct retro_log_callback *cb = (struct retro_log_callback *)data;
        cb->log = core_log_bridge;
        return true;
    }

    case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE:
        return false;

    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        if (!require_data(cmd, data))
            return false;
        *(const char **)data = g_frontend.core_assets_directory;
        return true;

    case RETRO_ENVIRONMENT_GET_PLAYLIST_DIRECTORY:
        if (!require_data(cmd, data))
            return false;
        *(const char **)data = g_frontend.playlist_directory;
        return true;

    case RETRO_ENVIRONMENT_GET_FILE_BROWSER_START_DIRECTORY:
        if (!require_data(cmd, data))
            return false;
        *(const char **)data = g_frontend.file_browser_directory;
        return true;

    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!require_data(cmd, data))
            return false;
        const char *dir = g_frontend.save_directory
                          ? g_frontend.save_directory
                          : g_frontend.system_directory;
        *(const char **)data = dir;
        LOG_DEBUG("Core queried save directory: %s", dir ? dir : "(null)");
        return true;

    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_game_geometry *geo =
            (const struct retro_game_geometry *)data;
        video_update_geometry(geo->base_width, geo->base_height,
                              geo->max_width, geo->max_height,
                              geo->aspect_ratio);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_system_av_info *av =
            (const struct retro_system_av_info *)data;
        g_av_info = *av;

        if (g_frontend.video.hw_render_enabled) {
            video_resize(av->geometry.max_width, av->geometry.max_height);
        }
        return true;
    }

    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        if (!require_data(cmd, data))
            return false;
        *(enum retro_language *)data = g_frontend.language;
        return true;

    case RETRO_ENVIRONMENT_GET_USERNAME:
        if (!require_data(cmd, data))
            return false;
        *(const char **)data = g_frontend.username;
        return g_frontend.username != NULL;

    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_controller_info *info =
            (const struct retro_controller_info *)data;

        controller_ports_clear();

        unsigned port = 0;
        for (const struct retro_controller_info *p = info;
             p && p->types && p->num_types > 0;
             ++p, ++port) {
            if (port >= FRONTEND_MAX_PORTS) {
                LOG_WARN("SET_CONTROLLER_INFO: dropping ports beyond %u",
                         (unsigned)FRONTEND_MAX_PORTS);
                break;
            }

            struct controller_port_info *slot = &g_frontend.controller_ports[port];
            slot->types = calloc(p->num_types, sizeof(*slot->types));
            if (!slot->types) {
                controller_ports_clear();
                return false;
            }
            slot->num_types = p->num_types;

            for (unsigned i = 0; i < p->num_types; ++i) {
                slot->types[i].id = p->types[i].id;
                slot->types[i].desc = p->types[i].desc
                    ? SDL_strdup(p->types[i].desc) : NULL;
            }
        }
        g_frontend.controller_port_count = port;

        LOG_INFO("Core registered controller info for %u port(s):", port);
        for (unsigned i = 0; i < port; ++i) {
            const struct controller_port_info *slot = &g_frontend.controller_ports[i];
            LOG_INFO("  port %u: %u device type(s)", i, slot->num_types);
            for (unsigned t = 0; t < slot->num_types; ++t) {
                LOG_INFO("    [%u] id=%u desc=%s",
                         t, slot->types[t].id,
                         slot->types[t].desc ? slot->types[t].desc : "(null)");
            }
        }
        return true;
    }

    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        if (!require_data(cmd, data))
            return false;
        *(unsigned *)data = 1;
        return true;

    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_disk_control_ext_callback *cb =
            (const struct retro_disk_control_ext_callback *)data;
        g_frontend.disk_control = *cb;
        g_frontend.has_disk_control = true;
        LOG_INFO("Core registered disk control ext interface (num_images=%u)",
                 cb->get_num_images ? cb->get_num_images() : 0);
        disk_control_apply_initial_index();
        return true;
    }

    case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER: {
        if (!require_data(cmd, data))
            return false;
        struct retro_framebuffer *fb = (struct retro_framebuffer *)data;
        if (fb->width == 0 || fb->height == 0)
            return false;

        /* Only honour the request when the core wants to write the buffer.
         * Read-only access would require a different mapping (we never
         * read back from the texture), so decline cleanly in that case. */
        if (!(fb->access_flags & RETRO_MEMORY_ACCESS_WRITE))
            return false;

        void *pixels = NULL;
        size_t pitch = 0;
        if (!video_get_software_framebuffer(fb->width, fb->height,
                                            g_frontend.video.pixel_format,
                                            &pixels, &pitch))
            return false;

        fb->data = pixels;
        fb->pitch = pitch;
        fb->format = g_frontend.video.pixel_format;
        fb->memory_flags = 0;
        return true;
    }

    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        if (!require_data(cmd, data))
            return false;
        /* bit0: audio enabled, bit1: video enabled, bit2: fast-forwarding,
         * bit3: hard disable audio. We never hard-disable, never report
         * fast-forwarding (not implemented yet), and video is always on. */
        *(int *)data = (g_frontend.no_audio ? 0 : 1) | (1 << 1);
        return true;

    case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
        if (!require_data(cmd, data))
            return false;
        /* The frontend currently maps a single keyboard to port 0 only.
         * Cores can use this to skip polling ports 1..N. */
        *(unsigned *)data = 1;
        return true;

    case RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE & ~RETRO_ENVIRONMENT_EXPERIMENTAL: {
        if (!require_data(cmd, data))
            return false;
        float rate = (g_av_info.timing.sample_rate > 0.0)
                     ? (float)g_av_info.timing.sample_rate
                     : (float)FRONTEND_AUDIO_SAMPLE_RATE;
        *(float *)data = rate;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK: {
        /* data may be NULL to clear the callback; the environment call
         * itself is still considered supported. */
        const struct retro_audio_buffer_status_callback *cb =
            (const struct retro_audio_buffer_status_callback *)data;
        audio_set_buffer_status_callback(cb ? cb->callback : NULL);
        LOG_INFO("Core %s audio buffer status callback",
                 cb && cb->callback ? "registered" : "unregistered");
        return true;
    }

    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY: {
        unsigned ms = (data) ? *(const unsigned *)data : 0;
        audio_set_minimum_latency(ms);
        LOG_INFO("Core requested minimum audio latency: %u ms", ms);
        return true;
    }

    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        if (!require_data(cmd, data))
            return false;
        *(bool *)data = false;
        return true;

    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE: {
        if (!require_data(cmd, data))
            return false;
        float rate = 60.0f;
        SDL_Window *win = g_frontend.video.window;
        if (win) {
            SDL_DisplayID disp = SDL_GetDisplayForWindow(win);
            if (disp) {
                const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(disp);
                if (mode && mode->refresh_rate > 0.0f)
                    rate = mode->refresh_rate;
            }
        }
        *(float *)data = rate;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        return false;

    case RETRO_ENVIRONMENT_SET_VARIABLE: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_variable *var = (const struct retro_variable *)data;
        if (!var->key || !var->value)
            return false;
        if (!core_options_table_set_value(&g_frontend.core_options,
                                          var->key, var->value))
            return false;
        if (!variable_table_set(&g_frontend.disk_overrides, var->key, var->value))
            return false;
        return true;
    }

    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE & ~RETRO_ENVIRONMENT_EXPERIMENTAL:
        if (!require_data(cmd, data))
            return false;
        return video_negotiate_hw_context(
            (const struct retro_hw_render_context_negotiation_interface *)data);

    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE & ~RETRO_ENVIRONMENT_EXPERIMENTAL: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_hw_render_interface **iface =
            (const struct retro_hw_render_interface **)data;
        if (!video_get_hw_render_interface(iface)) {
            LOG_WARN("GET_HW_RENDER_INTERFACE: no interface for active backend");
            return false;
        }
        LOG_INFO("GET_HW_RENDER_INTERFACE: returning %p",
                 (const void *)*iface);
        return true;
    }

    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: {
        if (!require_data(cmd, data))
            return false;
        struct retro_vfs_interface_info *info =
            (struct retro_vfs_interface_info *)data;
        if (info->required_interface_version > 1)
            return false;
        info->iface = vfs_get_interface();
        info->required_interface_version = 1;
        return true;
    }

    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (!require_data(cmd, data))
            return false;
        *(unsigned *)data = 2;
        return true;

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_core_option_definition *defs =
            (const struct retro_core_option_definition *)data;
        core_options_table_clear(&g_frontend.core_options);
        bool ok = add_options_from_v1_defs(defs);
        if (!ok) {
            core_options_table_clear(&g_frontend.core_options);
            return false;
        }
        size_t seeded = seed_disk_overrides_from_defaults();
        size_t total = core_options_table_count(&g_frontend.core_options);
        LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
                 total, seeded);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_core_options_intl *opts =
            (const struct retro_core_options_intl *)data;
        core_options_table_clear(&g_frontend.core_options);
        bool ok = true;
        if (opts->us)
            ok = add_options_from_v1_defs(opts->us);
        if (!ok) {
            core_options_table_clear(&g_frontend.core_options);
            return false;
        }
        size_t seeded = seed_disk_overrides_from_defaults();
        size_t total = core_options_table_count(&g_frontend.core_options);
        LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
                 total, seeded);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_core_options_v2 *opts =
            (const struct retro_core_options_v2 *)data;
        core_options_table_clear(&g_frontend.core_options);
        bool ok = add_options_from_v2_defs(opts->definitions);
        if (!ok) {
            core_options_table_clear(&g_frontend.core_options);
            return false;
        }
        size_t seeded = seed_disk_overrides_from_defaults();
        size_t total = core_options_table_count(&g_frontend.core_options);
        LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
                 total, seeded);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_core_options_v2_intl *opts =
            (const struct retro_core_options_v2_intl *)data;
        core_options_table_clear(&g_frontend.core_options);
        bool ok = true;
        if (opts->us)
            ok = add_options_from_v2_defs(opts->us->definitions);
        if (!ok) {
            core_options_table_clear(&g_frontend.core_options);
            return false;
        }
        size_t seeded = seed_disk_overrides_from_defaults();
        size_t total = core_options_table_count(&g_frontend.core_options);
        LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
                 total, seeded);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_core_options_update_display_callback *cb =
            (const struct retro_core_options_update_display_callback *)data;
        g_frontend.core_options_update_display_callback = cb->callback;
        return true;
    }

    case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT
         & ~RETRO_ENVIRONMENT_EXPERIMENTAL: {
        /* Cores poll this to discover which negotiation interface versions
         * the frontend understands. We currently support the Vulkan
         * negotiation interface (handled in video_negotiate_hw_context),
         * which is the only enum value defined upstream. Return the highest
         * interface_version the frontend recognises; other API types get 0. */
        if (!require_data(cmd, data))
            return false;
        struct retro_hw_render_context_negotiation_interface *iface =
            (struct retro_hw_render_context_negotiation_interface *)data;
        if (iface->interface_type ==
            RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN) {
            iface->interface_version = 2;
        } else {
            iface->interface_version = 0;
        }
        return true;
    }

    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE & ~RETRO_ENVIRONMENT_EXPERIMENTAL: {
        if (!require_data(cmd, data))
            return false;
        struct retro_throttle_state *ts = (struct retro_throttle_state *)data;
        ts->mode = RETRO_THROTTLE_NONE;
        ts->rate = (g_av_info.timing.fps > 0.0)
                   ? (float)g_av_info.timing.fps : 0.0f;
        return true;
    }

    case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT & ~RETRO_ENVIRONMENT_EXPERIMENTAL:
        if (!require_data(cmd, data))
            return false;
        *(int *)data = RETRO_SAVESTATE_CONTEXT_NORMAL;
        return true;

    case RETRO_ENVIRONMENT_GET_JIT_CAPABLE:
        if (!require_data(cmd, data))
            return false;
        /* All three desktop targets (Linux/macOS/Windows) allow JIT. The
         * libretro contract is "false only on locked-down platforms like
         * iOS / non-jailbroken consoles", which we never run on. */
        *(bool *)data = true;
        return true;

    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        if (!require_data(cmd, data))
            return false;
        *(unsigned *)data = 1;
        return true;

    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
        if (!require_data(cmd, data))
            return false;
        const struct retro_message_ext *msg =
            (const struct retro_message_ext *)data;
        if (!msg->msg)
            return false;
        /* Route via the logger. TARGET_LOG uses the message's own level;
         * TARGET_OSD / TARGET_ALL still go to the log since we have no GUI,
         * but at INFO (so they remain visible without spamming DEBUG). */
        enum log_level lvl;
        if (msg->target == RETRO_MESSAGE_TARGET_LOG) {
            switch (msg->level) {
            case RETRO_LOG_DEBUG: lvl = LOG_LEVEL_DEBUG; break;
            case RETRO_LOG_INFO:  lvl = LOG_LEVEL_INFO;  break;
            case RETRO_LOG_WARN:  lvl = LOG_LEVEL_WARN;  break;
            case RETRO_LOG_ERROR: lvl = LOG_LEVEL_ERROR; break;
            default:              lvl = LOG_LEVEL_INFO;  break;
            }
        } else {
            lvl = LOG_LEVEL_INFO;
        }
        log_emit(lvl, "CORE", NULL, 0, "%s", msg->msg);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS: {
        /* Note: SET_HW_SHARED_CONTEXT is (44 | EXPERIMENTAL), which collapses
         * to the same case-value after we strip the experimental flag. The
         * two are disambiguated via the raw command. SHARED_CONTEXT is
         * data-less, while SERIALIZATION_QUIRKS expects a uint64_t*. */
        if (raw_cmd == RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT) {
            g_frontend.video.hw_shared_context_requested = true;
            LOG_INFO("Core requested shared HW context "
                     "(honored at next GL init)");
            return true;
        }
        if (!require_data(cmd, data))
            return false;
        uint64_t *quirks = (uint64_t *)data;
        /* The frontend does not require the core to drop any quirks, so we
         * leave whatever the core wrote in place. Log the declared bits so
         * savestate misbehavior is easier to attribute. */
        LOG_INFO("Core serialization quirks: 0x%llx",
                 (unsigned long long)*quirks);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS
         & ~RETRO_ENVIRONMENT_EXPERIMENTAL: {
        if (!require_data(cmd, data))
            return false;
        g_frontend.core_supports_achievements = *(const bool *)data;
        LOG_INFO("Core declares achievement support: %s",
                 g_frontend.core_supports_achievements ? "yes" : "no");
        return true;
    }

    default:
        LOG_DEBUG("Unhandled cmd: %u (0x%x, raw 0x%x)", cmd, cmd, raw_cmd);
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
