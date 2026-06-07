/*
 * PureRetro — Software renderer
 *
 * SDL3 texture-based software rendering path.
 */

#ifndef VIDEO_SW_H
#define VIDEO_SW_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

struct video_sw_context
{
    SDL_Renderer *renderer;
    SDL_Texture  *texture;

    /* Cached texture properties (SDL_Texture is opaque in SDL3) */
    int          texture_width;
    int          texture_height;
    SDL_PixelFormat texture_format;

    /* Zero-copy framebuffer state for GET_CURRENT_SOFTWARE_FRAMEBUFFER.
     * When the core requests a frontend-owned framebuffer, we lock the
     * streaming texture and hand the raw pixel pointer back. The core
     * is then expected to call retro_video_refresh with the same
     * pointer; video_sw_present detects the match and skips the
     * UpdateTexture copy, unlocking the texture instead. */
    bool         locked;
    void        *locked_pixels;
    int          locked_pitch;
    int          locked_width;
    int          locked_height;
    SDL_PixelFormat locked_format;
};

/* Initialize the software renderer for the given window. */
bool video_sw_init(SDL_Window *window, struct video_sw_context **out_ctx);

/* Destroy the software renderer and free resources. */
void video_sw_destroy(struct video_sw_context *ctx);

/* Present a software frame.
 * 'data' points to raw pixel data in the negotiated pixel format.
 * The renderer may need to convert to a texture-friendly format. */
void video_sw_present(struct video_sw_context *ctx, const void *data,
                      unsigned width, unsigned height, size_t pitch,
                      enum retro_pixel_format fmt);

/* Lock the streaming texture and expose its pixel buffer to the core
 * for direct (zero-copy) rendering. The buffer remains valid until
 * the next call to video_sw_present or video_sw_destroy.
 * Returns false on size/format mismatch or if locking is unsupported
 * for the requested format. */
bool video_sw_get_framebuffer(struct video_sw_context *ctx,
                              unsigned width, unsigned height,
                              enum retro_pixel_format fmt,
                              void **out_data, size_t *out_pitch);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_SW_H */
