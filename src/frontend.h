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

    /* User-requested renderer preference (VIDEO_RENDERER_NONE = no preference).
     * Set via the --render CLI flag. The core may still request a different
     * renderer; this is a hint, not a hard requirement. */
    enum video_renderer preferred_renderer;

    struct video_state video;

    /* Audio stream handle */
    SDL_AudioStream *audio_stream;

    /* Input state (bitmask for RetroPad buttons) */
    uint16_t joypad_state[RETRO_DEVICE_ID_JOYPAD_MASK + 1];

    /* Core paths */
    const char *core_path;
    const char *content_path;

    /* Loaded ROM data (owned by frontend, freed on shutdown) */
    void *rom_data;
    size_t rom_size;
};

extern struct frontend_state g_frontend;

#ifdef __cplusplus
}
#endif

#endif /* FRONTEND_H */
