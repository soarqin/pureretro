/*
 * PureRetro — Vulkan hardware renderer
 *
 * Minimal Vulkan context and swapchain management.
 * Presentation uses vkCmdBlitImage (no shaders or render passes).
 */

#include "video_vk.h"
#include "video_backend.h"
#include "frontend.h"
#include "core.h"
#include "log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PURERETRO_VULKAN_ENABLED

static const char *vk_result_string(VkResult r)
{
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
    default: return "UNKNOWN_VK_RESULT";
    }
}

static bool vk_check(VkResult r, const char *op)
{
    if (r != VK_SUCCESS) {
        LOG_ERROR("Vulkan error in %s: %s (%d)", op, vk_result_string(r), (int)r);
        return false;
    }
    return true;
}

static bool create_instance(struct video_vk_context *ctx, bool debug)
{
    Uint32 ext_count = 0;
    const char * const *sdl_exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
    if (!sdl_exts) {
        LOG_ERROR("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
        return false;
    }

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "PureRetro";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "PureRetro";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = ext_count;
    create_info.ppEnabledExtensionNames = sdl_exts;

    const char *layers[1];
    if (debug) {
        layers[0] = "VK_LAYER_KHRONOS_validation";
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
    }

    VkResult r = vkCreateInstance(&create_info, NULL, &ctx->instance);
    if (!vk_check(r, "vkCreateInstance"))
        return false;

    ctx->get_instance_proc_addr = (PFN_vkGetInstanceProcAddr)
        vkGetInstanceProcAddr(ctx->instance, "vkGetInstanceProcAddr");
    return true;
}

static bool create_surface(struct video_vk_context *ctx, SDL_Window *window)
{
    if (!SDL_Vulkan_CreateSurface(window, ctx->instance, NULL, &ctx->surface)) {
        LOG_ERROR("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

static bool select_physical_device(struct video_vk_context *ctx)
{
    uint32_t count = 0;
    VkResult r = vkEnumeratePhysicalDevices(ctx->instance, &count, NULL);
    if (!vk_check(r, "vkEnumeratePhysicalDevices") || count == 0) {
        LOG_ERROR("No Vulkan physical devices found");
        return false;
    }

    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * count);
    if (!devices)
        return false;
    r = vkEnumeratePhysicalDevices(ctx->instance, &count, devices);
    if (!vk_check(r, "vkEnumeratePhysicalDevices")) {
        free(devices);
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, NULL);
        VkQueueFamilyProperties *qf_props = malloc(sizeof(VkQueueFamilyProperties) * qf_count);
        if (!qf_props)
            continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, qf_props);

        bool found = false;
        for (uint32_t q = 0; q < qf_count && !found; ++q) {
            VkBool32 present_support = VK_FALSE;
            r = vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, ctx->surface, &present_support);
            if (!vk_check(r, "vkGetPhysicalDeviceSurfaceSupportKHR"))
                continue;

            if ((qf_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
                uint32_t ext_count = 0;
                r = vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, NULL);
                if (!vk_check(r, "vkEnumerateDeviceExtensionProperties") || ext_count == 0)
                    continue;
                VkExtensionProperties *exts = malloc(sizeof(VkExtensionProperties) * ext_count);
                if (!exts)
                    continue;
                r = vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, exts);
                if (!vk_check(r, "vkEnumerateDeviceExtensionProperties")) {
                    free(exts);
                    continue;
                }

                bool has_swapchain = false;
                for (uint32_t e = 0; e < ext_count; ++e) {
                    if (strcmp(exts[e].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                        has_swapchain = true;
                        break;
                    }
                }
                free(exts);

                if (has_swapchain) {
                    ctx->physical_device = devices[i];
                    ctx->queue_family_index = q;
                    found = true;
                }
            }
        }
        free(qf_props);
        if (found) {
            free(devices);
            return true;
        }
    }

    LOG_ERROR("No suitable Vulkan physical device found");
    free(devices);
    return false;
}

static bool create_device(struct video_vk_context *ctx)
{
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = ctx->queue_family_index;
    qci.queueCount = 1;
    qci.pQueuePriorities = &queue_priority;

    const char *extensions[1] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = extensions;

    VkResult r = vkCreateDevice(ctx->physical_device, &dci, NULL, &ctx->device);
    if (!vk_check(r, "vkCreateDevice"))
        return false;

    vkGetDeviceQueue(ctx->device, ctx->queue_family_index, 0, &ctx->graphics_queue);
    ctx->get_device_proc_addr = (PFN_vkGetDeviceProcAddr)
        vkGetDeviceProcAddr(ctx->device, "vkGetDeviceProcAddr");
    return true;
}

static bool create_command_pool(struct video_vk_context *ctx)
{
    VkCommandPoolCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = ctx->queue_family_index;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult r = vkCreateCommandPool(ctx->device, &ci, NULL, &ctx->cmd_pool);
    return vk_check(r, "vkCreateCommandPool");
}

static void vk_swapchain_teardown(struct video_vk_context *ctx)
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

static bool vk_swapchain_create(struct video_vk_context *ctx, SDL_Window *window)
{
    VkSurfaceFormatKHR *formats = NULL;
    VkPresentModeKHR *modes = NULL;
    VkResult r;

    vk_swapchain_teardown(ctx);

    VkSurfaceCapabilitiesKHR caps;
    r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        ctx->physical_device, ctx->surface, &caps);
    if (!vk_check(r, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        goto fail;

    /* Choose format */
    uint32_t fmt_count = 0;
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &fmt_count, NULL);
    if (!vk_check(r, "vkGetPhysicalDeviceSurfaceFormatsKHR"))
        goto fail;
    formats = malloc(sizeof(VkSurfaceFormatKHR) * fmt_count);
    if (!formats)
        goto fail;
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &fmt_count, formats);
    if (!vk_check(r, "vkGetPhysicalDeviceSurfaceFormatsKHR"))
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
    if (!vk_check(r, "vkGetPhysicalDeviceSurfacePresentModesKHR"))
        goto fail;
    modes = malloc(sizeof(VkPresentModeKHR) * pm_count);
    if (!modes)
        goto fail;
    r = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &pm_count, modes);
    if (!vk_check(r, "vkGetPhysicalDeviceSurfacePresentModesKHR"))
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
    if (!vk_check(r, "vkCreateSwapchainKHR"))
        goto fail;

    vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
    ctx->swapchain = new_swapchain;

    /* Retrieve swapchain images */
    r = vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->image_count, NULL);
    if (!vk_check(r, "vkGetSwapchainImagesKHR"))
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
    if (!vk_check(r, "vkGetSwapchainImagesKHR"))
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
        if (!vk_check(r, "vkCreateImageView"))
            goto fail;
    }

    /* Allocate command buffers */
    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = ctx->cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = ctx->image_count;
    r = vkAllocateCommandBuffers(ctx->device, &cbai, ctx->cmd_buffers);
    if (!vk_check(r, "vkAllocateCommandBuffers"))
        goto fail;

    /* Create sync objects */
    VkSemaphoreCreateInfo sci_sem = {0};
    sci_sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        r = vkCreateSemaphore(ctx->device, &sci_sem, NULL, &ctx->image_available[i]);
        if (!vk_check(r, "vkCreateSemaphore (image_available)"))
            goto fail;
        r = vkCreateSemaphore(ctx->device, &sci_sem, NULL, &ctx->render_finished[i]);
        if (!vk_check(r, "vkCreateSemaphore (render_finished)"))
            goto fail;
        r = vkCreateFence(ctx->device, &fci, NULL, &ctx->frame_fence[i]);
        if (!vk_check(r, "vkCreateFence"))
            goto fail;
    }

    return true;

fail:
    free(formats);
    free(modes);
    vk_swapchain_teardown(ctx);
    vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
    ctx->swapchain = VK_NULL_HANDLE;
    return false;
}

static void vk_clear_pending_frame(struct video_vk_context *ctx);

static bool vk_reserve_wait_semaphores(struct video_vk_context *ctx,
                                       uint32_t count)
{
    if (count <= ctx->pending_wait_semaphore_capacity)
        return true;

    VkSemaphore *items = realloc(ctx->pending_wait_semaphores,
                                 sizeof(*items) * count);
    if (!items)
        return false;

    ctx->pending_wait_semaphores = items;
    ctx->pending_wait_semaphore_capacity = count;
    return true;
}

static bool vk_reserve_command_buffers(struct video_vk_context *ctx,
                                       uint32_t count)
{
    if (count <= ctx->pending_command_buffer_capacity)
        return true;

    VkCommandBuffer *items = realloc(ctx->pending_command_buffers,
                                     sizeof(*items) * count);
    if (!items)
        return false;

    ctx->pending_command_buffers = items;
    ctx->pending_command_buffer_capacity = count;
    return true;
}

static void vk_set_image(void *handle, const struct retro_vulkan_image *image,
                         uint32_t num_semaphores, const VkSemaphore *semaphores,
                         uint32_t src_queue_family)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    if (!ctx || !image)
        return;

    ctx->pending_image = *image;
    ctx->pending_src_queue_family = src_queue_family;
    ctx->pending_wait_semaphore_count = 0;

    if (semaphores && num_semaphores > 0) {
        if (!vk_reserve_wait_semaphores(ctx, num_semaphores)) {
            LOG_ERROR("Failed to store Vulkan core wait semaphores");
            vk_clear_pending_frame(ctx);
            return;
        }
        for (uint32_t i = 0; i < num_semaphores; ++i) {
            if (semaphores[i] != VK_NULL_HANDLE) {
                ctx->pending_wait_semaphores[
                    ctx->pending_wait_semaphore_count++] = semaphores[i];
            }
        }
    }
}

static void vk_clear_pending_frame(struct video_vk_context *ctx)
{
    /* Keep pending_image cached. Several hardware cores use libretro's
     * duplicate-frame convention and call video_refresh(NULL, 0, 0, 0)
     * without a fresh set_image(); the frontend must re-present the last
     * image in that case. Only per-frame sync/cmd state is consumed here. */
    ctx->pending_wait_semaphore_count = 0;
    ctx->pending_command_buffer_count = 0;
    ctx->pending_src_queue_family = VK_QUEUE_FAMILY_IGNORED;
    ctx->pending_signal_semaphore = VK_NULL_HANDLE;
}

static void vk_restore_frame_fence(struct video_vk_context *ctx, uint32_t frame_idx)
{
    VkFence old_fence = ctx->frame_fence[frame_idx];
    for (uint32_t i = 0; i < ctx->image_count; ++i) {
        if (ctx->images_in_flight[i] == old_fence)
            ctx->images_in_flight[i] = VK_NULL_HANDLE;
    }

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkDestroyFence(ctx->device, old_fence, NULL);
    ctx->frame_fence[frame_idx] = VK_NULL_HANDLE;
    vkCreateFence(ctx->device, &fci, NULL, &ctx->frame_fence[frame_idx]);
}

static uint32_t vk_get_sync_index(void *handle)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    if (!ctx)
        return 0;

    /* The sync index describes frontend frame slots that the core may use
     * for its own per-frame resources. Keep this aligned with
     * wait_sync_index(), which waits frame_fence[frame_index %
     * VK_MAX_FRAMES_IN_FLIGHT]. Exposing swapchain image_count while only
     * waiting two frame fences lets cores reuse resources still read by the
     * frontend on common triple-buffered swapchains. */
    return ctx->frame_index % VK_MAX_FRAMES_IN_FLIGHT;
}

static uint32_t vk_get_sync_index_mask(void *handle)
{
    (void)handle;
    return (1u << VK_MAX_FRAMES_IN_FLIGHT) - 1u;
}

static void vk_lock_queue(void *handle)
{
    (void)handle;
    /* No-op: single-threaded queue submission for minimal frontend */
}

static void vk_unlock_queue(void *handle)
{
    (void)handle;
    /* No-op */
}

static void vk_set_command_buffers(void *handle, uint32_t num_cmd,
                                   const VkCommandBuffer *cmd)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    if (!ctx)
        return;

    ctx->pending_command_buffer_count = 0;
    if (!cmd || num_cmd == 0)
        return;

    if (!vk_reserve_command_buffers(ctx, num_cmd)) {
        LOG_ERROR("Failed to store Vulkan core command buffers");
        vk_clear_pending_frame(ctx);
        return;
    }

    for (uint32_t i = 0; i < num_cmd; ++i) {
        if (cmd[i] != VK_NULL_HANDLE) {
            ctx->pending_command_buffers[
                ctx->pending_command_buffer_count++] = cmd[i];
        }
    }

}

static void vk_wait_sync_index(void *handle)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    if (!ctx || ctx->device == VK_NULL_HANDLE || ctx->image_count == 0)
        return;

    uint32_t frame_idx = ctx->frame_index % VK_MAX_FRAMES_IN_FLIGHT;
    vkWaitForFences(ctx->device, 1, &ctx->frame_fence[frame_idx],
                    VK_TRUE, UINT64_MAX);
}

static void vk_set_signal_semaphore(void *handle, VkSemaphore semaphore)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    if (!ctx)
        return;
    ctx->pending_signal_semaphore = semaphore;
}

bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx)
{
    struct video_vk_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return false;

    ctx->window = window;
    ctx->pending_src_queue_family = VK_QUEUE_FAMILY_IGNORED;

    if (!create_instance(ctx, hw->debug_context))
        goto fail;

    if (!create_surface(ctx, window))
        goto fail;

    if (!select_physical_device(ctx))
        goto fail;

    if (!create_device(ctx))
        goto fail;

    if (!create_command_pool(ctx))
        goto fail;

    if (!vk_swapchain_create(ctx, window))
        goto fail;

    /* Populate libretro interface */
    ctx->hw_if.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    ctx->hw_if.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    ctx->hw_if.handle = ctx;
    ctx->hw_if.instance = ctx->instance;
    ctx->hw_if.gpu = ctx->physical_device;
    ctx->hw_if.device = ctx->device;
    ctx->hw_if.queue = ctx->graphics_queue;
    ctx->hw_if.queue_index = ctx->queue_family_index;
    ctx->hw_if.get_device_proc_addr = ctx->get_device_proc_addr;
    ctx->hw_if.get_instance_proc_addr = ctx->get_instance_proc_addr;

    ctx->hw_if.set_image = vk_set_image;
    ctx->hw_if.get_sync_index = vk_get_sync_index;
    ctx->hw_if.get_sync_index_mask = vk_get_sync_index_mask;
    ctx->hw_if.lock_queue = vk_lock_queue;
    ctx->hw_if.unlock_queue = vk_unlock_queue;
    ctx->hw_if.set_command_buffers = vk_set_command_buffers;
    ctx->hw_if.wait_sync_index = vk_wait_sync_index;
    ctx->hw_if.set_signal_semaphore = vk_set_signal_semaphore;

    *out_ctx = ctx;
    return true;

fail:
    video_vk_destroy(ctx);
    *out_ctx = NULL;
    return false;
}

void video_vk_context_destroy(struct video_vk_context *ctx)
{
    if (!ctx)
        return;

    if (ctx->device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(ctx->device);

    if (g_frontend.video.hw.context_destroy) {
        g_frontend.video.hw.context_destroy();
        g_frontend.video.hw.context_destroy = NULL;
    }
}

void video_vk_destroy(struct video_vk_context *ctx)
{
    if (!ctx)
        return;

    video_vk_context_destroy(ctx);

    if (ctx->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->device);
        vk_swapchain_teardown(ctx);
        vkDestroyCommandPool(ctx->device, ctx->cmd_pool, NULL);
        vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);

        if (ctx->device_owned_by_core) {
            if (ctx->core_destroy_device)
                ctx->core_destroy_device();
        } else {
            vkDestroyDevice(ctx->device, NULL);
        }
    }

    if (ctx->instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
        vkDestroyInstance(ctx->instance, NULL);
    }

    free(ctx->swapchain_images);
    free(ctx->swapchain_views);
    free(ctx->cmd_buffers);
    free(ctx->images_in_flight);
    free(ctx->pending_wait_semaphores);
    free(ctx->pending_command_buffers);
    free(ctx);
}

void video_vk_present(struct video_vk_context *ctx, unsigned width, unsigned height,
                      bool frame_valid)
{
    if (!ctx)
        return;

    if (ctx->pending_image.create_info.image == VK_NULL_HANDLE)
        return;

    if (width > 0 && height > 0) {
        ctx->last_frame_width = width;
        ctx->last_frame_height = height;
    } else if (ctx->last_frame_width > 0 && ctx->last_frame_height > 0) {
        width = ctx->last_frame_width;
        height = ctx->last_frame_height;
    }

    /* Rebuild after OUT_OF_DATE/SUBOPTIMAL from a previous acquire/present. */
    if (ctx->swapchain_dirty) {
        if (!video_vk_resize(ctx, ctx->window)) {
            /* Recreation failed (e.g. minimised window with zero extent).
             * Drop per-frame sync; we'll try again next frame. */
            vk_clear_pending_frame(ctx);
            return;
        }
        ctx->swapchain_dirty = false;
    }

    uint32_t frame_idx = ctx->frame_index % VK_MAX_FRAMES_IN_FLIGHT;

    /* Wait for the frame slot's fence (CPU-side flow-control: caps the
     * number of frames the CPU may queue ahead of the GPU). */
    vkWaitForFences(ctx->device, 1, &ctx->frame_fence[frame_idx], VK_TRUE, UINT64_MAX);

    uint32_t image_index = 0;
    VkResult r = vkAcquireNextImageKHR(
        ctx->device, ctx->swapchain, UINT64_MAX,
        ctx->image_available[frame_idx], VK_NULL_HANDLE, &image_index);

    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        LOG_WARN("vkAcquireNextImageKHR returned %s; marking swapchain dirty",
                 vk_result_string(r));
        /* Defer recreation to next present so the image_available
         * semaphore from this aborted acquire is not lost. */
        ctx->swapchain_dirty = true;
        vk_clear_pending_frame(ctx);
        return;
    }
    if (!vk_check(r, "vkAcquireNextImageKHR")) {
        LOG_ERROR("Vulkan present aborted after acquire failure (context/device may be lost)");
        vk_clear_pending_frame(ctx);
        return;
    }

    /* Per-image guard: image_count may exceed VK_MAX_FRAMES_IN_FLIGHT
     * (FIFO triple-buffering on most drivers). frame_fence[frame_idx]
     * only protects the CPU from getting too far ahead; it does NOT
     * guarantee that this particular swapchain image's previous
     * submission has finished. If something is still in flight for
     * image_index, wait on its fence before reusing the image. */
    if (ctx->images_in_flight[image_index] != VK_NULL_HANDLE) {
        vkWaitForFences(ctx->device, 1, &ctx->images_in_flight[image_index],
                        VK_TRUE, UINT64_MAX);
    }
    ctx->images_in_flight[image_index] = ctx->frame_fence[frame_idx];

    VkCommandBuffer cmd = ctx->cmd_buffers[image_index];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo cbbi = {0};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!vk_check(vkBeginCommandBuffer(cmd, &cbbi), "vkBeginCommandBuffer")) {
        vk_clear_pending_frame(ctx);
        return;
    }

    /* The core image is available via the VkImageViewCreateInfo stored in retro_vulkan_image.
     *
     * Layout contract (from libretro_vulkan.h): the core must place the
     * source image in `pending_image.image_layout` before signalling
     * set_image(). Passing that layout into the barrier as `oldLayout`
     * matches the Vulkan spec; mismatched layouts produce undefined
     * results, which we treat as the core's responsibility. */
    VkImage core_image = ctx->pending_image.create_info.image;
    VkImageLayout core_layout = ctx->pending_image.image_layout;
    VkImageSubresourceRange core_range =
        ctx->pending_image.create_info.subresourceRange;
    if (core_range.aspectMask == 0)
        core_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (core_range.levelCount == 0)
        core_range.levelCount = 1;
    if (core_range.layerCount == 0)
        core_range.layerCount = 1;

    /* Transition swapchain image to TRANSFER_DST_OPTIMAL */
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = ctx->swapchain_images[image_index];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &barrier);

    /* Transition core image to TRANSFER_SRC_OPTIMAL and acquire ownership
     * when the core rendered on a different queue family. */
    uint32_t core_queue_family = ctx->pending_src_queue_family;
    bool transfer_core_ownership =
        core_queue_family != VK_QUEUE_FAMILY_IGNORED &&
        core_queue_family != ctx->queue_family_index;
    bool core_layout_general = (core_layout == VK_IMAGE_LAYOUT_GENERAL);
    /* Cores are free to produce the libretro image with graphics, compute,
     * transfer, or mixed pipelines. mednafen/beetle PSX HW in particular
     * creates compute pipelines during scene transitions, so a
     * COLOR_ATTACHMENT_OUTPUT-only dependency can miss shader writes and
     * present stale/black data. Use a conservative all-commands memory
     * dependency before our presentation blit.
     *
     * libretro_vulkan.h also specifies that the frontend must not perform
     * layout transitions when the core provides GENERAL; vkCmdBlitImage can
     * read from GENERAL directly, so use memory barriers only in that case. */
    if (core_layout_general && !transfer_core_ownership) {
        VkMemoryBarrier memory_barrier = {0};
        memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             1, &memory_barrier,
                             0, NULL,
                             0, NULL);
    } else {
        barrier.oldLayout = core_layout;
        barrier.newLayout = core_layout_general
                             ? VK_IMAGE_LAYOUT_GENERAL
                             : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = transfer_core_ownership
                                      ? core_queue_family
                                      : VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = transfer_core_ownership
                                      ? ctx->queue_family_index
                                      : VK_QUEUE_FAMILY_IGNORED;
        barrier.image = core_image;
        barrier.subresourceRange = core_range;
        barrier.srcAccessMask = transfer_core_ownership
                                ? 0
                                : VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0, NULL,
                             0, NULL,
                             1, &barrier);
    }

    /* Compute blit region preserving aspect ratio */
    int32_t src_width = (int32_t)width;
    int32_t src_height = (int32_t)height;
    if (src_width == 0) src_width = (int32_t)ctx->swapchain_extent.width;
    if (src_height == 0) src_height = (int32_t)ctx->swapchain_extent.height;

    int dst_x_i, dst_y_i, dst_w_i, dst_h_i;
    float display_aspect = g_av_info.geometry.aspect_ratio;
    if (!(display_aspect > 0.0f))
        display_aspect = (float)src_width / (float)src_height;

    float output_aspect = (float)ctx->swapchain_extent.width /
                          (float)ctx->swapchain_extent.height;
    if (display_aspect > output_aspect) {
        dst_w_i = (int)ctx->swapchain_extent.width;
        dst_h_i = (int)((float)dst_w_i / display_aspect);
        if (dst_h_i < 1)
            dst_h_i = 1;
        dst_x_i = 0;
        dst_y_i = ((int)ctx->swapchain_extent.height - dst_h_i) / 2;
    } else {
        dst_h_i = (int)ctx->swapchain_extent.height;
        dst_w_i = (int)((float)dst_h_i * display_aspect);
        if (dst_w_i < 1)
            dst_w_i = 1;
        dst_x_i = ((int)ctx->swapchain_extent.width - dst_w_i) / 2;
        dst_y_i = 0;
    }
    int32_t dst_x = (int32_t)dst_x_i;
    int32_t dst_y = (int32_t)dst_y_i;
    int32_t dst_w = (int32_t)dst_w_i;
    int32_t dst_h = (int32_t)dst_h_i;

    VkImageBlit blit = {0};
    blit.srcSubresource.aspectMask = core_range.aspectMask;
    blit.srcSubresource.mipLevel = core_range.baseMipLevel;
    blit.srcSubresource.baseArrayLayer = core_range.baseArrayLayer;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0].x = 0;
    blit.srcOffsets[0].y = 0;
    blit.srcOffsets[0].z = 0;
    blit.srcOffsets[1].x = src_width;
    blit.srcOffsets[1].y = src_height;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0].x = dst_x;
    blit.dstOffsets[0].y = dst_y;
    blit.dstOffsets[0].z = 0;
    blit.dstOffsets[1].x = dst_x + dst_w;
    blit.dstOffsets[1].y = dst_y + dst_h;
    blit.dstOffsets[1].z = 1;

    /* Honor rotation from SET_ROTATION. vkCmdBlitImage cannot express a
     * 90/270-degree rotation, so we support only 0 and 180 (axis flip).
     * 180 is achieved by swapping both dst offset corners. */
    unsigned rot = g_frontend.video.rotation & 3;
    if (rot == 1 || rot == 3) {
        LOG_WARN("video_vk_present: rotation %u not supported via "
                 "vkCmdBlitImage; treating as 0", rot);
        rot = 0;
    }
    if (rot == 2) {
        VkOffset3D tmp = blit.dstOffsets[0];
        blit.dstOffsets[0] = blit.dstOffsets[1];
        blit.dstOffsets[1] = tmp;
    }

    /* Clear letterbox/pillarbox regions before blitting. */
    if ((uint32_t)dst_w_i < ctx->swapchain_extent.width ||
        (uint32_t)dst_h_i < ctx->swapchain_extent.height) {
        VkClearColorValue clear_color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkImageSubresourceRange clear_range = {0};
        clear_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clear_range.baseMipLevel = 0;
        clear_range.levelCount = 1;
        clear_range.baseArrayLayer = 0;
        clear_range.layerCount = 1;
        vkCmdClearColorImage(cmd, ctx->swapchain_images[image_index],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear_color, 1, &clear_range);
    }

    vkCmdBlitImage(cmd,
                   core_image,
                   core_layout_general ? VK_IMAGE_LAYOUT_GENERAL
                                       : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   ctx->swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_LINEAR);

    /* Transition swapchain image to PRESENT_SRC_KHR */
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = ctx->swapchain_images[image_index];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &barrier);

    /* Transition core image back to its original layout and release queue
     * family ownership when we acquired it above. */
    if (core_layout_general && !transfer_core_ownership) {
        VkMemoryBarrier memory_barrier = {0};
        memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             1, &memory_barrier,
                             0, NULL,
                             0, NULL);
    } else {
        barrier.oldLayout = core_layout_general
                            ? VK_IMAGE_LAYOUT_GENERAL
                            : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = core_layout;
        barrier.srcQueueFamilyIndex = transfer_core_ownership
                                      ? ctx->queue_family_index
                                      : VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = transfer_core_ownership
                                      ? core_queue_family
                                      : VK_QUEUE_FAMILY_IGNORED;
        barrier.image = core_image;
        barrier.subresourceRange = core_range;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = transfer_core_ownership
                                ? 0
                                : (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0, NULL,
                             0, NULL,
                             1, &barrier);
    }

    if (!vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer")) {
        vk_clear_pending_frame(ctx);
        return;
    }

    /* libretro Vulkan contract: if frame duping is used, or if the core
     * supplies command buffers, the frontend must not wait on the semaphores
     * passed to set_image(). In the command-buffer case those semaphores may
     * be signalled by the submitted command buffers themselves; waiting on
     * them in the same queue submit can deadlock or leave presentation stuck
     * on stale/black frames. Always wait for the WSI acquire semaphore. */
    bool wait_for_core_semaphores = frame_valid &&
                                    ctx->pending_command_buffer_count == 0 &&
                                    ctx->pending_wait_semaphore_count > 0;
    uint32_t wait_capacity = 1 + (wait_for_core_semaphores
                                  ? ctx->pending_wait_semaphore_count : 0);
    VkSemaphore *wait_semaphores = calloc(wait_capacity,
                                          sizeof(*wait_semaphores));
    VkPipelineStageFlags *wait_stages = calloc(wait_capacity,
                                               sizeof(*wait_stages));
    uint32_t submit_cmd_capacity = 1 + ctx->pending_command_buffer_count;
    VkCommandBuffer *submit_cmds = calloc(submit_cmd_capacity,
                                          sizeof(*submit_cmds));
    if (!wait_semaphores || !wait_stages || !submit_cmds) {
        free(wait_semaphores);
        free(wait_stages);
        free(submit_cmds);
        LOG_ERROR("Failed to allocate Vulkan submit arrays");
        vk_clear_pending_frame(ctx);
        return;
    }

    uint32_t wait_count = 0;
    wait_semaphores[wait_count] = ctx->image_available[frame_idx];
    wait_stages[wait_count++] = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (wait_for_core_semaphores) {
        for (uint32_t i = 0; i < ctx->pending_wait_semaphore_count; ++i) {
            wait_semaphores[wait_count] = ctx->pending_wait_semaphores[i];
            wait_stages[wait_count++] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
    }

    uint32_t submit_cmd_count = 0;
    for (uint32_t i = 0; i < ctx->pending_command_buffer_count; ++i)
        submit_cmds[submit_cmd_count++] = ctx->pending_command_buffers[i];
    submit_cmds[submit_cmd_count++] = cmd;

    VkSemaphore signal_semaphores[2];
    uint32_t signal_count = 0;
    signal_semaphores[signal_count++] = ctx->render_finished[frame_idx];
    if (ctx->pending_signal_semaphore != VK_NULL_HANDLE)
        signal_semaphores[signal_count++] = ctx->pending_signal_semaphore;

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = wait_count;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = submit_cmd_count;
    submit_info.pCommandBuffers = submit_cmds;
    submit_info.signalSemaphoreCount = signal_count;
    submit_info.pSignalSemaphores = signal_semaphores;

    vkResetFences(ctx->device, 1, &ctx->frame_fence[frame_idx]);
    VkResult submit_result = vkQueueSubmit(ctx->graphics_queue, 1, &submit_info,
                                           ctx->frame_fence[frame_idx]);
    if (!vk_check(submit_result, "vkQueueSubmit")) {
        LOG_ERROR("Vulkan queue submit failed (valid=%d cmd=%u wait=%u)",
                  frame_valid ? 1 : 0,
                  ctx->pending_command_buffer_count,
                  wait_count);
        vk_restore_frame_fence(ctx, frame_idx);
        free(wait_semaphores);
        free(wait_stages);
        free(submit_cmds);
        g_frontend.running = false;
        vk_clear_pending_frame(ctx);
        return;
    }

    free(wait_semaphores);
    free(wait_stages);
    free(submit_cmds);

    VkPresentInfoKHR present_info = {0};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &ctx->render_finished[frame_idx];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &ctx->swapchain;
    present_info.pImageIndices = &image_index;

    r = vkQueuePresentKHR(ctx->graphics_queue, &present_info);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        LOG_WARN("vkQueuePresentKHR returned %s; marking swapchain dirty",
                 vk_result_string(r));
        /* Driver wants a new swapchain; rebuild on next present. */
        ctx->swapchain_dirty = true;
    } else if (!vk_check(r, "vkQueuePresentKHR")) {
        LOG_ERROR("Vulkan present failed; context/device may be lost");
    }

    ctx->frame_index++;
    vk_clear_pending_frame(ctx);
}

retro_proc_address_t video_vk_get_proc_address(struct video_vk_context *ctx,
                                                const char *sym)
{
    if (!ctx)
        return NULL;

    /* Try device proc address first (covers most functions) */
    if (ctx->get_device_proc_addr) {
        PFN_vkVoidFunction fp = ctx->get_device_proc_addr(ctx->device, sym);
        if (fp)
            return (retro_proc_address_t)fp;
    }

    /* Fallback to instance proc address */
    if (ctx->get_instance_proc_addr) {
        PFN_vkVoidFunction fp = ctx->get_instance_proc_addr(ctx->instance, sym);
        if (fp)
            return (retro_proc_address_t)fp;
    }

    return NULL;
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

    /* vk_swapchain_create() begins with vk_swapchain_teardown(), so
     * the old per-image state is released as part of recreation. */
    return vk_swapchain_create(ctx, window);
}

bool video_vk_negotiate_device(struct video_vk_context *ctx,
                               const struct retro_hw_render_context_negotiation_interface *iface)
{
    if (!ctx || !iface)
        return false;

    if (iface->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
        return false;

    const struct retro_hw_render_context_negotiation_interface_vulkan *vk_iface =
        (const struct retro_hw_render_context_negotiation_interface_vulkan *)iface;

    if (!vk_iface->create_device)
        return false;

    struct retro_vulkan_context retro_ctx;
    memset(&retro_ctx, 0, sizeof(retro_ctx));

    /* The core creates the logical device for negotiation-interface Vulkan
     * cores. Tell it which extensions the frontend needs for presentation;
     * otherwise it may return a device that can render core images but cannot
     * legally own our swapchain. */
    const char *required_device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    /* PPSSPP's vkCreateDevice_libretro dereferences required_features; never pass NULL. */
    VkPhysicalDeviceFeatures required_features = {0};

    bool ok = vk_iface->create_device(&retro_ctx,
                                      ctx->instance,
                                      ctx->physical_device,
                                      ctx->surface,
                                      ctx->get_instance_proc_addr,
                                      required_device_extensions,
                                      1,
                                      NULL, 0,
                                      &required_features);
    if (!ok) {
        LOG_ERROR("video_vk_negotiate_device: create_device failed");
        return false;
    }
    if (retro_ctx.device == VK_NULL_HANDLE || retro_ctx.queue == VK_NULL_HANDLE) {
        LOG_ERROR("video_vk_negotiate_device: core returned incomplete Vulkan context");
        if (vk_iface->destroy_device)
            vk_iface->destroy_device();
        return false;
    }

    LOG_INFO("video_vk_negotiate_device: adopting core device=%p queue=%p qfam=%u present_queue=%p present_qfam=%u",
             (void *)retro_ctx.device,
             (void *)retro_ctx.queue,
             retro_ctx.queue_family_index,
             (void *)retro_ctx.presentation_queue,
             retro_ctx.presentation_queue_family_index);

    VkDevice old_device = ctx->device;
    VkCommandPool old_cmd_pool = ctx->cmd_pool;
    VkSwapchainKHR old_swapchain = ctx->swapchain;
    bool old_owned_by_core = ctx->device_owned_by_core;
    void (*old_core_destroy_device)(void) = ctx->core_destroy_device;

    if (old_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(old_device);

    vk_swapchain_teardown(ctx);
    vkDestroySwapchainKHR(old_device, old_swapchain, NULL);
    ctx->swapchain = VK_NULL_HANDLE;
    vkDestroyCommandPool(old_device, old_cmd_pool, NULL);
    ctx->cmd_pool = VK_NULL_HANDLE;

    if (old_device != VK_NULL_HANDLE && old_device != retro_ctx.device) {
        if (old_owned_by_core) {
            if (old_core_destroy_device)
                old_core_destroy_device();
        } else {
            vkDestroyDevice(old_device, NULL);
        }
    }

    ctx->physical_device = retro_ctx.gpu != VK_NULL_HANDLE
                           ? retro_ctx.gpu : ctx->physical_device;
    ctx->device = retro_ctx.device;
    ctx->graphics_queue = retro_ctx.queue;
    ctx->queue_family_index = retro_ctx.queue_family_index;
    ctx->device_owned_by_core = true;
    ctx->core_destroy_device = vk_iface->destroy_device;
    ctx->get_device_proc_addr = (PFN_vkGetDeviceProcAddr)
        vkGetDeviceProcAddr(ctx->device, "vkGetDeviceProcAddr");

    if (!create_command_pool(ctx) || !vk_swapchain_create(ctx, ctx->window)) {
        LOG_ERROR("video_vk_negotiate_device: failed to rebuild frontend objects on core device");
        return false;
    }

    ctx->hw_if.gpu = ctx->physical_device;
    ctx->hw_if.device = ctx->device;
    ctx->hw_if.queue = ctx->graphics_queue;
    ctx->hw_if.queue_index = ctx->queue_family_index;
    ctx->hw_if.get_device_proc_addr = ctx->get_device_proc_addr;

    return true;
}

/* ----- video_backend vtable adapters ----- */

static bool vb_vk_match(enum retro_hw_context_type type)
{
    return type == RETRO_HW_CONTEXT_VULKAN;
}

static SDL_WindowFlags vb_vk_window_flags(void)
{
    return SDL_WINDOW_VULKAN;
}

static bool vb_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                       void **out_ctx)
{
    struct video_vk_context *ctx = NULL;
    if (!video_vk_init(window, hw, &ctx))
        return false;
    *out_ctx = ctx;
    return true;
}

static void vb_vk_destroy(void *ctx)
{
    video_vk_destroy((struct video_vk_context *)ctx);
}

static void vb_vk_present(void *ctx, const void *data, unsigned width,
                          unsigned height, size_t pitch,
                          enum retro_pixel_format fmt)
{
    (void)pitch;
    (void)fmt;
    video_vk_present((struct video_vk_context *)ctx, width, height,
                     data == RETRO_HW_FRAME_BUFFER_VALID);
}

static bool vb_vk_resize_render_target(void *ctx, unsigned width, unsigned height)
{
    (void)ctx;
    (void)width;
    (void)height;
    return true;
}

static bool vb_vk_resize_output_surface(void *ctx, SDL_Window *window)
{
    return video_vk_resize((struct video_vk_context *)ctx, window);
}

static uintptr_t vb_vk_get_current_framebuffer(void *ctx)
{
    (void)ctx;
    return 0;
}

static retro_proc_address_t vb_vk_get_proc_address(void *ctx, const char *sym)
{
    return video_vk_get_proc_address((struct video_vk_context *)ctx, sym);
}

static bool vb_vk_negotiate_device(void *ctx,
    const struct retro_hw_render_context_negotiation_interface *iface)
{
    return video_vk_negotiate_device((struct video_vk_context *)ctx, iface);
}

static bool vb_vk_get_hw_render_interface(void *ctx,
    const struct retro_hw_render_interface **out_iface)
{
    struct video_vk_context *vk = (struct video_vk_context *)ctx;
    if (!vk || !out_iface)
        return false;
    *out_iface = (const struct retro_hw_render_interface *)&vk->hw_if;
    return true;
}

static void vb_vk_context_destroy(void *ctx)
{
    video_vk_context_destroy((struct video_vk_context *)ctx);
}

const struct video_backend vk_backend = {
    .name                    = "vk",
    .id                      = VIDEO_RENDERER_VULKAN,
    .match_hw_context        = vb_vk_match,
    .window_flags            = vb_vk_window_flags,
    .init                    = vb_vk_init,
    .destroy                 = vb_vk_destroy,
    .present                 = vb_vk_present,
    .resize_render_target    = vb_vk_resize_render_target,
    .resize_output_surface   = vb_vk_resize_output_surface,
    .get_current_framebuffer = vb_vk_get_current_framebuffer,
    .get_proc_address        = vb_vk_get_proc_address,
    .negotiate_device        = vb_vk_negotiate_device,
    .get_hw_render_interface = vb_vk_get_hw_render_interface,
    .context_destroy         = vb_vk_context_destroy,
};

#else /* PURERETRO_VULKAN_ENABLED */

bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx)
{
    (void)window;
    (void)hw;
    (void)out_ctx;
    LOG_ERROR("Vulkan support is not compiled in.");
    return false;
}

void video_vk_destroy(struct video_vk_context *ctx)
{
    (void)ctx;
}

void video_vk_present(struct video_vk_context *ctx, unsigned width, unsigned height,
                      bool frame_valid)
{
    (void)ctx;
    (void)width;
    (void)height;
    (void)frame_valid;
}

retro_proc_address_t video_vk_get_proc_address(struct video_vk_context *ctx,
                                                const char *sym)
{
    (void)ctx;
    (void)sym;
    return NULL;
}

bool video_vk_resize(struct video_vk_context *ctx, SDL_Window *window)
{
    (void)ctx;
    (void)window;
    return false;
}

#endif /* PURERETRO_VULKAN_ENABLED */
