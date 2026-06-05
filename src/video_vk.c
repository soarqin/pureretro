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
        fprintf(stderr, "Vulkan error in %s: %s (0x%x)\n", op, vk_result_string(r), r);
        return false;
    }
    return true;
}

static bool create_instance(struct video_vk_context *ctx, bool debug)
{
    Uint32 ext_count = 0;
    char const * const *sdl_exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
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
    vkEnumeratePhysicalDevices(ctx->instance, &count, NULL);
    if (count == 0) {
        fprintf(stderr, "No Vulkan physical devices found\n");
        return false;
    }

    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * count);
    vkEnumeratePhysicalDevices(ctx->instance, &count, devices);

    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, NULL);
        VkQueueFamilyProperties *qf_props = malloc(sizeof(VkQueueFamilyProperties) * qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, qf_props);

        for (uint32_t q = 0; q < qf_count; ++q) {
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, ctx->surface, &present_support);

            if ((qf_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
                uint32_t ext_count = 0;
                vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, NULL);
                VkExtensionProperties *exts = malloc(sizeof(VkExtensionProperties) * ext_count);
                vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, exts);

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
                    free(qf_props);
                    free(devices);
                    return true;
                }
            }
        }
        free(qf_props);
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

/* --- Stubs for functions implemented in later tasks --- */

bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx)
{
    (void)window;
    (void)hw;
    (void)out_ctx;
    /* Implemented in Task 5 */
    return false;
}

void video_vk_destroy(struct video_vk_context *ctx)
{
    (void)ctx;
    /* Implemented in Task 5 */
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
    (void)ctx;
    (void)window;
    /* Implemented in Task 5 */
    return true;
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
