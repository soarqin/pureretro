/*
 * PureRetro — Video backend vtable
 *
 * Each renderer (software, OpenGL, Vulkan, ...) exports a single
 * `const struct video_backend XX_backend` symbol. video.c collects
 * these into a registry and dispatches all per-backend operations
 * through this vtable, so adding a new renderer is a single-file
 * change.
 *
 * The vtable's lifecycle:
 *   1. video.c walks the registry asking each backend's
 *      match_hw_context() whether it can satisfy a libretro
 *      RETRO_HW_CONTEXT_* type.
 *   2. video.c calls window_flags() to know what SDL_WindowFlags
 *      to OR into SDL_CreateWindow.
 *   3. video.c calls init() to create the backend context; the
 *      backend stores its own context behind an opaque void*.
 *   4. Per-frame and per-event calls go through the dispatch
 *      methods (present, resize, get_current_framebuffer, ...).
 *   5. context_destroy() runs before SDL_DestroyWindow (used by
 *      GL to release the GL context); destroy() frees the rest.
 */

#ifndef VIDEO_BACKEND_H
#define VIDEO_BACKEND_H

#include "frontend.h"

#include "libretro.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct video_backend {
    /* Human-readable name used in log messages ("sw" / "gl" / "vk"). */
    const char *name;

    /* Convenience tag duplicating what the renderer enum exposes;
     * lets video.c set g_frontend.video.renderer without dereferencing
     * the backend pointer for the common log path. */
    enum video_renderer id;

    /* Returns true if this backend can satisfy the given libretro
     * hardware context type. video.c uses this to select the active
     * backend; the first matching backend wins, so the registry
     * order in video.c matters. */
    bool (*match_hw_context)(enum retro_hw_context_type type);

    /* SDL window-creation flags this backend needs OR'd into
     * SDL_CreateWindow (e.g. SDL_WINDOW_OPENGL, SDL_WINDOW_VULKAN,
     * or 0 for software). */
    SDL_WindowFlags (*window_flags)(void);

    /* Create the backend's context. `hw` is NULL for the software
     * backend; otherwise it carries the core's hw_render request.
     * On success *out_ctx is set to an opaque context pointer the
     * backend owns. */
    bool (*init)(SDL_Window *window, struct retro_hw_render_callback *hw,
                 void **out_ctx);

    /* Free everything `init` allocated. After this returns, ctx
     * must not be used. */
    void (*destroy)(void *ctx);

    /* Per-frame present.
     *  - software: uses data/pitch/fmt to blit raw pixels.
     *  - hardware: ignores data/pitch/fmt and presents whatever
     *    the core rendered into the framebuffer.
     * Backends that don't care about the unused parameters must
     * still accept them (no NULL-arg shortcuts). */
    void (*present)(void *ctx, const void *data, unsigned width,
                    unsigned height, size_t pitch,
                    enum retro_pixel_format fmt);

    /* Resize the backend's render target.
     *  - software: no-op, returns true.
     *  - GL: resizes the FBO (ignores `window`).
     *  - VK: recreates the swapchain (ignores `width`/`height`).
     * Returns false only on real failure. */
    bool (*resize)(void *ctx, SDL_Window *window,
                   unsigned width, unsigned height);

    /* Return the currently-bound framebuffer object (GL) or 0
     * for backends that don't expose one. */
    uintptr_t (*get_current_framebuffer)(void *ctx);

    /* Resolve a backend symbol (e.g. an OpenGL or Vulkan function
     * pointer). Returns NULL for backends that don't support
     * symbol lookup (e.g. software). */
    retro_proc_address_t (*get_proc_address)(void *ctx, const char *sym);

    /* Vulkan only: call the core's create_device callback through
     * the negotiation interface. Other backends return false. */
    bool (*negotiate_device)(void *ctx,
        const struct retro_hw_render_context_negotiation_interface *iface);

    /* Vulkan only: hand the core a pointer to the backend-owned
     * retro_hw_render_interface (lives until destroy()).
     * Other backends return false. */
    bool (*get_hw_render_interface)(void *ctx,
        const struct retro_hw_render_interface **out_iface);

    /* Pre-shutdown context teardown. Used by GL to release the
     * GL context before SDL_DestroyWindow runs. No-op for other
     * backends. */
    void (*context_destroy)(void *ctx);
};

/* Backend instances. Each is defined in its own .c file. */
extern const struct video_backend sw_backend;
extern const struct video_backend gl_backend;
#ifdef PURERETRO_VULKAN_ENABLED
extern const struct video_backend vk_backend;
#endif

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_BACKEND_H */
