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
    /* Tear down every swapchain-derived GPU object owned by ctx, and
     * reset all related pointers/handles to a clean zero state. Safe
     * to call repeatedly and on a partially-constructed ctx (every
     * call site checks for VK_NULL_HANDLE first). */
    if (ctx->swapchain_views) {
        for (uint32_t i = 0; i < ctx->image_count; ++i) {
            if (ctx->swapchain_views[i] != VK_NULL_HANDLE)
                vkDestroyImageView(ctx->device, ctx->swapchain_views[i], NULL);
        }
    }
    if (ctx->cmd_pool != VK_NULL_HANDLE && ctx->cmd_buffers &&
        ctx->image_count > 0) {
        vkFreeCommandBuffers(ctx->device, ctx->cmd_pool,
                             ctx->image_count, ctx->cmd_buffers);
    }
    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ctx->image_available[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(ctx->device, ctx->image_available[i], NULL);
            ctx->image_available[i] = VK_NULL_HANDLE;
        }
        if (ctx->render_finished[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(ctx->device, ctx->render_finished[i], NULL);
            ctx->render_finished[i] = VK_NULL_HANDLE;
        }
        if (ctx->frame_fence[i] != VK_NULL_HANDLE) {
            vkDestroyFence(ctx->device, ctx->frame_fence[i], NULL);
            ctx->frame_fence[i] = VK_NULL_HANDLE;
        }
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
    /* I-3 contract: on early-return false, ctx is left in a clean zero
     * state — all swapchain-derived GPU objects destroyed, all slot
     * pointers/handles NULL/VK_NULL_HANDLE. Callers may safely retry
     * (the next call starts from a clean slate) or invoke
     * video_vk_destroy(). On success, ctx is fully populated. */

    VkSurfaceFormatKHR *formats = NULL;
    VkPresentModeKHR *modes = NULL;
    VkResult r;

    /* Tear down any previous swapchain state first. */
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

    if (ctx->swapchain != VK_NULL_HANDLE)
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
    /* I-3 cleanup: ensure ctx is in a clean zero state after partial
     * construction failures, so a retry or video_vk_destroy() works. */
    free(formats);
    free(modes);
    vk_swapchain_teardown(ctx);
    if (ctx->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
        ctx->swapchain = VK_NULL_HANDLE;
    }
    return false;
}

static void vk_set_image(void *handle, const struct retro_vulkan_image *image,
                         uint32_t num_semaphores, const VkSemaphore *semaphores,
                         uint32_t src_queue_family)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    (void)num_semaphores;
    (void)semaphores;
    (void)src_queue_family;
    if (image) {
        ctx->pending_image = *image;
        ctx->has_pending_image = true;
    }
}

static uint32_t vk_get_sync_index(void *handle)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    return ctx->frame_index % ctx->image_count;
}

static uint32_t vk_get_sync_index_mask(void *handle)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    uint32_t n = ctx->image_count;
    /* Guard against UB: shifting a uint32_t by 32 is undefined. The libretro
     * interface uses a 32-bit mask, so cap at 32 swapchain images. */
    if (n >= 32)
        return UINT32_MAX;
    return (1u << n) - 1u;
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
    (void)handle;
    (void)num_cmd;
    (void)cmd;
    /* Stub: not supported in minimal implementation */
}

static void vk_wait_sync_index(void *handle)
{
    (void)handle;
    /* Stub */
}

static void vk_set_signal_semaphore(void *handle, VkSemaphore semaphore)
{
    (void)handle;
    (void)semaphore;
    /* Stub */
}

/* --- Stubs for functions implemented in later tasks --- */

bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx)
{
    struct video_vk_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return false;

    ctx->window = window;

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

    /* Function pointers will be set in Task 6 */
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

void video_vk_destroy(struct video_vk_context *ctx)
{
    if (!ctx)
        return;

    /* Tear down everything that needs the device first, all inside one
     * guarded block. Anything destroyed outside this block must not touch
     * ctx->device. */
    if (ctx->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->device);

        for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
            if (ctx->image_available[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(ctx->device, ctx->image_available[i], NULL);
            if (ctx->render_finished[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(ctx->device, ctx->render_finished[i], NULL);
            if (ctx->frame_fence[i] != VK_NULL_HANDLE)
                vkDestroyFence(ctx->device, ctx->frame_fence[i], NULL);
        }

        if (ctx->cmd_pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(ctx->device, ctx->cmd_pool, NULL);

        if (ctx->swapchain_views) {
            for (uint32_t i = 0; i < ctx->image_count; ++i) {
                if (ctx->swapchain_views[i] != VK_NULL_HANDLE)
                    vkDestroyImageView(ctx->device, ctx->swapchain_views[i], NULL);
            }
        }

        if (ctx->swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);

        vkDestroyDevice(ctx->device, NULL);
    }

    if (ctx->instance != VK_NULL_HANDLE) {
        if (ctx->surface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
        vkDestroyInstance(ctx->instance, NULL);
    }

    free(ctx->swapchain_images);
    free(ctx->swapchain_views);
    free(ctx->cmd_buffers);
    free(ctx->images_in_flight);
    free(ctx);
}

void video_vk_present(struct video_vk_context *ctx, unsigned width, unsigned height)
{
    if (!ctx || !ctx->has_pending_image)
        return;

    /* I-2: if a previous present/acquire reported the swapchain
     * out-of-date or suboptimal, rebuild it before doing anything
     * else this frame. */
    if (ctx->swapchain_dirty) {
        if (!video_vk_resize(ctx, ctx->window)) {
            /* Recreation failed (e.g. minimised window with zero extent).
             * Drop the pending image; we'll try again next frame. */
            ctx->has_pending_image = false;
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
        /* Defer recreation to next present so the image_available
         * semaphore from this aborted acquire is not lost. */
        ctx->swapchain_dirty = true;
        ctx->has_pending_image = false;
        return;
    }
    if (!vk_check(r, "vkAcquireNextImageKHR"))
        return;

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

    /* Reset fence only after successful acquire to avoid deadlock on failure */
    vkResetFences(ctx->device, 1, &ctx->frame_fence[frame_idx]);

    VkCommandBuffer cmd = ctx->cmd_buffers[image_index];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo cbbi = {0};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!vk_check(vkBeginCommandBuffer(cmd, &cbbi), "vkBeginCommandBuffer"))
        return;

    /* The core image is available via the VkImageViewCreateInfo stored in retro_vulkan_image.
     *
     * Layout contract (from libretro_vulkan.h): the core must place the
     * source image in `pending_image.image_layout` before signalling
     * set_image(). Passing that layout into the barrier as `oldLayout`
     * matches the Vulkan spec; mismatched layouts produce undefined
     * results, which we treat as the core's responsibility. */
    VkImage core_image = ctx->pending_image.create_info.image;
    VkImageLayout core_layout = ctx->pending_image.image_layout;

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

    /* Transition core image to TRANSFER_SRC_OPTIMAL */
    barrier.oldLayout = core_layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = core_image;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &barrier);

    /* Compute blit region preserving aspect ratio */
    int32_t src_width = (int32_t)width;
    int32_t src_height = (int32_t)height;
    if (src_width == 0) src_width = (int32_t)ctx->swapchain_extent.width;
    if (src_height == 0) src_height = (int32_t)ctx->swapchain_extent.height;

    int dst_x_i, dst_y_i, dst_w_i, dst_h_i;
    fit_aspect((unsigned)src_width, (unsigned)src_height,
               (int)ctx->swapchain_extent.width,
               (int)ctx->swapchain_extent.height,
               &dst_x_i, &dst_y_i, &dst_w_i, &dst_h_i);
    int32_t dst_x = (int32_t)dst_x_i;
    int32_t dst_y = (int32_t)dst_y_i;
    int32_t dst_w = (int32_t)dst_w_i;
    int32_t dst_h = (int32_t)dst_h_i;

    VkImageBlit blit = {0};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
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

    /* I-5: clear the swapchain image to black before blitting so the
     * letterbox/pillarbox regions don't retain previous-frame garbage.
     * Only needed when the blit destination doesn't cover the full
     * swapchain extent. */
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
                   core_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   ctx->swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_LINEAR);

    /* Transition swapchain image to PRESENT_SRC_KHR */
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.image = ctx->swapchain_images[image_index];
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &barrier);

    /* Transition core image back to its original layout */
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = core_layout;
    barrier.image = core_image;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &barrier);

    if (!vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer"))
        return;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &ctx->image_available[frame_idx];
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &ctx->render_finished[frame_idx];

    if (!vk_check(vkQueueSubmit(ctx->graphics_queue, 1, &submit_info,
                                ctx->frame_fence[frame_idx]), "vkQueueSubmit"))
        return;

    VkPresentInfoKHR present_info = {0};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &ctx->render_finished[frame_idx];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &ctx->swapchain;
    present_info.pImageIndices = &image_index;

    r = vkQueuePresentKHR(ctx->graphics_queue, &present_info);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        /* Driver wants a new swapchain; rebuild on next present. */
        ctx->swapchain_dirty = true;
    } else {
        vk_check(r, "vkQueuePresentKHR");
    }

    ctx->frame_index++;
    ctx->has_pending_image = false;
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

    /* PPSSPP's vkCreateDevice_libretro dereferences required_features; never pass NULL. */
    VkPhysicalDeviceFeatures required_features = {0};

    bool ok = vk_iface->create_device(&retro_ctx,
                                       ctx->instance,
                                       ctx->physical_device,
                                       ctx->surface,
                                       ctx->get_instance_proc_addr,
                                       NULL, 0, NULL, 0, &required_features);
    if (!ok) {
        LOG_ERROR("video_vk_negotiate_device: create_device failed");
        return false;
    }

    LOG_INFO("video_vk_negotiate_device: create_device succeeded, device=%p, queue=%p",
             (void *)retro_ctx.device, (void *)retro_ctx.queue);
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
    (void)data;
    (void)pitch;
    (void)fmt;
    video_vk_present((struct video_vk_context *)ctx, width, height);
}

static bool vb_vk_resize(void *ctx, SDL_Window *window,
                         unsigned width, unsigned height)
{
    (void)width;
    (void)height;
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
    (void)ctx;
}

const struct video_backend vk_backend = {
    .name                    = "vk",
    .id                      = VIDEO_RENDERER_VULKAN,
    .match_hw_context        = vb_vk_match,
    .window_flags            = vb_vk_window_flags,
    .init                    = vb_vk_init,
    .destroy                 = vb_vk_destroy,
    .present                 = vb_vk_present,
    .resize                  = vb_vk_resize,
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

void video_vk_present(struct video_vk_context *ctx, unsigned width, unsigned height)
{
    (void)ctx;
    (void)width;
    (void)height;
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
