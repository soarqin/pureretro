/*
 * PureRetro — Core management
 *
 * Dynamic loading and lifecycle of libretro cores.
 */

#ifndef CORE_H
#define CORE_H

#include <stdbool.h>
#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Core function pointers */
struct core_functions
{
    void (RETRO_CALLCONV *retro_init)(void);
    void (RETRO_CALLCONV *retro_deinit)(void);
    unsigned (RETRO_CALLCONV *retro_api_version)(void);
    void (RETRO_CALLCONV *retro_get_system_info)(struct retro_system_info *info);
    void (RETRO_CALLCONV *retro_get_system_av_info)(struct retro_system_av_info *info);
    void (RETRO_CALLCONV *retro_set_environment)(retro_environment_t cb);
    void (RETRO_CALLCONV *retro_set_video_refresh)(retro_video_refresh_t cb);
    void (RETRO_CALLCONV *retro_set_audio_sample)(retro_audio_sample_t cb);
    void (RETRO_CALLCONV *retro_set_audio_sample_batch)(retro_audio_sample_batch_t cb);
    void (RETRO_CALLCONV *retro_set_input_poll)(retro_input_poll_t cb);
    void (RETRO_CALLCONV *retro_set_input_state)(retro_input_state_t cb);
    void (RETRO_CALLCONV *retro_set_controller_port_device)(unsigned port, unsigned device);
    void (RETRO_CALLCONV *retro_reset)(void);
    void (RETRO_CALLCONV *retro_run)(void);
    bool (RETRO_CALLCONV *retro_load_game)(const struct retro_game_info *game);
    void (RETRO_CALLCONV *retro_unload_game)(void);
    void (RETRO_CALLCONV *retro_get_region)(void);
    void* (RETRO_CALLCONV *retro_get_memory_data)(unsigned id);
    size_t (RETRO_CALLCONV *retro_get_memory_size)(unsigned id);
};

extern struct core_functions g_core;
extern struct retro_system_av_info g_av_info;

/* Load a core shared library from the given path.
 * Returns true on success, false on failure. */
bool core_load(const char *path);

/* Unload the currently loaded core. */
void core_unload(void);

/* Initialize the core (retro_init, set callbacks, load game).
 * Returns true on success. */
bool core_init(const char *content_path);

/* Run one frame of the core. */
void core_run(void);

/* Override a core option variable (used by --variable CLI flag).
 * CLI overrides take priority over disk-persisted values and are not saved. */
void core_variable_override(const char *key, const char *value);

/* Load persisted core option overrides from the given file path.
 * Missing files are not an error. Returns true on success. */
bool core_variables_load(const char *path);

/* Save the current disk overrides to the given file path.
 * CLI-only overrides are intentionally excluded. */
bool core_variables_save(const char *path);

/* Compute the per-core options file path, given the core shared object path
 * and a base directory (typically g_frontend.system_directory).
 * The returned string is heap-allocated and must be freed by the caller.
 * Returns NULL on allocation failure or if base_dir is NULL. */
char *core_variables_path(const char *core_path, const char *base_dir);

/* Environment callback exposed to the core. */
bool RETRO_CALLCONV core_environment(unsigned cmd, void *data);

/* Video refresh callback exposed to the core. */
void RETRO_CALLCONV core_video_refresh(const void *data, unsigned width,
                                       unsigned height, size_t pitch);

/* Audio sample callback exposed to the core. */
void RETRO_CALLCONV core_audio_sample(int16_t left, int16_t right);

/* Audio sample batch callback exposed to the core. */
size_t RETRO_CALLCONV core_audio_sample_batch(const int16_t *data, size_t frames);

/* Input poll callback exposed to the core. */
void RETRO_CALLCONV core_input_poll(void);

/* Input state callback exposed to the core. */
int16_t RETRO_CALLCONV core_input_state(unsigned port, unsigned device,
                                        unsigned index, unsigned id);

#ifdef __cplusplus
}
#endif

#endif /* CORE_H */
