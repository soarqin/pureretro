/*
 * PureRetro — Vulkan hardware renderer
 *
 * Vulkan context, swapchain, and presentation management via SDL3 + raw Vulkan.
 */

#ifndef VIDEO_VK_H
#define VIDEO_VK_H

#include "frontend.h"

#include "libretro.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_MAX_FRAMES_IN_FLIGHT 2

#ifdef PURERETRO_VULKAN_ENABLED
#include <vulkan/vulkan.h>
#include "libretro_vulkan.h"

struct video_vk_context
{
    /* Core handles */
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    uint32_t queue_family_index;
    VkSurfaceKHR surface;
    VkCommandPool cmd_pool;
    /* The window we created the surface and swapchain against. Kept so
     * video_vk_present() can recreate the swapchain in-place when the
     * driver reports VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR. */
    SDL_Window *window;

    /* Swapchain */
    VkSwapchainKHR swapchain;
    VkExtent2D swapchain_extent;
    VkFormat swapchain_format;
    uint32_t image_count;
    VkImage *swapchain_images;
    VkImageView *swapchain_views;

    /* Per-frame sync */
    uint32_t frame_index;
    VkSemaphore image_available[VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished[VK_MAX_FRAMES_IN_FLIGHT];
    VkFence frame_fence[VK_MAX_FRAMES_IN_FLIGHT];

    /* Per-image fence tracking. images_in_flight[i] points to the
     * frame_fence currently guarding swapchain image i (or VK_NULL_HANDLE
     * if no submission is outstanding for that image). With image_count
     * potentially exceeding VK_MAX_FRAMES_IN_FLIGHT, this prevents reuse
     * of a swapchain image whose previous submission has not yet
     * completed. Allocated alongside swapchain_images. */
    VkFence *images_in_flight;

    /* Command buffers (one per swapchain image) */
    VkCommandBuffer *cmd_buffers;

    /* libretro interface state */
    struct retro_hw_render_interface_vulkan hw_if;
    struct retro_vulkan_image pending_image;
    bool has_pending_image;

    /* Set when vkAcquireNextImageKHR / vkQueuePresentKHR returns
     * VK_ERROR_OUT_OF_DATE_KHR or VK_SUBOPTIMAL_KHR. The next
     * video_vk_present() call recreates the swapchain before
     * acquiring an image. */
    bool swapchain_dirty;

    /* Cached proc addresses */
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr;
};

#else

/* Opaque stub when Vulkan is not compiled in */
struct video_vk_context;

#endif /* PURERETRO_VULKAN_ENABLED */

/* Initialize the Vulkan renderer for the given window. */
bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx);

/* Destroy the Vulkan renderer and all Vulkan resources. */
void video_vk_destroy(struct video_vk_context *ctx);

/* Present a hardware-rendered frame. */
void video_vk_present(struct video_vk_context *ctx, unsigned width, unsigned height);

/* Resolve a Vulkan symbol. */
retro_proc_address_t video_vk_get_proc_address(struct video_vk_context *ctx,
                                                const char *sym);

/* Recreate swapchain after resize. */
bool video_vk_resize(struct video_vk_context *ctx, SDL_Window *window);

/* Call the core's Vulkan context-negotiation create_device callback.
 * This must be called after video_vk_init has created the instance and surface.
 */
bool video_vk_negotiate_device(struct video_vk_context *ctx,
                               const struct retro_hw_render_context_negotiation_interface *iface);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_VK_H */
