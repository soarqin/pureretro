# PureRetro — Runtime Validation Matrix (Phase 6.1)

This document is the checklist for validating PureRetro against real
libretro cores. All prior work (Phase 1–5 features, Phase 6.2 unit tests,
Phase 6.3 module split) was landed after code review only; the runtime
matrix here is the gap that turns "reviewed correct" into "verified on
hardware."

Every row must be exercised at least once before we declare Phase 6
complete. Record the outcome inline (fill the **Result** column) and open
a follow-up issue for any deviation.

## How to run

1. Build the release binary:
   ```bash
   cmake -S . -B build/ -DPURERETRO_ENABLE_VULKAN=ON
   cmake --build build/ --parallel
   ```
2. Fetch the core listed for the scenario (any recent RetroArch buildbot
   `.so` / `.dll` / `.dylib` is fine).
3. Run the exact command in the **Command** column; watch for the
   **Verification points**.
4. For Vulkan rows, launch with the validation layer enabled:
   ```bash
   VK_LOADER_LAYERS_ENABLE=VK_LAYER_KHRONOS_validation ./pureretro ...
   ```
   Zero validation errors during startup, one minute of gameplay, and
   window resize/minimize/restore is the pass bar.

## Test matrix

Legend for **Result**: `PASS` / `FAIL: <short note + issue link>` /
`SKIP: <reason>`.

### Software renderer (`--render sw`)

| # | Scenario | Core | Command | Verification points | Result |
|---|----------|------|---------|---------------------|--------|
| SW-1 | XRGB8888 basic frame | `nestopia_libretro` | `pureretro nestopia_libretro.so game.nes` | Game boots, renders, audio OK; F11 fullscreen toggles; ESC quits | |
| SW-2 | RGB565 pixel format path | `snes9x_libretro` | `pureretro snes9x_libretro.so game.sfc` | Colors correct (no red/blue swap), no `LOG_ERROR("Unsupported pixel format")` | |
| SW-3 | Duplicate-frame handling | `fceumm_libretro` (any NES ROM, pause the emulator via core menu if available) | Same as SW-1, then let the game idle so cores may issue `retro_video_refresh(NULL, ...)` | No SDL texture recreation spam; screen stays visible; no `LOG_ERROR` about zero-size texture | |
| SW-4 | Zero-copy framebuffer path | `gambatte_libretro` (calls `GET_CURRENT_SOFTWARE_FRAMEBUFFER`) | `pureretro gambatte_libretro.so game.gb` | With `--log-level debug`, observe the SW zero-copy lock path being hit; no visual glitches | |
| SW-5 | SRAM auto-save/load | `snes9x_libretro` with a game that uses SRAM (e.g. Zelda ALttP) | Play, save in-game, quit; relaunch and confirm the save persists | `<save_dir>/<basename>.srm` created; contents survive relaunch | |
| SW-6 | Savestate auto-load | Any SW core | `pureretro <core> <content> --savestate my.state` | State loads once at startup; no in-app hotkey; failure is non-fatal | |

### OpenGL renderer (`--render gl`)

> **Choosing a GL core:** Not every core has a GL renderer. `mgba` is
> software-only on desktop builds — running it with `--render gl` correctly
> falls back to the SW backend and logs
> `"user requested 'gl' but renderer is 'sw'"`. Pick a core that is known
> to call `SET_HW_RENDER(OPENGL_CORE)` (see below).

| # | Scenario | Core | Command | Verification points | Result |
|---|----------|------|---------|---------------------|--------|
| GL-1 | Basic HW render | `mupen64plus_next_libretro` (GL) or `dolphin_libretro` | `pureretro <core> <content> --render gl` | Frame renders through FBO; presentation blit correct; `LOG_INFO("Final active renderer: gl")`; no HW→SW fallback warning | |
| GL-2 | `need_fullpath` core | `mupen64plus_next_libretro` | `pureretro mupen64plus_next_libretro.so game.n64 --render gl` | Core receives real path, not memory buffer; `game.data == NULL, game.size == 0` visible under debug logging | |
| GL-3 | Window resize stability | Any GL core | Start GL core, resize window repeatedly for ~30 s | FBO not recreated; NO `context_destroy`/`context_reset` calls per resize (Phase 3 fix); presentation scales correctly | |
| GL-4 | Geometry change mid-run | Core that changes AV info (some N64 cores on video plugin switch) | Trigger a resolution change in-game | `resize_render_target` fires, `resize_output_surface` does not; window stays same size | |
| GL-5 | Shared GL context | `beetle_psx_hw_libretro` | `pureretro beetle_psx_hw_libretro.so game.chd --render gl` (needs PSX BIOS in `--system-dir`) | Core requests `SET_HW_SHARED_CONTEXT`; frontend sets `SDL_GL_SHARE_WITH_CURRENT_CONTEXT`; core boots into GL mode (not SW fallback) | |
| GL-6 | `SET_ROTATION` 180° | Any GL core with a rotate option, or any core after in-game rotation | Force rotation 2 via `--variable` or core option | Image flips vertically; 90/270 are logged as unsupported by GL/VK and treated as 0 | |

### Vulkan renderer (`--render vk`)

Run every Vulkan row with `VK_LOADER_LAYERS_ENABLE=VK_LAYER_KHRONOS_validation`.

| # | Scenario | Core | Command | Verification points | Result |
|---|----------|------|---------|---------------------|--------|
| VK-1 | Basic Vulkan HW render | `parallel_n64_libretro` (with Vulkan renderer option) or `beetle_psx_hw_libretro` (Vulkan) | `pureretro <core> <content> --render vk` | Frame renders; zero validation errors during startup + 1 minute of gameplay | |
| VK-2 | Sync contract | Same VK-1 core | Play through several minutes of intense scenes | No `WRITE_AFTER_WRITE`, `SYNC_HAZARD_*`, or layout-transition errors from the validation layer | |
| VK-3 | Window resize / swapchain recreate | Any VK core | Resize window multiple times, then minimize+restore | Swapchain recreated in `resize_output_surface`; core render target unchanged; no out-of-date / suboptimal errors persist | |
| VK-4 | Cross-queue-family transfer | A VK core that uses a different queue family for graphics vs. present (rare; skip if unavailable) | `pureretro <core> <content> --render vk` | Queue-family ownership transfer executed once per frame (Phase 4 fix); no validation errors | |
| VK-5 | Negotiation interface | Core that supplies `create_device` (e.g. `parallel_n64` Vulkan) | Same as VK-1 | Frontend calls the core's `create_device`; core-provided device / physical device / queue used for rendering | |
| VK-6 | `GET_HW_RENDER_INTERFACE` | Any VK core | Same as VK-1 with `--log-level debug` | Frontend returns a populated `retro_hw_render_interface_vulkan`; core proceeds past interface acquisition | |

### Environment / lifecycle features

| # | Scenario | Core | Command | Verification points | Result |
|---|----------|------|---------|---------------------|--------|
| ENV-1 | `--disk-index` on multi-disc content | `pcsx_rearmed_libretro` with a multi-disc `.m3u` | `pureretro pcsx_rearmed_libretro.so game.m3u --disk-index 1` | Core boots on disc 2; disk control ext callbacks invoked; `set_initial_image` honored | |
| ENV-2 | `--subsystem` load path | `mesen-s_libretro` or `sameboy_libretro` with SGB support | `pureretro <core> game.gb --subsystem sgb` | `retro_load_game_special` called instead of `retro_load_game`; matching subsystem descriptor used | |
| ENV-3 | `SET_CONTENT_INFO_OVERRIDE` | Any core that registers overrides (e.g. `beetle_pce`) | `pureretro <core> game.iso --log-level debug` | Extension-matched override applied to `need_fullpath` / `persistent_data`; visible in debug logs | |
| ENV-4 | `GET_LANGUAGE` | Any core with localization (e.g. `snes9x`) | `pureretro <core> <content> --lang ja` | Core reports Japanese strings when supported | |
| ENV-5 | `GET_USERNAME` | Any core that reads it | `pureretro <core> <content> --username Player1` | Confirm via core menu or savestate metadata | |
| ENV-6 | Fast-forward override | Core that toggles `SET_FASTFORWARDING_OVERRIDE` (many cores during loading screens) | Watch during long load / disc swap | Frame-pacing delay skipped; `GET_FASTFORWARDING` / `GET_THROTTLE_STATE` reflect true | |
| ENV-7 | Frame-time callback | Core that registers `SET_FRAME_TIME_CALLBACK` | Any run | Callback receives real microsecond delta, `reference` on frame 0 and after >1 s stalls | |
| ENV-8 | Minimum audio latency | Any core | `pureretro <core> <content> --audio-buffer-ms 32` | Audio queue depth cap changes; no SDL audio reinit; `LOG_INFO` confirms override | |
| ENV-9 | `--audio-rate` override | Any core | `pureretro <core> <content> --audio-rate 44100` | Core's sample rate replaced; log line confirms; playback pitch correct | |
| ENV-10 | Portable mode | Any core needing BIOS | `pureretro <core> <content> --portable` (BIOS placed in `./system/`) | System directory = `./system/`; SDL pref path not consulted | |
| ENV-11 | Persisted core options | Any core with variables (e.g. `nestopia`) | Run, change a core option, quit; relaunch | `<system_dir>/<core>.opt` created; changed value survives; CLI `--variable` still wins | |

## Non-scope for 6.1

The following are intentionally not covered by this matrix:

- Rumble / Sensors / Camera / Location / MIDI / Microphone / Perf
  (marked "Not planned" in AGENTS.md).
- Keyboard-to-analog axis mapping (open TODO in `core.c`).
- Automated CI runs of live cores (needs sandboxed host + core corpus;
  out of scope for a minimal educational frontend).

## When a row fails

1. Reproduce with `--log-level debug`; capture the full log tail.
2. If the failure is in frontend code, open a targeted issue and add
   a unit test where feasible (see `tests/unit/`).
3. If the failure is core-specific (e.g. a newer core requires an env
   callback we do not implement), record the environment number in
   AGENTS.md's callback table with status `📝 Stub (needs core X)`
   before implementing.

## Sign-off

Phase 6.1 is complete when every row above has a non-empty **Result**
and every `FAIL` has either been fixed or explicitly deferred with a
linked issue.
