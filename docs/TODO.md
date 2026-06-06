# PureRetro — TODO

Backlog of code-quality work surfaced by the 2026-06-06 review. The 18
Critical / Important findings have already been fixed; everything below
is either Minor (cosmetic / micro-optimisation), or a larger structural
change that needs design discussion before implementation.

Numbering follows the original review for traceability.

---

## Minor (M)

Each item is small and self-contained. Pick any of these for a
"good-first-PR" style cleanup.

### M-1 (review #21) — Remove dead `convert_surface` field
- `src/video_sw.h:23` declares `SDL_Surface *convert_surface`.
- `src/video_sw.c:37-38` destroys it on shutdown.
- It is never assigned anywhere.
- **Action:** delete the field and the destroy hook. If pixel-format
  conversion is genuinely needed later, reintroduce it with the same
  commit that uses it.

### M-2 (review #31) — Remove dead `framebuffers` array (Vulkan)
- `src/video_vk.h:44` declares `VkFramebuffer *framebuffers`.
- `src/video_vk.c:366` allocates it; `video_vk_destroy` frees the
  pointer.
- Never populated, never destroyed per-element, never read.
- **Action:** delete the field, the calloc, and the free. The comment
  ("reserved for future graphics pipeline use") violates the project
  rule that every line should justify its existence.

### M-3 (review #24) — Deduplicate aspect-ratio fitting
Three near-identical implementations:
- `src/video_sw.c:111-125`
- `src/video_gl.c:356-370`
- `src/video_vk.c:680-693`

Extract into a `static inline` in `frontend.h` (or a dedicated
`util.h`):
```c
static inline void fit_aspect(unsigned src_w, unsigned src_h,
                              int dst_w, int dst_h,
                              int *out_x, int *out_y,
                              int *out_w, int *out_h);
```

### M-4 (review #25) — Cache GL function pointers
`src/video_gl.c:53` defines `GLPROC(name)` which calls
`SDL_GL_GetProcAddress` every invocation. `video_gl_present` is on
the hot path and re-resolves five functions per frame.

**Action:** add cached fn-pointer fields to `struct video_gl_context`
and resolve them once at the end of `video_gl_init`. Reuse the
existing `GLPROC` macro for one-shot resolution sites
(`gl_fbo_create`, `gl_fbo_destroy`).

### M-5 (review #22, #30) — Tighten input-state arrays
- `joypad_state[RETRO_DEVICE_ID_JOYPAD_MASK + 1]` is 257 entries but
  only ~17 are real buttons; `MASK` is a sentinel, not an ID.
- `input_state_joypad` would index `joypad_state[256]` on `id==MASK`
  if `core.c` had not pre-routed that case to `input_state_joypad_mask`.

**Action:** shrink the array to `RETRO_DEVICE_ID_JOYPAD_R3 + 1` and add
an explicit `if (id > RETRO_DEVICE_ID_JOYPAD_R3) return 0;` at the top
of `input_state_joypad` (the existing check is by-id-not-by-bounds).

### M-6 (review #26) — Avoid heap alloc for `--variable` key
`src/main.c:124-130` mallocs the key just to NUL-terminate it before
passing to `core_variable_override`, which then mallocs again
internally.

**Action:** use a fixed-size stack buffer (`char key[256]`) with a
length check; bail with a clear error if the key is longer than the
buffer.

### M-7 (review #28) — Decide on `GET_SAVE_DIRECTORY` policy
`src/core.c` currently returns NULL but logs "core will use system
directory" — a behaviour libretro does not actually guarantee.

Two options:
- **A.** Return `g_frontend.system_directory` (matches most cores).
- **B.** Keep returning NULL but reword the log to be factual.

Pick one and apply consistently.

### M-8 (review #19 follow-up) — Fix Vulkan stub `video_vk_present` signature
The non-Vulkan branch in `src/video_vk.c:911` declares:
```c
void video_vk_present(struct video_vk_context *ctx);
```
but the real signature is:
```c
void video_vk_present(struct video_vk_context *ctx, unsigned width, unsigned height);
```
This silently passes because `video.c` wraps the call site in
`#ifdef PURERETRO_VULKAN_ENABLED`, so the stub is never linked
against. Fix the stub anyway to keep the two branches in sync.

### M-9 (review #32) — Faster input keymap lookup
`src/input.c:32-56` linearly scans `g_keymap` per key event. With 12
keys this is fine, but a direct lookup table keyed by scancode
(`uint8_t scancode_to_retro[SDL_SCANCODE_COUNT]` with `0xFF`
sentinel) is clearer and O(1).

### M-10 (review #33) — Pre-increment then index
`src/main.c:86, 117` use `argv[++i]` inside an expression that also
reads `argv[i]` for error reporting. Refactor to:
```c
++i;
... use argv[i] ...
```
for clarity, even though the current evaluation is well-defined.

### M-11 (review #34) — Annotate `.opt` file with active CLI overrides
When `--variable key=value` is in effect, `core_variables_save`
silently writes the on-disk default, not the active value. Add an
inline comment per affected key:
```
# (CLI override in effect this run: <value>)
```
so the file is self-documenting and users do not assume their CLI
flag persisted.

---

## Architecture (A)

Larger items: each should be its own PR with a design note.

### A-1 (review #35) — Backend vtable for video dispatcher
Currently `video.c` has 4–5 `switch (renderer) { case ... }` blocks
dispatching to `_sw`/`_gl`/`_vk` modules. A `struct video_backend`
vtable with one entry per renderer would make adding a 4th backend
(Metal, D3D11/12) trivial.

**Do NOT do this yet.** With 3 backends, the switches are clearer and
match the project's educational/minimal posture. Revisit when a 4th
backend is on the roadmap.

### A-2 (review #36) — Split `core.c` (~960 lines)
The file currently mixes four concerns:
1. Dynamic loading / lifecycle (`core_load`, `core_unload`).
2. Variable table CRUD (`variable_add`/`variables_find`/...).
3. Variable persistence (`core_variables_load`/`_save`,
   `core_variables_path`).
4. Environment callback dispatcher (`core_environment`).

Proposed split:
- `core.c` — keep 1 and 4 (callbacks).
- `core_variables.c` / `core_variables.h` — own 2 and 3.

Also wrap the three `retro_variable *` plus their `count`/`capacity`
fields on `g_frontend` into a `struct variable_table` for readability.

### A-3 (review #27) — Tighten `frontend.h` include footprint
`frontend.h` pulls in the entire `<SDL3/SDL.h>` to access two opaque
pointer types (`SDL_Window *`, `SDL_AudioStream *`). Forward
declaring those would cut compile time across every translation unit.

Risk: SDL3 may already typedef them in a way that breaks plain
`struct SDL_Window;` forward declarations on some platforms. Validate
on Linux, macOS, and Windows (MSVC + MinGW) before merging.

---

## Out of scope (documented and accepted)

These came up in review but are deliberately not changed:

- **Global singletons** (`g_frontend`, `g_core`, `g_av_info`,
  `g_core_handle`): unit-testability suffers, but for an SDL-style
  single-window frontend this is idiomatic. Don't change without a
  testing story.
- **`static` fallback buffers** in `GET_VARIABLE` / `SET_VARIABLES`
  (`core.c`): not thread-safe in the abstract, but libretro guarantees
  callbacks are synchronous on the run thread, so reuse is safe.
- **`vk_check` verbosity** (review #23): the error-handling pattern is
  spread out but correct. A cleanup-label refactor would help, but
  every Vulkan resource owner already relies on `video_vk_destroy` as
  the single catch-all, which keeps individual call sites short.
