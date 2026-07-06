/*
 * PureRetro — Vulkan presentation path
 */

#include "video_vk_internal.h"

#include "core.h"
#include "frontend.h"
#include "log.h"

#ifdef PURERETRO_VULKAN_ENABLED

#include <stdlib.h>

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
            video_vk_clear_pending_frame(ctx);
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
                 video_vk_result_string(r));
        /* Defer recreation to next present so the image_available
         * semaphore from this aborted acquire is not lost. */
        ctx->swapchain_dirty = true;
        video_vk_clear_pending_frame(ctx);
        return;
    }
    if (!video_vk_check(r, "vkAcquireNextImageKHR")) {
        LOG_ERROR("Vulkan present aborted after acquire failure (context/device may be lost)");
        video_vk_clear_pending_frame(ctx);
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
    if (!video_vk_check(vkBeginCommandBuffer(cmd, &cbbi), "vkBeginCommandBuffer")) {
        video_vk_clear_pending_frame(ctx);
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

    if (!video_vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer")) {
        video_vk_clear_pending_frame(ctx);
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
        video_vk_clear_pending_frame(ctx);
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
    if (!video_vk_check(submit_result, "vkQueueSubmit")) {
        LOG_ERROR("Vulkan queue submit failed (valid=%d cmd=%u wait=%u)",
                  frame_valid ? 1 : 0,
                  ctx->pending_command_buffer_count,
                  wait_count);
        video_vk_restore_frame_fence(ctx, frame_idx);
        free(wait_semaphores);
        free(wait_stages);
        free(submit_cmds);
        g_frontend.running = false;
        video_vk_clear_pending_frame(ctx);
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
                 video_vk_result_string(r));
        /* Driver wants a new swapchain; rebuild on next present. */
        ctx->swapchain_dirty = true;
    } else if (!video_vk_check(r, "vkQueuePresentKHR")) {
        LOG_ERROR("Vulkan present failed; context/device may be lost");
    }

    ctx->frame_index++;
    video_vk_clear_pending_frame(ctx);
}


#endif /* PURERETRO_VULKAN_ENABLED */
