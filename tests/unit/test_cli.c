#include <unity.h>

#include "cli.h"
#include "frontend.h"

#include <stdlib.h>
#include <string.h>

/* cli.c depends on core_variable_override (which touches g_frontend) and on
 * log helpers. We provide the frontend storage; the linker pulls in real
 * core_variables.c / core_variables_parse.c / log.c objects.
 * See tests/unit/CMakeLists.txt. */
struct frontend_state g_frontend;

/* Redirect stderr away from the test log so failure-mode tests do not
 * spam noise. The tests care about return values, not messages. */
static FILE *g_orig_stderr;

void setUp(void)
{
    memset(&g_frontend, 0, sizeof(g_frontend));
    g_frontend.initial_disk_index = -1;
    g_frontend.language = RETRO_LANGUAGE_ENGLISH;
}

void tearDown(void)
{
    free(g_frontend.system_directory);
    free(g_frontend.core_assets_directory);
    free(g_frontend.playlist_directory);
    free(g_frontend.file_browser_directory);
    free(g_frontend.username);
    variable_table_clear(&g_frontend.cli_overrides);
    memset(&g_frontend, 0, sizeof(g_frontend));
}

static bool run_cli(int argc, char *argv[])
{
    return cli_parse(argc, argv, &g_frontend);
}

static void reset_state(void)
{
    tearDown();
    setUp();
}

/* -------------------------------------------------------------------- */
/* Positional arguments                                                  */
/* -------------------------------------------------------------------- */

static void test_positional_core_only(void)
{
    char *argv[] = { (char *)"pureretro", (char *)"/tmp/core.so" };
    TEST_ASSERT_TRUE(run_cli(2, argv));
    TEST_ASSERT_EQUAL_STRING("/tmp/core.so", g_frontend.core_path);
    TEST_ASSERT_NULL(g_frontend.content_path);
}

static void test_positional_core_and_content(void)
{
    char *argv[] = {
        (char *)"pureretro", (char *)"/c/core.so", (char *)"/c/game.nes"
    };
    TEST_ASSERT_TRUE(run_cli(3, argv));
    TEST_ASSERT_EQUAL_STRING("/c/core.so",  g_frontend.core_path);
    TEST_ASSERT_EQUAL_STRING("/c/game.nes", g_frontend.content_path);
}

/* An argv[2] that matches a known flag is NOT taken as content. Regression
 * guard for the flag/content ambiguity at position 2. */
static void test_argv2_flag_is_not_content(void)
{
    char *argv[] = { (char *)"pureretro", (char *)"core.so", (char *)"-f" };
    TEST_ASSERT_TRUE(run_cli(3, argv));
    TEST_ASSERT_EQUAL_STRING("core.so", g_frontend.core_path);
    TEST_ASSERT_NULL(g_frontend.content_path);
    TEST_ASSERT_TRUE(g_frontend.fullscreen);
}

/* A dash-prefixed argv[2] that is NOT a known flag is passed through as
 * content. This lets "-weird-name.sfc" be loaded verbatim. */
static void test_unknown_dash_argv2_is_content(void)
{
    char *argv[] = { (char *)"pr", (char *)"c.so", (char *)"-weird.sfc" };
    TEST_ASSERT_TRUE(run_cli(3, argv));
    TEST_ASSERT_EQUAL_STRING("-weird.sfc", g_frontend.content_path);
}

static void test_too_few_args_fails(void)
{
    char *argv[] = { (char *)"pureretro" };
    TEST_ASSERT_FALSE(run_cli(1, argv));
}

/* -------------------------------------------------------------------- */
/* Boolean and short-form flags                                          */
/* -------------------------------------------------------------------- */

static void test_fullscreen_short_and_long(void)
{
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"-f" };
        TEST_ASSERT_TRUE(run_cli(3, argv));
        TEST_ASSERT_TRUE(g_frontend.fullscreen);
    }
    reset_state();
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--fullscreen" };
        TEST_ASSERT_TRUE(run_cli(3, argv));
        TEST_ASSERT_TRUE(g_frontend.fullscreen);
    }
}

static void test_no_audio_and_portable(void)
{
    char *argv[] = {
        (char *)"pr", (char *)"core.so", (char *)"--no-audio",
        (char *)"--portable"
    };
    TEST_ASSERT_TRUE(run_cli(4, argv));
    TEST_ASSERT_TRUE(g_frontend.no_audio);
    TEST_ASSERT_TRUE(g_frontend.portable);
}

/* -------------------------------------------------------------------- */
/* --render                                                              */
/* -------------------------------------------------------------------- */

static void test_render_valid_values(void)
{
    const struct { const char *arg; enum video_renderer id; } cases[] = {
        { "vk", VIDEO_RENDERER_VULKAN },
        { "gl", VIDEO_RENDERER_OPENGL },
        { "sw", VIDEO_RENDERER_SW },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        reset_state();
        char *argv[] = {
            (char *)"pr", (char *)"c",
            (char *)"--render", (char *)cases[i].arg
        };
        TEST_ASSERT_TRUE(run_cli(4, argv));
        TEST_ASSERT_EQUAL_INT(cases[i].id, g_frontend.preferred_renderer);
    }
}

static void test_render_invalid_value_fails(void)
{
    char *argv[] = {
        (char *)"pr", (char *)"c", (char *)"--render", (char *)"metal"
    };
    TEST_ASSERT_FALSE(run_cli(4, argv));
}

/* -------------------------------------------------------------------- */
/* --scale                                                               */
/* -------------------------------------------------------------------- */

static void test_scale_valid_and_bounds(void)
{
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--scale", (char *)"1" };
        TEST_ASSERT_TRUE(run_cli(4, argv));
        TEST_ASSERT_EQUAL_UINT(1, g_frontend.window_scale);
    }
    reset_state();
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--scale", (char *)"16" };
        TEST_ASSERT_TRUE(run_cli(4, argv));
        TEST_ASSERT_EQUAL_UINT(16, g_frontend.window_scale);
    }
}

static void test_scale_rejects_out_of_range_and_junk(void)
{
    const char *bad[] = { "0", "17", "-1", "abc", "3.5", "" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        reset_state();
        char *argv[] = {
            (char *)"pr", (char *)"c", (char *)"--scale", (char *)bad[i]
        };
        TEST_ASSERT_FALSE_MESSAGE(run_cli(4, argv), bad[i]);
    }
}

/* -------------------------------------------------------------------- */
/* --disk-index                                                          */
/* -------------------------------------------------------------------- */

static void test_disk_index_range(void)
{
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--disk-index", (char *)"0" };
        TEST_ASSERT_TRUE(run_cli(4, argv));
        TEST_ASSERT_EQUAL_INT(0, g_frontend.initial_disk_index);
    }
    reset_state();
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--disk-index", (char *)"255" };
        TEST_ASSERT_TRUE(run_cli(4, argv));
        TEST_ASSERT_EQUAL_INT(255, g_frontend.initial_disk_index);
    }
    const char *bad[] = { "256", "-1", "foo" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        reset_state();
        char *argv[] = {
            (char *)"pr", (char *)"c", (char *)"--disk-index", (char *)bad[i]
        };
        TEST_ASSERT_FALSE_MESSAGE(run_cli(4, argv), bad[i]);
    }
}

/* -------------------------------------------------------------------- */
/* --audio-rate, --audio-buffer-ms                                       */
/* -------------------------------------------------------------------- */

static void test_audio_rate_bounds(void)
{
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--audio-rate", (char *)"48000" };
        TEST_ASSERT_TRUE(run_cli(4, argv));
        TEST_ASSERT_EQUAL_UINT(48000, g_frontend.audio_rate_override);
    }
    reset_state();
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--audio-rate", (char *)"3999" };
        TEST_ASSERT_FALSE(run_cli(4, argv));
    }
    reset_state();
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--audio-rate", (char *)"384001" };
        TEST_ASSERT_FALSE(run_cli(4, argv));
    }
}

static void test_audio_buffer_ms_bounds(void)
{
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--audio-buffer-ms", (char *)"64" };
        TEST_ASSERT_TRUE(run_cli(4, argv));
        TEST_ASSERT_EQUAL_UINT(64, g_frontend.audio_buffer_ms_override);
    }
    reset_state();
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--audio-buffer-ms", (char *)"0" };
        TEST_ASSERT_FALSE(run_cli(4, argv));
    }
    reset_state();
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--audio-buffer-ms", (char *)"5001" };
        TEST_ASSERT_FALSE(run_cli(4, argv));
    }
}

/* -------------------------------------------------------------------- */
/* --variable                                                            */
/* -------------------------------------------------------------------- */

static void test_variable_populates_cli_overrides(void)
{
    char *argv[] = {
        (char *)"pr", (char *)"c", (char *)"--variable", (char *)"video_scale=3"
    };
    TEST_ASSERT_TRUE(run_cli(4, argv));
    TEST_ASSERT_EQUAL_STRING("3",
        variable_table_get(&g_frontend.cli_overrides, "video_scale"));
}

static void test_variable_rejects_missing_equals(void)
{
    char *argv[] = {
        (char *)"pr", (char *)"c", (char *)"--variable", (char *)"no-equals"
    };
    TEST_ASSERT_FALSE(run_cli(4, argv));
}

/* -------------------------------------------------------------------- */
/* --lang                                                                */
/* -------------------------------------------------------------------- */

static void test_lang_known_and_unknown(void)
{
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--lang", (char *)"ja" };
        TEST_ASSERT_TRUE(run_cli(4, argv));
        TEST_ASSERT_EQUAL_INT(RETRO_LANGUAGE_JAPANESE, g_frontend.language);
    }
    reset_state();
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--lang", (char *)"zh_cn" };
        TEST_ASSERT_TRUE(run_cli(4, argv));
        TEST_ASSERT_EQUAL_INT(RETRO_LANGUAGE_CHINESE_SIMPLIFIED, g_frontend.language);
    }
    reset_state();
    {
        char *argv[] = { (char *)"pr", (char *)"c", (char *)"--lang", (char *)"klingon" };
        TEST_ASSERT_FALSE(run_cli(4, argv));
    }
}

/* -------------------------------------------------------------------- */
/* Path-taking flags: --system-dir, --username, --core-assets-dir,       */
/* --playlist-dir, --file-browser-dir, --config, --subsystem,            */
/* --savestate                                                           */
/* -------------------------------------------------------------------- */

static void test_path_flags_are_stored_or_duplicated(void)
{
    char *argv[] = {
        (char *)"pr", (char *)"c",
        (char *)"--system-dir",       (char *)"/tmp/sys",
        (char *)"--username",         (char *)"Player1",
        (char *)"--core-assets-dir",  (char *)"/tmp/assets",
        (char *)"--playlist-dir",     (char *)"/tmp/pl",
        (char *)"--file-browser-dir", (char *)"/tmp/fb",
        (char *)"--config",           (char *)"/tmp/keys.cfg",
        (char *)"--subsystem",        (char *)"sgb",
        (char *)"--savestate",        (char *)"/tmp/save.state",
    };
    TEST_ASSERT_TRUE(run_cli(sizeof(argv) / sizeof(argv[0]), argv));

    /* strdup'd fields */
    TEST_ASSERT_EQUAL_STRING("/tmp/sys",    g_frontend.system_directory);
    TEST_ASSERT_EQUAL_STRING("Player1",     g_frontend.username);
    TEST_ASSERT_EQUAL_STRING("/tmp/assets", g_frontend.core_assets_directory);
    TEST_ASSERT_EQUAL_STRING("/tmp/pl",     g_frontend.playlist_directory);
    TEST_ASSERT_EQUAL_STRING("/tmp/fb",     g_frontend.file_browser_directory);

    /* Borrowed pointers (not duplicated) */
    TEST_ASSERT_EQUAL_STRING("/tmp/keys.cfg",   g_frontend.config_path);
    TEST_ASSERT_EQUAL_STRING("sgb",             g_frontend.subsystem_ident);
    TEST_ASSERT_EQUAL_STRING("/tmp/save.state", g_frontend.savestate_load_path);
}

/* --system-dir given twice: the first strdup must be freed, second wins. */
static void test_system_dir_replaces_previous_value(void)
{
    char *argv[] = {
        (char *)"pr", (char *)"c",
        (char *)"--system-dir", (char *)"/tmp/first",
        (char *)"--system-dir", (char *)"/tmp/second"
    };
    TEST_ASSERT_TRUE(run_cli(sizeof(argv) / sizeof(argv[0]), argv));
    TEST_ASSERT_EQUAL_STRING("/tmp/second", g_frontend.system_directory);
}

/* -------------------------------------------------------------------- */
/* --log-level                                                           */
/* -------------------------------------------------------------------- */

static void test_log_level_valid_and_invalid(void)
{
    {
        char *argv[] = {
            (char *)"pr", (char *)"c", (char *)"--log-level", (char *)"warn"
        };
        TEST_ASSERT_TRUE(run_cli(4, argv));
    }
    reset_state();
    {
        char *argv[] = {
            (char *)"pr", (char *)"c", (char *)"--log-level", (char *)"loud"
        };
        TEST_ASSERT_FALSE(run_cli(4, argv));
    }
}

/* -------------------------------------------------------------------- */
/* Unknown / malformed flags                                             */
/* -------------------------------------------------------------------- */

static void test_unknown_flag_fails(void)
{
    /* Position 3+ is unambiguously a flag position; unknown flags must fail.
     * (argv[2] would fall through to the content-path heuristic instead.) */
    char *argv[] = {
        (char *)"pr", (char *)"c.so", (char *)"--fullscreen", (char *)"--nope"
    };
    TEST_ASSERT_FALSE(run_cli(4, argv));
}

static void test_missing_argument_fails(void)
{
    /* --scale requires an argument. */
    char *argv[] = { (char *)"pr", (char *)"c.so", (char *)"--scale" };
    TEST_ASSERT_FALSE(run_cli(3, argv));
}

/* -------------------------------------------------------------------- */
/* Runner                                                                */
/* -------------------------------------------------------------------- */

int main(void)
{
    /* Silence stderr for tests that intentionally trigger failure paths.
     * We keep the fd open by redirecting to /dev/null (or NUL on Windows). */
    g_orig_stderr = stderr;
#if defined(_WIN32)
    freopen("NUL", "w", stderr);
#else
    freopen("/dev/null", "w", stderr);
#endif
    (void)g_orig_stderr;

    UNITY_BEGIN();

    RUN_TEST(test_positional_core_only);
    RUN_TEST(test_positional_core_and_content);
    RUN_TEST(test_argv2_flag_is_not_content);
    RUN_TEST(test_unknown_dash_argv2_is_content);
    RUN_TEST(test_too_few_args_fails);

    RUN_TEST(test_fullscreen_short_and_long);
    RUN_TEST(test_no_audio_and_portable);

    RUN_TEST(test_render_valid_values);
    RUN_TEST(test_render_invalid_value_fails);

    RUN_TEST(test_scale_valid_and_bounds);
    RUN_TEST(test_scale_rejects_out_of_range_and_junk);

    RUN_TEST(test_disk_index_range);

    RUN_TEST(test_audio_rate_bounds);
    RUN_TEST(test_audio_buffer_ms_bounds);

    RUN_TEST(test_variable_populates_cli_overrides);
    RUN_TEST(test_variable_rejects_missing_equals);

    RUN_TEST(test_lang_known_and_unknown);

    RUN_TEST(test_path_flags_are_stored_or_duplicated);
    RUN_TEST(test_system_dir_replaces_previous_value);

    RUN_TEST(test_log_level_valid_and_invalid);

    RUN_TEST(test_unknown_flag_fails);
    RUN_TEST(test_missing_argument_fails);

    return UNITY_END();
}
