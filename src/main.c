/*
 * PureRetro — Entry point
 *
 * Thin orchestrator: CLI -> frontend init -> core load/init ->
 * SRAM/savestate -> run loop -> SRAM/opt persist -> shutdown.
 *
 * All per-flag logic lives in cli.c; SDL/log/video bring-up lives in
 * frontend_lifecycle.c; the frame loop lives in run_loop.c.
 */

#define _POSIX_C_SOURCE 200809L

#include "cli.h"
#include "core.h"
#include "core_variables.h"
#include "audio.h"
#include "frontend.h"
#include "frontend_lifecycle.h"
#include "input.h"
#include "log.h"
#include "run_loop.h"
#include "video.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

/* Global frontend state */
struct frontend_state g_frontend;

int main(int argc, char *argv[])
{
    memset(&g_frontend, 0, sizeof(g_frontend));
    g_frontend.initial_disk_index = -1;
    g_frontend.language = RETRO_LANGUAGE_ENGLISH;

    if (!cli_parse(argc, argv, &g_frontend))
        return EXIT_FAILURE;

    if (!frontend_init())
        return EXIT_FAILURE;

    if (!core_load(g_frontend.core_path)) {
        LOG_ERROR("Failed to load core: %s", g_frontend.core_path);
        frontend_shutdown();
        return EXIT_FAILURE;
    }

    /* Load persisted core option overrides before core_init so the core
     * sees them on its first GET_VARIABLE calls. */
    char *opt_path = core_variables_path(g_frontend.core_path,
                                          g_frontend.system_directory);
    if (opt_path)
        core_variables_load(opt_path);

    if (g_frontend.config_path) {
        if (!input_load_keymap(g_frontend.config_path)) {
            LOG_WARN("Failed to load keymap config: %s",
                     g_frontend.config_path);
        }
    }

    if (!core_init(g_frontend.content_path)) {
        LOG_ERROR("Failed to initialize core");
        free(opt_path);
        /* Tear the core down first (stops its background threads, releases
         * its own resources) before destroying the SDL / video subsystems
         * the core may still be holding pointers into. */
        core_unload();
        frontend_shutdown();
        return EXIT_FAILURE;
    }

    /* Software-only cores never call SET_HW_RENDER, so bootstrap
     * the software backend here. No-op for hardware cores. */
    if (!video_ensure_software_renderer()) {
        LOG_ERROR("Failed to initialize software renderer");
        free(opt_path);
        core_unload();
        frontend_shutdown();
        return EXIT_FAILURE;
    }

    if (g_frontend.fullscreen && g_frontend.video.window) {
        SDL_SetWindowFullscreen(g_frontend.video.window, true);
    }

    /* Log the final renderer state after core init. If the core never called
     * SET_HW_RENDER (e.g. software-only core), this still shows sw. */
    LOG_INFO("Final active renderer: %s",
             renderer_name(g_frontend.video.renderer));

    /* Warn if the user requested HW but ended up in software. This usually
     * means the core failed to load (retro_load_game returned false) or the
     * core never called SET_HW_RENDER despite being a HW core. */
    if (g_frontend.preferred_renderer != VIDEO_RENDERER_NONE &&
        g_frontend.preferred_renderer != VIDEO_RENDERER_SW &&
        g_frontend.video.renderer == VIDEO_RENDERER_SW) {
        LOG_WARN("user requested '%s' but renderer is 'sw'.",
                 renderer_name(g_frontend.preferred_renderer));
        if (!g_frontend.hw_render_requested) {
            LOG_WARN("The core never called SET_HW_RENDER. Common causes:");
            LOG_WARN("  - Missing firmware/BIOS (cores like Beetle PSX HW");
            LOG_WARN("    silently fall back to software without a valid BIOS)");
            LOG_WARN("  - Core does not support the requested renderer");
            LOG_WARN("  - Core failed to load content (check earlier errors)");
        }
    }

    /* Initialize audio now that we know the core's sample rate */
    if (!g_frontend.no_audio) {
        double rate = (g_frontend.audio_rate_override > 0)
                      ? (double)g_frontend.audio_rate_override
                      : g_av_info.timing.sample_rate;
        if (g_frontend.audio_rate_override > 0) {
            LOG_INFO("Overriding core sample rate %.2f Hz with %u Hz from --audio-rate",
                     g_av_info.timing.sample_rate,
                     g_frontend.audio_rate_override);
        }
        if (!audio_init(rate)) {
            LOG_WARN("Failed to initialize audio");
        } else if (g_frontend.audio_buffer_ms_override > 0) {
            audio_set_minimum_latency(g_frontend.audio_buffer_ms_override);
            LOG_INFO("Audio buffer minimum latency set to %u ms via --audio-buffer-ms",
                     g_frontend.audio_buffer_ms_override);
        }
    }

    /* SRAM auto-persistence. The path is derived from the content's basename
     * (without extension) under save_directory (falling back to the system
     * directory when save_directory was not set). Cores with no SRAM region
     * silently no-op. */
    {
        const char *save_dir = g_frontend.save_directory
                               ? g_frontend.save_directory
                               : g_frontend.system_directory;
        g_frontend.sram_path = core_sram_path(save_dir, g_frontend.content_path);
        if (g_frontend.sram_path)
            core_sram_load(g_frontend.sram_path);
    }

    /* Load an explicit savestate if requested via --savestate. Failure here
     * is non-fatal: the user still gets the game from a fresh state. */
    if (g_frontend.savestate_load_path)
        core_savestate_load(g_frontend.savestate_load_path);

    run_loop();

    /* Persist SRAM before tearing down the core. retro_get_memory_data
     * returns NULL after core_unload, so this must run first. */
    if (g_frontend.sram_path) {
        core_sram_save(g_frontend.sram_path);
        free(g_frontend.sram_path);
        g_frontend.sram_path = NULL;
    }

    if (g_frontend.video.hw_render_enabled) {
        video_context_destroy();
    }

    /* Persist the current disk overrides while the table is still alive. */
    if (opt_path) {
        core_variables_save(opt_path);
        free(opt_path);
    }

    core_unload();
    frontend_shutdown();

    return EXIT_SUCCESS;
}
