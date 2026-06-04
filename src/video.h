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

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_H */
