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

static void subsystem_info_clear(void)
{
    for (unsigned i = 0; i < g_frontend.subsystem_info_count; ++i) {
        struct subsystem_storage *ss = &g_frontend.subsystem_info[i];
        free(ss->desc);
        free(ss->ident);
        for (unsigned j = 0; j < ss->num_roms; ++j) {
            struct subsystem_rom_storage *rs = &ss->roms[j];
            free(rs->desc);
            free(rs->valid_extensions);
            for (unsigned k = 0; k < rs->num_memory; ++k)
                free(rs->memory_extensions[k]);
            free(rs->memory_extensions);
            free(rs->memory);
        }
        free(ss->roms);
    }
    free(g_frontend.subsystem_info);
    g_frontend.subsystem_info = NULL;
    g_frontend.subsystem_info_count = 0;
}

static void memory_maps_clear(void)
{
    for (unsigned i = 0; i < g_frontend.memory_descriptor_count; ++i)
        free(g_frontend.memory_addrspace_strings[i]);
    free(g_frontend.memory_addrspace_strings);
    free(g_frontend.memory_descriptors);
    g_frontend.memory_descriptors = NULL;
    g_frontend.memory_addrspace_strings = NULL;
    g_frontend.memory_descriptor_count = 0;
}

static void content_overrides_clear(void)
{
    for (unsigned i = 0; i < g_frontend.content_override_count; ++i)
        free(g_frontend.content_overrides[i].extensions);
    free(g_frontend.content_overrides);
    g_frontend.content_overrides = NULL;
    g_frontend.content_override_count = 0;
}

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

static bool game_info_ext_populate(const char *content_path)
{
    memset(&g_frontend.game_info_ext, 0, sizeof(g_frontend.game_info_ext));

    g_frontend.game_info_ext.full_path = content_path;
    g_frontend.game_info_ext.data = g_frontend.rom_data;
    g_frontend.game_info_ext.size = g_frontend.rom_size;
    g_frontend.game_info_ext.persistent_data = true;

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
    if (g_game_loaded && g_core.retro_unload_game)
        g_core.retro_unload_game();
    g_game_loaded = false;

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

    controller_ports_clear();
    subsystem_info_clear();
    memory_maps_clear();
    content_overrides_clear();
    game_info_ext_clear();
    memset(&g_frontend.disk_control, 0, sizeof(g_frontend.disk_control));
    g_frontend.has_disk_control = false;

    memset(&g_core, 0, sizeof(g_core));
}

static bool core_load_game(const char *content_path)
{
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

        /* Populate extended game info for any GET_GAME_INFO_EXT calls
         * the core may make during retro_load_game. */
        game_info_ext_populate(content_path);

        bool loaded;
        if (g_frontend.subsystem_ident) {
            /* Look up the requested subsystem in the deep-copied registry. */
            const struct subsystem_storage *match = NULL;
            for (unsigned i = 0; i < g_frontend.subsystem_info_count; ++i) {
                if (g_frontend.subsystem_info[i].ident &&
                    strcmp(g_frontend.subsystem_info[i].ident,
                           g_frontend.subsystem_ident) == 0) {
                    match = &g_frontend.subsystem_info[i];
                    break;
                }
            }
            if (!match) {
                LOG_ERROR("--subsystem '%s' is not declared by the core",
                          g_frontend.subsystem_ident);
                free(g_frontend.rom_data);
                g_frontend.rom_data = NULL;
                g_frontend.rom_size = 0;
                return false;
            }
            if (!g_core.retro_load_game_special) {
                LOG_ERROR("Core does not export retro_load_game_special; "
                          "cannot use --subsystem");
                free(g_frontend.rom_data);
                g_frontend.rom_data = NULL;
                g_frontend.rom_size = 0;
                return false;
            }
            if (match->num_roms != 1) {
                LOG_WARN("--subsystem '%s' expects %u ROMs but only 1 content "
                         "path was provided; behaviour may be undefined",
                         g_frontend.subsystem_ident, match->num_roms);
            }
            LOG_INFO("Calling retro_load_game_special(id=%u, '%s')...",
                     match->id, g_frontend.subsystem_ident);
            loaded = g_core.retro_load_game_special(match->id, &game, 1);
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
            free(g_frontend.rom_data);
            g_frontend.rom_data = NULL;
            g_frontend.rom_size = 0;
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
/* SRAM / Savestate persistence                                       */
/* ------------------------------------------------------------------ */

/* Extract "name" from "<dir>/name.ext". Returns heap-allocated string. */
static char *content_basename_noext(const char *content_path)
{
    if (!content_path)
        return NULL;

    const char *last_sep = content_path;
    for (const char *p = content_path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            last_sep = p + 1;
    }

    const char *dot = NULL;
    for (const char *p = last_sep; *p; ++p) {
        if (*p == '.')
            dot = p;
    }

    size_t len = dot ? (size_t)(dot - last_sep) : strlen(last_sep);
    if (len == 0)
        return NULL;

    char *out = malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, last_sep, len);
    out[len] = '\0';
    return out;
}

char *core_sram_path(const char *save_dir, const char *content_path)
{
    if (!save_dir || !content_path)
        return NULL;

    char *base = content_basename_noext(content_path);
    if (!base)
        return NULL;

    size_t dl = strlen(save_dir);
    bool need_sep = dl > 0 && save_dir[dl - 1] != '/' && save_dir[dl - 1] != '\\';
    size_t total = dl + (need_sep ? 1 : 0) + strlen(base) + 4 + 1;
    char *out = malloc(total);
    if (!out) {
        free(base);
        return NULL;
    }
    snprintf(out, total, "%s%s%s.srm", save_dir, need_sep ? "/" : "", base);
    free(base);
    return out;
}

bool core_sram_load(const char *path)
{
    if (!path || !g_core.retro_get_memory_data || !g_core.retro_get_memory_size)
        return false;

    void *dst = g_core.retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t dst_size = g_core.retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!dst || dst_size == 0)
        return false;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return true; /* missing is fine */

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return false;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    if ((size_t)size > dst_size) {
        LOG_ERROR("SRAM file %s is %ld bytes but core slot is only %zu bytes; "
                  "refusing to truncate (would corrupt the save)", path, size, dst_size);
        fclose(fp);
        return false;
    }

    size_t read_size = (size_t)size;
    size_t got = fread(dst, 1, read_size, fp);
    fclose(fp);

    if (got != read_size) {
        LOG_WARN("SRAM read incomplete: got %zu of %zu bytes from %s",
                 got, read_size, path);
        return false;
    }
    if ((size_t)size != dst_size) {
        LOG_INFO("Loaded %zu bytes of SRAM from %s "
                 "(core slot is %zu bytes)",
                 read_size, path, dst_size);
    } else {
        LOG_INFO("Loaded SRAM (%zu bytes) from %s", read_size, path);
    }
    return true;
}

bool core_sram_save(const char *path)
{
    if (!path || !g_core.retro_get_memory_data || !g_core.retro_get_memory_size)
        return false;

    const void *src = g_core.retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = g_core.retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!src || size == 0)
        return false;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        LOG_ERROR("Failed to open SRAM path for writing: %s", path);
        return false;
    }

    size_t wrote = fwrite(src, 1, size, fp);
    fclose(fp);

    if (wrote != size) {
        LOG_ERROR("SRAM write incomplete: wrote %zu of %zu bytes to %s",
                  wrote, size, path);
        return false;
    }
    LOG_INFO("Saved SRAM (%zu bytes) to %s", size, path);
    return true;
}

bool core_savestate_load(const char *path)
{
    if (!path || !g_core.retro_unserialize) {
        LOG_WARN("Savestate load skipped: core does not export retro_unserialize");
        return false;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("Failed to open savestate for reading: %s", path);
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        LOG_ERROR("Refusing to load empty savestate: %s", path);
        return false;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    void *buf = malloc((size_t)size);
    if (!buf) {
        fclose(fp);
        return false;
    }
    size_t got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
        free(buf);
        LOG_ERROR("Savestate read incomplete: %s", path);
        return false;
    }

    bool ok = g_core.retro_unserialize(buf, (size_t)size);
    free(buf);
    if (!ok) {
        LOG_ERROR("retro_unserialize rejected %s (%ld bytes)", path, size);
        return false;
    }
    LOG_INFO("Loaded savestate (%ld bytes) from %s", size, path);
    return true;
}

bool core_savestate_save(const char *path)
{
    if (!path || !g_core.retro_serialize || !g_core.retro_serialize_size) {
        LOG_WARN("Savestate save skipped: core does not export retro_serialize");
        return false;
    }

    size_t size = g_core.retro_serialize_size();
    if (size == 0) {
        LOG_ERROR("retro_serialize_size returned 0; nothing to save");
        return false;
    }

    void *buf = malloc(size);
    if (!buf)
        return false;

    if (!g_core.retro_serialize(buf, size)) {
        free(buf);
        LOG_ERROR("retro_serialize failed for %s", path);
        return false;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(buf);
        LOG_ERROR("Failed to open savestate for writing: %s", path);
        return false;
    }
    size_t wrote = fwrite(buf, 1, size, fp);
    fclose(fp);
    free(buf);

    if (wrote != size) {
        LOG_ERROR("Savestate write incomplete: wrote %zu of %zu bytes to %s",
                  wrote, size, path);
        return false;
    }
    LOG_INFO("Saved savestate (%zu bytes) to %s", size, path);
    return true;
}

/* ------------------------------------------------------------------ */
/* Callbacks exposed to the core                                      */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Environment dispatch table (R1)                                    */
/*                                                                    */
/* Each entry registers a handler for a single RETRO_ENVIRONMENT_*    */
/* command. Lookup tries the raw cmd first (preserving the            */
/* EXPERIMENTAL flag) so colliding pairs like SET_SERIALIZATION_QUIRKS*/
/* (44) and SET_HW_SHARED_CONTEXT (44|EXP) can have distinct          */
/* handlers (I-10). If no exact match is found we fall back to the    */
/* EXP-stripped cmd: most callbacks behave identically whether the    */
/* core ORs in EXPERIMENTAL or not (some cores like Beetle PSX HW do),*/
/* so handlers only need to register once with the canonical value.   */
/*                                                                    */
/* Handlers take a frontend pointer + the raw `data` argument so they */
/* can be unit-tested in isolation against a stub frontend.           */
/* ------------------------------------------------------------------ */

typedef bool (*env_handler_fn)(struct frontend_state *fe, void *data);

struct env_handler_entry {
    unsigned cmd;            /* Raw cmd including EXPERIMENTAL if applicable */
    const char *name;        /* For diagnostics */
    env_handler_fn handler;
};

/* ---- Simple handlers (no frontend mutation beyond a single field) ---- */

static bool env_get_can_dupe(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) return false;
    *(bool *)data = true;
    return true;
}

static bool env_get_overscan(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) return false;
    *(bool *)data = false;
    return true;
}

static bool env_shutdown(struct frontend_state *fe, void *data)
{
    (void)data;
    fe->running = false;
    return true;
}

static bool env_set_message(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) return false;
    const struct retro_message *msg = (const struct retro_message *)data;
    log_emit(LOG_LEVEL_INFO, "CORE", NULL, 0, "%s", msg->msg);
    return true;
}

static bool env_set_performance_level(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) return false;
    LOG_INFO("Core performance level hint: %u", *(const unsigned *)data);
    return true;
}

static bool env_get_system_directory(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->system_directory;
    LOG_DEBUG("Core queried system directory: %s",
              fe->system_directory ? fe->system_directory : "(null)");
    return true;
}

static bool env_get_libretro_path(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->core_path;
    return true;
}

static bool env_get_core_assets_directory(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->core_assets_directory;
    return true;
}

static bool env_get_playlist_directory(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->playlist_directory;
    return true;
}

static bool env_get_file_browser_start_directory(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->file_browser_directory;
    return true;
}

static bool env_set_support_no_game(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return true;
}

static bool env_set_input_descriptors(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

/* ---- R1b: mid-complexity handlers ---- */

static bool env_set_rotation(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_rotation: NULL data"); return false; }
    unsigned rot = *(const unsigned *)data;
    if (rot > 3) {
        LOG_WARN("SET_ROTATION: ignoring invalid rotation %u (expected 0-3)",
                 rot);
        return false;
    }
    fe->video.rotation = rot;
    LOG_INFO("Core requested rotation: %u (%u degrees CCW)",
             rot, rot * 90);
    return true;
}

static bool env_set_pixel_format(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_pixel_format: NULL data"); return false; }
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
    fe->video.pixel_format = fmt;
    return true;
}

static bool env_set_keyboard_callback(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_keyboard_callback: NULL data"); return false; }
    const struct retro_keyboard_callback *cb =
        (const struct retro_keyboard_callback *)data;
    fe->keyboard_callback = *cb;
    return true;
}

static bool env_set_disk_control_interface(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_disk_control_interface: NULL data"); return false; }
    const struct retro_disk_control_callback *legacy =
        (const struct retro_disk_control_callback *)data;
    /* The legacy struct's first 7 fields are layout-identical to the
     * ext struct. memcpy those and leave the ext-only fields NULL
     * (memset guarantees set_initial_image/get_image_path/get_image_label). */
    memset(&fe->disk_control, 0, sizeof(fe->disk_control));
    memcpy(&fe->disk_control, legacy,
           sizeof(struct retro_disk_control_callback));
    fe->has_disk_control = true;
    LOG_INFO("Core registered legacy disk control interface");
    disk_control_apply_initial_index();
    return true;
}

static bool env_get_preferred_hw_render(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_preferred_hw_render: NULL data"); return false; }
    int *preferred = (int *)data;
    bool result;
    switch (fe->preferred_renderer) {
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

static bool env_get_variable_update(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_variable_update: NULL data"); return false; }
    *(bool *)data = false;
    return true;
}

static bool env_set_frame_time_callback(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_frame_time_callback: NULL data"); return false; }
    const struct retro_frame_time_callback *cb =
        (const struct retro_frame_time_callback *)data;
    fe->frame_time_callback = cb->callback;
    fe->frame_time_reference = cb->reference;
    LOG_INFO("Core registered frame-time callback (reference=%lld us)",
             (long long)cb->reference);
    return true;
}

static bool env_set_audio_callback(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

static bool env_get_rumble_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

static bool env_get_input_bitmasks(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_input_bitmasks: NULL data"); return false; }
    *(bool *)data = true;
    return true;
}

static bool env_get_input_device_capabilities(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_input_device_capabilities: NULL data"); return false; }
    *(uint64_t *)data = (1 << RETRO_DEVICE_JOYPAD);
    return true;
}

static bool env_get_sensor_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

static bool env_get_camera_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

static bool env_get_log_interface(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_log_interface: NULL data"); return false; }
    struct retro_log_callback *cb = (struct retro_log_callback *)data;
    cb->log = core_log_bridge;
    return true;
}

static bool env_get_perf_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

static bool env_get_location_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

static bool env_get_save_directory(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_save_directory: NULL data"); return false; }
    const char *dir = fe->save_directory
                      ? fe->save_directory
                      : fe->system_directory;
    *(const char **)data = dir;
    LOG_DEBUG("Core queried save directory: %s", dir ? dir : "(null)");
    return true;
}

static bool env_set_geometry(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_geometry: NULL data"); return false; }
    const struct retro_game_geometry *geo =
        (const struct retro_game_geometry *)data;
    video_update_geometry(geo->base_width, geo->base_height,
                          geo->max_width, geo->max_height,
                          geo->aspect_ratio);
    return true;
}

static bool env_set_system_av_info(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_system_av_info: NULL data"); return false; }
    const struct retro_system_av_info *av =
        (const struct retro_system_av_info *)data;
    g_av_info = *av;

    if (fe->video.hw_render_enabled) {
        video_resize(av->geometry.max_width, av->geometry.max_height);
    }
    return true;
}

static bool env_get_language(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_language: NULL data"); return false; }
    *(enum retro_language *)data = fe->language;
    return true;
}

static bool env_get_username(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_username: NULL data"); return false; }
    *(const char **)data = fe->username;
    return fe->username != NULL;
}

static bool env_get_disk_control_interface_version(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_disk_control_interface_version: NULL data"); return false; }
    *(unsigned *)data = 1;
    return true;
}

static bool env_get_input_max_users(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_input_max_users: NULL data"); return false; }
    /* The frontend currently maps a single keyboard to port 0 only.
     * Cores can use this to skip polling ports 1..N. */
    *(unsigned *)data = 1;
    return true;
}

static bool env_get_audio_video_enable(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_audio_video_enable: NULL data"); return false; }
    /* libretro spec: bit0 = RETRO_AV_ENABLE_VIDEO (always on),
     * bit1 = RETRO_AV_ENABLE_AUDIO (gated by --no-audio),
     * bit2 = RETRO_AV_ENABLE_FAST_SAVESTATES (not used),
     * bit3 = RETRO_AV_ENABLE_HARD_DISABLE_AUDIO (never set).
     * Fast-forward state is reported via GET_FASTFORWARDING, not here. */
    *(int *)data = RETRO_AV_ENABLE_VIDEO
                   | (fe->no_audio ? 0 : RETRO_AV_ENABLE_AUDIO);
    return true;
}

static bool env_get_target_sample_rate(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_target_sample_rate: NULL data"); return false; }
    float rate = (g_av_info.timing.sample_rate > 0.0)
                 ? (float)g_av_info.timing.sample_rate
                 : (float)FRONTEND_AUDIO_SAMPLE_RATE;
    *(float *)data = rate;
    return true;
}

static bool env_set_audio_buffer_status_callback(struct frontend_state *fe, void *data)
{
    (void)fe;
    /* data may be NULL to clear the callback; the environment call
     * itself is still considered supported. */
    const struct retro_audio_buffer_status_callback *cb =
        (const struct retro_audio_buffer_status_callback *)data;
    audio_set_buffer_status_callback(cb ? cb->callback : NULL);
    LOG_INFO("Core %s audio buffer status callback",
             cb && cb->callback ? "registered" : "unregistered");
    return true;
}

static bool env_set_minimum_audio_latency(struct frontend_state *fe, void *data)
{
    (void)fe;
    unsigned ms = (data) ? *(const unsigned *)data : 0;
    audio_set_minimum_latency(ms);
    LOG_INFO("Core requested minimum audio latency: %u ms", ms);
    return true;
}

static bool env_get_fastforwarding(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_fastforwarding: NULL data"); return false; }
    *(bool *)data = fe->fast_forward_active;
    return true;
}

static bool env_get_target_refresh_rate(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_target_refresh_rate: NULL data"); return false; }
    float rate = 60.0f;
    SDL_Window *win = fe->video.window;
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

static bool env_set_variable(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_variable: NULL data"); return false; }
    const struct retro_variable *var = (const struct retro_variable *)data;
    if (!var->key || !var->value)
        return false;
    if (!core_options_table_set_value(&fe->core_options,
                                      var->key, var->value))
        return false;
    if (!variable_table_set(&fe->disk_overrides, var->key, var->value))
        return false;
    return true;
}

static bool env_set_hw_render_context_negotiation_interface(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_hw_render_context_negotiation_interface: NULL data"); return false; }
    return video_negotiate_hw_context(
        (const struct retro_hw_render_context_negotiation_interface *)data);
}

static bool env_get_hw_render_interface(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_hw_render_interface: NULL data"); return false; }
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

static bool env_get_vfs_interface(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_vfs_interface: NULL data"); return false; }
    struct retro_vfs_interface_info *info =
        (struct retro_vfs_interface_info *)data;
    if (info->required_interface_version > 1)
        return false;
    info->iface = vfs_get_interface();
    info->required_interface_version = 1;
    return true;
}

static bool env_get_core_options_version(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_core_options_version: NULL data"); return false; }
    *(unsigned *)data = 2;
    return true;
}

static bool env_set_core_options_update_display_callback(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_update_display_callback: NULL data"); return false; }
    const struct retro_core_options_update_display_callback *cb =
        (const struct retro_core_options_update_display_callback *)data;
    fe->core_options_update_display_callback = cb->callback;
    return true;
}

static bool env_get_hw_render_context_negotiation_interface_support(struct frontend_state *fe, void *data)
{
    (void)fe;
    /* Cores poll this to discover which negotiation interface versions
     * the frontend understands. We currently support the Vulkan
     * negotiation interface (handled in video_negotiate_hw_context),
     * which is the only enum value defined upstream. Return the highest
     * interface_version the frontend recognises; other API types get 0. */
    if (!data) { LOG_WARN("env_get_hw_render_context_negotiation_interface_support: NULL data"); return false; }
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

static bool env_get_throttle_state(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_throttle_state: NULL data"); return false; }
    struct retro_throttle_state *ts = (struct retro_throttle_state *)data;
    if (fe->fast_forward_active) {
        ts->mode = RETRO_THROTTLE_FAST_FORWARD;
        ts->rate = 0.0f; /* unlimited */
    } else {
        ts->mode = RETRO_THROTTLE_NONE;
        ts->rate = (g_av_info.timing.fps > 0.0)
                   ? (float)g_av_info.timing.fps : 0.0f;
    }
    return true;
}

static bool env_get_savestate_context(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_savestate_context: NULL data"); return false; }
    *(int *)data = RETRO_SAVESTATE_CONTEXT_NORMAL;
    return true;
}

static bool env_get_jit_capable(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_jit_capable: NULL data"); return false; }
    /* All three desktop targets (Linux/macOS/Windows) allow JIT. The
     * libretro contract is "false only on locked-down platforms like
     * iOS / non-jailbroken consoles", which we never run on. */
    *(bool *)data = true;
    return true;
}

static bool env_get_message_interface_version(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_message_interface_version: NULL data"); return false; }
    *(unsigned *)data = 1;
    return true;
}

static bool env_set_message_ext(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_message_ext: NULL data"); return false; }
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

static bool env_set_proc_address_callback(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_proc_address_callback: NULL data"); return false; }
    const struct retro_get_proc_address_interface *iface =
        (const struct retro_get_proc_address_interface *)data;
    fe->get_proc_address = iface->get_proc_address;
    LOG_INFO("Core registered get_proc_address interface (%p)",
             (void *)(uintptr_t)iface->get_proc_address);
    return true;
}

static bool env_set_support_achievements(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_support_achievements: NULL data"); return false; }
    fe->core_supports_achievements = *(const bool *)data;
    LOG_INFO("Core declares achievement support: %s",
             fe->core_supports_achievements ? "yes" : "no");
    return true;
}

static bool env_set_fastforwarding_override(struct frontend_state *fe, void *data)
{
    /* NULL data is a support probe per the libretro contract. */
    if (!data) {
        LOG_DEBUG("Core probed SET_FASTFORWARDING_OVERRIDE support");
        return true;
    }
    const struct retro_fastforwarding_override *ov =
        (const struct retro_fastforwarding_override *)data;
    fe->ff_override_active = ov->fastforward;
    fe->ff_inhibit_toggle  = ov->inhibit_toggle;
    fe->fast_forward_active = ov->fastforward;
    LOG_INFO("Core fast-forward override: active=%s ratio=%.2f "
             "notification=%s inhibit_toggle=%s",
             ov->fastforward ? "yes" : "no",
             ov->ratio,
             ov->notification ? "yes" : "no",
             ov->inhibit_toggle ? "yes" : "no");
    return true;
}

static bool env_get_game_info_ext(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_game_info_ext: NULL data"); return false; }
    /* libretro contract: only valid inside retro_load_game[_special]. */
    if (!fe->rom_data && !fe->game_info_ext.full_path) {
        LOG_WARN("GET_GAME_INFO_EXT called outside retro_load_game");
        return false;
    }
    const struct retro_game_info_ext **out =
        (const struct retro_game_info_ext **)data;
    *out = &fe->game_info_ext;
    return true;
}

static bool env_get_current_software_framebuffer(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_current_software_framebuffer: NULL data"); return false; }
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
                                        fe->video.pixel_format,
                                        &pixels, &pitch))
        return false;

    fb->data = pixels;
    fb->pitch = pitch;
    fb->format = fe->video.pixel_format;
    fb->memory_flags = 0;
    return true;
}

/* ---- R1c: heavy handlers ---- */

static bool env_set_hw_render(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_hw_render: NULL data"); return false; }
    return video_set_hw_render((struct retro_hw_render_callback *)data);
}

static bool env_get_variable(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_variable: NULL data"); return false; }
    struct retro_variable *var = (struct retro_variable *)data;
    if (!var->key)
        return false;

    const char *override = variable_table_get(&fe->cli_overrides,
                                              var->key);
    if (!override)
        override = variable_table_get(&fe->disk_overrides, var->key);
    if (override) {
        var->value = override;
        return true;
    }

    const struct core_option *opt =
        core_options_table_get(&fe->core_options, var->key);
    if (!opt)
        return false;

    var->value = opt->current_value ? opt->current_value : opt->default_value;
    return true;
}

static bool env_set_variables(struct frontend_state *fe, void *data)
{
    const struct retro_variable *vars = (const struct retro_variable *)data;
    if (!vars)
        return false;

    core_options_table_clear(&fe->core_options);

    bool ok = true;
    for (const struct retro_variable *v = vars; v && v->key; ++v) {
        char desc[256];
        core_var_parse_description(v->value, desc, sizeof(desc));

        char def[256];
        core_var_parse_default(v->value, def, sizeof(def));

        const char *choices = core_var_choices_begin(v->value);
        /* libretro spec caps choices at RETRO_NUM_CORE_OPTION_VALUES_MAX
         * (128). Allocate one extra slot for the trailing NULL sentinel
         * required by core_options_table_add. */
        const char *values[RETRO_NUM_CORE_OPTION_VALUES_MAX + 1];
        size_t val_count = 0;

        if (choices) {
            const char *p = choices;
            while (*p) {
                if (val_count >= RETRO_NUM_CORE_OPTION_VALUES_MAX) {
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

        if (!core_options_table_add(&fe->core_options,
                                    v->key, desc, NULL, values, def)) {
            ok = false;
        }

        for (size_t i = 0; i < val_count; ++i)
            free((char *)values[i]);

        if (!ok)
            break;
    }

    if (!ok)
        core_options_table_clear(&fe->core_options);

    size_t seeded = 0;
    size_t total = core_options_table_count(&fe->core_options);
    for (size_t i = 0; i < total; ++i) {
        const struct core_option *opt =
            core_options_table_at(&fe->core_options, i);
        if (variable_table_get(&fe->disk_overrides, opt->key))
            continue;
        if (variable_table_set(&fe->disk_overrides,
                               opt->key, opt->default_value))
            seeded++;
    }

    LOG_INFO("Core registered %zu variables (%zu seeded from defaults)",
             total, seeded);
    return ok;
}

static bool env_set_core_options(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options: NULL data"); return false; }
    const struct retro_core_option_definition *defs =
        (const struct retro_core_option_definition *)data;
    core_options_table_clear(&fe->core_options);
    bool ok = add_options_from_v1_defs(defs);
    if (!ok) {
        core_options_table_clear(&fe->core_options);
        return false;
    }
    size_t seeded = seed_disk_overrides_from_defaults();
    size_t total = core_options_table_count(&fe->core_options);
    LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
             total, seeded);
    return true;
}

static bool env_set_core_options_intl(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_intl: NULL data"); return false; }
    const struct retro_core_options_intl *opts =
        (const struct retro_core_options_intl *)data;
    if (opts->local && opts->local != opts->us) {
        LOG_WARN("SET_CORE_OPTIONS_INTL: ignoring localized definitions, "
                 "only the US variant is consumed");
    }
    core_options_table_clear(&fe->core_options);
    bool ok = true;
    if (opts->us)
        ok = add_options_from_v1_defs(opts->us);
    if (!ok) {
        core_options_table_clear(&fe->core_options);
        return false;
    }
    size_t seeded = seed_disk_overrides_from_defaults();
    size_t total = core_options_table_count(&fe->core_options);
    LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
             total, seeded);
    return true;
}

static bool env_set_core_options_v2(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_v2: NULL data"); return false; }
    const struct retro_core_options_v2 *opts =
        (const struct retro_core_options_v2 *)data;
    core_options_table_clear(&fe->core_options);
    bool ok = add_options_from_v2_defs(opts->definitions);
    if (!ok) {
        core_options_table_clear(&fe->core_options);
        return false;
    }
    size_t seeded = seed_disk_overrides_from_defaults();
    size_t total = core_options_table_count(&fe->core_options);
    LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
             total, seeded);
    return true;
}

static bool env_set_core_options_v2_intl(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_v2_intl: NULL data"); return false; }
    const struct retro_core_options_v2_intl *opts =
        (const struct retro_core_options_v2_intl *)data;
    if (opts->local && opts->local != opts->us) {
        LOG_WARN("SET_CORE_OPTIONS_V2_INTL: ignoring localized definitions, "
                 "only the US variant is consumed");
    }
    core_options_table_clear(&fe->core_options);
    bool ok = true;
    if (opts->us)
        ok = add_options_from_v2_defs(opts->us->definitions);
    if (!ok) {
        core_options_table_clear(&fe->core_options);
        return false;
    }
    size_t seeded = seed_disk_overrides_from_defaults();
    size_t total = core_options_table_count(&fe->core_options);
    LOG_INFO("Core registered %zu options (%zu seeded from defaults)",
             total, seeded);
    return true;
}

static bool env_set_core_options_display(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_display: NULL data"); return false; }
    const struct retro_core_option_display *disp =
        (const struct retro_core_option_display *)data;
    if (!disp->key)
        return false;
    if (!core_options_table_set_visible(&fe->core_options,
                                        disp->key, disp->visible)) {
        /* The core may toggle visibility for an option it has not yet
         * declared (e.g. during a multi-stage SET_VARIABLES sequence).
         * Return false so the core knows we did not record it. */
        return false;
    }
    return true;
}

static bool env_set_controller_info(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_controller_info: NULL data"); return false; }
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

        struct controller_port_info *slot = &fe->controller_ports[port];
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
    fe->controller_port_count = port;

    LOG_INFO("Core registered controller info for %u port(s):", port);
    for (unsigned i = 0; i < port; ++i) {
        const struct controller_port_info *slot = &fe->controller_ports[i];
        LOG_INFO("  port %u: %u device type(s)", i, slot->num_types);
        for (unsigned t = 0; t < slot->num_types; ++t) {
            LOG_INFO("    [%u] id=%u desc=%s",
                     t, slot->types[t].id,
                     slot->types[t].desc ? slot->types[t].desc : "(null)");
        }
    }
    return true;
}

static bool env_set_disk_control_ext_interface(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_disk_control_ext_interface: NULL data"); return false; }
    const struct retro_disk_control_ext_callback *cb =
        (const struct retro_disk_control_ext_callback *)data;
    fe->disk_control = *cb;
    fe->has_disk_control = true;
    LOG_INFO("Core registered disk control ext interface (num_images=%u)",
             cb->get_num_images ? cb->get_num_images() : 0);
    disk_control_apply_initial_index();
    return true;
}

static bool env_set_serialization_quirks(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_serialization_quirks: NULL data"); return false; }
    uint64_t *quirks = (uint64_t *)data;
    /* The frontend does not require the core to drop any quirks, so we
     * leave whatever the core wrote in place. Log the declared bits so
     * savestate misbehavior is easier to attribute. */
    LOG_INFO("Core serialization quirks: 0x%llx",
             (unsigned long long)*quirks);
    return true;
}

static bool env_set_hw_shared_context(struct frontend_state *fe, void *data)
{
    (void)data;
    fe->video.hw_shared_context_requested = true;
    LOG_INFO("Core requested shared GL context");
    return true;
}

static bool env_set_subsystem_info(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_subsystem_info: NULL data"); return false; }
    const struct retro_subsystem_info *list =
        (const struct retro_subsystem_info *)data;

    subsystem_info_clear();

    /* Count entries first (terminated by a zeroed-out struct). */
    unsigned count = 0;
    for (const struct retro_subsystem_info *p = list;
         p && (p->desc || p->ident || p->roms || p->num_roms);
         ++p)
        count++;

    if (count == 0) {
        LOG_INFO("Core registered 0 subsystems");
        return true;
    }

    fe->subsystem_info = calloc(count,
        sizeof(*fe->subsystem_info));
    if (!fe->subsystem_info)
        return false;
    fe->subsystem_info_count = count;

    for (unsigned i = 0; i < count; ++i) {
        const struct retro_subsystem_info *src = &list[i];
        struct subsystem_storage *dst = &fe->subsystem_info[i];
        dst->desc  = src->desc  ? SDL_strdup(src->desc)  : NULL;
        dst->ident = src->ident ? SDL_strdup(src->ident) : NULL;
        dst->id    = src->id;
        dst->num_roms = src->num_roms;
        if (src->num_roms == 0)
            continue;

        dst->roms = calloc(src->num_roms, sizeof(*dst->roms));
        if (!dst->roms) {
            subsystem_info_clear();
            return false;
        }

        for (unsigned j = 0; j < src->num_roms; ++j) {
            const struct retro_subsystem_rom_info *srom = &src->roms[j];
            struct subsystem_rom_storage *drom = &dst->roms[j];
            drom->desc             = srom->desc
                                     ? SDL_strdup(srom->desc) : NULL;
            drom->valid_extensions = srom->valid_extensions
                                     ? SDL_strdup(srom->valid_extensions)
                                     : NULL;
            drom->need_fullpath    = srom->need_fullpath;
            drom->block_extract    = srom->block_extract;
            drom->required         = srom->required;
            drom->num_memory       = srom->num_memory;
            if (srom->num_memory == 0)
                continue;

            drom->memory = calloc(srom->num_memory, sizeof(*drom->memory));
            drom->memory_extensions =
                calloc(srom->num_memory, sizeof(*drom->memory_extensions));
            if (!drom->memory || !drom->memory_extensions) {
                subsystem_info_clear();
                return false;
            }
            for (unsigned k = 0; k < srom->num_memory; ++k) {
                drom->memory_extensions[k] = srom->memory[k].extension
                    ? SDL_strdup(srom->memory[k].extension) : NULL;
                drom->memory[k].extension = drom->memory_extensions[k];
                drom->memory[k].type      = srom->memory[k].type;
            }
        }
    }

    LOG_INFO("Core registered %u subsystem(s):", count);
    for (unsigned i = 0; i < count; ++i) {
        const struct subsystem_storage *ss = &fe->subsystem_info[i];
        LOG_INFO("  [%u] id=%u ident=%s desc=%s roms=%u",
                 i, ss->id,
                 ss->ident ? ss->ident : "(null)",
                 ss->desc  ? ss->desc  : "(null)",
                 ss->num_roms);
    }
    return true;
}

static bool env_set_memory_maps(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_memory_maps: NULL data"); return false; }
    const struct retro_memory_map *map = (const struct retro_memory_map *)data;

    memory_maps_clear();

    if (map->num_descriptors == 0 || !map->descriptors) {
        LOG_INFO("Core registered 0 memory descriptors");
        return true;
    }

    fe->memory_descriptors = calloc(map->num_descriptors,
        sizeof(*fe->memory_descriptors));
    fe->memory_addrspace_strings = calloc(map->num_descriptors,
        sizeof(*fe->memory_addrspace_strings));
    if (!fe->memory_descriptors ||
        !fe->memory_addrspace_strings) {
        memory_maps_clear();
        return false;
    }

    for (unsigned i = 0; i < map->num_descriptors; ++i) {
        fe->memory_descriptors[i] = map->descriptors[i];
        if (map->descriptors[i].addrspace) {
            fe->memory_addrspace_strings[i] =
                SDL_strdup(map->descriptors[i].addrspace);
            fe->memory_descriptors[i].addrspace =
                fe->memory_addrspace_strings[i];
        } else {
            fe->memory_descriptors[i].addrspace = NULL;
        }
    }
    fe->memory_descriptor_count = map->num_descriptors;

    LOG_INFO("Core registered %u memory descriptor(s)",
             map->num_descriptors);
    return true;
}

static bool env_set_content_info_override(struct frontend_state *fe, void *data)
{
    content_overrides_clear();
    /* NULL data is a support probe per the libretro contract. */
    if (!data) {
        LOG_DEBUG("Core probed SET_CONTENT_INFO_OVERRIDE support");
        return true;
    }
    const struct retro_system_content_info_override *list =
        (const struct retro_system_content_info_override *)data;

    unsigned count = 0;
    for (const struct retro_system_content_info_override *p = list;
         p && p->extensions; ++p)
        count++;

    if (count == 0)
        return true;

    fe->content_overrides = calloc(count,
        sizeof(*fe->content_overrides));
    if (!fe->content_overrides)
        return false;
    fe->content_override_count = count;

    for (unsigned i = 0; i < count; ++i) {
        fe->content_overrides[i].extensions =
            SDL_strdup(list[i].extensions);
        fe->content_overrides[i].need_fullpath =
            list[i].need_fullpath;
        fe->content_overrides[i].persistent_data =
            list[i].persistent_data;
    }

    LOG_INFO("Core registered %u content info override(s); frontend keeps "
             "ROM data alive until shutdown regardless of persistent_data",
             count);
    return true;
}

static const struct env_handler_entry g_env_table[] = {
    { RETRO_ENVIRONMENT_GET_CAN_DUPE,                     "GET_CAN_DUPE",                     env_get_can_dupe                     },
    { RETRO_ENVIRONMENT_GET_OVERSCAN,                     "GET_OVERSCAN",                     env_get_overscan                     },
    { RETRO_ENVIRONMENT_SHUTDOWN,                         "SHUTDOWN",                         env_shutdown                         },
    { RETRO_ENVIRONMENT_SET_MESSAGE,                      "SET_MESSAGE",                      env_set_message                      },
    { RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL,            "SET_PERFORMANCE_LEVEL",            env_set_performance_level            },
    { RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,             "GET_SYSTEM_DIRECTORY",             env_get_system_directory             },
    { RETRO_ENVIRONMENT_GET_LIBRETRO_PATH,                "GET_LIBRETRO_PATH",                env_get_libretro_path                },
    { RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY,        "GET_CORE_ASSETS_DIRECTORY",        env_get_core_assets_directory        },
    { RETRO_ENVIRONMENT_GET_PLAYLIST_DIRECTORY,           "GET_PLAYLIST_DIRECTORY",           env_get_playlist_directory           },
    { RETRO_ENVIRONMENT_GET_FILE_BROWSER_START_DIRECTORY, "GET_FILE_BROWSER_START_DIRECTORY", env_get_file_browser_start_directory },
    { RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME,              "SET_SUPPORT_NO_GAME",              env_set_support_no_game              },
    { RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS,            "SET_INPUT_DESCRIPTORS",            env_set_input_descriptors            },
    { RETRO_ENVIRONMENT_SET_ROTATION,                                          "SET_ROTATION",                                          env_set_rotation                                          },
    { RETRO_ENVIRONMENT_SET_PIXEL_FORMAT,                                      "SET_PIXEL_FORMAT",                                      env_set_pixel_format                                      },
    { RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK,                                 "SET_KEYBOARD_CALLBACK",                                 env_set_keyboard_callback                                 },
    { RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE,                            "SET_DISK_CONTROL_INTERFACE",                            env_set_disk_control_interface                            },
    { RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER,                               "GET_PREFERRED_HW_RENDER",                               env_get_preferred_hw_render                               },
    { RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE,                                   "GET_VARIABLE_UPDATE",                                   env_get_variable_update                                   },
    { RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK,                               "SET_FRAME_TIME_CALLBACK",                               env_set_frame_time_callback                               },
    { RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK,                                    "SET_AUDIO_CALLBACK",                                    env_set_audio_callback                                    },
    { RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE,                                  "GET_RUMBLE_INTERFACE",                                  env_get_rumble_interface                                  },
    { RETRO_ENVIRONMENT_GET_INPUT_BITMASKS,                                    "GET_INPUT_BITMASKS",                                    env_get_input_bitmasks                                    },
    { RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES,                         "GET_INPUT_DEVICE_CAPABILITIES",                         env_get_input_device_capabilities                         },
    { RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE,                                  "GET_SENSOR_INTERFACE",                                  env_get_sensor_interface                                  },
    { RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE,                                  "GET_CAMERA_INTERFACE",                                  env_get_camera_interface                                  },
    { RETRO_ENVIRONMENT_GET_LOG_INTERFACE,                                     "GET_LOG_INTERFACE",                                     env_get_log_interface                                     },
    { RETRO_ENVIRONMENT_GET_PERF_INTERFACE,                                    "GET_PERF_INTERFACE",                                    env_get_perf_interface                                    },
    { RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE,                                "GET_LOCATION_INTERFACE",                                env_get_location_interface                                },
    { RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,                                    "GET_SAVE_DIRECTORY",                                    env_get_save_directory                                    },
    { RETRO_ENVIRONMENT_SET_GEOMETRY,                                          "SET_GEOMETRY",                                          env_set_geometry                                          },
    { RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO,                                    "SET_SYSTEM_AV_INFO",                                    env_set_system_av_info                                    },
    { RETRO_ENVIRONMENT_GET_LANGUAGE,                                          "GET_LANGUAGE",                                          env_get_language                                          },
    { RETRO_ENVIRONMENT_GET_USERNAME,                                          "GET_USERNAME",                                          env_get_username                                          },
    { RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION,                    "GET_DISK_CONTROL_INTERFACE_VERSION",                    env_get_disk_control_interface_version                    },
    { RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS,                                   "GET_INPUT_MAX_USERS",                                   env_get_input_max_users                                   },
    { RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE,                                "GET_AUDIO_VIDEO_ENABLE",                                env_get_audio_video_enable                                },
    { RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE,                                "GET_TARGET_SAMPLE_RATE",                                env_get_target_sample_rate                                },
    { RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK,                      "SET_AUDIO_BUFFER_STATUS_CALLBACK",                      env_set_audio_buffer_status_callback                      },
    { RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY,                             "SET_MINIMUM_AUDIO_LATENCY",                             env_set_minimum_audio_latency                             },
    { RETRO_ENVIRONMENT_GET_FASTFORWARDING,                                    "GET_FASTFORWARDING",                                    env_get_fastforwarding                                    },
    { RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE,                               "GET_TARGET_REFRESH_RATE",                               env_get_target_refresh_rate                               },
    { RETRO_ENVIRONMENT_SET_VARIABLE,                                          "SET_VARIABLE",                                          env_set_variable                                          },
    { RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE,           "SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE",           env_set_hw_render_context_negotiation_interface           },
    { RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE,                               "GET_HW_RENDER_INTERFACE",                               env_get_hw_render_interface                               },
    { RETRO_ENVIRONMENT_GET_VFS_INTERFACE,                                     "GET_VFS_INTERFACE",                                     env_get_vfs_interface                                     },
    { RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION,                              "GET_CORE_OPTIONS_VERSION",                              env_get_core_options_version                              },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK,              "SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK",              env_set_core_options_update_display_callback              },
    { RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT,   "GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT",   env_get_hw_render_context_negotiation_interface_support   },
    { RETRO_ENVIRONMENT_GET_THROTTLE_STATE,                                    "GET_THROTTLE_STATE",                                    env_get_throttle_state                                    },
    { RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT,                                 "GET_SAVESTATE_CONTEXT",                                 env_get_savestate_context                                 },
    { RETRO_ENVIRONMENT_GET_JIT_CAPABLE,                                       "GET_JIT_CAPABLE",                                       env_get_jit_capable                                       },
    { RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION,                         "GET_MESSAGE_INTERFACE_VERSION",                         env_get_message_interface_version                         },
    { RETRO_ENVIRONMENT_SET_MESSAGE_EXT,                                       "SET_MESSAGE_EXT",                                       env_set_message_ext                                       },
    { RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK,                             "SET_PROC_ADDRESS_CALLBACK",                             env_set_proc_address_callback                             },
    { RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS,                              "SET_SUPPORT_ACHIEVEMENTS",                              env_set_support_achievements                              },
    { RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE,                           "SET_FASTFORWARDING_OVERRIDE",                           env_set_fastforwarding_override                           },
    { RETRO_ENVIRONMENT_GET_GAME_INFO_EXT,                                     "GET_GAME_INFO_EXT",                                     env_get_game_info_ext                                     },
    { RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER,                      "GET_CURRENT_SOFTWARE_FRAMEBUFFER",                      env_get_current_software_framebuffer                      },
    { RETRO_ENVIRONMENT_SET_HW_RENDER,                                         "SET_HW_RENDER",                                         env_set_hw_render                                         },
    { RETRO_ENVIRONMENT_GET_VARIABLE,                                          "GET_VARIABLE",                                          env_get_variable                                          },
    { RETRO_ENVIRONMENT_SET_VARIABLES,                                         "SET_VARIABLES",                                         env_set_variables                                         },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS,                                      "SET_CORE_OPTIONS",                                      env_set_core_options                                      },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL,                                 "SET_CORE_OPTIONS_INTL",                                 env_set_core_options_intl                                 },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,                                   "SET_CORE_OPTIONS_V2",                                   env_set_core_options_v2                                   },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,                              "SET_CORE_OPTIONS_V2_INTL",                              env_set_core_options_v2_intl                              },
    { RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY,                              "SET_CORE_OPTIONS_DISPLAY",                              env_set_core_options_display                              },
    { RETRO_ENVIRONMENT_SET_CONTROLLER_INFO,                                   "SET_CONTROLLER_INFO",                                   env_set_controller_info                                   },
    { RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE,                        "SET_DISK_CONTROL_EXT_INTERFACE",                        env_set_disk_control_ext_interface                        },
    { RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO,                                    "SET_SUBSYSTEM_INFO",                                    env_set_subsystem_info                                    },
    { RETRO_ENVIRONMENT_SET_MEMORY_MAPS,                                       "SET_MEMORY_MAPS",                                       env_set_memory_maps                                       },
    { RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE,                             "SET_CONTENT_INFO_OVERRIDE",                             env_set_content_info_override                             },
    { RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS,                              "SET_SERIALIZATION_QUIRKS",                              env_set_serialization_quirks                              },
    { RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT,                                 "SET_HW_SHARED_CONTEXT",                                 env_set_hw_shared_context                                 },
};

static const struct env_handler_entry *env_table_lookup(unsigned cmd)
{
    /* Pass 1: exact raw cmd match (preserves EXP for I-10 collision pairs). */
    for (size_t i = 0; i < sizeof(g_env_table) / sizeof(g_env_table[0]); ++i) {
        if (g_env_table[i].cmd == cmd)
            return &g_env_table[i];
    }
    /* Pass 2: stripped EXP fallback. Lets cores OR in EXPERIMENTAL without
     * needing a duplicate table entry for every handler. */
    unsigned stripped = cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL;
    if (stripped == cmd)
        return NULL;
    for (size_t i = 0; i < sizeof(g_env_table) / sizeof(g_env_table[0]); ++i) {
        if (g_env_table[i].cmd == stripped)
            return &g_env_table[i];
    }
    return NULL;
}

bool RETRO_CALLCONV core_environment(unsigned cmd, void *data)
{
    const struct env_handler_entry *entry = env_table_lookup(cmd);
    if (entry)
        return entry->handler(&g_frontend, data);
    LOG_DEBUG("Unhandled env cmd: 0x%x", cmd);
    return false;
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
