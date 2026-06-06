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

    /* Core variables (SET_VARIABLES / GET_VARIABLE).
     * Stores the key and the raw value string (description; default|opt1|...).
     * User overrides are stored separately so they survive SET_VARIABLES resets.
     *
     * Lookup order in GET_VARIABLE: cli_overrides -> disk_overrides -> default.
     * cli_overrides come from --variable CLI flags and are never persisted.
     * disk_overrides are loaded from / saved to a per-core .opt file. */
    struct variable_table variables;
    struct variable_table disk_overrides;
    struct variable_table cli_overrides;
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
