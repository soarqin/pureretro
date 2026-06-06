/*
 * libretro_vulkan.h — Vulkan render interface for libretro
 *
 * Adapted from upstream libretro-common. Included here because the
 * project's libretro.h references this interface but does not define it.
 */

#ifndef LIBRETRO_VULKAN_H__
#define LIBRETRO_VULKAN_H__

#include <stdint.h>
#include <vulkan/vulkan.h>
#include "libretro.h"

#define RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION 5
#define RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION 2

struct retro_vulkan_image
{
    VkImageView image_view;
    VkImageLayout image_layout;
    VkImageViewCreateInfo create_info;
};

typedef void (*retro_vulkan_set_image_t)(void *handle,
    const struct retro_vulkan_image *image,
    uint32_t num_semaphores,
    const VkSemaphore *semaphores,
    uint32_t src_queue_family);

typedef uint32_t (*retro_vulkan_get_sync_index_t)(void *handle);
typedef uint32_t (*retro_vulkan_get_sync_index_mask_t)(void *handle);
typedef void (*retro_vulkan_set_command_buffers_t)(void *handle,
    uint32_t num_cmd,
    const VkCommandBuffer *cmd);
typedef void (*retro_vulkan_wait_sync_index_t)(void *handle);
typedef void (*retro_vulkan_lock_queue_t)(void *handle);
typedef void (*retro_vulkan_unlock_queue_t)(void *handle);
typedef void (*retro_vulkan_set_signal_semaphore_t)(void *handle, VkSemaphore semaphore);

struct retro_hw_render_interface_vulkan
{
    enum retro_hw_render_interface_type interface_type;
    unsigned interface_version;
    void *handle;
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkDevice device;
    PFN_vkGetDeviceProcAddr get_device_proc_addr;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    VkQueue queue;
    unsigned queue_index;
    retro_vulkan_set_image_t set_image;
    retro_vulkan_get_sync_index_t get_sync_index;
    retro_vulkan_get_sync_index_mask_t get_sync_index_mask;
    retro_vulkan_set_command_buffers_t set_command_buffers;
    retro_vulkan_wait_sync_index_t wait_sync_index;
    retro_vulkan_lock_queue_t lock_queue;
    retro_vulkan_unlock_queue_t unlock_queue;
    retro_vulkan_set_signal_semaphore_t set_signal_semaphore;
};

struct retro_vulkan_context
{
    VkPhysicalDevice gpu;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family_index;
    VkQueue presentation_queue;
    uint32_t presentation_queue_family_index;
};

struct retro_hw_render_context_negotiation_interface_vulkan
{
    enum retro_hw_render_interface_type interface_type;
    unsigned interface_version;
    const VkApplicationInfo *(*get_application_info)(void);
    bool (*create_device)(struct retro_vulkan_context *context,
                          VkInstance instance,
                          VkPhysicalDevice gpu,
                          VkSurfaceKHR surface,
                          PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                          const char **required_device_extensions,
                          unsigned num_required_device_extensions,
                          const char **required_device_layers,
                          unsigned num_required_device_layers,
                          const VkPhysicalDeviceFeatures *required_features);
    void (*destroy_device)(void);
};

#endif
