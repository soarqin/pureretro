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
    SDL_Surface  *convert_surface; /* For pixel format conversion */

    /* Cached texture properties (SDL_Texture is opaque in SDL3) */
    int          texture_width;
    int          texture_height;
    SDL_PixelFormat texture_format;
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

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_SW_H */
