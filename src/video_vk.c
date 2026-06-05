/*
 * PureRetro — Vulkan hardware renderer
 *
 * Vulkan instance, device, surface, and swapchain management.
 * This is a minimal best-effort implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "video_vk.h"

#ifdef PURERETRO_VULKAN_ENABLED
#include <vulkan/vulkan.h>
#endif

bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx)
{
#ifdef PURERETRO_VULKAN_ENABLED
    struct video_vk_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return false;

    (void)window;
    (void)hw;

    /* TODO: Create VkInstance with required extensions.
     * TODO: Create VkSurfaceKHR via SDL_Vulkan_CreateSurface.
     * TODO: Select physical device and queue family.
     * TODO: Create VkDevice and VkQueue.
     * TODO: Create swapchain.
     * TODO: Populate retro_hw_render_interface for RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE.
     */

    fprintf(stderr, "Vulkan renderer stub: initialization not yet fully implemented.\n");

    *out_ctx = ctx;
    return true;
#else
    (void)window;
    (void)hw;
    (void)out_ctx;
    fprintf(stderr, "Vulkan support is not compiled in.\n");
    return false;
#endif
}

void video_vk_destroy(struct video_vk_context *ctx)
{
#ifdef PURERETRO_VULKAN_ENABLED
    if (!ctx)
        return;

    /* TODO: Destroy swapchain, device, surface, instance. */

    free(ctx);
#else
    (void)ctx;
#endif
}

void video_vk_present(struct video_vk_context *ctx)
{
#ifdef PURERETRO_VULKAN_ENABLED
    (void)ctx;
    /* TODO: Acquire next image, present swapchain. */
#else
    (void)ctx;
#endif
}

retro_proc_address_t video_vk_get_proc_address(struct video_vk_context *ctx,
                                                const char *sym)
{
#ifdef PURERETRO_VULKAN_ENABLED
    (void)ctx;
    (void)sym;
    /* TODO: Implement via vkGetInstanceProcAddr / vkGetDeviceProcAddr. */
    return NULL;
#else
    (void)ctx;
    (void)sym;
    return NULL;
#endif
}

bool video_vk_resize(struct video_vk_context *ctx, SDL_Window *window)
{
    (void)ctx;
    (void)window;
    /* TODO: Full implementation in Task 5 */
    return true;
}
