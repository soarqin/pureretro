/*
 * PureRetro — Basic directory, message, log, and I/O environment callbacks
 */

#include "core_internal.h"

#include "frontend.h"
#include "log.h"
#include "vfs.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdint.h>

static void RETRO_CALLCONV core_log_bridge(enum retro_log_level level,
                                           const char *fmt, ...)
{
    enum log_level lvl;
    switch (level) {
    case RETRO_LOG_DEBUG: lvl = LOG_LEVEL_DEBUG; break;
    case RETRO_LOG_INFO:  lvl = LOG_LEVEL_INFO;  break;
    case RETRO_LOG_WARN:  lvl = LOG_LEVEL_WARN;  break;
    case RETRO_LOG_ERROR: lvl = LOG_LEVEL_ERROR; break;
    default:              lvl = LOG_LEVEL_INFO;  break;
    }
    va_list va;
    va_start(va, fmt);
    log_emit_v(lvl, "CORE", NULL, 0, fmt, va);
    va_end(va);
}


bool env_get_can_dupe(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) return false;
    *(bool *)data = true;
    return true;
}

bool env_get_overscan(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) return false;
    *(bool *)data = false;
    return true;
}

bool env_shutdown(struct frontend_state *fe, void *data)
{
    (void)data;
    fe->running = false;
    return true;
}

bool env_set_message(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) return false;
    const struct retro_message *msg = (const struct retro_message *)data;
    log_emit(LOG_LEVEL_INFO, "CORE", NULL, 0, "%s", msg->msg);
    return true;
}

bool env_set_performance_level(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) return false;
    LOG_INFO("Core performance level hint: %u", *(const unsigned *)data);
    return true;
}

bool env_get_system_directory(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->system_directory;
    LOG_DEBUG("Core queried system directory: %s",
              fe->system_directory ? fe->system_directory : "(null)");
    return true;
}

bool env_get_libretro_path(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->core_path;
    return true;
}

bool env_get_core_assets_directory(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->core_assets_directory;
    return true;
}

bool env_get_playlist_directory(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->playlist_directory;
    return true;
}

bool env_get_file_browser_start_directory(struct frontend_state *fe, void *data)
{
    if (!data) return false;
    *(const char **)data = fe->file_browser_directory;
    return true;
}

bool env_set_support_no_game(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return true;
}

bool env_set_input_descriptors(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}
bool env_set_keyboard_callback(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_keyboard_callback: NULL data"); return false; }
    const struct retro_keyboard_callback *cb =
        (const struct retro_keyboard_callback *)data;
    fe->keyboard_callback = *cb;
    return true;
}
bool env_get_variable_update(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_variable_update: NULL data"); return false; }
    *(bool *)data = false;
    return true;
}
bool env_set_audio_callback(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

bool env_get_rumble_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}
bool env_get_sensor_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

bool env_get_camera_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

bool env_get_log_interface(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_log_interface: NULL data"); return false; }
    struct retro_log_callback *cb = (struct retro_log_callback *)data;
    cb->log = core_log_bridge;
    return true;
}

bool env_get_perf_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

bool env_get_location_interface(struct frontend_state *fe, void *data)
{
    (void)fe; (void)data;
    return false;
}

bool env_get_save_directory(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_save_directory: NULL data"); return false; }
    const char *dir = fe->save_directory
                      ? fe->save_directory
                      : fe->system_directory;
    *(const char **)data = dir;
    LOG_DEBUG("Core queried save directory: %s", dir ? dir : "(null)");
    return true;
}
bool env_get_language(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_language: NULL data"); return false; }
    *(enum retro_language *)data = fe->language;
    return true;
}

bool env_get_username(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_username: NULL data"); return false; }
    *(const char **)data = fe->username;
    return fe->username != NULL;
}

bool env_get_disk_control_interface_version(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_disk_control_interface_version: NULL data"); return false; }
    *(unsigned *)data = 1;
    return true;
}
bool env_get_vfs_interface(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_vfs_interface: NULL data"); return false; }
    struct retro_vfs_interface_info *info =
        (struct retro_vfs_interface_info *)data;
    if (info->required_interface_version > 1)
        return false;
    info->iface = vfs_get_interface();
    info->required_interface_version = 1;
    return true;
}

bool env_get_core_options_version(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_core_options_version: NULL data"); return false; }
    *(unsigned *)data = 2;
    return true;
}

bool env_set_core_options_update_display_callback(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_core_options_update_display_callback: NULL data"); return false; }
    const struct retro_core_options_update_display_callback *cb =
        (const struct retro_core_options_update_display_callback *)data;
    fe->core_options_update_display_callback = cb->callback;
    return true;
}
bool env_get_savestate_context(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_savestate_context: NULL data"); return false; }
    *(int *)data = RETRO_SAVESTATE_CONTEXT_NORMAL;
    return true;
}

bool env_get_jit_capable(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_jit_capable: NULL data"); return false; }
    /* All three desktop targets (Linux/macOS/Windows) allow JIT. The
     * libretro contract is "false only on locked-down platforms like
     * iOS / non-jailbroken consoles", which we never run on. */
    *(bool *)data = true;
    return true;
}

bool env_get_message_interface_version(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_get_message_interface_version: NULL data"); return false; }
    *(unsigned *)data = 1;
    return true;
}

bool env_set_message_ext(struct frontend_state *fe, void *data)
{
    (void)fe;
    if (!data) { LOG_WARN("env_set_message_ext: NULL data"); return false; }
    const struct retro_message_ext *msg =
        (const struct retro_message_ext *)data;
    if (!msg->msg)
        return false;
    /* Route via the logger. TARGET_LOG uses the message's own level;
     * TARGET_OSD / TARGET_ALL still go to the log since we have no GUI,
     * but at INFO (so they remain visible without spamming DEBUG). */
    enum log_level lvl;
    if (msg->target == RETRO_MESSAGE_TARGET_LOG) {
        switch (msg->level) {
        case RETRO_LOG_DEBUG: lvl = LOG_LEVEL_DEBUG; break;
        case RETRO_LOG_INFO:  lvl = LOG_LEVEL_INFO;  break;
        case RETRO_LOG_WARN:  lvl = LOG_LEVEL_WARN;  break;
        case RETRO_LOG_ERROR: lvl = LOG_LEVEL_ERROR; break;
        default:              lvl = LOG_LEVEL_INFO;  break;
        }
    } else {
        lvl = LOG_LEVEL_INFO;
    }
    log_emit(lvl, "CORE", NULL, 0, "%s", msg->msg);
    return true;
}

bool env_set_proc_address_callback(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_proc_address_callback: NULL data"); return false; }
    const struct retro_get_proc_address_interface *iface =
        (const struct retro_get_proc_address_interface *)data;
    fe->get_proc_address = iface->get_proc_address;
    LOG_INFO("Core registered get_proc_address interface (%p)",
             (void *)(uintptr_t)iface->get_proc_address);
    return true;
}

bool env_set_support_achievements(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_set_support_achievements: NULL data"); return false; }
    fe->core_supports_achievements = *(const bool *)data;
    LOG_INFO("Core declares achievement support: %s",
             fe->core_supports_achievements ? "yes" : "no");
    return true;
}
bool env_get_game_info_ext(struct frontend_state *fe, void *data)
{
    if (!data) { LOG_WARN("env_get_game_info_ext: NULL data"); return false; }
    /* libretro contract: only valid inside retro_load_game[_special]. */
    if (!fe->rom_data && !fe->game_info_ext.full_path) {
        LOG_WARN("GET_GAME_INFO_EXT called outside retro_load_game");
        return false;
    }
    const struct retro_game_info_ext **out =
        (const struct retro_game_info_ext **)data;
    *out = &fe->game_info_ext;
    return true;
}
