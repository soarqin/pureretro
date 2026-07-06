/*
 * PureRetro — Video subsystem dispatcher
 *
 * Creates the SDL window and dispatches rendering to the active
 * backend through the video_backend vtable. Backends are registered
 * once in the g_backends[] array; backend selection walks that array
 * asking each backend's match_hw_context() predicate.
 */

#include "video.h"
#include "video_backend.h"
#include "video_sw.h"
#include "video_gl.h"
#ifdef PURERETRO_VULKAN_ENABLED
#include "video_vk.h"
#endif
#include "frontend.h"
#include "core.h"
#include "log.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Backend registry. Order matters: the first backend whose
 * match_hw_context() returns true for the requested context type
 * wins. sw_backend matches RETRO_HW_CONTEXT_NONE exclusively. */
static const struct video_backend *const g_backends[] = {
    &sw_backend,
    &gl_backend,
#ifdef PURERETRO_VULKAN_ENABLED
    &vk_backend,
#endif
};

static const size_t g_backend_count =
    sizeof(g_backends) / sizeof(g_backends[0]);

/* Compute the initial window size based on the core's base resolution.
 * If --scale was specified, use it as an integer multiplier.
 * Otherwise, auto-scale so the smaller dimension is at least 480px
 * while preserving aspect ratio, but never smaller than 1x. */
static void compute_window_size(int *out_w, int *out_h)
{
    unsigned base_w = g_av_info.geometry.base_width;
    unsigned base_h = g_av_info.geometry.base_height;

    if (base_w == 0 || base_h == 0) {
        *out_w = 640;
        *out_h = 480;
        return;
    }

    if (g_frontend.window_scale > 0) {
        *out_w = (int)(base_w * g_frontend.window_scale);
        *out_h = (int)(base_h * g_frontend.window_scale);
        return;
    }

    const unsigned min_px = 480;
    if (base_w < base_h) {
        *out_w = (int)(base_w * min_px / base_h);
        *out_h = (int)min_px;
    } else {
        *out_w = (int)min_px;
        *out_h = (int)(base_h * min_px / base_w);
    }

    /* Never scale below 1x. */
    if (*out_w < (int)base_w)
        *out_w = (int)base_w;
    if (*out_h < (int)base_h)
        *out_h = (int)base_h;
}

static const struct video_backend *find_backend(enum retro_hw_context_type type)
{
    for (size_t i = 0; i < g_backend_count; ++i) {
        if (g_backends[i]->match_hw_context(type))
            return g_backends[i];
    }
    return NULL;
}

bool video_init(const char *title, unsigned width, unsigned height)
{
    struct video_state *v = &g_frontend.video;

    (void)title;
    (void)width;
    (void)height;

    memset(v, 0, sizeof(*v));
    v->renderer = VIDEO_RENDERER_SW;
    v->pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;

    LOG_INFO("Video initialized: default renderer is %s "
             "(window will be created when core selects renderer)",
             renderer_name(v->renderer));

    return true;
}

void video_shutdown(void)
{
    struct video_state *v = &g_frontend.video;

    if (v->backend && v->backend_ctx) {
        v->backend->destroy(v->backend_ctx);
        v->backend_ctx = NULL;
        v->backend = NULL;
    }

    if (v->window) {
        SDL_DestroyWindow(v->window);
        v->window = NULL;
    }
}

void video_present(const void *data, unsigned width, unsigned height, size_t pitch)
{
    struct video_state *v = &g_frontend.video;

    if (!v->backend || !v->backend_ctx)
        return;

    v->backend->present(v->backend_ctx, data, width, height, pitch,
                        v->pixel_format);
}

void video_process_event(const SDL_Event *event)
{
    struct video_state *v = &g_frontend.video;

    if (event->type != SDL_EVENT_WINDOW_RESIZED &&
        event->type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
        event->type != SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED &&
        event->type != SDL_EVENT_WINDOW_ENTER_FULLSCREEN &&
        event->type != SDL_EVENT_WINDOW_LEAVE_FULLSCREEN &&
        event->type != SDL_EVENT_WINDOW_RESTORED &&
        event->type != SDL_EVENT_WINDOW_SHOWN) {
        return;
    }

    if (!v->hw_render_enabled || !v->backend || !v->backend_ctx)
        return;

    LOG_INFO("Window output surface changed (event=%u); resizing %s backend",
             (unsigned)event->type, v->backend->name);
    if (!v->backend->resize_output_surface(v->backend_ctx, v->window)) {
        LOG_ERROR("Backend %s failed to resize after window resize",
                  v->backend->name);
        if (v->backend->id == VIDEO_RENDERER_VULKAN)
            g_frontend.running = false;
    }
}

bool video_set_hw_render(struct retro_hw_render_callback *hw)
{
    struct video_state *v = &g_frontend.video;
    const struct video_backend *new_backend = find_backend(hw->context_type);

    LOG_INFO("Core requested HW context: type=%d",
             (int)hw->context_type);
    g_frontend.hw_render_requested = true;

    if (!new_backend) {
        LOG_ERROR("Unsupported HW context type: %d",
                  (int)hw->context_type);
        if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE) {
            LOG_WARN("  user preferred '%s' but core requested an "
                     "unsupported context",
                     renderer_name(g_frontend.preferred_renderer));
        }
        return false;
    }

    /* If the same backend is already active we're done. The NONE-with-sw
     * case used to be the special-cased early return; that path is now
     * just "new_backend == current_backend". */
    if (v->backend == new_backend && v->backend_ctx) {
        v->renderer = new_backend->id;
        v->hw_render_enabled = (new_backend->id != VIDEO_RENDERER_SW);
        return true;
    }

    /* Tear down any active backend before bringing the new one up. */
    if (v->backend && v->backend_ctx) {
        v->backend->destroy(v->backend_ctx);
        v->backend_ctx = NULL;
        v->backend = NULL;
    }

    if (!v->window) {
        SDL_WindowFlags flags = new_backend->window_flags();
        int win_w, win_h;
        compute_window_size(&win_w, &win_h);

        if (new_backend->id == VIDEO_RENDERER_OPENGL &&
            !video_gl_prepare_context_attributes(hw)) {
            return false;
        }

        v->window = SDL_CreateWindow("PureRetro", win_w, win_h, flags);
        if (!v->window) {
            LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
            return false;
        }
        LOG_INFO("Created window %dx%d with flags 0x%llx for renderer %s",
                 win_w, win_h, (unsigned long long)flags, new_backend->name);
    }

    void *ctx = NULL;
    struct retro_hw_render_callback *hw_arg =
        (new_backend->id == VIDEO_RENDERER_SW) ? NULL : hw;
    if (!new_backend->init(v->window, hw_arg, &ctx)) {
        LOG_ERROR("Backend %s init failed", new_backend->name);
        if (new_backend->id != VIDEO_RENDERER_SW && v->window) {
            SDL_DestroyWindow(v->window);
            v->window = NULL;
        }
        return false;
    }

    v->backend = new_backend;
    v->backend_ctx = ctx;
    v->renderer = new_backend->id;
    v->hw_render_enabled = (new_backend->id != VIDEO_RENDERER_SW);

    if (v->hw_render_enabled) {
        hw->get_current_framebuffer = video_get_current_framebuffer;
        hw->get_proc_address = video_get_proc_address;
        memcpy(&v->hw, hw, sizeof(v->hw));
    }

    LOG_INFO("Active renderer: %s", new_backend->name);
    if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE &&
        g_frontend.preferred_renderer != new_backend->id) {
        LOG_WARN("  warning: user preferred '%s' but core chose '%s'",
                 renderer_name(g_frontend.preferred_renderer),
                 new_backend->name);
    }
    return true;
}

uintptr_t video_get_current_framebuffer(void)
{
    struct video_state *v = &g_frontend.video;
    if (!v->backend || !v->backend_ctx)
        return 0;
    return v->backend->get_current_framebuffer(v->backend_ctx);
}

retro_proc_address_t video_get_proc_address(const char *sym)
{
    struct video_state *v = &g_frontend.video;
    if (!v->backend || !v->backend_ctx)
        return NULL;
    return v->backend->get_proc_address(v->backend_ctx, sym);
}

bool video_negotiate_hw_context(
    const struct retro_hw_render_context_negotiation_interface *iface)
{
    struct video_state *v = &g_frontend.video;
    if (!iface || !v->backend || !v->backend_ctx)
        return false;
    return v->backend->negotiate_device(v->backend_ctx, iface);
}

bool video_resize(unsigned width, unsigned height)
{
    struct video_state *v = &g_frontend.video;
    if (!v->backend || !v->backend_ctx)
        return false;
    return v->backend->resize_render_target(v->backend_ctx, width, height);
}

void video_update_geometry(unsigned base_width, unsigned base_height,
                           unsigned max_width, unsigned max_height,
                           float aspect_ratio)
{
    g_av_info.geometry.base_width   = base_width;
    g_av_info.geometry.base_height  = base_height;
    g_av_info.geometry.max_width    = max_width;
    g_av_info.geometry.max_height   = max_height;
    g_av_info.geometry.aspect_ratio = aspect_ratio;

    if (g_frontend.video.hw_render_enabled && max_width > 0 && max_height > 0) {
        video_resize(max_width, max_height);
    }

    /* Resize the window to match the new base resolution. */
    video_resize_window_to_geometry();

    LOG_INFO("Geometry updated: %ux%u (max %ux%u) aspect %.3f",
             base_width, base_height, max_width, max_height,
             aspect_ratio > 0.0f ? aspect_ratio : 0.0f);
}

void video_resize_window_to_geometry(void)
{
    struct video_state *v = &g_frontend.video;
    if (!v->window)
        return;

    /* Don't resize while fullscreen — the display owns the size. */
    if (g_frontend.fullscreen)
        return;

    unsigned base_w = g_av_info.geometry.base_width;
    unsigned base_h = g_av_info.geometry.base_height;
    if (base_w == 0 || base_h == 0)
        return;

    int win_w, win_h;
    compute_window_size(&win_w, &win_h);
    SDL_SetWindowSize(v->window, win_w, win_h);
    LOG_INFO("Window resized to %dx%d", win_w, win_h);
}

bool video_get_hw_render_interface(const struct retro_hw_render_interface **out)
{
    struct video_state *v = &g_frontend.video;
    if (!out || !v->backend || !v->backend_ctx)
        return false;
    return v->backend->get_hw_render_interface(v->backend_ctx, out);
}

void video_context_destroy(void)
{
    struct video_state *v = &g_frontend.video;
    if (!v->backend || !v->backend_ctx)
        return;
    v->backend->context_destroy(v->backend_ctx);
}

bool video_ensure_software_renderer(void)
{
    struct video_state *v = &g_frontend.video;
    if (v->backend && v->backend_ctx)
        return true;

    if (!v->window) {
        int win_w, win_h;
        compute_window_size(&win_w, &win_h);
        v->window = SDL_CreateWindow("PureRetro", win_w, win_h,
                                     sw_backend.window_flags());
        if (!v->window) {
            LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
            return false;
        }
        LOG_INFO("Created window %dx%d for software renderer",
                 win_w, win_h);
    }

    void *ctx = NULL;
    if (!sw_backend.init(v->window, NULL, &ctx))
        return false;

    v->backend = &sw_backend;
    v->backend_ctx = ctx;
    v->renderer = VIDEO_RENDERER_SW;
    v->hw_render_enabled = false;
    return true;
}

bool video_get_software_framebuffer(unsigned width, unsigned height,
                                    enum retro_pixel_format fmt,
                                    void **out_data, size_t *out_pitch)
{
    struct video_state *v = &g_frontend.video;
    if (!v->backend || !v->backend_ctx || v->backend != &sw_backend)
        return false;
    return video_sw_get_framebuffer((struct video_sw_context *)v->backend_ctx,
                                    width, height, fmt, out_data, out_pitch);
}
