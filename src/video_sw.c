/*
 * PureRetro — Software renderer
 *
 * Blits core pixel data to an SDL3 texture.
 */

#include "video_sw.h"
#include "video_backend.h"
#include "log.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool video_sw_init(SDL_Window *window, struct video_sw_context **out_ctx)
{
    struct video_sw_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return false;

    ctx->renderer = SDL_CreateRenderer(window, NULL);
    if (!ctx->renderer) {
        LOG_ERROR("SDL_CreateRenderer failed: %s", SDL_GetError());
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

    if (ctx->locked && ctx->texture) {
        SDL_UnlockTexture(ctx->texture);
        ctx->locked = false;
    }

    if (ctx->texture)
        SDL_DestroyTexture(ctx->texture);

    if (ctx->renderer)
        SDL_DestroyRenderer(ctx->renderer);

    free(ctx);
}

/* Map a libretro pixel format to its SDL equivalent. Returns SDL_PIXELFORMAT_UNKNOWN
 * when the format cannot be represented. */
static SDL_PixelFormat sdl_format_for(enum retro_pixel_format fmt)
{
    switch (fmt) {
    case RETRO_PIXEL_FORMAT_0RGB1555: return SDL_PIXELFORMAT_XRGB1555;
    case RETRO_PIXEL_FORMAT_XRGB8888: return SDL_PIXELFORMAT_XRGB8888;
    case RETRO_PIXEL_FORMAT_RGB565:   return SDL_PIXELFORMAT_RGB565;
    default:                          return SDL_PIXELFORMAT_UNKNOWN;
    }
}

/* (Re)create the streaming texture if dimensions or format changed. */
static bool ensure_texture(struct video_sw_context *ctx, unsigned width,
                           unsigned height, SDL_PixelFormat sdl_fmt)
{
    if (ctx->texture &&
        ctx->texture_width == (int)width &&
        ctx->texture_height == (int)height &&
        ctx->texture_format == sdl_fmt)
        return true;

    if (ctx->texture) {
        if (ctx->locked) {
            SDL_UnlockTexture(ctx->texture);
            ctx->locked = false;
        }
        SDL_DestroyTexture(ctx->texture);
        ctx->texture = NULL;
    }

    ctx->texture = SDL_CreateTexture(ctx->renderer, sdl_fmt,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      (int)width, (int)height);
    if (!ctx->texture) {
        LOG_ERROR("SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }

    ctx->texture_width = (int)width;
    ctx->texture_height = (int)height;
    ctx->texture_format = sdl_fmt;
    return true;
}

void video_sw_present(struct video_sw_context *ctx, const void *data,
                      unsigned width, unsigned height, size_t pitch,
                      enum retro_pixel_format fmt)
{
    SDL_PixelFormat sdl_fmt;

    if (!ctx || !ctx->renderer)
        return;

    sdl_fmt = sdl_format_for(fmt);
    if (sdl_fmt == SDL_PIXELFORMAT_UNKNOWN) {
        LOG_ERROR("Unsupported pixel format: %d", fmt);
        return;
    }

    if (!ensure_texture(ctx, width, height, sdl_fmt))
        return;

    /* Zero-copy fast path: the core handed back the pointer we previously
     * gave it via video_sw_get_framebuffer. Just unlock the texture and
     * skip the UpdateTexture copy. We still verify the size/format match
     * the lock so a stale buffer doesn't sneak through. */
    if (ctx->locked && data == ctx->locked_pixels &&
        (int)width == ctx->locked_width &&
        (int)height == ctx->locked_height &&
        sdl_fmt == ctx->locked_format) {
        SDL_UnlockTexture(ctx->texture);
        ctx->locked = false;
        ctx->locked_pixels = NULL;
    } else {
        /* If a previous lock was never followed by a refresh with the
         * locked pointer (core changed its mind), drop it now. */
        if (ctx->locked) {
            SDL_UnlockTexture(ctx->texture);
            ctx->locked = false;
            ctx->locked_pixels = NULL;
        }

        if (data) {
            if (!SDL_UpdateTexture(ctx->texture, NULL, data, (int)pitch)) {
                LOG_ERROR("SDL_UpdateTexture failed: %s", SDL_GetError());
                return;
            }
        }
    }

    /* Clear and present with aspect ratio preservation. */
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);

    int win_w, win_h;
    SDL_GetRenderOutputSize(ctx->renderer, &win_w, &win_h);

    /* Honor the rotation requested by the core via SET_ROTATION.
     * For 90/270 the source aspect is effectively swapped, so fit_aspect
     * sees swapped dimensions. SDL_RenderTextureRotated then rotates
     * the texture by `angle` degrees clockwise around the dst center. */
    unsigned rot = g_frontend.video.rotation & 3;
    unsigned eff_w = (rot == 1 || rot == 3) ? height : width;
    unsigned eff_h = (rot == 1 || rot == 3) ? width  : height;

    int dst_x, dst_y, dst_w, dst_h;
    fit_aspect(eff_w, eff_h, win_w, win_h, &dst_x, &dst_y, &dst_w, &dst_h);

    SDL_FRect dst;
    dst.x = (float)dst_x;
    dst.y = (float)dst_y;
    dst.w = (float)dst_w;
    dst.h = (float)dst_h;

    if (rot == 0) {
        SDL_RenderTexture(ctx->renderer, ctx->texture, NULL, &dst);
    } else {
        /* For 90/270 the dst rect we feed RenderTextureRotated must be the
         * post-rotation footprint expressed with the ORIGINAL aspect, since
         * SDL rotates the *texture* into that rect. Swap back for the call. */
        SDL_FRect tex_dst = dst;
        if (rot == 1 || rot == 3) {
            tex_dst.x = (float)(dst_x + (dst_w - dst_h) / 2);
            tex_dst.y = (float)(dst_y + (dst_h - dst_w) / 2);
            tex_dst.w = (float)dst_h;
            tex_dst.h = (float)dst_w;
        }
        /* SDL angles are clockwise; libretro rotation=1 is CCW (270 CW) */
        static const double rot_deg[4] = { 0.0, 270.0, 180.0, 90.0 };
        SDL_RenderTextureRotated(ctx->renderer, ctx->texture, NULL, &tex_dst,
                                 rot_deg[rot], NULL, SDL_FLIP_NONE);
    }
    SDL_RenderPresent(ctx->renderer);
}

bool video_sw_get_framebuffer(struct video_sw_context *ctx,
                              unsigned width, unsigned height,
                              enum retro_pixel_format fmt,
                              void **out_data, size_t *out_pitch)
{
    if (!ctx || !ctx->renderer || !out_data || !out_pitch)
        return false;

    SDL_PixelFormat sdl_fmt = sdl_format_for(fmt);
    if (sdl_fmt == SDL_PIXELFORMAT_UNKNOWN)
        return false;

    if (!ensure_texture(ctx, width, height, sdl_fmt))
        return false;

    /* Only one outstanding lock per frame: re-locking would double-map
     * the same texture. If the core asks twice without presenting in
     * between, return the existing lock to keep the contract simple. */
    if (ctx->locked) {
        *out_data = ctx->locked_pixels;
        *out_pitch = (size_t)ctx->locked_pitch;
        return true;
    }

    void *pixels = NULL;
    int pitch = 0;
    if (!SDL_LockTexture(ctx->texture, NULL, &pixels, &pitch)) {
        LOG_ERROR("SDL_LockTexture failed: %s", SDL_GetError());
        return false;
    }

    ctx->locked = true;
    ctx->locked_pixels = pixels;
    ctx->locked_pitch = pitch;
    ctx->locked_width = (int)width;
    ctx->locked_height = (int)height;
    ctx->locked_format = sdl_fmt;

    *out_data = pixels;
    *out_pitch = (size_t)pitch;
    return true;
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
