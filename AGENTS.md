# PureRetro — Agent Guidance

This document provides architectural context, coding conventions, and workflow guidance for agents working on the PureRetro codebase.

## Project Identity

PureRetro is a **minimal libretro frontend**. It is educational by design — every line should justify its existence. Avoid feature creep: no GUI framework, no configuration file parser, no shader stack, no rewind UI. Savestates are intentionally simple: `--savestate <file>` auto-loads on startup; no in-app save/load UI or hotkeys.

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
SDL3 and other dependencies are managed via CPM (`cmake/Dependencies.cmake`):
- By default SDL3 is fetched and built from source (3.2.4 release tarball, static).
- Pass `-DPURERETRO_PREFER_SYSTEM_SDL3=ON` to prefer a system-installed SDL3 (via `find_package(SDL3 CONFIG)`), falling back to CPM if not found.

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
main            ->  core, video, audio, input, frontend
video           ->  video_backend (vtable) -> video_sw, video_gl, video_vk
core            ->  libretro.h, frontend, core_variables
core_variables  ->  libretro.h, frontend, core_variables_parse
audio           ->  SDL3
input           ->  SDL3
frontend        ->  (shared typedefs and globals)
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

The `core.c` module implements the frontend's `retro_environment_t`. The table below lists callbacks that have explicit case branches; everything else falls through to the `default` arm (logged and returns false). For the full inventory and future work, see `docs/DEVELOPMENT_PLAN.md`.

| Callback | Status | Notes |
|----------|--------|-------|
| `SET_PIXEL_FORMAT` | ✅ Implemented | Stores negotiated format in `g_frontend.video.pixel_format`. |
| `GET_CAN_DUPE` | ✅ Implemented | Always returns true. |
| `SET_HW_RENDER` | ✅ Implemented | Dispatches to GL or VK renderer; returns false for unsupported types. |
| `SET_SYSTEM_AV_INFO` | ✅ Implemented | Updates `g_av_info` and resizes the active HW render target. |
| `SET_GEOMETRY` | ✅ Implemented | Updates geometry + resizes the window. |
| `GET_HW_RENDER_INTERFACE` | ✅ Implemented | Returns the populated `retro_hw_render_interface_vulkan` when Vulkan is active. |
| `GET_PREFERRED_HW_RENDER` | ✅ Implemented | Reports the user's `--render` preference. |
| `SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE` | ✅ Implemented | Calls the core's `create_device` callback for Vulkan. |
| `GET_SYSTEM_DIRECTORY` | ✅ Implemented | Returns `g_frontend.system_directory` (resolved by `SDL_GetPrefPath` or `--portable`). |
| `GET_SAVE_DIRECTORY` | ✅ Implemented | Returns `g_frontend.save_directory`, falling back to system directory. |
| `GET_CORE_ASSETS_DIRECTORY` | ✅ Implemented | Returns `g_frontend.core_assets_directory` (settable via `--core-assets-dir`). |
| `GET_LOG_INTERFACE` | ✅ Implemented | Bridges core log messages to the loglevel-aware logger (`src/log.c`). |
| `GET_VFS_INTERFACE` | ✅ Implemented | v1 stdio-backed VFS (see `vfs.c`). |
| `SHUTDOWN` | ✅ Implemented | Sets `g_frontend.running = false`. |
| `SET_SUPPORT_NO_GAME` | ✅ Implemented | Returns true. |
| `SET_MESSAGE` | ✅ Implemented | Prints to stderr. |
| `GET_VARIABLE` / `SET_VARIABLES` | ✅ Implemented | Lookup: `cli_overrides` -> `disk_overrides` -> default. |
| `SET_CORE_OPTIONS` / `SET_CORE_OPTIONS_INTL` / `SET_CORE_OPTIONS_V2` / `SET_CORE_OPTIONS_V2_INTL` | ✅ Implemented | All definition formats normalize to `core_options_table`. |
| `SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK` | ✅ Implemented | Stores the callback for later use. |
| `SET_VARIABLE` | ✅ Implemented | Updates `core_options` + `disk_overrides` for runtime persistence. |
| `GET_VARIABLE_UPDATE` | ✅ Implemented | Always returns false. |
| `GET_CORE_OPTIONS_VERSION` | ✅ Implemented | Returns 2. |
| `GET_LIBRETRO_PATH` | ✅ Implemented | Returns `g_frontend.core_path`. |
| `GET_LANGUAGE` | ✅ Implemented | Returns `g_frontend.language` (default `ENGLISH`, overridable via `--lang`). |
| `GET_USERNAME` | ✅ Implemented | Returns `g_frontend.username` (set via `--username`); returns false when unset. |
| `GET_OVERSCAN` | ✅ Implemented | Returns false. |
| `SET_KEYBOARD_CALLBACK` | ✅ Implemented | Stores the keyboard callback in `g_frontend.keyboard_callback`. |
| `GET_INPUT_BITMASKS` | ✅ Implemented | Returns true; `core_input_state` answers JOYPAD_MASK. |
| `GET_INPUT_DEVICE_CAPABILITIES` | ✅ Implemented | Reports JOYPAD only. |
| `SET_CONTROLLER_INFO` | ✅ Implemented | Deep-copies per-port descriptions into `g_frontend.controller_ports[]`. |
| `SET_DISK_CONTROL_EXT_INTERFACE` | ✅ Implemented | Stores callbacks + applies `--disk-index <N>`. |
| `GET_DISK_CONTROL_INTERFACE_VERSION` | ✅ Implemented | Returns 1. |
| `GET_CURRENT_SOFTWARE_FRAMEBUFFER` | ✅ Implemented | Zero-copy SW path: locked SDL_Texture pixels; present() detects match and unlocks. |
| `GET_AUDIO_VIDEO_ENABLE` | ✅ Implemented | Bit 0 = video (always on), bit 1 = audio (reflected via `--no-audio`). |
| `GET_FASTFORWARDING` | ✅ Implemented | Reflects `g_frontend.fast_forward_active`. |
| `GET_TARGET_REFRESH_RATE` | ✅ Implemented | Reports real display refresh rate via `SDL_GetCurrentDisplayMode` (fallback 60Hz). |
| `GET_INPUT_MAX_USERS` | ✅ Implemented | Returns 1 (keyboard-only port 0). |
| `GET_TARGET_SAMPLE_RATE` | ✅ Implemented | Returns core's reported sample rate (fallback 48000). |
| `SET_AUDIO_BUFFER_STATUS_CALLBACK` | ✅ Implemented | Stores callback; invoked per frame before `retro_run()`. |
| `SET_MINIMUM_AUDIO_LATENCY` | ✅ Implemented | Adjusts queue depth cap (no SDL audio reinit). |
| `GET_PLAYLIST_DIRECTORY` | ✅ Implemented | Returns `g_frontend.playlist_directory` (settable via `--playlist-dir`). |
| `GET_FILE_BROWSER_START_DIRECTORY` | ✅ Implemented | Returns `g_frontend.file_browser_directory` (settable via `--file-browser-dir`). |
| `SET_DISK_CONTROL_INTERFACE` | ✅ Implemented | Legacy 7-field struct bridged to the EXT path via memcpy. |
| `GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT` | ✅ Implemented | Returns `interface_version = 2` for Vulkan; 0 for unknown API types. |
| `GET_THROTTLE_STATE` | ✅ Implemented | Returns `{ RETRO_THROTTLE_NONE, core_fps }`. |
| `GET_SAVESTATE_CONTEXT` | ✅ Implemented | Returns `RETRO_SAVESTATE_CONTEXT_NORMAL`. |
| `GET_JIT_CAPABLE` | ✅ Implemented | Returns true (all desktop targets allow JIT). |
| `GET_MESSAGE_INTERFACE_VERSION` | ✅ Implemented | Returns 1. |
| `SET_MESSAGE_EXT` | ✅ Implemented | Routes via logger; `TARGET_LOG` uses message level, OSD/ALL go to INFO. |
| `SET_SERIALIZATION_QUIRKS` | ✅ Implemented | Logs declared quirks; frontend requires none. |
| `SET_SUPPORT_ACHIEVEMENTS` | ✅ Implemented | Stored in `g_frontend.core_supports_achievements` + INFO log. |
| `SET_HW_SHARED_CONTEXT` | ✅ Implemented | Sets `g_frontend.video.hw_shared_context_requested`; honored at next GL context creation via `SDL_GL_SHARE_WITH_CURRENT_CONTEXT`. |
| `SET_PROC_ADDRESS_CALLBACK` | ✅ Implemented | Stores `get_proc_address` for optional future use (frontend currently defines no core extension symbols). |
| `SET_SUBSYSTEM_INFO` | ✅ Implemented | Deep-copies the subsystem array (ident/desc/ROMs/memory). With `--subsystem <ident>` set, `core_init` calls `retro_load_game_special`. |
| `SET_MEMORY_MAPS` | ✅ Implemented | Deep-copies the descriptor array + addrspace strings. Future use for achievements/rewind. |
| `SET_FASTFORWARDING_OVERRIDE` | ✅ Implemented | Toggles `g_frontend.fast_forward_active`; run_loop skips the frame-pacing delay and `GET_FASTFORWARDING` / `GET_THROTTLE_STATE` reflect the state. |
| `SET_CONTENT_INFO_OVERRIDE` | ✅ Implemented | Deep-copies the override array. Frontend keeps ROM data alive until shutdown regardless of `persistent_data`. |
| `GET_GAME_INFO_EXT` | ✅ Implemented | Returns the populated extended info; valid inside `retro_load_game`/`retro_load_game_special`. |
| `SET_ROTATION` | ✅ Implemented | Stored in `g_frontend.video.rotation` (0/90/180/270 CCW). SW renderer rotates natively; GL/VK support 0 and 180 only (90/270 logged + treated as 0). |
| `SET_PERFORMANCE_LEVEL` | ✅ Implemented | Logs the core's hint at INFO level. |
| `SET_INPUT_DESCRIPTORS` | 📝 Stub | Returns false. |
| `SET_FRAME_TIME_CALLBACK` | ✅ Implemented | Stored callback; invoked once per frame before `retro_run()` with actual microseconds (or `reference` on first frame / after a >1s stall). |
| `SET_AUDIO_CALLBACK` | 📝 Stub | Returns false. |
| `SET_CORE_OPTIONS_DISPLAY` | ✅ Implemented | Updates the `visible` flag on the matching `core_option`. Returns false when the key was not declared. |
| `GET_RUMBLE_INTERFACE` | 📝 Stub (Not planned) | Returns false. |
| `GET_SENSOR_INTERFACE` | 📝 Stub (Not planned) | Returns false. |
| `GET_CAMERA_INTERFACE` | 📝 Stub (Not planned) | Returns false. |
| `GET_PERF_INTERFACE` | 📝 Stub (Not planned) | Returns false. |
| `GET_LOCATION_INTERFACE` | 📝 Stub (Not planned) | Returns false. |

## Command-Line Flags

`main.c` parses these after `<core> [<content>]`:

| Flag | Argument | Purpose |
|------|----------|---------|
| `--fullscreen`, `-f` | — | Start in fullscreen mode. |
| `--render <api>` | `vk` / `gl` / `sw` | Hint for `GET_PREFERRED_HW_RENDER`. |
| `--scale <N>` | 1–16 | Integer window scale. |
| `--no-audio` | — | Disable audio (`GET_AUDIO_VIDEO_ENABLE` bit 1 cleared). |
| `--variable <k=v>` | `key=value` | CLI override for a core option (highest priority). |
| `--portable` | — | Use `./system/` for system directory. |
| `--config <path>` | file path | Keymap configuration. |
| `--disk-index <N>` | 0–255 | Initial disc index for multi-disc content. |
| `--lang <code>` | locale code | Language for `GET_LANGUAGE` (30+ codes supported). |
| `--username <name>` | string | Player name for `GET_USERNAME`. |
| `--subsystem <ident>` | string | Load content through `retro_load_game_special` using the subsystem with the matching `ident`. |
| `--core-assets-dir <path>` | dir path | Returned by `GET_CORE_ASSETS_DIRECTORY`. |
| `--playlist-dir <path>` | dir path | Returned by `GET_PLAYLIST_DIRECTORY`. |
| `--file-browser-dir <path>` | dir path | Returned by `GET_FILE_BROWSER_START_DIRECTORY`. |
| `--audio-rate <Hz>` | 4000–384000 | Override audio sample rate (default: core's reported rate). |
| `--audio-buffer-ms <ms>` | 1–5000 | Override minimum audio buffer latency (default: `FRONTEND_AUDIO_BUFFER_MS`). |
| `--log-level <lvl>` | `debug` / `info` / `warn` / `error` | Override logger threshold (also `PURERETRO_LOG` env var). Default `info`. |
| `--savestate <path>` | file path | Load a savestate file after core init. |

## Logging

All frontend code and forwarded core log messages route through the
loglevel-aware logger in `src/log.c`. The format is
`[HH:MM:SS.mmm] [LEVEL] [SRC] message`, where `SRC` is `FRONTEND` for
in-frontend messages and `CORE` for those forwarded via
`RETRO_ENVIRONMENT_GET_LOG_INTERFACE`. The active level is chosen as
`--log-level` > `PURERETRO_LOG` env var > `info` default. Messages below
the active level are dropped cheaply. Internal code uses the
`LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR` macros from `log.h`; pre-init
CLI-usage output remains direct `fprintf(stderr, ...)`.

## Persistence

- **SRAM (.srm)**: Auto-loaded from `<save_dir>/<content_basename>.srm` after core init,
  auto-saved before core unload. Uses `retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)`.
  No-op when the core has no SRAM region or the content path is NULL.
  The `save_directory` is resolved as `g_frontend.save_directory` (falling back to
  `system_directory`). Missing `.srm` files are silently ignored.
- **Savestates**: `--savestate <file>` loads a savestate (via `retro_unserialize`)
  once after core init. `retro_serialize` / `retro_unserialize` / `retro_serialize_size`
  are loaded as optional symbols; cores that do not export them silently no-op.
  No in-app hotkey or auto-save — keep persistence intentional and minimal.

## Cross-Platform Rules

- Use SDL3 for platform abstraction whenever possible (windowing, audio, input, dynamic loading).
- For core loading, prefer `SDL_LoadObject` / `SDL_LoadFunction` over raw `dlopen` / `LoadLibrary`.
- Platform-specific `#ifdef` blocks should be minimal and isolated.
- Test compilation logic on all three targets (Windows, Linux, macOS) mentally before committing.

## Testing

Unit tests live under `tests/unit/` and use the Unity framework (pulled via CPM).
Dependencies are managed by `cmake/Dependencies.cmake`. SDL3 may be resolved
from the system (`-DPURERETRO_PREFER_SYSTEM_SDL3=ON`) or fetched via CPM
(default). Build and run:

```bash
cmake -S . -B build/
cmake --build build/ --parallel
ctest --test-dir build/ --output-on-failure
```

To skip tests entirely: `-DPURERETRO_BUILD_TESTS=OFF`.

Currently `tests/unit/` contains a smoke test only. Upcoming work
will add coverage for pure-function modules: `core_variables_parse`,
`core_variables`, `vfs` (real tmp files), `log_level_from_string`,
`input` keymap parser, CLI `parse_cli`. Subsystems requiring live
SDL/GL/Vulkan context are intentionally not unit-tested.

## Agent Workflow

1. **Read this file** before making changes.
2. **Keep changes minimal.** Do not refactor unrelated code.
3. **Update this file** if you change architecture, build options, or style rules.
4. **Commit messages** should be concise English descriptions (e.g., `Add OpenGL FBO management`, `Fix audio crackling on macOS`).
