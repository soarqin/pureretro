/*
 * PureRetro — Command-line argument parsing (see cli.h).
 *
 * Table-driven dispatch: one static cli_* handler + one row per flag.
 * cli_parse itself is ~40 lines and contains no per-flag logic.
 */

#define _POSIX_C_SOURCE 200809L

#include "cli.h"

#include "core_variables.h"
#include "frontend.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward decls for helpers used inside handlers. */
static bool parse_lang(const char *arg, enum retro_language *out);
static bool parse_render(const char *arg, enum video_renderer *out);

struct cli_option {
    const char *long_name;
    const char *short_name;
    bool wants_arg;
    bool (*handler)(const char *arg, struct frontend_state *cfg);
    const char *help;
};

/* -------------------------------------------------------------------- */
/* Per-flag handlers                                                     */
/* -------------------------------------------------------------------- */

static bool cli_fullscreen(const char *arg, struct frontend_state *cfg)
{
    (void)arg;
    cfg->fullscreen = true;
    return true;
}

static bool cli_render(const char *arg, struct frontend_state *cfg)
{
    if (!parse_render(arg, &cfg->preferred_renderer)) {
        fprintf(stderr, "Invalid renderer: '%s' (expected vk, gl, or sw)\n", arg);
        return false;
    }
    LOG_INFO("Renderer preference: %s",
             renderer_name(cfg->preferred_renderer));
    return true;
}

static bool cli_scale(const char *arg, struct frontend_state *cfg)
{
    char *endptr = NULL;
    long val = strtol(arg, &endptr, 10);
    if (*endptr != '\0' || val < 1 || val > 16) {
        fprintf(stderr, "Invalid scale: '%s' (expected 1-16)\n", arg);
        return false;
    }
    cfg->window_scale = (unsigned)val;
    return true;
}

static bool cli_no_audio(const char *arg, struct frontend_state *cfg)
{
    (void)arg;
    cfg->no_audio = true;
    return true;
}

static bool cli_variable(const char *arg, struct frontend_state *cfg)
{
    (void)cfg;
    const char *eq = strchr(arg, '=');
    if (!eq) {
        fprintf(stderr, "Invalid variable syntax: '%s' (expected key=value)\n", arg);
        return false;
    }
    size_t key_len = (size_t)(eq - arg);
    char key[256];
    if (key_len >= sizeof(key)) {
        fprintf(stderr, "Variable key too long (max %zu): '%.*s...'\n",
                sizeof(key) - 1, (int)(sizeof(key) - 1), arg);
        return false;
    }
    memcpy(key, arg, key_len);
    key[key_len] = '\0';
    core_variable_override(key, eq + 1);
    return true;
}

static bool cli_portable(const char *arg, struct frontend_state *cfg)
{
    (void)arg;
    cfg->portable = true;
    return true;
}

static bool cli_system_dir(const char *arg, struct frontend_state *cfg)
{
    free(cfg->system_directory);
    cfg->system_directory = strdup(arg);
    return true;
}

static bool cli_config(const char *arg, struct frontend_state *cfg)
{
    cfg->config_path = arg;
    return true;
}

static bool cli_disk_index(const char *arg, struct frontend_state *cfg)
{
    char *endptr = NULL;
    long val = strtol(arg, &endptr, 10);
    if (*endptr != '\0' || val < 0 || val > 255) {
        fprintf(stderr, "Invalid disk index: '%s' (expected 0-255)\n", arg);
        return false;
    }
    cfg->initial_disk_index = (int)val;
    return true;
}

static bool cli_lang(const char *arg, struct frontend_state *cfg)
{
    if (!parse_lang(arg, &cfg->language)) {
        fprintf(stderr, "Unknown language code: '%s'\n", arg);
        return false;
    }
    return true;
}

static bool cli_username(const char *arg, struct frontend_state *cfg)
{
    free(cfg->username);
    cfg->username = strdup(arg);
    return true;
}

static bool cli_subsystem(const char *arg, struct frontend_state *cfg)
{
    cfg->subsystem_ident = arg;
    return true;
}

static bool cli_core_assets_dir(const char *arg, struct frontend_state *cfg)
{
    free(cfg->core_assets_directory);
    cfg->core_assets_directory = strdup(arg);
    return true;
}

static bool cli_playlist_dir(const char *arg, struct frontend_state *cfg)
{
    free(cfg->playlist_directory);
    cfg->playlist_directory = strdup(arg);
    return true;
}

static bool cli_file_browser_dir(const char *arg, struct frontend_state *cfg)
{
    free(cfg->file_browser_directory);
    cfg->file_browser_directory = strdup(arg);
    return true;
}

static bool cli_audio_rate(const char *arg, struct frontend_state *cfg)
{
    char *endptr = NULL;
    long val = strtol(arg, &endptr, 10);
    if (*endptr != '\0' || val < 4000 || val > 384000) {
        fprintf(stderr, "Invalid audio rate: '%s' (expected 4000-384000)\n", arg);
        return false;
    }
    cfg->audio_rate_override = (unsigned)val;
    return true;
}

static bool cli_audio_buffer_ms(const char *arg, struct frontend_state *cfg)
{
    char *endptr = NULL;
    long val = strtol(arg, &endptr, 10);
    if (*endptr != '\0' || val < 1 || val > 5000) {
        fprintf(stderr, "Invalid audio buffer: '%s' (expected 1-5000 ms)\n", arg);
        return false;
    }
    cfg->audio_buffer_ms_override = (unsigned)val;
    return true;
}

static bool cli_log_level(const char *arg, struct frontend_state *cfg)
{
    (void)cfg;
    enum log_level lvl;
    if (!log_parse_level(arg, &lvl)) {
        fprintf(stderr, "Invalid log level: '%s'\n", arg);
        return false;
    }
    log_set_level(lvl);
    return true;
}

static bool cli_savestate(const char *arg, struct frontend_state *cfg)
{
    cfg->savestate_load_path = arg;
    return true;
}

/* -------------------------------------------------------------------- */
/* Option dispatch table                                                 */
/* -------------------------------------------------------------------- */

static const struct cli_option g_cli_options[] = {
    { "--fullscreen",        "-f", false, cli_fullscreen,
      "Start in fullscreen mode" },
    { "--render",            NULL, true,  cli_render,
      "Preferred renderer: vk, gl, or sw (core may still choose another)" },
    { "--scale",             NULL, true,  cli_scale,
      "Integer window scale (1-16)" },
    { "--no-audio",          NULL, false, cli_no_audio,
      "Disable audio output" },
    { "--variable",          NULL, true,  cli_variable,
      "Override a core option variable (key=value); highest priority" },
    { "--portable",          NULL, false, cli_portable,
      "Portable mode: use ./system as the config base" },
    { "--system-dir",        NULL, true,  cli_system_dir,
      "Directory for GET_SYSTEM_DIRECTORY (overrides --portable / SDL pref path)" },
    { "--config",            NULL, true,  cli_config,
      "Load key remapping configuration file" },
    { "--disk-index",        NULL, true,  cli_disk_index,
      "Multi-disc content: initial disk index (0-255)" },
    { "--lang",              NULL, true,  cli_lang,
      "Language code (en, ja, fr, de, es, it, pt_br, pt_pt, ru, ko, zh_cn, zh_tw, ...)" },
    { "--username",          NULL, true,  cli_username,
      "Player name reported via GET_USERNAME" },
    { "--subsystem",         NULL, true,  cli_subsystem,
      "Load content via subsystem identifier (e.g. sgb, bsx)" },
    { "--core-assets-dir",   NULL, true,  cli_core_assets_dir,
      "Directory for GET_CORE_ASSETS_DIRECTORY" },
    { "--playlist-dir",      NULL, true,  cli_playlist_dir,
      "Directory for GET_PLAYLIST_DIRECTORY" },
    { "--file-browser-dir",  NULL, true,  cli_file_browser_dir,
      "Directory for GET_FILE_BROWSER_START_DIRECTORY" },
    { "--audio-rate",        NULL, true,  cli_audio_rate,
      "Override audio sample rate in Hz (4000-384000); default: core's rate" },
    { "--audio-buffer-ms",   NULL, true,  cli_audio_buffer_ms,
      "Override minimum audio buffer latency in ms (1-5000)" },
    { "--log-level",         NULL, true,  cli_log_level,
      "debug, info (default), warn, or error (also PURERETRO_LOG env var)" },
    { "--savestate",         NULL, true,  cli_savestate,
      "Load a savestate file after core init" },
};

#define NUM_CLI_OPTIONS (sizeof(g_cli_options) / sizeof(g_cli_options[0]))

/* -------------------------------------------------------------------- */
/* Language / renderer helpers                                           */
/* -------------------------------------------------------------------- */

static bool parse_lang(const char *arg, enum retro_language *out)
{
    /* Compact 2-letter / locale-style codes mapped to RETRO_LANGUAGE_*.
     * Only the most common codes are wired; extend as needed. */
    static const struct { const char *code; enum retro_language id; } map[] = {
        { "en",     RETRO_LANGUAGE_ENGLISH },
        { "en_gb",  RETRO_LANGUAGE_BRITISH_ENGLISH },
        { "ja",     RETRO_LANGUAGE_JAPANESE },
        { "fr",     RETRO_LANGUAGE_FRENCH },
        { "es",     RETRO_LANGUAGE_SPANISH },
        { "de",     RETRO_LANGUAGE_GERMAN },
        { "it",     RETRO_LANGUAGE_ITALIAN },
        { "nl",     RETRO_LANGUAGE_DUTCH },
        { "pt_br",  RETRO_LANGUAGE_PORTUGUESE_BRAZIL },
        { "pt_pt",  RETRO_LANGUAGE_PORTUGUESE_PORTUGAL },
        { "ru",     RETRO_LANGUAGE_RUSSIAN },
        { "ko",     RETRO_LANGUAGE_KOREAN },
        { "zh_tw",  RETRO_LANGUAGE_CHINESE_TRADITIONAL },
        { "zh_cn",  RETRO_LANGUAGE_CHINESE_SIMPLIFIED },
        { "eo",     RETRO_LANGUAGE_ESPERANTO },
        { "pl",     RETRO_LANGUAGE_POLISH },
        { "vi",     RETRO_LANGUAGE_VIETNAMESE },
        { "ar",     RETRO_LANGUAGE_ARABIC },
        { "el",     RETRO_LANGUAGE_GREEK },
        { "tr",     RETRO_LANGUAGE_TURKISH },
        { "sk",     RETRO_LANGUAGE_SLOVAK },
        { "fa",     RETRO_LANGUAGE_PERSIAN },
        { "he",     RETRO_LANGUAGE_HEBREW },
        { "fi",     RETRO_LANGUAGE_FINNISH },
        { "id",     RETRO_LANGUAGE_INDONESIAN },
        { "sv",     RETRO_LANGUAGE_SWEDISH },
        { "uk",     RETRO_LANGUAGE_UKRAINIAN },
        { "cs",     RETRO_LANGUAGE_CZECH },
        { "hu",     RETRO_LANGUAGE_HUNGARIAN },
        { "no",     RETRO_LANGUAGE_NORWEGIAN },
        { "ga",     RETRO_LANGUAGE_IRISH },
        { "th",     RETRO_LANGUAGE_THAI },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(arg, map[i].code) == 0) {
            *out = map[i].id;
            return true;
        }
    }
    return false;
}

static bool parse_render(const char *arg, enum video_renderer *out)
{
    if (strcmp(arg, "vk") == 0) { *out = VIDEO_RENDERER_VULKAN; return true; }
    if (strcmp(arg, "gl") == 0) { *out = VIDEO_RENDERER_OPENGL; return true; }
    if (strcmp(arg, "sw") == 0) { *out = VIDEO_RENDERER_SW;     return true; }
    return false;
}

/* -------------------------------------------------------------------- */
/* Public entry points                                                   */
/* -------------------------------------------------------------------- */

static const struct cli_option *find_cli_option(const char *name)
{
    for (size_t k = 0; k < NUM_CLI_OPTIONS; ++k) {
        const struct cli_option *opt = &g_cli_options[k];
        if (strcmp(name, opt->long_name) == 0)
            return opt;
        if (opt->short_name && strcmp(name, opt->short_name) == 0)
            return opt;
    }
    return NULL;
}

void cli_print_usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s <core> [<content>] [options]\n", argv0);
    fprintf(stderr, "  <core>     Path to the libretro core (e.g., nestopia_libretro.so)\n");
    fprintf(stderr, "  <content>  Path to the game ROM or content file\n");
    fprintf(stderr, "Options:\n");
    for (size_t k = 0; k < NUM_CLI_OPTIONS; ++k) {
        const struct cli_option *opt = &g_cli_options[k];
        char head[64];
        if (opt->short_name) {
            snprintf(head, sizeof(head), "%s, %s%s",
                     opt->long_name, opt->short_name,
                     opt->wants_arg ? " <arg>" : "");
        } else {
            snprintf(head, sizeof(head), "%s%s",
                     opt->long_name, opt->wants_arg ? " <arg>" : "");
        }
        fprintf(stderr, "  %-28s %s\n", head, opt->help);
    }
    fprintf(stderr, "  --log-level / PURERETRO_LOG env var sets the log threshold.\n");
}

bool cli_parse(int argc, char *argv[], struct frontend_state *cfg)
{
    if (argc < 2) {
        cli_print_usage(argv[0]);
        return false;
    }

    cfg->core_path = argv[1];

    int i;
    /* Treat argv[2] as content unless it matches a known flag. This lets
     * legitimate filenames that begin with '-' (e.g. "-test.sfc") be loaded
     * as content. Unknown '-' tokens still fall through to the flag parser,
     * which reports them via cli_print_usage. */
    if (argc >= 3 && find_cli_option(argv[2]) == NULL) {
        cfg->content_path = argv[2];
        i = 3;
    } else {
        cfg->content_path = NULL;
        i = 2;
    }

    for (; i < argc; ++i) {
        const struct cli_option *opt = find_cli_option(argv[i]);
        if (!opt) {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            cli_print_usage(argv[0]);
            return false;
        }
        const char *arg = NULL;
        if (opt->wants_arg) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requires an argument\n", argv[i]);
                cli_print_usage(argv[0]);
                return false;
            }
            arg = argv[++i];
        }
        if (!opt->handler(arg, cfg)) {
            cli_print_usage(argv[0]);
            return false;
        }
    }

    return true;
}
