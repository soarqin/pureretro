/*
 * PureRetro — Software renderer
 *
 * Blits core pixel data to an SDL3 texture.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "video_sw.h"

bool video_sw_init(SDL_Window *window, struct video_sw_context **out_ctx)
{
    struct video_sw_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return false;

    ctx->renderer = SDL_CreateRenderer(window, NULL);
    if (!ctx->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        free(ctx);
        return false;
    }

    SDL_SetRenderVSync(ctx->renderer, 1);

    *out_ctx = ctx;
    return true;
}

void video_sw_destroy(struct video_sw_context *ctx)
{
    if (!ctx)
        return;

    if (ctx->convert_surface)
        SDL_DestroySurface(ctx->convert_surface);

    if (ctx->texture)
        SDL_DestroyTexture(ctx->texture);

    if (ctx->renderer)
        SDL_DestroyRenderer(ctx->renderer);

    free(ctx);
}

void video_sw_present(struct video_sw_context *ctx, const void *data,
                      unsigned width, unsigned height, size_t pitch,
                      enum retro_pixel_format fmt)
{
    SDL_PixelFormat sdl_fmt;

    if (!ctx || !ctx->renderer)
        return;

    /* Map libretro pixel format to SDL pixel format. */
    switch (fmt) {
    case RETRO_PIXEL_FORMAT_0RGB1555:
        sdl_fmt = SDL_PIXELFORMAT_XRGB1555;
        break;
    case RETRO_PIXEL_FORMAT_XRGB8888:
        sdl_fmt = SDL_PIXELFORMAT_XRGB8888;
        break;
    case RETRO_PIXEL_FORMAT_RGB565:
        sdl_fmt = SDL_PIXELFORMAT_RGB565;
        break;
    default:
        fprintf(stderr, "Unsupported pixel format: %d\n", fmt);
        return;
    }

    /* Recreate the texture if dimensions or format changed. */
    if (!ctx->texture ||
        ctx->texture_width != (int)width ||
        ctx->texture_height != (int)height ||
        ctx->texture_format != sdl_fmt) {

        if (ctx->texture)
            SDL_DestroyTexture(ctx->texture);

        ctx->texture = SDL_CreateTexture(ctx->renderer, sdl_fmt,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          (int)width, (int)height);
        if (!ctx->texture) {
            fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
            return;
        }

        ctx->texture_width = (int)width;
        ctx->texture_height = (int)height;
        ctx->texture_format = sdl_fmt;
    }

    /* Update texture with pixel data. */
    if (data) {
        if (!SDL_UpdateTexture(ctx->texture, NULL, data, (int)pitch)) {
            fprintf(stderr, "SDL_UpdateTexture failed: %s\n", SDL_GetError());
            return;
        }
    }

    /* Clear and present. */
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderTexture(ctx->renderer, ctx->texture, NULL, NULL);
    SDL_RenderPresent(ctx->renderer);
}
