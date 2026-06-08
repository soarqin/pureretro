/*
 * PureRetro — OpenGL hardware renderer
 *
 * OpenGL context management via SDL3.
 */

#ifndef VIDEO_GL_H
#define VIDEO_GL_H

#include "frontend.h"

#include "libretro.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct video_gl_context
{
    SDL_GLContext gl_context;

    /* Framebuffer object managed by the frontend for get_current_framebuffer */
    unsigned fbo;
    unsigned fbo_width;
    unsigned fbo_height;
    unsigned fbo_texture;
    unsigned fbo_depth_rb;     /* depth or combined depth+stencil renderbuffer */
    unsigned fbo_stencil_rb;   /* separate stencil renderbuffer (if needed) */

    /* Cached proc addresses */
    retro_hw_get_proc_address_t get_proc_address;

    /* Cached hot-path GL function pointers, resolved once at init time.
     * Typed as the generic libretro proc address to avoid leaking GL
     * typedefs into this header; the implementation casts back to the
     * correct PFNGL*PROC types. */
    retro_proc_address_t fn_bind_framebuffer;
    retro_proc_address_t fn_blit_framebuffer;
    retro_proc_address_t fn_clear_color;
    retro_proc_address_t fn_clear;
    retro_proc_address_t fn_viewport;

    bool cache_context;          /* copied from hw->cache_context */
    bool bottom_left_origin;     /* copied from hw->bottom_left_origin */
};

/* Initialize the OpenGL renderer for the given window.
 * 'hw' describes the core's requested GL context parameters. */
bool video_gl_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_gl_context **out_ctx);

/* Destroy the OpenGL renderer and all GL resources. */
void video_gl_destroy(struct video_gl_context *ctx);

/* Resize the FBO (and its attachments) to new dimensions.
 * Destroys and recreates the FBO if the size changed. */
bool video_gl_resize(struct video_gl_context *ctx, unsigned width, unsigned height);

/* Called after the GL context is created or reset. */
void video_gl_context_reset(struct video_gl_context *ctx);

/* Called before the GL context is destroyed. */
void video_gl_context_destroy(struct video_gl_context *ctx);

/* Present a hardware-rendered frame by blitting the rendered region
 * to the window and swapping. */
void video_gl_present(struct video_gl_context *ctx, unsigned width, unsigned height);

/* Return the current framebuffer (FBO name). */
uintptr_t video_gl_get_current_framebuffer(struct video_gl_context *ctx);

/* Resolve an OpenGL symbol. */
retro_proc_address_t video_gl_get_proc_address(struct video_gl_context *ctx,
                                                const char *sym);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_GL_H */
