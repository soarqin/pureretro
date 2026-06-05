/*
 * PureRetro — Video subsystem dispatcher
 *
 * Creates the SDL window and dispatches rendering to the active
 * backend (software, OpenGL, or Vulkan).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "video.h"
#include "video_sw.h"
#include "video_gl.h"
#include "frontend.h"

#ifdef PURERETRO_VULKAN_ENABLED
#include "video_vk.h"
#endif

bool video_init(const char *title, unsigned width, unsigned height)
{
    struct video_state *v = &g_frontend.video;

    memset(v, 0, sizeof(*v));
    v->renderer = VIDEO_RENDERER_SW;
    v->pixel_format = RETRO_PIXEL_FORMAT_0RGB1555; /* libretro default */

    v->window = SDL_CreateWindow(title, (int)width, (int)height, 0);
    if (!v->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    /* Start with the software renderer. */
    if (!video_sw_init(v->window, &v->sw)) {
        fprintf(stderr, "Failed to initialize software renderer\n");
        SDL_DestroyWindow(v->window);
        v->window = NULL;
        return false;
    }

    fprintf(stderr, "Video initialized: default renderer is %s (will switch when core calls SET_HW_RENDER)\n",
            renderer_name(v->renderer));

    return true;
}

void video_shutdown(void)
{
    struct video_state *v = &g_frontend.video;

    if (v->sw) {
        video_sw_destroy(v->sw);
        v->sw = NULL;
    }

    if (v->gl) {
        video_gl_destroy(v->gl);
        v->gl = NULL;
    }

#ifdef PURERETRO_VULKAN_ENABLED
    if (v->vk) {
        video_vk_destroy(v->vk);
        v->vk = NULL;
    }
#endif

    if (v->window) {
        SDL_DestroyWindow(v->window);
        v->window = NULL;
    }
}

void video_present(const void *data, unsigned width, unsigned height, size_t pitch)
{
    struct video_state *v = &g_frontend.video;

    if (v->hw_render_enabled) {
        switch (v->renderer) {
        case VIDEO_RENDERER_OPENGL:
            if (v->gl)
                video_gl_present(v->gl);
            break;
#ifdef PURERETRO_VULKAN_ENABLED
        case VIDEO_RENDERER_VULKAN:
            if (v->vk)
                video_vk_present(v->vk);
            break;
#endif
        default:
            break;
        }
        return;
    }

    if (v->sw) {
        video_sw_present(v->sw, data, width, height, pitch, v->pixel_format);
    }
}

void video_process_event(const SDL_Event *event)
{
    struct video_state *v = &g_frontend.video;

    if (event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        v->frame_width  = (unsigned)event->window.data1;
        v->frame_height = (unsigned)event->window.data2;

        if (v->hw_render_enabled && v->renderer == VIDEO_RENDERER_OPENGL &&
            v->gl && !v->gl->cache_context) {
            video_gl_destroy(v->gl);
            v->gl = NULL;
            if (!video_gl_init(v->window, &v->hw, &v->gl)) {
                fprintf(stderr, "Failed to recreate GL context after resize\n");
                g_frontend.running = false;
            }
        }

#ifdef PURERETRO_VULKAN_ENABLED
        if (v->hw_render_enabled && v->renderer == VIDEO_RENDERER_VULKAN && v->vk) {
            if (!video_vk_resize(v->vk, v->window)) {
                fprintf(stderr, "Failed to recreate Vulkan swapchain after resize\n");
                g_frontend.running = false;
            }
        }
#endif
    }
}

static const char *hw_context_name(enum retro_hw_context_type t)
{
    switch (t) {
    case RETRO_HW_CONTEXT_NONE:             return "none";
    case RETRO_HW_CONTEXT_OPENGL:           return "opengl";
    case RETRO_HW_CONTEXT_OPENGLES2:        return "opengles2";
    case RETRO_HW_CONTEXT_OPENGL_CORE:      return "opengl-core";
    case RETRO_HW_CONTEXT_OPENGLES3:        return "opengles3";
    case RETRO_HW_CONTEXT_OPENGLES_VERSION: return "opengles-version";
    case RETRO_HW_CONTEXT_VULKAN:           return "vulkan";
    case RETRO_HW_CONTEXT_D3D9:             return "d3d9";
    case RETRO_HW_CONTEXT_D3D10:            return "d3d10";
    case RETRO_HW_CONTEXT_D3D11:            return "d3d11";
    case RETRO_HW_CONTEXT_D3D12:            return "d3d12";
    case RETRO_HW_CONTEXT_DUMMY:            return "dummy";
    }
    return "?";
}

bool video_set_hw_render(struct retro_hw_render_callback *hw)
{
    struct video_state *v = &g_frontend.video;

    fprintf(stderr, "Core requested HW context: %s\n", hw_context_name(hw->context_type));
    g_frontend.hw_render_requested = true;

    /* Destroy the software renderer before switching to HW. */
    if (v->sw) {
        video_sw_destroy(v->sw);
        v->sw = NULL;
    }

    switch (hw->context_type) {
    case RETRO_HW_CONTEXT_NONE:
        /* Core changed its mind; stay software. */
        v->hw_render_enabled = false;
        v->renderer = VIDEO_RENDERER_SW;
        if (!video_sw_init(v->window, &v->sw))
            return false;
        fprintf(stderr, "Active renderer: sw (software)\n");
        return true;

    case RETRO_HW_CONTEXT_OPENGL:
    case RETRO_HW_CONTEXT_OPENGLES2:
    case RETRO_HW_CONTEXT_OPENGL_CORE:
    case RETRO_HW_CONTEXT_OPENGLES3:
    case RETRO_HW_CONTEXT_OPENGLES_VERSION: {
        if (!video_gl_init(v->window, hw, &v->gl)) {
            video_sw_init(v->window, &v->sw);
            return false;
        }
        v->renderer = VIDEO_RENDERER_OPENGL;
        v->hw_render_enabled = true;
        memcpy(&v->hw, hw, sizeof(v->hw));
        hw->get_current_framebuffer = video_get_current_framebuffer;
        hw->get_proc_address = video_get_proc_address;
        fprintf(stderr, "Active renderer: gl (opengl)\n");
        if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE &&
            g_frontend.preferred_renderer != VIDEO_RENDERER_OPENGL) {
            fprintf(stderr, "  warning: user preferred '%s' but core chose 'gl'\n",
                    renderer_name(g_frontend.preferred_renderer));
        }
        return true;
    }

#ifdef PURERETRO_VULKAN_ENABLED
    case RETRO_HW_CONTEXT_VULKAN:
        v->renderer = VIDEO_RENDERER_VULKAN;
        v->hw_render_enabled = true;
        memcpy(&v->hw, hw, sizeof(v->hw));
        if (!video_vk_init(v->window, hw, &v->vk))
            return false;
        hw->get_current_framebuffer = video_get_current_framebuffer;
        hw->get_proc_address = video_get_proc_address;
        fprintf(stderr, "Active renderer: vk (vulkan)\n");
        if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE &&
            g_frontend.preferred_renderer != VIDEO_RENDERER_VULKAN) {
            fprintf(stderr, "  warning: user preferred '%s' but core chose 'vk'\n",
                    renderer_name(g_frontend.preferred_renderer));
        }
        return true;
#endif

    default:
        fprintf(stderr, "Unsupported HW context type: %s (%d)\n",
                hw_context_name(hw->context_type), (int)hw->context_type);
        if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE)
            fprintf(stderr, "  user preferred '%s' but core requested an unsupported context\n",
                    renderer_name(g_frontend.preferred_renderer));
        return false;
    }
}

uintptr_t video_get_current_framebuffer(void)
{
    struct video_state *v = &g_frontend.video;

    switch (v->renderer) {
    case VIDEO_RENDERER_OPENGL:
        if (v->gl)
            return video_gl_get_current_framebuffer(v->gl);
        break;
#ifdef PURERETRO_VULKAN_ENABLED
    case VIDEO_RENDERER_VULKAN:
        /* Vulkan uses image indices rather than a single framebuffer handle. */
        break;
#endif
    default:
        break;
    }

    return 0;
}

retro_proc_address_t video_get_proc_address(const char *sym)
{
    struct video_state *v = &g_frontend.video;

    switch (v->renderer) {
    case VIDEO_RENDERER_OPENGL:
        if (v->gl)
            return video_gl_get_proc_address(v->gl, sym);
        break;
#ifdef PURERETRO_VULKAN_ENABLED
    case VIDEO_RENDERER_VULKAN:
        if (v->vk)
            return video_vk_get_proc_address(v->vk, sym);
        break;
#endif
    default:
        break;
    }

    return NULL;
}
