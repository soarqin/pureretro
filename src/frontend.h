/*
 * PureRetro — Shared frontend definitions
 *
 * Global state, constants, and typedefs used across modules.
 */

#ifndef FRONTEND_H
#define FRONTEND_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "libretro.h"
#include "core_variables.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum dimensions for the rendering surface */
#define FRONTEND_MAX_WIDTH  4096
#define FRONTEND_MAX_HEIGHT 4096

/* Audio configuration */
#define FRONTEND_AUDIO_SAMPLE_RATE 48000
#define FRONTEND_AUDIO_CHANNELS    2
#define FRONTEND_AUDIO_BUFFER_MS   64

/* Rendering backends */
enum video_renderer
{
    VIDEO_RENDERER_NONE = 0,
    VIDEO_RENDERER_SW,
    VIDEO_RENDERER_OPENGL,
    VIDEO_RENDERER_VULKAN,
};

struct video_backend;

/* Video state shared across renderers */
struct video_state
{
    SDL_Window *window;
    enum video_renderer renderer;

    /* Active backend dispatched through the video_backend vtable.
     * Once set by video_set_hw_render, all per-frame operations go
     * via backend->method(backend_ctx, ...). */
    const struct video_backend *backend;
    void *backend_ctx;

    /* Current frame dimensions */
    unsigned frame_width;
    unsigned frame_height;
    unsigned frame_pitch;

    /* Pixel format negotiated with the core */
    enum retro_pixel_format pixel_format;

    /* Hardware render callback (valid for GL/VK) */
    struct retro_hw_render_callback hw;
    bool hw_render_enabled;

    /* Core requested a shared GL context (RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT).
     * Honored when creating the next GL context by setting
     * SDL_GL_SHARE_WITH_CURRENT_CONTEXT. Has no effect on Vulkan/software. */
    bool hw_shared_context_requested;

    /* Screen rotation requested by the core via SET_ROTATION.
     * Values are 0..3 representing 0/90/180/270-degree clockwise rotation.
     * Applied at present time by each renderer (SW: SDL_RenderTextureRotated;
     * GL: swap blit corners; VK: rotate blit destination corners). */
    unsigned rotation;
};

/* Maximum number of input ports we track controller info for.
 * libretro itself has no hard cap, but real cores top out around 8.
 * Slots beyond this are accepted but not recorded. */
#define FRONTEND_MAX_PORTS 8

/* Per-port controller info recorded from RETRO_ENVIRONMENT_SET_CONTROLLER_INFO.
 * We deep-copy the descriptors so the storage outlives the core's call. */
struct controller_port_info
{
    struct retro_controller_description *types;
    unsigned num_types;
};

/* Deep-copy of a single subsystem's ROM info (SET_SUBSYSTEM_INFO).
 * Memory descriptors and all strings are owned by the frontend. */
struct subsystem_rom_storage
{
    char *desc;
    char *valid_extensions;
    bool need_fullpath;
    bool block_extract;
    bool required;
    struct retro_subsystem_memory_info *memory;
    unsigned num_memory;
    /* Backing storage for each memory[i].extension string (parallel array). */
    char **memory_extensions;
};

/* Deep-copy of a subsystem descriptor declared by the core. */
struct subsystem_storage
{
    char *desc;
    char *ident;
    unsigned id;
    struct subsystem_rom_storage *roms;
    unsigned num_roms;
};

/* Deep-copy of a content-info override entry (SET_CONTENT_INFO_OVERRIDE). */
struct content_info_override_storage
{
    char *extensions;
    bool need_fullpath;
    bool persistent_data;
};

/* Global frontend state */
struct frontend_state
{
    bool running;
    bool fullscreen;
    bool no_audio;

    /* Portable mode: when true, the system directory lives in the current
     * working directory ("<cwd>/system") instead of the user data directory
     * returned by SDL_GetPrefPath. Set via the --portable CLI flag. */
    bool portable;

    /* User-requested renderer preference (VIDEO_RENDERER_NONE = no preference).
     * Set via the --render CLI flag. The core may still request a different
     * renderer; this is a hint, not a hard requirement. */
    enum video_renderer preferred_renderer;

    /* Set true once the core has called SET_HW_RENDER at least once.
     * Used for diagnostics: if the user requested HW but this is false,
     * the core never tried to initialize HW rendering. */
    bool hw_render_requested;

    /* Integer window scale factor. 0 means no scaling (use default size).
     * Applied after core init based on base_width/base_height. */
    unsigned window_scale;

    struct video_state video;

    /* Audio stream handle */
    SDL_AudioStream *audio_stream;

    /* Input state (one entry per RetroPad button id; MASK is a sentinel,
     * not a real id, so it is excluded from the array). */
    uint16_t joypad_state[RETRO_DEVICE_ID_JOYPAD_R3 + 1];

    /* Core paths */
    const char *core_path;
    const char *content_path;

    /* System directory (for firmware/BIOS files). Owned by the frontend,
     * freed on shutdown. NULL if not configured. */
    char *system_directory;

    /* Loaded ROM data (owned by frontend, freed on shutdown) */
    void *rom_data;
    size_t rom_size;

    /* Core options (SET_VARIABLES / GET_VARIABLE).
     * Structured storage for options declared by the core. User overrides
     * are stored separately so they survive SET_VARIABLES resets.
     *
     * Lookup order in GET_VARIABLE: cli_overrides -> disk_overrides -> default.
     * cli_overrides come from --variable CLI flags and are never persisted.
     * disk_overrides are loaded from / saved to a per-core .opt file. */
    struct core_options_table core_options;
    struct variable_table disk_overrides;
    struct variable_table cli_overrides;

    char *save_directory;
    const char *config_path;
    struct retro_keyboard_callback keyboard_callback;
    retro_core_options_update_display_callback_t core_options_update_display_callback;

    /* Per-port controller info from SET_CONTROLLER_INFO (deep-copied). */
    struct controller_port_info controller_ports[FRONTEND_MAX_PORTS];
    unsigned controller_port_count;

    /* Disk control (SET_DISK_CONTROL_EXT_INTERFACE).
     * Both the legacy and ext callbacks are stored; helpers always prefer
     * the ext-only fields when available. has_disk_control means at least
     * one of the basic eject/index callbacks is wired. */
    struct retro_disk_control_ext_callback disk_control;
    bool has_disk_control;

    /* Initial disk index requested via --disk-index. Applied after the
     * core registers its disk control interface. -1 means "no override". */
    int initial_disk_index;

    /* Optional player name reported through GET_USERNAME. NULL when unset. */
    char *username;

    /* Language reported through GET_LANGUAGE (defaults to ENGLISH). */
    enum retro_language language;

    /* Directory hints set via CLI, returned from the corresponding
     * RETRO_ENVIRONMENT_GET_*_DIRECTORY callbacks. NULL = unset. */
    char *core_assets_directory;
    char *playlist_directory;
    char *file_browser_directory;

    /* SET_SUPPORT_ACHIEVEMENTS: core claims achievement support. We don't
     * implement cheevos, but acknowledging it lets cores use the right path. */
    bool core_supports_achievements;

    /* Audio CLI overrides --audio-rate <Hz> (0 = no override) and
     * --audio-buffer-ms <ms> (0 = no override). Applied after the core
     * reports AV info. */
    unsigned audio_rate_override;
    unsigned audio_buffer_ms_override;

    /* Core proc address callback (SET_PROC_ADDRESS_CALLBACK).
     * Stores the function pointer for optional future use; we do not
     * currently define any core extension symbols. */
    retro_get_proc_address_t get_proc_address;

    /* Memory map (SET_MEMORY_MAPS).
     * Deep-copied: descriptors array + addrspace strings are owned by
     * the frontend. Freed in core_unload. */
    struct retro_memory_descriptor *memory_descriptors;
    char **memory_addrspace_strings;
    unsigned memory_descriptor_count;

    /* Subsystem info (SET_SUBSYSTEM_INFO).
     * Deep-copied array; all strings and sub-arrays are owned by the
     * frontend. Freed in core_unload. */
    struct subsystem_storage *subsystem_info;
    unsigned subsystem_info_count;

    /* --subsystem <ident> CLI: when set, core_init calls
     * retro_load_game_special() instead of retro_load_game().
     * The ident is matched against subsystem_info[].ident. NULL = no
     * subsystem selected (regular load). */
    const char *subsystem_ident;

    /* Fast-forward state.
     * fast_forward_active: frontend is currently skipping frame delays.
     * ff_override_active: core has an active override request.
     * ff_inhibit_toggle: core prohibits manual toggle of fast-forward. */
    bool fast_forward_active;
    bool ff_override_active;
    bool ff_inhibit_toggle;

    /* Content info overrides (SET_CONTENT_INFO_OVERRIDE).
     * Deep-copied array; strings owned by frontend. Freed in core_unload. */
    struct content_info_override_storage *content_overrides;
    unsigned content_override_count;

    /* Extended game info (GET_GAME_INFO_EXT).
     * Populated by core_init before retro_load_game; the struct and its
     * backing strings (dir/name/ext) remain valid until core_unload.
     * Only valid inside retro_load_game() or retro_load_game_special(). */
    struct retro_game_info_ext game_info_ext;
    char *game_info_ext_dir;
    char *game_info_ext_name;
    char *game_info_ext_ext;

    /* Frame-time callback (SET_FRAME_TIME_CALLBACK).
     * If callback is non-NULL, run_loop invokes it once per frame before
     * retro_run() with the actual microseconds since the previous call,
     * or `reference` on the first frame. */
    retro_frame_time_callback_t frame_time_callback;
    retro_usec_t frame_time_reference;

    /* SRAM persistence path (.srm file).
     * Computed once in main() from save_directory + content basename.
     * On startup, contents are loaded into RETRO_MEMORY_SAVE_RAM.
     * On shutdown, the current SRAM is written back. NULL when content
     * is absent or save_directory cannot be resolved. Owned by frontend. */
    char *sram_path;

    /* Savestate auto-load path passed via --savestate <file>.
     * Loaded once after core_init via retro_unserialize. Not owned. */
    const char *savestate_load_path;
};

extern struct frontend_state g_frontend;

/* Convert enum video_renderer to a short human-readable string. */
static inline const char *renderer_name(enum video_renderer r)
{
    switch (r) {
    case VIDEO_RENDERER_NONE:   return "none";
    case VIDEO_RENDERER_SW:     return "sw";
    case VIDEO_RENDERER_OPENGL: return "gl";
    case VIDEO_RENDERER_VULKAN: return "vk";
    }
    return "?";
}

/* Compute a centered destination rectangle of (out_w, out_h) within
 * (dst_w, dst_h) that preserves the source aspect ratio of (src_w, src_h).
 * The result is written to *out_x, *out_y, *out_w, *out_h. */
static inline void fit_aspect(unsigned src_w, unsigned src_h,
                              int dst_w, int dst_h,
                              int *out_x, int *out_y,
                              int *out_w, int *out_h)
{
    float src_aspect = (float)src_w / (float)src_h;
    float dst_aspect = (float)dst_w / (float)dst_h;

    if (src_aspect > dst_aspect) {
        *out_w = dst_w;
        *out_h = (int)((float)dst_w / src_aspect);
        *out_x = 0;
        *out_y = (dst_h - *out_h) / 2;
    } else {
        *out_h = dst_h;
        *out_w = (int)((float)dst_h * src_aspect);
        *out_x = (dst_w - *out_w) / 2;
        *out_y = 0;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* FRONTEND_H */
