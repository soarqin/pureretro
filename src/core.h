/*
 * PureRetro — Core management
 *
 * Dynamic loading and lifecycle of libretro cores.
 */

#ifndef CORE_H
#define CORE_H

#include "libretro.h"

#include <stdbool.h>

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
    bool (RETRO_CALLCONV *retro_load_game_special)(unsigned game_type,
                                                   const struct retro_game_info *info,
                                                   size_t num_info);
    void (RETRO_CALLCONV *retro_unload_game)(void);
    void (RETRO_CALLCONV *retro_get_region)(void);
    void* (RETRO_CALLCONV *retro_get_memory_data)(unsigned id);
    size_t (RETRO_CALLCONV *retro_get_memory_size)(unsigned id);

    /* Optional savestate symbols. Loaded as optional in core_load;
     * NULL when the core does not export them. */
    size_t (RETRO_CALLCONV *retro_serialize_size)(void);
    bool (RETRO_CALLCONV *retro_serialize)(void *data, size_t size);
    bool (RETRO_CALLCONV *retro_unserialize)(const void *data, size_t size);
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

/* Compute the per-content SRAM path under save_directory.
 * Returns NULL when save_directory or content_path is unavailable.
 * Caller frees. */
char *core_sram_path(const char *save_dir, const char *content_path);

/* Load RETRO_MEMORY_SAVE_RAM contents from `path` (if present).
 * Missing files are not an error. */
bool core_sram_load(const char *path);

/* Save RETRO_MEMORY_SAVE_RAM contents to `path`. No-op if the core
 * exposes a zero-sized SRAM region. */
bool core_sram_save(const char *path);

/* Load a savestate from `path` via retro_unserialize. Returns false if
 * the core does not export retro_serialize / retro_unserialize. */
bool core_savestate_load(const char *path);

/* Save a savestate to `path` via retro_serialize. Returns false if the
 * core does not export retro_serialize / retro_serialize_size. */
bool core_savestate_save(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* CORE_H */
