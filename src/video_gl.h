/*
 * PureRetro — OpenGL hardware renderer
 *
 * OpenGL context management via SDL3.
 */

#ifndef VIDEO_GL_H
#define VIDEO_GL_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "libretro.h"
#include "frontend.h"

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
};

/* Initialize the OpenGL renderer for the given window.
 * 'hw' describes the core's requested GL context parameters. */
bool video_gl_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_gl_context **out_ctx);

/* Destroy the OpenGL renderer and all GL resources. */
void video_gl_destroy(struct video_gl_context *ctx);

/* Called after the GL context is created or reset. */
void video_gl_context_reset(struct video_gl_context *ctx);

/* Called before the GL context is destroyed. */
void video_gl_context_destroy(struct video_gl_context *ctx);

/* Present a hardware-rendered frame by swapping the window. */
void video_gl_present(struct video_gl_context *ctx);

/* Return the current framebuffer (FBO name). */
uintptr_t video_gl_get_current_framebuffer(struct video_gl_context *ctx);

/* Resolve an OpenGL symbol. */
retro_proc_address_t video_gl_get_proc_address(struct video_gl_context *ctx,
                                                const char *sym);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_GL_H */
