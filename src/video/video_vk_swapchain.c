/*
 * PureRetro — Vulkan swapchain management
 */

#include "video_vk_internal.h"

#include "log.h"

#ifdef PURERETRO_VULKAN_ENABLED

#include <SDL3/SDL.h>

#include <stdlib.h>

void video_vk_swapchain_teardown(struct video_vk_context *ctx)
{
    if (ctx->swapchain_views) {
        for (uint32_t i = 0; i < ctx->image_count; ++i)
            vkDestroyImageView(ctx->device, ctx->swapchain_views[i], NULL);
    }
    if (ctx->cmd_pool != VK_NULL_HANDLE && ctx->cmd_buffers &&
        ctx->image_count > 0) {
        vkFreeCommandBuffers(ctx->device, ctx->cmd_pool,
                             ctx->image_count, ctx->cmd_buffers);
    }
    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(ctx->device, ctx->image_available[i], NULL);
        vkDestroySemaphore(ctx->device, ctx->render_finished[i], NULL);
        vkDestroyFence(ctx->device, ctx->frame_fence[i], NULL);
        ctx->image_available[i] = VK_NULL_HANDLE;
        ctx->render_finished[i] = VK_NULL_HANDLE;
        ctx->frame_fence[i] = VK_NULL_HANDLE;
    }
    free(ctx->swapchain_images);
    free(ctx->swapchain_views);
    free(ctx->cmd_buffers);
    free(ctx->images_in_flight);
    ctx->swapchain_images = NULL;
    ctx->swapchain_views = NULL;
    ctx->cmd_buffers = NULL;
    ctx->images_in_flight = NULL;
    ctx->image_count = 0;
}

bool video_vk_swapchain_create(struct video_vk_context *ctx, SDL_Window *window)
{
    VkSurfaceFormatKHR *formats = NULL;
    VkPresentModeKHR *modes = NULL;
    VkResult r;

    video_vk_swapchain_teardown(ctx);

    VkSurfaceCapabilitiesKHR caps;
    r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        ctx->physical_device, ctx->surface, &caps);
    if (!video_vk_check(r, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        goto fail;

    /* Choose format */
    uint32_t fmt_count = 0;
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &fmt_count, NULL);
    if (!video_vk_check(r, "vkGetPhysicalDeviceSurfaceFormatsKHR"))
        goto fail;
    formats = malloc(sizeof(VkSurfaceFormatKHR) * fmt_count);
    if (!formats)
        goto fail;
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &fmt_count, formats);
    if (!video_vk_check(r, "vkGetPhysicalDeviceSurfaceFormatsKHR"))
        goto fail;

    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < fmt_count; ++i) {
        if ((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
             formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = formats[i];
            break;
        }
    }
    free(formats);
    formats = NULL;
    ctx->swapchain_format = chosen.format;

    /* Choose present mode: prefer FIFO (vsync), fallback to first available */
    uint32_t pm_count = 0;
    r = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &pm_count, NULL);
    if (!video_vk_check(r, "vkGetPhysicalDeviceSurfacePresentModesKHR"))
        goto fail;
    modes = malloc(sizeof(VkPresentModeKHR) * pm_count);
    if (!modes)
        goto fail;
    r = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &pm_count, modes);
    if (!video_vk_check(r, "vkGetPhysicalDeviceSurfacePresentModesKHR"))
        goto fail;

    VkPresentModeKHR present_mode = modes[0];
    for (uint32_t i = 0; i < pm_count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            break;
        }
    }
    free(modes);
    modes = NULL;

    /* Extent */
    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        extent.width = (uint32_t)w;
        extent.height = (uint32_t)h;
        if (extent.width < caps.minImageExtent.width)
            extent.width = caps.minImageExtent.width;
        if (extent.width > caps.maxImageExtent.width)
            extent.width = caps.maxImageExtent.width;
        if (extent.height < caps.minImageExtent.height)
            extent.height = caps.minImageExtent.height;
        if (extent.height > caps.maxImageExtent.height)
            extent.height = caps.maxImageExtent.height;
    }
    ctx->swapchain_extent = extent;

    /* Image count */
    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci = {0};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = ctx->surface;
    sci.minImageCount = image_count;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = present_mode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = ctx->swapchain;

    /* Use local handle to avoid losing old swapchain on creation failure */
    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    r = vkCreateSwapchainKHR(ctx->device, &sci, NULL, &new_swapchain);
    if (!video_vk_check(r, "vkCreateSwapchainKHR"))
        goto fail;

    vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
    ctx->swapchain = new_swapchain;

    /* Retrieve swapchain images */
    r = vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->image_count, NULL);
    if (!video_vk_check(r, "vkGetSwapchainImagesKHR"))
        goto fail;
    ctx->swapchain_images = calloc(ctx->image_count, sizeof(VkImage));
    ctx->swapchain_views = calloc(ctx->image_count, sizeof(VkImageView));
    ctx->cmd_buffers = calloc(ctx->image_count, sizeof(VkCommandBuffer));
    /* images_in_flight uses calloc so each slot starts as VK_NULL_HANDLE
     * (the libretro Vulkan handle type is a pointer or uint64; either way
     * 0 == VK_NULL_HANDLE). */
    ctx->images_in_flight = calloc(ctx->image_count, sizeof(VkFence));
    if (!ctx->swapchain_images || !ctx->swapchain_views ||
        !ctx->cmd_buffers || !ctx->images_in_flight) {
        LOG_ERROR("Failed to allocate swapchain arrays");
        goto fail;
    }
    r = vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->image_count, ctx->swapchain_images);
    if (!video_vk_check(r, "vkGetSwapchainImagesKHR"))
        goto fail;

    /* Create image views */
    for (uint32_t i = 0; i < ctx->image_count; ++i) {
        VkImageViewCreateInfo ivci = {0};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = ctx->swapchain_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = ctx->swapchain_format;
        ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;

        r = vkCreateImageView(ctx->device, &ivci, NULL, &ctx->swapchain_views[i]);
        if (!video_vk_check(r, "vkCreateImageView"))
            goto fail;
    }

    /* Allocate command buffers */
    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = ctx->cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = ctx->image_count;
    r = vkAllocateCommandBuffers(ctx->device, &cbai, ctx->cmd_buffers);
    if (!video_vk_check(r, "vkAllocateCommandBuffers"))
        goto fail;

    /* Create sync objects */
    VkSemaphoreCreateInfo sci_sem = {0};
    sci_sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        r = vkCreateSemaphore(ctx->device, &sci_sem, NULL, &ctx->image_available[i]);
        if (!video_vk_check(r, "vkCreateSemaphore (image_available)"))
            goto fail;
        r = vkCreateSemaphore(ctx->device, &sci_sem, NULL, &ctx->render_finished[i]);
        if (!video_vk_check(r, "vkCreateSemaphore (render_finished)"))
            goto fail;
        r = vkCreateFence(ctx->device, &fci, NULL, &ctx->frame_fence[i]);
        if (!video_vk_check(r, "vkCreateFence"))
            goto fail;
    }

    return true;

fail:
    free(formats);
    free(modes);
    video_vk_swapchain_teardown(ctx);
    vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
    ctx->swapchain = VK_NULL_HANDLE;
    return false;
}


bool video_vk_resize(struct video_vk_context *ctx, SDL_Window *window)
{
    if (!ctx || ctx->device == VK_NULL_HANDLE)
        return false;

    ctx->window = window;

    int pixel_w = 0;
    int pixel_h = 0;
    SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h);
    if (pixel_w <= 0 || pixel_h <= 0 ||
        (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {
        ctx->swapchain_dirty = true;
        return true;
    }

    LOG_INFO("Recreating Vulkan swapchain for window size %dx%d",
             pixel_w, pixel_h);
    vkDeviceWaitIdle(ctx->device);

    /* video_vk_swapchain_create() begins with video_vk_swapchain_teardown(), so
     * the old per-image state is released as part of recreation. */
    return video_vk_swapchain_create(ctx, window);
}


#endif /* PURERETRO_VULKAN_ENABLED */
