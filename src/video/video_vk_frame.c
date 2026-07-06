/*
 * PureRetro — Vulkan libretro frame interface
 */

#include "video_vk_internal.h"

#include "log.h"

#ifdef PURERETRO_VULKAN_ENABLED

#include <stdlib.h>

static bool video_vk_reserve_wait_semaphores(struct video_vk_context *ctx,
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

static bool video_vk_reserve_command_buffers(struct video_vk_context *ctx,
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

void video_vk_set_image(void *handle, const struct retro_vulkan_image *image,
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
        if (!video_vk_reserve_wait_semaphores(ctx, num_semaphores)) {
            LOG_ERROR("Failed to store Vulkan core wait semaphores");
            video_vk_clear_pending_frame(ctx);
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

void video_vk_clear_pending_frame(struct video_vk_context *ctx)
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

void video_vk_restore_frame_fence(struct video_vk_context *ctx, uint32_t frame_idx)
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

uint32_t video_vk_get_sync_index(void *handle)
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

uint32_t video_vk_get_sync_index_mask(void *handle)
{
    (void)handle;
    return (1u << VK_MAX_FRAMES_IN_FLIGHT) - 1u;
}

void video_vk_lock_queue(void *handle)
{
    (void)handle;
    /* No-op: single-threaded queue submission for minimal frontend */
}

void video_vk_unlock_queue(void *handle)
{
    (void)handle;
    /* No-op */
}

void video_vk_set_command_buffers(void *handle, uint32_t num_cmd,
                                   const VkCommandBuffer *cmd)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    if (!ctx)
        return;

    ctx->pending_command_buffer_count = 0;
    if (!cmd || num_cmd == 0)
        return;

    if (!video_vk_reserve_command_buffers(ctx, num_cmd)) {
        LOG_ERROR("Failed to store Vulkan core command buffers");
        video_vk_clear_pending_frame(ctx);
        return;
    }

    for (uint32_t i = 0; i < num_cmd; ++i) {
        if (cmd[i] != VK_NULL_HANDLE) {
            ctx->pending_command_buffers[
                ctx->pending_command_buffer_count++] = cmd[i];
        }
    }

}

void video_vk_wait_sync_index(void *handle)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    if (!ctx || ctx->device == VK_NULL_HANDLE || ctx->image_count == 0)
        return;

    uint32_t frame_idx = ctx->frame_index % VK_MAX_FRAMES_IN_FLIGHT;
    vkWaitForFences(ctx->device, 1, &ctx->frame_fence[frame_idx],
                    VK_TRUE, UINT64_MAX);
}

void video_vk_set_signal_semaphore(void *handle, VkSemaphore semaphore)
{
    struct video_vk_context *ctx = (struct video_vk_context *)handle;
    if (!ctx)
        return;
    ctx->pending_signal_semaphore = semaphore;
}


#endif /* PURERETRO_VULKAN_ENABLED */
