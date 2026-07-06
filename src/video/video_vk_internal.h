/*
 * PureRetro — Private Vulkan renderer helpers
 */

#ifndef VIDEO_VK_INTERNAL_H
#define VIDEO_VK_INTERNAL_H

#include "video_vk.h"

#ifdef PURERETRO_VULKAN_ENABLED

#include <vulkan/vulkan.h>

const char *video_vk_result_string(VkResult r);
bool video_vk_check(VkResult r, const char *op);

bool video_vk_create_instance(struct video_vk_context *ctx, bool debug);
bool video_vk_create_surface(struct video_vk_context *ctx, SDL_Window *window);
bool video_vk_select_physical_device(struct video_vk_context *ctx);
bool video_vk_create_device(struct video_vk_context *ctx);
bool video_vk_create_command_pool(struct video_vk_context *ctx);

void video_vk_swapchain_teardown(struct video_vk_context *ctx);
bool video_vk_swapchain_create(struct video_vk_context *ctx, SDL_Window *window);

void video_vk_clear_pending_frame(struct video_vk_context *ctx);
void video_vk_restore_frame_fence(struct video_vk_context *ctx, uint32_t frame_idx);

void video_vk_set_image(void *handle, const struct retro_vulkan_image *image,
                        uint32_t num_semaphores, const VkSemaphore *semaphores,
                        uint32_t src_queue_family);
uint32_t video_vk_get_sync_index(void *handle);
uint32_t video_vk_get_sync_index_mask(void *handle);
void video_vk_lock_queue(void *handle);
void video_vk_unlock_queue(void *handle);
void video_vk_set_command_buffers(void *handle, uint32_t num_cmd,
                                  const VkCommandBuffer *cmd);
void video_vk_wait_sync_index(void *handle);
void video_vk_set_signal_semaphore(void *handle, VkSemaphore semaphore);

#endif /* PURERETRO_VULKAN_ENABLED */

#endif /* VIDEO_VK_INTERNAL_H */
