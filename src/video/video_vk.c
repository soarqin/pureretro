/*
 * PureRetro — Vulkan hardware renderer
 *
 * High-level backend glue. Device, swapchain, frame-interface, and
 * presentation helpers live in the sibling video_vk_*.c files.
 */

#include "video_vk.h"
#include "video_vk_internal.h"
#include "video_backend.h"
#include "frontend.h"
#include "log.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

#ifdef PURERETRO_VULKAN_ENABLED

bool video_vk_init(SDL_Window *window, struct retro_hw_render_callback *hw,
                   struct video_vk_context **out_ctx)
{
    struct video_vk_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return false;

    ctx->window = window;
    ctx->pending_src_queue_family = VK_QUEUE_FAMILY_IGNORED;

    if (!video_vk_create_instance(ctx, hw->debug_context))
        goto fail;

    if (!video_vk_create_surface(ctx, window))
        goto fail;

    if (!video_vk_select_physical_device(ctx))
        goto fail;

    if (!video_vk_create_device(ctx))
        goto fail;

    if (!video_vk_create_command_pool(ctx))
        goto fail;

    if (!video_vk_swapchain_create(ctx, window))
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

    ctx->hw_if.set_image = video_vk_set_image;
    ctx->hw_if.get_sync_index = video_vk_get_sync_index;
    ctx->hw_if.get_sync_index_mask = video_vk_get_sync_index_mask;
    ctx->hw_if.lock_queue = video_vk_lock_queue;
    ctx->hw_if.unlock_queue = video_vk_unlock_queue;
    ctx->hw_if.set_command_buffers = video_vk_set_command_buffers;
    ctx->hw_if.wait_sync_index = video_vk_wait_sync_index;
    ctx->hw_if.set_signal_semaphore = video_vk_set_signal_semaphore;

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
        video_vk_swapchain_teardown(ctx);
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
        LOG_ERROR("video_vk_negotiate_device: video_vk_create_device failed");
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

    video_vk_swapchain_teardown(ctx);
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

    if (!video_vk_create_command_pool(ctx) || !video_vk_swapchain_create(ctx, ctx->window)) {
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
