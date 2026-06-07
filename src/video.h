/*
 * PureRetro — Video subsystem dispatcher
 *
 * Renderer-agnostic window management and runtime dispatch
 * to the active renderer (software, OpenGL, or Vulkan).
 */

#ifndef VIDEO_H
#define VIDEO_H

#include <stdbool.h>
#include <stdint.h>
#include "libretro.h"
#include "frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the video subsystem.
 * If the core requests hardware rendering later, the window is recreated
 * with the appropriate flags. */
bool video_init(const char *title, unsigned width, unsigned height);

/* Shutdown the video subsystem and destroy the window. */
void video_shutdown(void);

/* Present a frame. If hw_render is enabled, this simply swaps/presents.
 * Otherwise, 'data' is blitted via the software renderer. */
void video_present(const void *data, unsigned width, unsigned height, size_t pitch);

/* Handle window events (resize, fullscreen toggle, etc.). */
void video_process_event(const SDL_Event *event);

/* Request a hardware rendering context from the video subsystem.
 * Called by the core via RETRO_ENVIRONMENT_SET_HW_RENDER. */
bool video_set_hw_render(struct retro_hw_render_callback *hw);

/* Get the current framebuffer for hardware rendering.
 * Valid only when hw_render is active. */
uintptr_t video_get_current_framebuffer(void);

/* Get a proc address for the current hardware context.
 * Valid only when hw_render is active. */
retro_proc_address_t video_get_proc_address(const char *sym);

/* Negotiate hardware context creation with the core.
 * Called when the core sets RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE. */
bool video_negotiate_hw_context(const struct retro_hw_render_context_negotiation_interface *iface);

/* Resize the active backend's render target (FBO for GL, swapchain
 * for VK, no-op for SW). Called from SET_SYSTEM_AV_INFO and from
 * window-resize events. Returns false only on real failure. */
bool video_resize(unsigned width, unsigned height);

/* Update geometry dimensions and aspect ratio. Called when the core
 * sends RETRO_ENVIRONMENT_SET_GEOMETRY. Resizes the HW render target
 * if hardware rendering is active. */
void video_update_geometry(unsigned base_width, unsigned base_height,
                           unsigned max_width, unsigned max_height,
                           float aspect_ratio);

/* Resize the window to match the current base resolution in g_av_info,
 * unless fullscreen is active. No-op if no window exists. */
void video_resize_window_to_geometry(void);

/* Populate *out with a pointer to the backend-owned hardware render
 * interface (e.g. retro_hw_render_interface_vulkan for Vulkan).
 * Returns false if the active backend doesn't have one (SW, GL).
 * The returned pointer lives until the backend is destroyed. */
bool video_get_hw_render_interface(const struct retro_hw_render_interface **out);

/* Backend-specific pre-shutdown teardown. Currently used by GL to
 * release the GL context before SDL_DestroyWindow runs. No-op for
 * SW and VK. Safe to call when no backend is active. */
void video_context_destroy(void);

/* Bring up the software backend if no backend has been activated
 * yet (i.e. the core never sent SET_HW_RENDER). Creates the SDL
 * window with sw_backend.window_flags() and calls sw_backend.init.
 * No-op (returns true) if a backend is already active. */
bool video_ensure_software_renderer(void);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_H */
