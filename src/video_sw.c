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
#include "video_backend.h"

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

    /* Clear and present with aspect ratio preservation. */
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);

    int win_w, win_h;
    SDL_GetRenderOutputSize(ctx->renderer, &win_w, &win_h);

    int dst_x, dst_y, dst_w, dst_h;
    fit_aspect(width, height, win_w, win_h, &dst_x, &dst_y, &dst_w, &dst_h);

    SDL_FRect dst;
    dst.x = (float)dst_x;
    dst.y = (float)dst_y;
    dst.w = (float)dst_w;
    dst.h = (float)dst_h;

    SDL_RenderTexture(ctx->renderer, ctx->texture, NULL, &dst);
    SDL_RenderPresent(ctx->renderer);
}

/* ----- video_backend vtable adapters ----- */

static bool vb_sw_match(enum retro_hw_context_type type)
{
    return type == RETRO_HW_CONTEXT_NONE;
}

static SDL_WindowFlags vb_sw_window_flags(void)
{
    return 0;
}

static bool vb_sw_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                       void **out_ctx)
{
    (void)hw;
    struct video_sw_context *ctx = NULL;
    if (!video_sw_init(window, &ctx))
        return false;
    *out_ctx = ctx;
    return true;
}

static void vb_sw_destroy(void *ctx)
{
    video_sw_destroy((struct video_sw_context *)ctx);
}

static void vb_sw_present(void *ctx, const void *data, unsigned width,
                          unsigned height, size_t pitch,
                          enum retro_pixel_format fmt)
{
    video_sw_present((struct video_sw_context *)ctx, data, width, height,
                     pitch, fmt);
}

static bool vb_sw_resize(void *ctx, SDL_Window *window,
                         unsigned width, unsigned height)
{
    (void)ctx;
    (void)window;
    (void)width;
    (void)height;
    return true;
}

static uintptr_t vb_sw_get_current_framebuffer(void *ctx)
{
    (void)ctx;
    return 0;
}

static retro_proc_address_t vb_sw_get_proc_address(void *ctx, const char *sym)
{
    (void)ctx;
    (void)sym;
    return NULL;
}

static bool vb_sw_negotiate_device(void *ctx,
    const struct retro_hw_render_context_negotiation_interface *iface)
{
    (void)ctx;
    (void)iface;
    return false;
}

static bool vb_sw_get_hw_render_interface(void *ctx,
    const struct retro_hw_render_interface **out_iface)
{
    (void)ctx;
    (void)out_iface;
    return false;
}

static void vb_sw_context_destroy(void *ctx)
{
    (void)ctx;
}

const struct video_backend sw_backend = {
    .name                    = "sw",
    .id                      = VIDEO_RENDERER_SW,
    .match_hw_context        = vb_sw_match,
    .window_flags            = vb_sw_window_flags,
    .init                    = vb_sw_init,
    .destroy                 = vb_sw_destroy,
    .present                 = vb_sw_present,
    .resize                  = vb_sw_resize,
    .get_current_framebuffer = vb_sw_get_current_framebuffer,
    .get_proc_address        = vb_sw_get_proc_address,
    .negotiate_device        = vb_sw_negotiate_device,
    .get_hw_render_interface = vb_sw_get_hw_render_interface,
    .context_destroy         = vb_sw_context_destroy,
};
