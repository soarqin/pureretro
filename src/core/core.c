/*
 * PureRetro — Core management
 *
 * Dynamic loading of libretro cores and game lifecycle management.
 */

#include "core.h"

#include "core_content.h"
#include "core_internal.h"
#include "frontend.h"
#include "log.h"
#include "video.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct core_functions g_core;
struct retro_system_av_info g_av_info;

static SDL_SharedObject *g_core_handle = NULL;

/* Lifecycle guards for paired libretro calls. Tracking these explicitly
 * lets core_unload() be safely invoked from any failure path in
 * core_init() without invoking retro_unload_game() before
 * retro_load_game() ever succeeded, or retro_deinit() before
 * retro_init() ran. */
static bool g_core_initialized = false;
static bool g_game_loaded = false;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void game_info_ext_clear(void)
{
    free(g_frontend.game_info_ext_dir);
    free(g_frontend.game_info_ext_name);
    free(g_frontend.game_info_ext_ext);
    g_frontend.game_info_ext_dir = NULL;
    g_frontend.game_info_ext_name = NULL;
    g_frontend.game_info_ext_ext = NULL;
    memset(&g_frontend.game_info_ext, 0, sizeof(g_frontend.game_info_ext));
}

static bool game_info_ext_populate(const char *content_path,
                                   bool provide_data,
                                   bool persistent_data)
{
    memset(&g_frontend.game_info_ext, 0, sizeof(g_frontend.game_info_ext));

    g_frontend.game_info_ext.full_path = content_path;
    if (provide_data) {
        g_frontend.game_info_ext.data = g_frontend.rom_data;
        g_frontend.game_info_ext.size = g_frontend.rom_size;
        g_frontend.game_info_ext.persistent_data = persistent_data;
    }

    if (!content_path)
    {
        g_frontend.game_info_ext.file_in_archive = false;
        return true;
    }

    /* Find last '/' for dir, then last '.' for ext */
    const char *last_slash = strrchr(content_path, '/');
#ifdef _WIN32
    {
        const char *bs = strrchr(content_path, '\\');
        if (!last_slash || bs > last_slash)
            last_slash = bs;
    }
#endif
    const char *basename = last_slash ? last_slash + 1 : content_path;

    size_t base_dir_len = last_slash
        ? (size_t)(last_slash - content_path)
        : 0;
    if (base_dir_len > 0) {
        g_frontend.game_info_ext_dir = malloc(base_dir_len + 1);
        if (g_frontend.game_info_ext_dir) {
            memcpy(g_frontend.game_info_ext_dir, content_path, base_dir_len);
            g_frontend.game_info_ext_dir[base_dir_len] = '\0';
            g_frontend.game_info_ext.dir = g_frontend.game_info_ext_dir;
        }
    }

    size_t basename_len = strlen(basename);
    const char *dot = NULL;
    for (const char *p = basename + basename_len; p > basename; --p) {
        if (*p == '.') {
            dot = p;
            break;
        }
    }

    if (dot) {
        size_t name_len = (size_t)(dot - basename);
        g_frontend.game_info_ext_name = malloc(name_len + 1);
        if (g_frontend.game_info_ext_name) {
            memcpy(g_frontend.game_info_ext_name, basename, name_len);
            g_frontend.game_info_ext_name[name_len] = '\0';
            g_frontend.game_info_ext.name = g_frontend.game_info_ext_name;
        }

        size_t ext_len = basename_len - name_len - 1;
        g_frontend.game_info_ext_ext = malloc(ext_len + 1);
        if (g_frontend.game_info_ext_ext) {
            for (size_t i = 0; i < ext_len; ++i)
                g_frontend.game_info_ext_ext[i] =
                    (basename[name_len + 1 + i] >= 'A' &&
                     basename[name_len + 1 + i] <= 'Z')
                    ? (char)(basename[name_len + 1 + i] + 32)
                    : basename[name_len + 1 + i];
            g_frontend.game_info_ext_ext[ext_len] = '\0';
            g_frontend.game_info_ext.ext = g_frontend.game_info_ext_ext;
        }
    } else {
        g_frontend.game_info_ext_name = SDL_strdup(basename);
        if (g_frontend.game_info_ext_name)
            g_frontend.game_info_ext.name = g_frontend.game_info_ext_name;
    }

    g_frontend.game_info_ext.file_in_archive = false;
    return true;
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

    /* retro_load_game_special is part of the libretro ABI but only used
     * when --subsystem is set. Treat it as optional so we don't refuse to
     * load older or trimmed cores that may not export it. */
    {
        SDL_FunctionPointer fp = SDL_LoadFunction(g_core_handle,
                                                  "retro_load_game_special");
        if (fp)
            memcpy(&g_core.retro_load_game_special, &fp,
                   sizeof(g_core.retro_load_game_special));
    }

    /* Savestate symbols are optional: some cores (e.g. early arcade cores)
     * do not export them. Missing symbols simply disable savestate features;
     * SRAM persistence works independently via retro_get_memory_*. */
    {
        SDL_FunctionPointer fp;
        fp = SDL_LoadFunction(g_core_handle, "retro_serialize_size");
        if (fp)
            memcpy(&g_core.retro_serialize_size, &fp,
                   sizeof(g_core.retro_serialize_size));
        fp = SDL_LoadFunction(g_core_handle, "retro_serialize");
        if (fp)
            memcpy(&g_core.retro_serialize, &fp,
                   sizeof(g_core.retro_serialize));
        fp = SDL_LoadFunction(g_core_handle, "retro_unserialize");
        if (fp)
            memcpy(&g_core.retro_unserialize, &fp,
                   sizeof(g_core.retro_unserialize));
    }

    return true;
}

void core_unload(void)
{
    /* libretro lifecycle: retro_unload_game() must be called before
     * retro_deinit(). Cores like PPSSPP spawn background threads during
     * retro_load_game(); retro_unload_game() is responsible for stopping
     * them. Calling retro_deinit() first would tear down global state
     * while those threads are still running, leading to use-after-free
     * or null-pointer crashes (e.g. in System_AudioPushSamples).
     *
     * Both calls are also guarded by g_game_loaded / g_core_initialized
     * so a failed core_init() that bails out partway does not invoke
     * either function with an unpaired or out-of-order lifecycle. */
    if (g_frontend.video.hw_render_enabled)
        video_context_destroy();

    if (g_game_loaded && g_core.retro_unload_game)
        g_core.retro_unload_game();
    g_game_loaded = false;

    /* Hardware backends may own callbacks into the core (notably Vulkan's
     * destroy_device from the negotiation interface). Destroy them before
     * retro_deinit()/SDL_UnloadObject so those callbacks and core-side GPU
     * allocators are still valid. frontend_shutdown() calls video_shutdown()
     * again later; it is a no-op once the backend/window pointers are NULL. */
    if (g_frontend.video.hw_render_enabled && g_frontend.video.backend_ctx)
        video_shutdown();

    if (g_core_initialized && g_core.retro_deinit)
        g_core.retro_deinit();
    g_core_initialized = false;

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

    core_controller_ports_clear(&g_frontend);
    core_subsystem_info_clear(&g_frontend);
    core_memory_maps_clear(&g_frontend);
    core_content_overrides_clear(&g_frontend);
    game_info_ext_clear();
    memset(&g_frontend.disk_control, 0, sizeof(g_frontend.disk_control));
    g_frontend.has_disk_control = false;

    memset(&g_core, 0, sizeof(g_core));
}

static void core_release_rom_data(void)
{
    free(g_frontend.rom_data);
    g_frontend.rom_data = NULL;
    g_frontend.rom_size = 0;
}

static const struct subsystem_storage *core_find_requested_subsystem(void)
{
    if (!g_frontend.subsystem_ident)
        return NULL;

    for (unsigned i = 0; i < g_frontend.subsystem_info_count; ++i) {
        if (g_frontend.subsystem_info[i].ident &&
            strcmp(g_frontend.subsystem_info[i].ident,
                   g_frontend.subsystem_ident) == 0) {
            return &g_frontend.subsystem_info[i];
        }
    }

    return NULL;
}

static bool core_load_game(const char *content_path)
{
    if (content_path) {
        struct retro_game_info game;
        memset(&game, 0, sizeof(game));
        game.path = content_path;

        const struct subsystem_storage *subsystem =
            core_find_requested_subsystem();
        if (g_frontend.subsystem_ident && !subsystem) {
            LOG_ERROR("--subsystem '%s' is not declared by the core",
                      g_frontend.subsystem_ident);
            return false;
        }

        struct core_content_load_policy policy;
        policy.need_fullpath = g_frontend.core_need_fullpath;
        policy.persistent_data = true;

        if (subsystem && subsystem->num_roms > 0)
            policy.need_fullpath = subsystem->roms[0].need_fullpath;
        core_content_apply_overrides(content_path,
                                     g_frontend.content_overrides,
                                     g_frontend.content_override_count,
                                     &policy);

        if (!policy.need_fullpath) {
            if (!load_file(content_path, &g_frontend.rom_data,
                           &g_frontend.rom_size)) {
                LOG_ERROR("Failed to load content file: %s", content_path);
                return false;
            }
            game.data = g_frontend.rom_data;
            game.size = g_frontend.rom_size;
            /* PureRetro keeps memory-backed content alive until shutdown,
             * so the persistent-data promise is always true in practice. */
            policy.persistent_data = true;
        }

        /* Populate extended game info for any GET_GAME_INFO_EXT calls
         * the core may make during retro_load_game. */
        game_info_ext_populate(content_path, !policy.need_fullpath,
                               policy.persistent_data);

        bool loaded;
        if (g_frontend.subsystem_ident) {
            if (!g_core.retro_load_game_special) {
                LOG_ERROR("Core does not export retro_load_game_special; "
                          "cannot use --subsystem");
                core_release_rom_data();
                return false;
            }
            if (subsystem->num_roms != 1) {
                LOG_WARN("--subsystem '%s' expects %u ROMs but only 1 content "
                         "path was provided; behaviour may be undefined",
                         g_frontend.subsystem_ident, subsystem->num_roms);
            }
            LOG_INFO("Calling retro_load_game_special(id=%u, '%s')...",
                     subsystem->id, g_frontend.subsystem_ident);
            loaded = g_core.retro_load_game_special(subsystem->id, &game, 1);
        } else {
            LOG_INFO("Calling retro_load_game...");
            loaded = g_core.retro_load_game(&game);
        }

        if (!loaded) {
            LOG_ERROR("Game load failed (core rejected the content)");
            /* Release the ROM buffer immediately so a caller that bails out
             * without invoking core_unload does not leak it. core_unload()
             * also handles this case, so a double-free is avoided by
             * nulling the pointers below. */
            core_release_rom_data();
            return false;
        }
        g_game_loaded = true;
        LOG_INFO("Game load succeeded");
    } else {
        LOG_INFO("Calling retro_load_game(NULL)...");
        if (!g_core.retro_load_game(NULL)) {
            LOG_ERROR("retro_load_game(NULL) failed");
            return false;
        }
        g_game_loaded = true;
        LOG_INFO("retro_load_game(NULL) succeeded");
    }

    return true;
}

static bool core_init_hw_render(void)
{
    /* For HW cores the window was created during SET_HW_RENDER before
     * AV info was available. Resize it now that we know the real resolution. */
    if (g_frontend.video.hw_render_enabled &&
        g_av_info.geometry.max_width > 0 &&
        g_av_info.geometry.max_height > 0) {
        video_resize(g_av_info.geometry.max_width,
                     g_av_info.geometry.max_height);
    }

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
    g_core_initialized = true;

    g_core.retro_get_system_info(&info);
    g_frontend.core_need_fullpath = info.need_fullpath;
    g_frontend.core_block_extract = info.block_extract;
    LOG_INFO("Core: %s (v%s)", info.library_name, info.library_version);

    if (!core_load_game(content_path))
        return false;

    g_core.retro_get_system_av_info(&g_av_info);
    LOG_INFO("AV: %ux%u @ %.2f Hz, audio: %.2f Hz",
             g_av_info.geometry.base_width,
             g_av_info.geometry.base_height,
             g_av_info.timing.fps,
             g_av_info.timing.sample_rate);

    if (!core_init_hw_render())
        return false;

    return true;
}

void core_run(void)
{
    g_core.retro_run();
}

/* ------------------------------------------------------------------ */
