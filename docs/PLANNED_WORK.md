# PureRetro — Planned Work

This document tracks the remaining implementation phases for PureRetro. Phase 1 (skeleton) and Phase 2 (software-only core) are complete.

---

## Phase 3 — OpenGL Hardware Rendering

### Goal
Enable cores that request `RETRO_HW_CONTEXT_OPENGL*` to render via an OpenGL context managed by the frontend.

### Tasks

1. **Context creation lifecycle**
   - Verify `video_gl_init()` correctly handles all OpenGL context types:
     - `RETRO_HW_CONTEXT_OPENGL` (compatibility)
     - `RETRO_HW_CONTEXT_OPENGL_CORE` (core profile)
     - `RETRO_HW_CONTEXT_OPENGLES2`
     - `RETRO_HW_CONTEXT_OPENGLES3`
     - `RETRO_HW_CONTEXT_OPENGLES_VERSION`
   - Ensure `version_major` / `version_minor` are passed to `SDL_GL_SetAttribute`.
   - Handle `debug_context` flag.

2. **FBO robustness**
   - Currently the FBO uses `GL_RGBA8` / `GL_RGBA`. Verify this works with core expectations.
   - Add depth/stencil attachments if `hw->depth` or `hw->stencil` is true.
   - Resize FBO when the core calls `RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO` with new max dimensions.

3. **Context reset / destroy**
   - Implement `video_gl_context_destroy()` to call `hw->context_destroyed` before tearing down the GL context.
   - Handle `hw->cache_context`: if false, recreate context on fullscreen toggle or window resize.

4. **Presentation path**
   - Verify `video_gl_present()` correctly presents the FBO contents.
   - Some cores render directly to the default framebuffer (0); ensure returning 0 from `video_gl_get_current_framebuffer()` is supported as a fallback.

5. **Testing**
   - Test with an OpenGL hardware core (e.g., beetle-psx-hw, Mupen64Plus-Next, or flycast).
   - Verify frame output, audio sync, and input response.

---

## Phase 4 — Vulkan Hardware Rendering

### Goal
Enable cores that request `RETRO_HW_CONTEXT_VULKAN` to render via a Vulkan context.

### Tasks

1. **Vulkan initialization**
   - Create `VkInstance` with required extensions (including `VK_KHR_surface` and platform-specific surface extension).
   - Create `VkSurfaceKHR` via `SDL_Vulkan_CreateSurface`.
   - Select a suitable `VkPhysicalDevice` and queue family.
   - Create `VkDevice` and retrieve graphics queue.

2. **Swapchain management**
   - Create a `VkSwapchainKHR` with double or triple buffering.
   - Retrieve swapchain images and create `VkImageView`s.
   - Implement `video_vk_present()` to acquire next image and present.

3. **libretro Vulkan interface**
   - Respond to `RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE` with a populated `retro_hw_render_interface` struct.
   - Provide `get_proc_address` via `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr`.

4. **Risk mitigation**
   - Vulkan swapchain code is significantly more complex than OpenGL. If the minimal implementation proves too large, provide the API-level handles and document presentation limitations.

5. **Testing**
   - Test with a Vulkan-capable core (e.g., beetle-psx-hw with Vulkan renderer).

---

## Phase 5 — Polish & Cross-Platform Verification

### Goal
Ensure the frontend is stable and builds cleanly on all three target platforms.

### Tasks

1. **Platform builds**
   - **Linux**: Verify with GCC and Clang. Test on X11 and Wayland.
   - **macOS**: Verify with Xcode Command Line Tools. Test Metal fallback if needed. Document MoltenVK dependency for Vulkan.
   - **Windows**: Verify with Visual Studio 2022 and MinGW-w64.

2. **Window management**
   - Handle window resizing correctly for all three renderers.
   - Fix fullscreen toggle edge cases (resolution change, multi-monitor).

3. **Audio polish**
   - Fix audio crackling or drift if observed during testing.
   - Consider adding audio resampling if core sample rate doesn't match device rate.

4. **Command-line enhancements**
   - Add `--scale <N>` for integer window scaling.
   - Add `--fullscreen` (already partially implemented).
   - Add `--no-audio` for silent mode.

5. **CI / build automation (optional)**
   - Add a GitHub Actions workflow to build on Linux, macOS, and Windows.

---

## Deferred / Future Ideas

- Gamepad input support (SDL3 gamepad API)
- Savestate support via libretro callbacks
- On-screen display for core messages
- Shader support (not aligned with minimal philosophy, likely out of scope)
