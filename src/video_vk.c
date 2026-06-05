/*
 * PureRetro — Vulkan hardware renderer
 *
 * Minimal Vulkan context and swapchain management.
 * Presentation uses vkCmdBlitImage (no shaders or render passes).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "video_vk.h"
#include "frontend.h"
#include "core.h"

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
        fprintf(stderr, "Vulkan error in %s: %s (%d)\n", op, vk_result_string(r), (int)r);
        return false;
    }
    return true;
}

static bool create_instance(struct video_vk_context *ctx, bool debug)
{
    Uint32 ext_count = 0;
    const char * const *sdl_exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
    if (!sdl_exts) {
        fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions failed: %s\n", SDL_GetError());
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
        fprintf(stderr, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool select_physical_device(struct video_vk_context *ctx)
{
    uint32_t count = 0;
    VkResult r = vkEnumeratePhysicalDevices(ctx->instance, &count, NULL);
    if (!vk_check(r, "vkEnumeratePhysicalDevices") || count == 0) {
        fprintf(stderr, "No Vulkan physical devices found\n");
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

    fprintf(stderr, "No suitable Vulkan physical device found\n");
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

static bool vk_swapchain_create(struct video_vk_context *ctx, SDL_Window *window)
{
    /* Free old GPU resources and arrays from any previous swapchain */
    if (ctx->swapchain_views) {
        for (uint32_t i = 0; i < ctx->image_count; ++i) {
            if (ctx->swapchain_views[i] != VK_NULL_HANDLE)
                vkDestroyImageView(ctx->device, ctx->swapchain_views[i], NULL);
        }
    }
    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ctx->image_available[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx->device, ctx->image_available[i], NULL);
        if (ctx->render_finished[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx->device, ctx->render_finished[i], NULL);
        if (ctx->frame_fence[i] != VK_NULL_HANDLE)
            vkDestroyFence(ctx->device, ctx->frame_fence[i], NULL);
    }
    free(ctx->swapchain_images);
    free(ctx->swapchain_views);
    free(ctx->framebuffers);
    free(ctx->cmd_buffers);
    ctx->swapchain_images = NULL;
    ctx->swapchain_views = NULL;
    ctx->framebuffers = NULL;
    ctx->cmd_buffers = NULL;
    ctx->image_count = 0;

    VkSurfaceCapabilitiesKHR caps;
    VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        ctx->physical_device, ctx->surface, &caps);
    if (!vk_check(r, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return false;

    /* Choose format */
    uint32_t fmt_count = 0;
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &fmt_count, NULL);
    if (!vk_check(r, "vkGetPhysicalDeviceSurfaceFormatsKHR"))
        return false;
    VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * fmt_count);
    if (!formats)
        return false;
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, ctx->surface, &fmt_count, formats);
    if (!vk_check(r, "vkGetPhysicalDeviceSurfaceFormatsKHR")) {
        free(formats);
        return false;
    }

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
    ctx->swapchain_format = chosen.format;

    /* Choose present mode: prefer FIFO (vsync), fallback to first available */
    uint32_t pm_count = 0;
    r = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &pm_count, NULL);
    if (!vk_check(r, "vkGetPhysicalDeviceSurfacePresentModesKHR"))
        return false;
    VkPresentModeKHR *modes = malloc(sizeof(VkPresentModeKHR) * pm_count);
    if (!modes)
        return false;
    r = vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, ctx->surface, &pm_count, modes);
    if (!vk_check(r, "vkGetPhysicalDeviceSurfacePresentModesKHR")) {
        free(modes);
        return false;
    }

    VkPresentModeKHR present_mode = modes[0];
    for (uint32_t i = 0; i < pm_count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            break;
        }
    }
    free(modes);

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
        return false;

    if (ctx->swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
    ctx->swapchain = new_swapchain;

    /* Retrieve swapchain images */
    r = vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->image_count, NULL);
    if (!vk_check(r, "vkGetSwapchainImagesKHR"))
        return false;
    ctx->swapchain_images = calloc(ctx->image_count, sizeof(VkImage));
    ctx->swapchain_views = calloc(ctx->image_count, sizeof(VkImageView));
    ctx->framebuffers = calloc(ctx->image_count, sizeof(VkFramebuffer));
    ctx->cmd_buffers = calloc(ctx->image_count, sizeof(VkCommandBuffer));
    if (!ctx->swapchain_images || !ctx->swapchain_views ||
        !ctx->framebuffers || !ctx->cmd_buffers) {
        fprintf(stderr, "Failed to allocate swapchain arrays\n");
        return false;
    }
    r = vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->image_count, ctx->swapchain_images);
    if (!vk_check(r, "vkGetSwapchainImagesKHR"))
        return false;

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
            return false;
    }

    /* Allocate command buffers */
    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = ctx->cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = ctx->image_count;
    r = vkAllocateCommandBuffers(ctx->device, &cbai, ctx->cmd_buffers);
    if (!vk_check(r, "vkAllocateCommandBuffers"))
        return false;

    /* Create sync objects */
    VkSemaphoreCreateInfo sci_sem = {0};
    sci_sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        r = vkCreateSemaphore(ctx->device, &sci_sem, NULL, &ctx->image_available[i]);
        if (!vk_check(r, "vkCreateSemaphore (image_available)"))
            return false;
        r = vkCreateSemaphore(ctx->device, &sci_sem, NULL, &ctx->render_finished[i]);
        if (!vk_check(r, "vkCreateSemaphore (render_finished)"))
            return false;
        r = vkCreateFence(ctx->device, &fci, NULL, &ctx->frame_fence[i]);
        if (!vk_check(r, "vkCreateFence"))
            return false;
    }

    return true;
}

/* --- Stubs for functions implemented in later tasks --- */

bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx)
{
    struct video_vk_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return false;

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

    *out_ctx = ctx;

    if (hw->context_reset)
        hw->context_reset();

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

    if (ctx->device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(ctx->device);

    for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ctx->image_available[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx->device, ctx->image_available[i], NULL);
        if (ctx->render_finished[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx->device, ctx->render_finished[i], NULL);
        if (ctx->frame_fence[i] != VK_NULL_HANDLE)
            vkDestroyFence(ctx->device, ctx->frame_fence[i], NULL);
    }

    if (ctx->cmd_pool != VK_NULL_HANDLE) {
        if (ctx->cmd_buffers)
            vkFreeCommandBuffers(ctx->device, ctx->cmd_pool, ctx->image_count, ctx->cmd_buffers);
        vkDestroyCommandPool(ctx->device, ctx->cmd_pool, NULL);
    }

    if (ctx->swapchain_views) {
        for (uint32_t i = 0; i < ctx->image_count; ++i) {
            if (ctx->swapchain_views[i] != VK_NULL_HANDLE)
                vkDestroyImageView(ctx->device, ctx->swapchain_views[i], NULL);
        }
    }

    if (ctx->swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);

    if (ctx->device != VK_NULL_HANDLE)
        vkDestroyDevice(ctx->device, NULL);

    if (ctx->surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);

    if (ctx->instance != VK_NULL_HANDLE)
        vkDestroyInstance(ctx->instance, NULL);

    free(ctx->swapchain_images);
    free(ctx->swapchain_views);
    free(ctx->framebuffers);
    free(ctx->cmd_buffers);
    free(ctx);
}

void video_vk_present(struct video_vk_context *ctx)
{
    (void)ctx;
    /* Implemented in Task 7 */
}

retro_proc_address_t video_vk_get_proc_address(struct video_vk_context *ctx,
                                                const char *sym)
{
    (void)ctx;
    (void)sym;
    /* Implemented in Task 8 */
    return NULL;
}

bool video_vk_resize(struct video_vk_context *ctx, SDL_Window *window)
{
    if (!ctx || ctx->device == VK_NULL_HANDLE)
        return false;

    vkDeviceWaitIdle(ctx->device);

    /* Free old command buffers and image views; vk_swapchain_create handles the rest */
    if (ctx->cmd_pool != VK_NULL_HANDLE && ctx->cmd_buffers)
        vkFreeCommandBuffers(ctx->device, ctx->cmd_pool, ctx->image_count, ctx->cmd_buffers);

    for (uint32_t i = 0; i < ctx->image_count; ++i) {
        if (ctx->swapchain_views && ctx->swapchain_views[i] != VK_NULL_HANDLE)
            vkDestroyImageView(ctx->device, ctx->swapchain_views[i], NULL);
    }
    free(ctx->swapchain_images);
    free(ctx->swapchain_views);
    free(ctx->framebuffers);
    free(ctx->cmd_buffers);
    ctx->swapchain_images = NULL;
    ctx->swapchain_views = NULL;
    ctx->framebuffers = NULL;
    ctx->cmd_buffers = NULL;
    ctx->image_count = 0;

    return vk_swapchain_create(ctx, window);
}

#else /* PURERETRO_VULKAN_ENABLED */

bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx)
{
    (void)window;
    (void)hw;
    (void)out_ctx;
    fprintf(stderr, "Vulkan support is not compiled in.\n");
    return false;
}

void video_vk_destroy(struct video_vk_context *ctx)
{
    (void)ctx;
}

void video_vk_present(struct video_vk_context *ctx)
{
    (void)ctx;
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
