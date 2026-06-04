/*
 * PureRetro — OpenGL hardware renderer
 *
 * Manages an SDL3 OpenGL context and an FBO for core rendering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include "video_gl.h"
#include "core.h"

/* APIENTRY may not be defined on all platforms */
#ifndef APIENTRY
#define APIENTRY
#endif

/* Core OpenGL 1.1 function typedefs not provided by SDL_opengl_glext.h */
typedef void (APIENTRY *PFNGLGENTEXTURESPROC)(GLsizei n, GLuint *textures);
typedef void (APIENTRY *PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void (APIENTRY *PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat,
                                               GLsizei width, GLsizei height, GLint border,
                                               GLenum format, GLenum type, const void *pixels);
typedef void (APIENTRY *PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void (APIENTRY *PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint *textures);

#define GLPROC(name) ((name)ctx->get_proc_address(#name))

static bool gl_fbo_create(struct video_gl_context *ctx, unsigned width, unsigned height)
{
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = GLPROC(PFNGLGENFRAMEBUFFERSPROC);
    PFNGLGENTEXTURESPROC glGenTextures = GLPROC(PFNGLGENTEXTURESPROC);
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = GLPROC(PFNGLBINDFRAMEBUFFERPROC);
    PFNGLBINDTEXTUREPROC glBindTexture = GLPROC(PFNGLBINDTEXTUREPROC);
    PFNGLTEXIMAGE2DPROC glTexImage2D = GLPROC(PFNGLTEXIMAGE2DPROC);
    PFNGLTEXPARAMETERIPROC glTexParameteri = GLPROC(PFNGLTEXPARAMETERIPROC);
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = GLPROC(PFNGLFRAMEBUFFERTEXTURE2DPROC);

    if (!glGenFramebuffers || !glGenTextures || !glBindFramebuffer ||
        !glBindTexture || !glTexImage2D || !glTexParameteri || !glFramebufferTexture2D) {
        fprintf(stderr, "OpenGL: Failed to load required FBO functions\n");
        return false;
    }

    glGenFramebuffers(1, &ctx->fbo);
    glGenTextures(1, &ctx->fbo_texture);

    glBindTexture(GL_TEXTURE_2D, ctx->fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 (GLsizei)width, (GLsizei)height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, ctx->fbo_texture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ctx->fbo_width = width;
    ctx->fbo_height = height;

    return true;
}

static void gl_fbo_destroy(struct video_gl_context *ctx)
{
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = GLPROC(PFNGLDELETEFRAMEBUFFERSPROC);
    PFNGLDELETETEXTURESPROC glDeleteTextures = GLPROC(PFNGLDELETETEXTURESPROC);

    if (glDeleteFramebuffers && ctx->fbo)
        glDeleteFramebuffers(1, &ctx->fbo);

    if (glDeleteTextures && ctx->fbo_texture)
        glDeleteTextures(1, &ctx->fbo_texture);

    ctx->fbo = 0;
    ctx->fbo_texture = 0;
    ctx->fbo_width = 0;
    ctx->fbo_height = 0;
}

bool video_gl_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_gl_context **out_ctx)
{
    struct video_gl_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return false;

    /* Set GL attributes before creating the context. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, (int)hw->version_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, (int)hw->version_minor);

    if (hw->context_type == RETRO_HW_CONTEXT_OPENGL_CORE ||
        hw->context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    } else if (hw->context_type == RETRO_HW_CONTEXT_OPENGLES2 ||
               hw->context_type == RETRO_HW_CONTEXT_OPENGLES3) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    }

    if (hw->debug_context)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

    ctx->gl_context = SDL_GL_CreateContext(window);
    if (!ctx->gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        free(ctx);
        return false;
    }

    /* Enable vsync. */
    SDL_GL_SetSwapInterval(1);

    ctx->get_proc_address = (retro_hw_get_proc_address_t)SDL_GL_GetProcAddress;
    if (!ctx->get_proc_address) {
        fprintf(stderr, "SDL_GL_GetProcAddress unavailable\n");
        SDL_GL_DestroyContext(ctx->gl_context);
        free(ctx);
        return false;
    }

    /* Create an FBO for the core to render into. */
    if (!gl_fbo_create(ctx, g_av_info.geometry.max_width, g_av_info.geometry.max_height)) {
        SDL_GL_DestroyContext(ctx->gl_context);
        free(ctx);
        return false;
    }

    *out_ctx = ctx;

    /* Notify the core that the context is ready. */
    if (hw->context_reset)
        hw->context_reset();

    return true;
}

void video_gl_destroy(struct video_gl_context *ctx)
{
    if (!ctx)
        return;

    if (ctx->fbo)
        gl_fbo_destroy(ctx);

    if (ctx->gl_context)
        SDL_GL_DestroyContext(ctx->gl_context);

    free(ctx);
}

void video_gl_context_reset(struct video_gl_context *ctx)
{
    (void)ctx;
    /* Context reset is handled by the hw callback in video_gl_init. */
}

void video_gl_context_destroy(struct video_gl_context *ctx)
{
    (void)ctx;
    /* TODO: Notify the core via hw->context_destroy if set. */
}

void video_gl_present(struct video_gl_context *ctx)
{
    (void)ctx;
    /* The core has rendered into our FBO; just swap the window. */
    SDL_GL_SwapWindow(g_frontend.video.window);
}

uintptr_t video_gl_get_current_framebuffer(struct video_gl_context *ctx)
{
    return ctx ? ctx->fbo : 0;
}

retro_proc_address_t video_gl_get_proc_address(struct video_gl_context *ctx,
                                                const char *sym)
{
    if (!ctx || !ctx->get_proc_address)
        return NULL;

    return ctx->get_proc_address(sym);
}
