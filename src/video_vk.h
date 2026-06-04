/*
 * PureRetro — Vulkan hardware renderer
 *
 * Vulkan context and swapchain management via SDL3 + raw Vulkan.
 */

#ifndef VIDEO_VK_H
#define VIDEO_VK_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "libretro.h"
#include "frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

struct video_vk_context
{
    /* Vulkan handles (opaque pointer types to avoid exposing vulkan.h globally) */
    void *instance;        /* VkInstance */
    void *physical_device; /* VkPhysicalDevice */
    void *device;          /* VkDevice */
    void *surface;         /* VkSurfaceKHR */
    void *queue;           /* VkQueue */
    uint32_t queue_family_index;

    /* Swapchain */
    void *swapchain;       /* VkSwapchainKHR */
    uint32_t swapchain_image_count;
    void **swapchain_images; /* VkImage* array */

    /* Cached proc address wrapper */
    retro_hw_get_proc_address_t get_proc_address;
};

/* Initialize the Vulkan renderer for the given window. */
bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx);

/* Destroy the Vulkan renderer and all Vulkan resources. */
void video_vk_destroy(struct video_vk_context *ctx);

/* Present a hardware-rendered frame. */
void video_vk_present(struct video_vk_context *ctx);

/* Resolve a Vulkan symbol. */
retro_proc_address_t video_vk_get_proc_address(struct video_vk_context *ctx,
                                                const char *sym);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_VK_H */
