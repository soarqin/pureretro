/*
 * PureRetro — Video subsystem dispatcher
 *
 * Creates the SDL window and dispatches rendering to the active
 * backend through the video_backend vtable. Backends are registered
 * once in the g_backends[] array; backend selection walks that array
 * asking each backend's match_hw_context() predicate.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "video.h"
#include "video_backend.h"
#include "video_sw.h"
#include "video_gl.h"
#include "frontend.h"

#ifdef PURERETRO_VULKAN_ENABLED
#include "video_vk.h"
#endif

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

static const struct video_backend *find_backend(enum retro_hw_context_type type)
{
    for (size_t i = 0; i < g_backend_count; ++i) {
        if (g_backends[i]->match_hw_context(type))
            return g_backends[i];
    }
    return NULL;
}

/* Mirror the new opaque pointer into the legacy concrete-typed
 * fields on video_state so external callers in core.c / main.c
 * continue to work during the A-1 migration. Task 5 deletes
 * both this helper and the legacy fields. */
static void sync_legacy_fields(struct video_state *v)
{
    v->sw = NULL;
    v->gl = NULL;
#ifdef PURERETRO_VULKAN_ENABLED
    v->vk = NULL;
#endif
    if (!v->backend)
        return;
    switch (v->backend->id) {
    case VIDEO_RENDERER_SW:
        v->sw = (struct video_sw_context *)v->backend_ctx;
        break;
    case VIDEO_RENDERER_OPENGL:
        v->gl = (struct video_gl_context *)v->backend_ctx;
        break;
#ifdef PURERETRO_VULKAN_ENABLED
    case VIDEO_RENDERER_VULKAN:
        v->vk = (struct video_vk_context *)v->backend_ctx;
        break;
#endif
    default:
        break;
    }
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

    fprintf(stderr,
            "Video initialized: default renderer is %s "
            "(window will be created when core selects renderer)\n",
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
        sync_legacy_fields(v);
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
        event->type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        return;
    }

    if (!v->hw_render_enabled || !v->backend || !v->backend_ctx)
        return;

    unsigned new_w = (unsigned)event->window.data1;
    unsigned new_h = (unsigned)event->window.data2;

    if (!v->backend->resize(v->backend_ctx, v->window, new_w, new_h)) {
        fprintf(stderr, "Backend %s failed to resize after window resize\n",
                v->backend->name);
        if (v->backend->id == VIDEO_RENDERER_VULKAN)
            g_frontend.running = false;
    }
}

bool video_set_hw_render(struct retro_hw_render_callback *hw)
{
    struct video_state *v = &g_frontend.video;
    const struct video_backend *new_backend = find_backend(hw->context_type);

    fprintf(stderr, "Core requested HW context: type=%d\n",
            (int)hw->context_type);
    g_frontend.hw_render_requested = true;

    if (!new_backend) {
        fprintf(stderr, "Unsupported HW context type: %d\n",
                (int)hw->context_type);
        if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE) {
            fprintf(stderr,
                    "  user preferred '%s' but core requested an "
                    "unsupported context\n",
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
        v->window = SDL_CreateWindow("PureRetro", 640, 480, flags);
        if (!v->window) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return false;
        }
        fprintf(stderr, "Created window with flags 0x%llx for renderer %s\n",
                (unsigned long long)flags, new_backend->name);
    }

    void *ctx = NULL;
    struct retro_hw_render_callback *hw_arg =
        (new_backend->id == VIDEO_RENDERER_SW) ? NULL : hw;
    if (!new_backend->init(v->window, hw_arg, &ctx)) {
        fprintf(stderr, "Backend %s init failed; falling back to software\n",
                new_backend->name);
        if (sw_backend.init(v->window, NULL, &ctx)) {
            v->backend = &sw_backend;
            v->backend_ctx = ctx;
            v->renderer = VIDEO_RENDERER_SW;
            v->hw_render_enabled = false;
            sync_legacy_fields(v);
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

    sync_legacy_fields(v);

    fprintf(stderr, "Active renderer: %s\n", new_backend->name);
    if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE &&
        g_frontend.preferred_renderer != new_backend->id) {
        fprintf(stderr, "  warning: user preferred '%s' but core chose '%s'\n",
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
