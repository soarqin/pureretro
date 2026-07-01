# PureRetro Repair and Refactor Plan — 2026-07-01

## Scope

This plan is a read-only review outcome for the current repository. It focuses on defects and unreasonable implementation choices that affect correctness, portability, maintainability, or documentation accuracy. It intentionally avoids feature expansion beyond PureRetro's minimal educational frontend goal.

## Current implementation status

Phases 1-5 have been executed after this review was written. The remaining validation gap is manual runtime testing with real OpenGL/Vulkan libretro cores and Vulkan validation layers; automated unit tests intentionally do not create live renderer contexts.

## Current validation baseline

- `ctest --test-dir build/ --output-on-failure` passes: 8/8 tests.
- Unit coverage includes smoke, VFS, content load-policy helpers, and input keymap/callback behavior. Core variables, log level parsing, CLI parsing, and live renderer context paths remain future test candidates.

## Historical findings

The following findings are preserved as the original repair brief. Items in phases 1-5 have been addressed by the subsequent implementation; use this section as historical context rather than as a current defect list.

### P0 — Correctness defects to fix first

1. **Software duplicate frames can create a zero-size texture.**
   - Evidence: `video_sw_present()` always calls `ensure_texture(ctx, width, height, ...)` before handling `data == NULL` or `width == height == 0` (`src/video_sw.c:100-116`). `GET_CAN_DUPE` returns true, so cores may legally send duplicate frames with no new pixel buffer.
   - Risk: SDL texture creation failure or log spam on normal libretro duplicate-frame behavior.
   - Fix: Track the last valid frame dimensions in `video_sw_context`. If `data == NULL` and dimensions are zero, present the existing texture without recreating it. If no previous texture exists, return quietly.

2. **Content loading ignores `retro_system_info.need_fullpath` and content overrides.**
   - Evidence: `core_init()` retrieves `retro_system_info`, but `core_load_game()` always reads the whole content file into memory and always sets `game.data` / `game.size` (`src/core.c:410-424`). `SET_CONTENT_INFO_OVERRIDE` is stored but not used for load decisions (`src/core.c:1933-1967`).
   - Risk: cores that require a real path may fail or behave incorrectly; large or archive-like content is unnecessarily loaded into memory.
   - Fix: Store `retro_system_info.need_fullpath` / `block_extract` in frontend state after `retro_get_system_info()`. Resolve per-content override by extension. When `need_fullpath` is true, pass `path` with `data = NULL`, `size = 0`. When false, keep current memory-backed behavior and honor `persistent_data` only as far as minimal lifetime rules allow.

3. **OpenGL window resize incorrectly resizes the core render FBO.**
   - Evidence: `video_process_event()` passes window pixel dimensions to every hardware backend resize callback (`src/video.c:133-164`). `video_gl_resize()` recreates the libretro render FBO and calls `context_destroy/reset` for those dimensions (`src/video_gl.c:309-335`).
   - Risk: resizing the host window changes the core render target, triggers unnecessary context resets, and may break cores that expect the FBO to match libretro geometry rather than window size.
   - Fix: Split backend operations into `resize_render_target` and `resize_surface` or add a backend flag/callback distinction. Window events should resize Vulkan swapchain/surface only. OpenGL should keep its FBO at core `max_width/max_height`; presentation should scale to the current window.

4. **Vulkan presentation ignores core-provided synchronization objects.**
   - Evidence: `vk_set_image()` discards `num_semaphores`, `semaphores`, and `src_queue_family` (`src/video_vk.c:478-489`). `set_command_buffers`, `wait_sync_index`, and `set_signal_semaphore` are stubs (`src/video_vk.c:521-540`).
   - Risk: races between core rendering and frontend blit/present; Vulkan cores that rely on the libretro interface contract may render corrupted frames or hit validation errors.
   - Fix: Either implement the minimal synchronization contract or document Vulkan as experimental and fail unsupported paths explicitly. Preferred minimal fix: store the semaphores from `set_image()`, include them in `vkQueueSubmit` waits, handle queue-family ownership when needed, and wire the command-buffer/signal-semaphore callbacks if required by target cores.

### P1 — High-value cleanup and behavioral fixes

5. **Hardware renderer fallback is misleading.**
   - Evidence: when a requested hardware backend fails to initialize, `video_set_hw_render()` creates a software backend but still returns false (`src/video.c:219-228`).
   - Risk: the core sees hardware setup failure while the frontend appears to have fallen back; the process may continue in a state that cannot present the core's hardware frames.
   - Fix: For hardware context requests, fail cleanly without installing software fallback, or return true only for a deliberate software context request. Keep fallback messaging out of hardware-core paths.

6. **Keyboard callback cores do not receive keys mapped to RetroPad.**
   - Evidence: `input_process_event()` returns immediately after updating the joypad state for mapped scancodes (`src/input.c:319-324`), so arrows, Enter, Z/X/A/S/Q/W, etc. are not forwarded to registered keyboard callbacks (`src/input.c:326-344`).
   - Risk: computer cores that register keyboard callbacks cannot receive common keys that are also default RetroPad bindings.
   - Fix: Decide and document the policy. A practical minimal policy is: always update joypad state, and also forward recognized keys to the keyboard callback when registered. Keep frontend hotkeys guarded in `main.c` as today.

7. **Keymap documentation does not match parser syntax.**
   - Evidence: README says `<scancode> <button>` / `SPACE A` (`README.md:179`), while the parser requires `scancode=button` (`src/input.c:254-260`).
   - Risk: user-visible configuration failure.
   - Fix: Either update README to `SPACE=A` or expand the parser to accept both whitespace and `=` forms. Prefer accepting both to preserve existing configs and match docs.

8. **Documentation language policy is now clean, but should remain guarded.**
   - Evidence: obsolete completed planning documents were removed from `docs/`; the remaining repository documentation contains no Chinese prose at the time of this plan update. `AGENTS.md` still requires all documentation, code comments, and commit messages to be English.
   - Risk: future planning notes or review reports may reintroduce non-English documentation into the tracked docs tree.
   - Fix: Keep all new or revised documentation in English. If non-English scratch notes are useful during development, keep them outside tracked project documentation or translate them before committing.

### P2 — Testability and maintainability improvements

9. **Core option and CLI behavior lack unit tests.**
   - Evidence: current tests cover only smoke and VFS; AGENTS.md already lists `core_variables_parse`, `core_variables`, `log_level_from_string`, `input` keymap parser, and CLI parsing as remaining pure-function candidates.
   - Risk: table-driven parsing regressions go unnoticed.
   - Fix: Add tests in this order: `core_variables_parse`, `core_variables` table lookup/override precedence, keymap parser syntax, log level parser. Consider extracting CLI parsing into a testable module before testing it.

10. **`main.c` remains broad despite table-driven CLI parsing.**
    - Evidence: `main.c` handles CLI, frontend initialization, main loop, frame timing, audio setup, SRAM/savestate orchestration, and shutdown.
    - Risk: future changes to lifecycle and frame loop are harder to reason about.
    - Fix: Do only small extractions: `frontend_lifecycle.c` for init/shutdown and `run_loop.c` for event/frame pacing. Do not introduce a framework or configuration subsystem.

11. **Renderer backend interface conflates render-target and surface lifecycle.**
    - Evidence: one `resize` callback is used for software no-op, OpenGL FBO recreation, and Vulkan swapchain recreation.
    - Risk: P0 OpenGL bug and future backend confusion.
    - Fix: Refactor `video_backend` minimally: keep `present`, add `resize_output_surface` for window events, and keep/rename `resize_render_target` for libretro geometry changes.

## Proposed implementation phases

### Phase 1 — Safe correctness fixes

1. Fix software duplicate-frame handling.
2. Fix README/keymap syntax mismatch; optionally accept both `KEY=BUTTON` and `KEY BUTTON`.
3. Remove or clarify hardware fallback behavior for failed HW contexts.
4. Add focused unit tests where possible:
   - software duplicate-frame handling may need a small helper test or be validated manually with a core that duplicates frames;
   - keymap parser can be unit-tested with temporary files;
   - hardware fallback can be covered by a narrow helper if backend selection is made injectable; otherwise validate by code review.

Validation:

```bash
cmake --build build/ --parallel
ctest --test-dir build/ --output-on-failure
cmake -S . -B build-novk -DPURERETRO_ENABLE_VULKAN=OFF
cmake --build build-novk --parallel
```

### Phase 2 — Libretro content contract

1. Store `retro_system_info` load-relevant fields in `frontend_state`.
2. Implement helper functions:
   - content extension extraction;
   - matching `content_overrides` by extension list;
   - deciding whether to pass full path or memory data.
3. Update `game_info_ext_populate()` to reflect the same decision.
4. Add pure tests for extension matching and load decision helpers.

Validation:

```bash
ctest --test-dir build/ --output-on-failure
# Manual: run one path-required core and one memory-backed core.
```

### Phase 3 — Renderer lifecycle refactor

1. Split backend resize semantics in `video_backend.h`.
2. Update `video_process_event()` to call output-surface resize only.
3. Update `env_set_geometry()` / `env_set_system_av_info()` paths to resize render targets only.
4. For OpenGL, keep FBO size tied to core geometry, not window size.
5. For Vulkan, keep swapchain recreation tied to window/surface events and pending out-of-date state.

Validation:

```bash
cmake --build build/ --parallel
ctest --test-dir build/ --output-on-failure
# Manual: OpenGL core, resize window repeatedly, verify no unnecessary context reset churn and correct presentation.
# Manual: Vulkan core, resize window/minimize/restore, verify swapchain recreation.
```

### Phase 4 — Vulkan contract hardening

1. Decide supported Vulkan scope explicitly.
2. If keeping Vulkan as a claimed feature, implement semaphore wait/signal handling and replace stubs with contract-compliant behavior or explicit unsupported failure paths.
3. Run with validation layers and at least one known Vulkan libretro core.

Validation:

```bash
cmake --build build/ --parallel
# Manual with Vulkan validation enabled: no synchronization/layout validation errors during startup, resize, and several minutes of rendering.
```

### Phase 5 — Documentation and test cleanup

1. Keep documentation, code comments, and commit messages in English; do not add non-English planning notes under tracked project docs.
2. Update README feature claims if Vulkan remains partial/experimental.
3. Add unit tests for core variables, log levels, and extracted CLI parsing.
4. Keep AGENTS.md in sync only if architecture or workflow rules actually change.

## Non-goals

- No GUI framework, shader stack, rewind UI, hotkey-based savestate UI, or configuration parser expansion.
- No libretro-common import.
- No broad renderer rewrite unless required by the resize split.
- No attempt to implement rumble, sensors, camera, location, MIDI, microphone, or performance interfaces unless a concrete supported-core need appears.
