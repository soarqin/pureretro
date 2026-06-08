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
#include "video_backend.h"
#include "core.h"
#include "log.h"

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
typedef void (APIENTRY *PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (APIENTRY *PFNGLCLEARPROC)(GLbitfield mask);
typedef void (APIENTRY *PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);

/* Aliases so the GLPROC macro can use mixed-case token pasting. */
typedef PFNGLGENTEXTURESPROC PFNGLGenTexturesPROC;
typedef PFNGLBINDTEXTUREPROC PFNGLBindTexturePROC;
typedef PFNGLTEXIMAGE2DPROC PFNGLTexImage2DPROC;
typedef PFNGLTEXPARAMETERIPROC PFNGLTexParameteriPROC;
typedef PFNGLDELETETEXTURESPROC PFNGLDeleteTexturesPROC;
typedef PFNGLGENFRAMEBUFFERSPROC PFNGLGenFramebuffersPROC;
typedef PFNGLBINDFRAMEBUFFERPROC PFNGLBindFramebufferPROC;
typedef PFNGLFRAMEBUFFERTEXTURE2DPROC PFNGLFramebufferTexture2DPROC;
typedef PFNGLDELETEFRAMEBUFFERSPROC PFNGLDeleteFramebuffersPROC;
typedef PFNGLGENRENDERBUFFERSPROC PFNGLGenRenderbuffersPROC;
typedef PFNGLBINDRENDERBUFFERPROC PFNGLBindRenderbufferPROC;
typedef PFNGLRENDERBUFFERSTORAGEPROC PFNGLRenderbufferStoragePROC;
typedef PFNGLFRAMEBUFFERRENDERBUFFERPROC PFNGLFramebufferRenderbufferPROC;
typedef PFNGLCHECKFRAMEBUFFERSTATUSPROC PFNGLCheckFramebufferStatusPROC;
typedef PFNGLDELETERENDERBUFFERSPROC PFNGLDeleteRenderbuffersPROC;
typedef PFNGLCLEARCOLORPROC PFNGLClearColorPROC;
typedef PFNGLCLEARPROC PFNGLClearPROC;
typedef PFNGLVIEWPORTPROC PFNGLViewportPROC;
typedef PFNGLBLITFRAMEBUFFERPROC PFNGLBlitFramebufferPROC;

#define GLPROC(name) ((PFNGL##name##PROC)ctx->get_proc_address("gl" #name))

static void gl_fbo_destroy(struct video_gl_context *ctx);

static bool gl_fbo_create(struct video_gl_context *ctx, unsigned width, unsigned height,
                            bool need_depth, bool need_stencil)
{
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = GLPROC(GenFramebuffers);
    PFNGLGENTEXTURESPROC glGenTextures = GLPROC(GenTextures);
    PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers = GLPROC(GenRenderbuffers);
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = GLPROC(BindFramebuffer);
    PFNGLBINDTEXTUREPROC glBindTexture = GLPROC(BindTexture);
    PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer = GLPROC(BindRenderbuffer);
    PFNGLTEXIMAGE2DPROC glTexImage2D = GLPROC(TexImage2D);
    PFNGLTEXPARAMETERIPROC glTexParameteri = GLPROC(TexParameteri);
    PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage = GLPROC(RenderbufferStorage);
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = GLPROC(FramebufferTexture2D);
    PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = GLPROC(FramebufferRenderbuffer);
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = GLPROC(CheckFramebufferStatus);

    if (!glGenFramebuffers || !glGenTextures || !glBindFramebuffer ||
        !glBindTexture || !glTexImage2D || !glTexParameteri ||
        !glFramebufferTexture2D || !glCheckFramebufferStatus) {
        LOG_ERROR("OpenGL: Failed to load required FBO functions");
        return false;
    }

    glGenFramebuffers(1, &ctx->fbo);
    glGenTextures(1, &ctx->fbo_texture);

    glBindTexture(GL_TEXTURE_2D, ctx->fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 (GLsizei)width, (GLsizei)height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, ctx->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, ctx->fbo_texture, 0);

    if (need_depth || need_stencil) {
        if (!glGenRenderbuffers || !glBindRenderbuffer ||
            !glRenderbufferStorage || !glFramebufferRenderbuffer) {
            LOG_ERROR("OpenGL: Failed to load renderbuffer functions");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            gl_fbo_destroy(ctx);
            return false;
        }
    }

    if (need_depth && need_stencil) {
        glGenRenderbuffers(1, &ctx->fbo_depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, ctx->fbo_depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                              (GLsizei)width, (GLsizei)height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, ctx->fbo_depth_rb);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, ctx->fbo_depth_rb);
    } else if (need_depth) {
        glGenRenderbuffers(1, &ctx->fbo_depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, ctx->fbo_depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              (GLsizei)width, (GLsizei)height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, ctx->fbo_depth_rb);
    } else if (need_stencil) {
        glGenRenderbuffers(1, &ctx->fbo_stencil_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, ctx->fbo_stencil_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                              (GLsizei)width, (GLsizei)height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, ctx->fbo_stencil_rb);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("OpenGL: FBO incomplete (status 0x%x)", status);
        gl_fbo_destroy(ctx);
        return false;
    }

    ctx->fbo_width = width;
    ctx->fbo_height = height;

    return true;
}

static void gl_fbo_destroy(struct video_gl_context *ctx)
{
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = GLPROC(DeleteFramebuffers);
    PFNGLDELETETEXTURESPROC glDeleteTextures = GLPROC(DeleteTextures);
    PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers = GLPROC(DeleteRenderbuffers);

    if (glDeleteFramebuffers && ctx->fbo)
        glDeleteFramebuffers(1, &ctx->fbo);

    if (glDeleteTextures && ctx->fbo_texture)
        glDeleteTextures(1, &ctx->fbo_texture);

    if (glDeleteRenderbuffers && ctx->fbo_depth_rb)
        glDeleteRenderbuffers(1, &ctx->fbo_depth_rb);

    if (glDeleteRenderbuffers && ctx->fbo_stencil_rb)
        glDeleteRenderbuffers(1, &ctx->fbo_stencil_rb);

    ctx->fbo = 0;
    ctx->fbo_texture = 0;
    ctx->fbo_depth_rb = 0;
    ctx->fbo_stencil_rb = 0;
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

    switch (hw->context_type) {
    case RETRO_HW_CONTEXT_OPENGL:
#ifdef __APPLE__
        if (hw->version_major > 3 ||
            (hw->version_major == 3 && hw->version_minor >= 2)) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                                SDL_GL_CONTEXT_PROFILE_CORE);
        }
        /* else: macOS legacy 2.1 — no profile mask */
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
        break;

    case RETRO_HW_CONTEXT_OPENGL_CORE:
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);
        break;

    case RETRO_HW_CONTEXT_OPENGLES2:
    case RETRO_HW_CONTEXT_OPENGLES3:
    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
#ifdef __APPLE__
        LOG_ERROR("OpenGL ES not supported on macOS");
        free(ctx);
        return false;
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_ES);
#endif
        break;

    default:
        LOG_ERROR("Unsupported HW context type: %d", hw->context_type);
        free(ctx);
        return false;
    }

    if (hw->debug_context)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

    /* Honor RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT: cores using this hint
     * expect to be able to share resources (e.g. video-decode textures)
     * with the current GL context. SDL only consults this attribute at
     * creation time; setting it is a no-op when no current context exists. */
    if (g_frontend.video.hw_shared_context_requested) {
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    }

    ctx->gl_context = SDL_GL_CreateContext(window);
    if (!ctx->gl_context) {
        LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        free(ctx);
        return false;
    }

    if (!SDL_GL_MakeCurrent(window, ctx->gl_context)) {
        LOG_ERROR("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        SDL_GL_DestroyContext(ctx->gl_context);
        free(ctx);
        return false;
    }

    /* Enable vsync. */
    SDL_GL_SetSwapInterval(1);

    ctx->get_proc_address = (retro_hw_get_proc_address_t)SDL_GL_GetProcAddress;
    if (!ctx->get_proc_address) {
        LOG_ERROR("SDL_GL_GetProcAddress unavailable");
        SDL_GL_DestroyContext(ctx->gl_context);
        free(ctx);
        return false;
    }

    ctx->cache_context = hw->cache_context;
    ctx->bottom_left_origin = hw->bottom_left_origin;

    /* Resolve hot-path GL function pointers once. video_gl_present runs
     * every frame; re-resolving via SDL_GL_GetProcAddress per call wastes
     * cycles. The pointers are stored as void* in the header to avoid
     * leaking GL typedefs there. */
    ctx->fn_bind_framebuffer = (retro_proc_address_t)GLPROC(BindFramebuffer);
    ctx->fn_blit_framebuffer = (retro_proc_address_t)GLPROC(BlitFramebuffer);
    ctx->fn_clear_color      = (retro_proc_address_t)GLPROC(ClearColor);
    ctx->fn_clear            = (retro_proc_address_t)GLPROC(Clear);
    ctx->fn_viewport         = (retro_proc_address_t)GLPROC(Viewport);

    /* Create an FBO for the core to render into. */
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (!gl_fbo_create(ctx, (unsigned)w, (unsigned)h, hw->depth, hw->stencil)) {
        SDL_GL_DestroyContext(ctx->gl_context);
        free(ctx);
        return false;
    }

    *out_ctx = ctx;
    return true;
}

void video_gl_destroy(struct video_gl_context *ctx)
{
    if (!ctx)
        return;

    video_gl_context_destroy(ctx);

    if (ctx->fbo)
        gl_fbo_destroy(ctx);

    if (ctx->gl_context)
        SDL_GL_DestroyContext(ctx->gl_context);

    free(ctx);
}

bool video_gl_resize(struct video_gl_context *ctx, unsigned width, unsigned height)
{
    if (!ctx)
        return false;

    if (ctx->fbo_width == width && ctx->fbo_height == height)
        return true;

    gl_fbo_destroy(ctx);
    return gl_fbo_create(ctx, width, height,
                         g_frontend.video.hw.depth,
                         g_frontend.video.hw.stencil);
}

void video_gl_context_reset(struct video_gl_context *ctx)
{
    (void)ctx;

    if (g_frontend.video.hw.context_reset)
        g_frontend.video.hw.context_reset();
}

void video_gl_context_destroy(struct video_gl_context *ctx)
{
    if (!ctx)
        return;

    if (g_frontend.video.hw.context_destroy) {
        g_frontend.video.hw.context_destroy();
        /* Zero the pointer so video_gl_destroy (called after core_unload)
         * does not try to invoke code from an unloaded shared object. */
        g_frontend.video.hw.context_destroy = NULL;
    }
}

void video_gl_present(struct video_gl_context *ctx, unsigned width, unsigned height)
{
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)ctx->fn_bind_framebuffer;
    PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer = (PFNGLBLITFRAMEBUFFERPROC)ctx->fn_blit_framebuffer;
    PFNGLCLEARCOLORPROC glClearColor = (PFNGLCLEARCOLORPROC)ctx->fn_clear_color;
    PFNGLCLEARPROC glClear = (PFNGLCLEARPROC)ctx->fn_clear;
    PFNGLVIEWPORTPROC glViewport = (PFNGLVIEWPORTPROC)ctx->fn_viewport;

    if (!glBindFramebuffer) {
        LOG_WARN("video_gl_present: glBindFramebuffer not available");
        SDL_GL_SwapWindow(g_frontend.video.window);
        return;
    }

    if (!glBlitFramebuffer) {
        LOG_WARN("video_gl_present: glBlitFramebuffer not available, swapping only");
        SDL_GL_SwapWindow(g_frontend.video.window);
        return;
    }

    int w, h;
    SDL_GetWindowSizeInPixels(g_frontend.video.window, &w, &h);

    SDL_GL_MakeCurrent(g_frontend.video.window, ctx->gl_context);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx->fbo);

    /* Blit the rendered region (width x height) to a centered rectangle
     * that preserves aspect ratio. */
    int dst_x, dst_y, dst_w, dst_h;
    fit_aspect(width, height, w, h, &dst_x, &dst_y, &dst_w, &dst_h);

    GLint src_y0 = ctx->bottom_left_origin ? 0 : (GLint)height;
    GLint src_y1 = ctx->bottom_left_origin ? (GLint)height : 0;
    glBlitFramebuffer(0, src_y0, (GLint)width, src_y1,
                      dst_x, dst_y, dst_x + dst_w, dst_y + dst_h,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

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

/* ----- video_backend vtable adapters ----- */

static bool vb_gl_match(enum retro_hw_context_type type)
{
    switch (type) {
    case RETRO_HW_CONTEXT_OPENGL:
    case RETRO_HW_CONTEXT_OPENGLES2:
    case RETRO_HW_CONTEXT_OPENGL_CORE:
    case RETRO_HW_CONTEXT_OPENGLES3:
    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
        return true;
    default:
        return false;
    }
}

static SDL_WindowFlags vb_gl_window_flags(void)
{
    return SDL_WINDOW_OPENGL;
}

static bool vb_gl_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                       void **out_ctx)
{
    struct video_gl_context *ctx = NULL;
    if (!video_gl_init(window, hw, &ctx))
        return false;
    *out_ctx = ctx;
    return true;
}

static void vb_gl_destroy(void *ctx)
{
    video_gl_destroy((struct video_gl_context *)ctx);
}

static void vb_gl_present(void *ctx, const void *data, unsigned width,
                          unsigned height, size_t pitch,
                          enum retro_pixel_format fmt)
{
    (void)data;
    (void)pitch;
    (void)fmt;
    video_gl_present((struct video_gl_context *)ctx, width, height);
}

static bool vb_gl_resize(void *ctx, SDL_Window *window,
                         unsigned width, unsigned height)
{
    (void)window;
    return video_gl_resize((struct video_gl_context *)ctx, width, height);
}

static uintptr_t vb_gl_get_current_framebuffer(void *ctx)
{
    return video_gl_get_current_framebuffer((struct video_gl_context *)ctx);
}

static retro_proc_address_t vb_gl_get_proc_address(void *ctx, const char *sym)
{
    return video_gl_get_proc_address((struct video_gl_context *)ctx, sym);
}

static bool vb_gl_negotiate_device(void *ctx,
    const struct retro_hw_render_context_negotiation_interface *iface)
{
    (void)ctx;
    (void)iface;
    return false;
}

static bool vb_gl_get_hw_render_interface(void *ctx,
    const struct retro_hw_render_interface **out_iface)
{
    (void)ctx;
    (void)out_iface;
    return false;
}

static void vb_gl_context_destroy(void *ctx)
{
    video_gl_context_destroy((struct video_gl_context *)ctx);
}

const struct video_backend gl_backend = {
    .name                    = "gl",
    .id                      = VIDEO_RENDERER_OPENGL,
    .match_hw_context        = vb_gl_match,
    .window_flags            = vb_gl_window_flags,
    .init                    = vb_gl_init,
    .destroy                 = vb_gl_destroy,
    .present                 = vb_gl_present,
    .resize                  = vb_gl_resize,
    .get_current_framebuffer = vb_gl_get_current_framebuffer,
    .get_proc_address        = vb_gl_get_proc_address,
    .negotiate_device        = vb_gl_negotiate_device,
    .get_hw_render_interface = vb_gl_get_hw_render_interface,
    .context_destroy         = vb_gl_context_destroy,
};
