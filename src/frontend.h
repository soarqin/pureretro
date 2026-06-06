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

/* Forward declarations for renderer-specific contexts */
struct video_sw_context;
struct video_gl_context;
struct video_vk_context;

/* Video state shared across renderers */
struct video_state
{
    SDL_Window *window;
    enum video_renderer renderer;

    /* Software renderer context */
    struct video_sw_context *sw;

    /* OpenGL renderer context */
    struct video_gl_context *gl;

    /* Vulkan renderer context */
    struct video_vk_context *vk;

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

    /* Input state (bitmask for RetroPad buttons) */
    uint16_t joypad_state[RETRO_DEVICE_ID_JOYPAD_MASK + 1];

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
    struct retro_variable *variables;
    size_t variable_count;
    size_t variable_capacity;
    struct retro_variable *disk_overrides;
    size_t disk_override_count;
    size_t disk_override_capacity;
    struct retro_variable *cli_overrides;
    size_t cli_override_count;
    size_t cli_override_capacity;
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

#ifdef __cplusplus
}
#endif

#endif /* FRONTEND_H */
