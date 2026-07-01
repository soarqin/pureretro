/*
 * PureRetro — Frontend lifecycle implementation (see frontend_lifecycle.h).
 */

#define _POSIX_C_SOURCE 200809L

#include "frontend_lifecycle.h"

#include "audio.h"
#include "frontend.h"
#include "log.h"
#include "video.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compute and create g_frontend.system_directory. Best-effort: on failure
 * the field is left NULL and the program continues (cores that need a
 * system directory will simply error out). */
static void set_system_directory(void)
{
    /* If --system-dir was explicitly given, skip automatic resolution. */
    if (g_frontend.system_directory) {
        LOG_INFO("System directory: %s (from --system-dir)",
                 g_frontend.system_directory);
        SDL_CreateDirectory(g_frontend.system_directory);
        return;
    }

    if (g_frontend.portable) {
        /* Portable mode: keep all data alongside the binary's working
         * directory. The system directory is "<cwd>/system". */
        char *cwd = SDL_GetCurrentDirectory();
        if (!cwd)
            return;

        size_t cwd_len = strlen(cwd);
        /* Strip a trailing separator (e.g., "/" on Unix roots). */
        while (cwd_len > 1 &&
               (cwd[cwd_len - 1] == '/' || cwd[cwd_len - 1] == '\\'))
            cwd_len--;

        size_t total = cwd_len + 1 + strlen("system") + 1;
        g_frontend.system_directory = malloc(total);
        if (g_frontend.system_directory) {
            snprintf(g_frontend.system_directory, total,
                     "%.*s/system", (int)cwd_len, cwd);
            LOG_INFO("System directory (portable): %s",
                     g_frontend.system_directory);
            SDL_CreateDirectory(g_frontend.system_directory);
        }
        SDL_free(cwd);
        return;
    }

    /* Default: SDL_GetPrefPath returns a platform-appropriate user data
     * directory, already terminated with a separator. */
    char *pref = SDL_GetPrefPath("pureretro", "system");
    if (!pref)
        return;

    size_t len = strlen(pref);
    g_frontend.system_directory = malloc(len + 1);
    if (g_frontend.system_directory) {
        memcpy(g_frontend.system_directory, pref, len + 1);
        LOG_INFO("System directory: %s", g_frontend.system_directory);
        SDL_CreateDirectory(g_frontend.system_directory);
    }
    SDL_free(pref);
}

bool frontend_init(void)
{
    log_init();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    g_frontend.running = true;

    /* Set up the system directory for firmware/BIOS files.
     * Cores like Beetle PSX HW look here for scph5500.bin etc. */
    set_system_directory();

    if (!video_init("PureRetro", 640, 480)) {
        LOG_ERROR("Failed to initialize video");
        free(g_frontend.system_directory);
        g_frontend.system_directory = NULL;
        return false;
    }

    /* Audio will be initialized after the core loads and reports its sample rate. */

    return true;
}

void frontend_shutdown(void)
{
    audio_shutdown();
    video_shutdown();
    free(g_frontend.system_directory);
    g_frontend.system_directory = NULL;
    free(g_frontend.save_directory);
    g_frontend.save_directory = NULL;
    free(g_frontend.core_assets_directory);
    g_frontend.core_assets_directory = NULL;
    free(g_frontend.playlist_directory);
    g_frontend.playlist_directory = NULL;
    free(g_frontend.file_browser_directory);
    g_frontend.file_browser_directory = NULL;
    free(g_frontend.username);
    g_frontend.username = NULL;
    SDL_Quit();
}
