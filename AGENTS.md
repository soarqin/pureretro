# PureRetro — Agent Guidance

This document provides architectural context, coding conventions, and workflow guidance for agents working on the PureRetro codebase.

## Project Identity

PureRetro is a **minimal libretro frontend**. It is educational by design — every line should justify its existence. Avoid feature creep: no GUI framework, no configuration file parser, no shader stack, no rewind/savestate UI.

## Project Status

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1 | ✅ Complete | Project skeleton, documentation, CMake build system, latest `libretro.h` |
| Phase 2 | ✅ Complete | Software-only working frontend: core loading, video (SDL3 texture), audio (SDL3 stream), input (keyboard → RetroPad) |
| Phase 3 | ✅ Complete | OpenGL hardware rendering: context creation, FBO management, presentation blit, context lifecycle (`docs/PLANNED_WORK.md`) |
| Phase 4 | ✅ Complete | Vulkan hardware rendering: instance, device, swapchain, presentation blit, libretro interface (`docs/PLANNED_WORK.md`) |
| Phase 5 | ✅ Complete | Cross-platform polish and testing (`docs/PLANNED_WORK.md`) |

### Verified Build Targets
- **Linux (GCC 13.3, Clang 22, Ubuntu 24.04)**: ✅ Builds and links successfully
- **macOS**: CI-tested (GitHub Actions macos-latest)
- **Windows**: CI-tested (GitHub Actions windows-latest with MSVC and MinGW-w64)

### CMake Notes
SDL3 is resolved automatically:
1. `find_package(SDL3 CONFIG QUIET)` searches the system first.
2. If not found, `FetchContent` downloads SDL3 3.2.4 release tarball and builds it statically.

## Language Rule

**All documentation, code comments, and commit messages must be written in English.**

## Architecture

### Split Renderer Modules

The project uses a split-renderer architecture (as opposed to a monolithic `video.c`):

- `video.c` / `video.h` — Renderer-agnostic code: window creation, renderer selection/dispatch, common state.
- `video_sw.c` / `video_sw.h` — Software renderer using SDL3 textures.
- `video_gl.c` / `video_gl.h` — OpenGL hardware renderer using SDL3 GL context.
- `video_vk.c` / `video_vk.h` — Vulkan hardware renderer using SDL3 Vulkan surface.

This separation keeps each renderer self-contained and easier to reason about in isolation.

### Module Map

```
main       ->  core, video, audio, input, frontend
video      ->  video_sw, video_gl, video_vk (runtime dispatch)
core       ->  libretro.h, frontend
audio      ->  SDL3
input      ->  SDL3
frontend   ->  (shared typedefs and globals)
```

### No libretro-common Policy

We **only** use `libretro.h` from the libretro project. Do not pull in any other files from `libretro-common` (rthreads, rbuf, file/streams, etc.). If you need a utility, implement it locally or use SDL3.

## Coding Style

- **Language:** Pure C. No C++.
- **Standard:** C99 minimum. Use C11 features only if they improve clarity (e.g., `static_assert`).
- **Naming:** `snake_case` for functions and variables; `UPPER_CASE` for macros and constants.
- **Indentation:** 4 spaces. No tabs.
- **Braces:** K&R style — opening brace on the same line.
- **Line length:** Prefer <= 100 characters, but do not sacrifice readability to enforce it.
- **Headers:** Include order: (1) system headers, (2) SDL3 headers, (3) `libretro.h`, (4) local headers.

### Example

```c
#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "libretro.h"
#include "frontend.h"

static bool g_running = true;

bool frontend_is_running(void)
{
    return g_running;
}
```

## Hardware Rendering Notes

### OpenGL

- Create the GL context through SDL3 (`SDL_GL_CreateContext`).
- Provide `get_proc_address` via `SDL_GL_GetProcAddress`.
- The frontend manages an FBO for `get_current_framebuffer`; returning 0 is valid for the default framebuffer but explicit FBO management is preferred for consistency.
- Honor `retro_hw_render_callback::bottom_left_origin`.
- Call `context_reset` after context creation, but **only after** `retro_environment(SET_HW_RENDER)` has returned so the core has finished its own setup. Call `context_destroy` before tearing the context down.

### Vulkan

- Vulkan initialization goes through SDL3 surface creation plus raw Vulkan calls.
- The core requests the Vulkan render interface via `RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE`.
- We must populate a `retro_hw_render_interface` with valid Vulkan handles (device, queue, command pool, swapchain images).
- If full swapchain management would bloat the codebase beyond educational scope, provide the API-level handles and document any presentation limitations clearly.

## Environment Callbacks

The `core.c` module implements the frontend's `retro_environment_t`. Supported callbacks (add as needed):

| Callback | Status | Notes |
|----------|--------|-------|
| `SET_PIXEL_FORMAT` | ✅ Implemented | Stores negotiated format in `g_frontend.video.pixel_format`. |
| `GET_CAN_DUPE` | ✅ Implemented | Always returns true. |
| `SET_HW_RENDER` | ✅ Implemented | Dispatches to GL or VK renderer; returns false for unsupported types. |
| `SET_SYSTEM_AV_INFO` | ✅ Implemented | Updates `g_av_info` and resizes the OpenGL FBO if active. |
| `GET_HW_RENDER_INTERFACE` | ✅ Implemented | Returns populated `retro_hw_render_interface_vulkan` when Vulkan renderer is active. |
| `GET_SYSTEM_DIRECTORY` | ✅ Implemented | Returns NULL. |
| `GET_SAVE_DIRECTORY` | ✅ Implemented | Returns NULL. |
| `GET_LOG_INTERFACE` | ✅ Implemented | Provides `log_stderr` callback. |
| `SHUTDOWN` | ✅ Implemented | Sets `g_frontend.running = false`. |
| `SET_SUPPORT_NO_GAME` | ✅ Implemented | Returns true. |
| `SET_MESSAGE` | ✅ Implemented | Prints to stderr. |
| `GET_VARIABLE` / `SET_VARIABLES` | ✅ Implemented | Stores variables sorted by key; returns the first option as the default. Supports `--variable key=value` CLI overrides. |
| `GET_VARIABLE_UPDATE` | ✅ Implemented | Always returns false. |
| `GET_LIBRETRO_PATH` | ✅ Implemented | Returns `g_frontend.core_path`. |
| `SET_ROTATION` | 📝 Stub | Returns false. |
| `GET_OVERSCAN` | ✅ Implemented | Returns false. |
| `SET_PERFORMANCE_LEVEL` | 📝 Stub | Returns false. |
| `SET_INPUT_DESCRIPTORS` | 📝 Stub | Returns false. |
| `SET_KEYBOARD_CALLBACK` | 📝 Stub | Returns false. |
| `SET_DISK_CONTROL_INTERFACE` | 📝 Stub | Returns false. |
| `SET_FRAME_TIME_CALLBACK` | 📝 Stub | Returns false. |
| `SET_AUDIO_CALLBACK` | 📝 Stub | Returns false. |
| `GET_RUMBLE_INTERFACE` | 📝 Stub | Returns false. |
| `GET_INPUT_DEVICE_CAPABILITIES` | ✅ Implemented | Reports joypad support only. |
| `GET_SENSOR_INTERFACE` | 📝 Stub | Returns false. |
| `GET_CAMERA_INTERFACE` | 📝 Stub | Returns false. |
| `GET_PERF_INTERFACE` | 📝 Stub | Returns false. |
| `GET_LOCATION_INTERFACE` | 📝 Stub | Returns false. |
| `GET_CORE_ASSETS_DIRECTORY` | ✅ Implemented | Returns NULL. |
| `GET_LANGUAGE` | ✅ Implemented | Returns `RETRO_LANGUAGE_ENGLISH`. |
| `SET_CORE_OPTIONS_DISPLAY` | 📝 Stub | Returns false. |
| `SET_VARIABLE` | 📝 Stub | Returns false. |
| `SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE` | ✅ Implemented | Calls core's `create_device` callback for Vulkan. |

## Cross-Platform Rules

- Use SDL3 for platform abstraction whenever possible (windowing, audio, input, dynamic loading).
- For core loading, prefer `SDL_LoadObject` / `SDL_LoadFunction` over raw `dlopen` / `LoadLibrary`.
- Platform-specific `#ifdef` blocks should be minimal and isolated.
- Test compilation logic on all three targets (Windows, Linux, macOS) mentally before committing.

## Agent Workflow

1. **Read this file** before making changes.
2. **Keep changes minimal.** Do not refactor unrelated code.
3. **Update this file** if you change architecture, build options, or style rules.
4. **Commit messages** should be concise English descriptions (e.g., `Add OpenGL FBO management`, `Fix audio crackling on macOS`).
