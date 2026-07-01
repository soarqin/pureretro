#include <unity.h>

#include "core_variables.h"
#include "frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* core_variables.c references g_frontend from a handful of helpers
 * (core_variable_override, core_variables_load/save). We provide the
 * storage here so the object file links. */
struct frontend_state g_frontend;

/* Resolve a variable using the same precedence env_get_variable applies:
 *   cli_overrides -> disk_overrides -> current_value -> default_value.
 * Duplicated here to keep the test independent of core.c (which pulls in
 * SDL/libretro glue we do not want in this unit test). */
static const char *resolve_variable(const char *key)
{
    const char *v = variable_table_get(&g_frontend.cli_overrides, key);
    if (v) return v;
    v = variable_table_get(&g_frontend.disk_overrides, key);
    if (v) return v;

    const struct core_option *opt =
        core_options_table_get(&g_frontend.core_options, key);
    if (!opt) return NULL;
    return opt->current_value ? opt->current_value : opt->default_value;
}

void setUp(void)
{
    memset(&g_frontend, 0, sizeof(g_frontend));
}

void tearDown(void)
{
    variable_table_clear(&g_frontend.cli_overrides);
    variable_table_clear(&g_frontend.disk_overrides);
    core_options_table_clear(&g_frontend.core_options);
}

/* -------------------------------------------------------------------- */
/* variable_table                                                        */
/* -------------------------------------------------------------------- */

static void test_variable_table_set_and_get_returns_value(void)
{
    struct variable_table t = {0};

    TEST_ASSERT_TRUE(variable_table_set(&t, "video_scale", "3"));
    TEST_ASSERT_EQUAL_STRING("3", variable_table_get(&t, "video_scale"));
    TEST_ASSERT_EQUAL_size_t(1, variable_table_count(&t));

    variable_table_clear(&t);
}

static void test_variable_table_get_missing_returns_null(void)
{
    struct variable_table t = {0};

    TEST_ASSERT_NULL(variable_table_get(&t, "absent"));
    TEST_ASSERT_TRUE(variable_table_set(&t, "a", "1"));
    TEST_ASSERT_NULL(variable_table_get(&t, "b"));

    variable_table_clear(&t);
}

static void test_variable_table_update_replaces_value_in_place(void)
{
    struct variable_table t = {0};

    TEST_ASSERT_TRUE(variable_table_set(&t, "region", "ntsc"));
    TEST_ASSERT_EQUAL_STRING("ntsc", variable_table_get(&t, "region"));

    TEST_ASSERT_TRUE(variable_table_set(&t, "region", "pal"));
    TEST_ASSERT_EQUAL_STRING("pal", variable_table_get(&t, "region"));
    TEST_ASSERT_EQUAL_size_t(1, variable_table_count(&t));

    variable_table_clear(&t);
}

static void test_variable_table_keeps_items_sorted_for_bsearch(void)
{
    struct variable_table t = {0};

    /* Insert deliberately out of order. */
    TEST_ASSERT_TRUE(variable_table_set(&t, "zebra",  "z"));
    TEST_ASSERT_TRUE(variable_table_set(&t, "alpha",  "a"));
    TEST_ASSERT_TRUE(variable_table_set(&t, "middle", "m"));
    TEST_ASSERT_EQUAL_size_t(3, variable_table_count(&t));

    /* All lookups succeed (bsearch requires sorted storage). */
    TEST_ASSERT_EQUAL_STRING("a", variable_table_get(&t, "alpha"));
    TEST_ASSERT_EQUAL_STRING("m", variable_table_get(&t, "middle"));
    TEST_ASSERT_EQUAL_STRING("z", variable_table_get(&t, "zebra"));

    /* Iteration is in ascending key order. */
    TEST_ASSERT_EQUAL_STRING("alpha",  variable_table_at(&t, 0)->key);
    TEST_ASSERT_EQUAL_STRING("middle", variable_table_at(&t, 1)->key);
    TEST_ASSERT_EQUAL_STRING("zebra",  variable_table_at(&t, 2)->key);

    variable_table_clear(&t);
}

static void test_variable_table_rejects_null_inputs(void)
{
    struct variable_table t = {0};

    TEST_ASSERT_FALSE(variable_table_set(NULL, "k", "v"));
    TEST_ASSERT_FALSE(variable_table_set(&t, NULL, "v"));
    TEST_ASSERT_FALSE(variable_table_set(&t, "k", NULL));

    TEST_ASSERT_NULL(variable_table_get(NULL, "k"));
    TEST_ASSERT_NULL(variable_table_get(&t, NULL));

    variable_table_clear(NULL);
    TEST_ASSERT_EQUAL_size_t(0, variable_table_count(NULL));
    TEST_ASSERT_NULL(variable_table_at(NULL, 0));
    TEST_ASSERT_NULL(variable_table_at(&t, 0)); /* empty */
}

/* -------------------------------------------------------------------- */
/* core_options_table                                                    */
/* -------------------------------------------------------------------- */

static void test_core_options_table_add_and_lookup(void)
{
    struct core_options_table t = {0};
    const char *const vals[] = { "off", "on", NULL };

    TEST_ASSERT_TRUE(core_options_table_add(&t, "hack",
                                             "Speed hack",
                                             "Skip cycles for speed",
                                             vals, "off"));

    const struct core_option *opt = core_options_table_get(&t, "hack");
    TEST_ASSERT_NOT_NULL(opt);
    TEST_ASSERT_EQUAL_STRING("hack",       opt->key);
    TEST_ASSERT_EQUAL_STRING("Speed hack", opt->desc);
    TEST_ASSERT_EQUAL_STRING("off",        opt->default_value);
    TEST_ASSERT_NULL(opt->current_value);
    TEST_ASSERT_TRUE(opt->visible);
    TEST_ASSERT_EQUAL_STRING("off", opt->values[0]);
    TEST_ASSERT_EQUAL_STRING("on",  opt->values[1]);
    TEST_ASSERT_NULL(opt->values[2]);

    TEST_ASSERT_EQUAL_size_t(1, core_options_table_count(&t));

    core_options_table_clear(&t);
}

static void test_core_options_table_add_rejects_duplicate_key(void)
{
    struct core_options_table t = {0};
    const char *const vals[] = { "a", NULL };

    TEST_ASSERT_TRUE (core_options_table_add(&t, "k", "d",  NULL, vals, "a"));
    TEST_ASSERT_FALSE(core_options_table_add(&t, "k", "d2", NULL, vals, "a"));
    TEST_ASSERT_EQUAL_size_t(1, core_options_table_count(&t));

    core_options_table_clear(&t);
}

static void test_core_options_table_preserves_declaration_order(void)
{
    struct core_options_table t = {0};
    const char *const vals[] = { "x", NULL };

    TEST_ASSERT_TRUE(core_options_table_add(&t, "zeta",  "z", NULL, vals, "x"));
    TEST_ASSERT_TRUE(core_options_table_add(&t, "alpha", "a", NULL, vals, "x"));
    TEST_ASSERT_TRUE(core_options_table_add(&t, "mid",   "m", NULL, vals, "x"));

    /* _at() iterates in insertion (declaration) order. */
    TEST_ASSERT_EQUAL_STRING("zeta",  core_options_table_at(&t, 0)->key);
    TEST_ASSERT_EQUAL_STRING("alpha", core_options_table_at(&t, 1)->key);
    TEST_ASSERT_EQUAL_STRING("mid",   core_options_table_at(&t, 2)->key);
    TEST_ASSERT_NULL(core_options_table_at(&t, 3));

    /* Lookup works for all three, thanks to the sorted index. */
    TEST_ASSERT_NOT_NULL(core_options_table_get(&t, "alpha"));
    TEST_ASSERT_NOT_NULL(core_options_table_get(&t, "zeta"));
    TEST_ASSERT_NOT_NULL(core_options_table_get(&t, "mid"));
    TEST_ASSERT_NULL(core_options_table_get(&t, "absent"));

    core_options_table_clear(&t);
}

static void test_core_options_table_set_value_updates_current(void)
{
    struct core_options_table t = {0};
    const char *const vals[] = { "off", "on", NULL };
    TEST_ASSERT_TRUE(core_options_table_add(&t, "hack", "d", NULL, vals, "off"));

    TEST_ASSERT_TRUE(core_options_table_set_value(&t, "hack", "on"));
    TEST_ASSERT_EQUAL_STRING("on",
        core_options_table_get(&t, "hack")->current_value);

    /* Overwriting yields the new value. */
    TEST_ASSERT_TRUE(core_options_table_set_value(&t, "hack", "off"));
    TEST_ASSERT_EQUAL_STRING("off",
        core_options_table_get(&t, "hack")->current_value);

    /* Setting a missing key fails and does not create an entry. */
    TEST_ASSERT_FALSE(core_options_table_set_value(&t, "absent", "on"));
    TEST_ASSERT_NULL(core_options_table_get(&t, "absent"));

    core_options_table_clear(&t);
}

static void test_core_options_table_set_visible_toggles_flag(void)
{
    struct core_options_table t = {0};
    const char *const vals[] = { "off", NULL };
    TEST_ASSERT_TRUE(core_options_table_add(&t, "hack", "d", NULL, vals, "off"));
    TEST_ASSERT_TRUE(core_options_table_get(&t, "hack")->visible);

    TEST_ASSERT_TRUE(core_options_table_set_visible(&t, "hack", false));
    TEST_ASSERT_FALSE(core_options_table_get(&t, "hack")->visible);

    TEST_ASSERT_TRUE(core_options_table_set_visible(&t, "hack", true));
    TEST_ASSERT_TRUE(core_options_table_get(&t, "hack")->visible);

    /* Missing key -> false, no crash. */
    TEST_ASSERT_FALSE(core_options_table_set_visible(&t, "absent", true));

    core_options_table_clear(&t);
}

static void test_core_options_table_null_safety(void)
{
    TEST_ASSERT_NULL(core_options_table_get(NULL, "k"));
    TEST_ASSERT_NULL(core_options_table_at(NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, core_options_table_count(NULL));
    TEST_ASSERT_FALSE(core_options_table_set_value(NULL, "k", "v"));
    TEST_ASSERT_FALSE(core_options_table_set_visible(NULL, "k", true));
    core_options_table_clear(NULL); /* must not crash */
}

/* -------------------------------------------------------------------- */
/* Lookup precedence: cli -> disk -> current -> default                  */
/* -------------------------------------------------------------------- */

static void test_lookup_uses_default_when_no_overrides(void)
{
    const char *const vals[] = { "off", "on", NULL };
    TEST_ASSERT_TRUE(core_options_table_add(&g_frontend.core_options,
                                             "hack", "d", NULL, vals, "off"));

    TEST_ASSERT_EQUAL_STRING("off", resolve_variable("hack"));
}

static void test_lookup_prefers_current_over_default(void)
{
    const char *const vals[] = { "off", "on", NULL };
    TEST_ASSERT_TRUE(core_options_table_add(&g_frontend.core_options,
                                             "hack", "d", NULL, vals, "off"));
    TEST_ASSERT_TRUE(core_options_table_set_value(&g_frontend.core_options,
                                                   "hack", "on"));
    TEST_ASSERT_EQUAL_STRING("on", resolve_variable("hack"));
}

static void test_lookup_prefers_disk_over_current_and_default(void)
{
    const char *const vals[] = { "off", "on", "turbo", NULL };
    TEST_ASSERT_TRUE(core_options_table_add(&g_frontend.core_options,
                                             "hack", "d", NULL, vals, "off"));
    TEST_ASSERT_TRUE(core_options_table_set_value(&g_frontend.core_options,
                                                   "hack", "on"));
    TEST_ASSERT_TRUE(variable_table_set(&g_frontend.disk_overrides,
                                         "hack", "turbo"));

    TEST_ASSERT_EQUAL_STRING("turbo", resolve_variable("hack"));
}

static void test_lookup_prefers_cli_over_disk(void)
{
    const char *const vals[] = { "off", "on", "turbo", NULL };
    TEST_ASSERT_TRUE(core_options_table_add(&g_frontend.core_options,
                                             "hack", "d", NULL, vals, "off"));
    TEST_ASSERT_TRUE(variable_table_set(&g_frontend.disk_overrides,
                                         "hack", "on"));
    TEST_ASSERT_TRUE(variable_table_set(&g_frontend.cli_overrides,
                                         "hack", "turbo"));

    TEST_ASSERT_EQUAL_STRING("turbo", resolve_variable("hack"));
}

static void test_lookup_missing_key_returns_null(void)
{
    TEST_ASSERT_NULL(resolve_variable("never-declared"));

    /* Even with a disk/cli value, a variable the core never declared
     * still resolves via the overrides — this documents the current
     * behavior mirrored from env_get_variable. */
    TEST_ASSERT_TRUE(variable_table_set(&g_frontend.disk_overrides,
                                         "undeclared", "whatever"));
    TEST_ASSERT_EQUAL_STRING("whatever", resolve_variable("undeclared"));
}

static void test_core_variable_override_populates_cli_table(void)
{
    core_variable_override("scale", "3");
    TEST_ASSERT_EQUAL_STRING("3",
        variable_table_get(&g_frontend.cli_overrides, "scale"));

    /* NULL args must be a no-op, not a crash. */
    core_variable_override(NULL, "x");
    core_variable_override("k", NULL);
    TEST_ASSERT_EQUAL_size_t(1, variable_table_count(&g_frontend.cli_overrides));
}

/* -------------------------------------------------------------------- */
/* core_variables_path                                                   */
/* -------------------------------------------------------------------- */

static void test_variables_path_strips_libretro_suffix_and_ext(void)
{
    char *p;

    p = core_variables_path("/opt/libretro/nestopia_libretro.so", "/tmp");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("/tmp/nestopia.opt", p);
    free(p);

    p = core_variables_path("C:\\cores\\snes9x_libretro.dll", "C:\\data");
    TEST_ASSERT_NOT_NULL(p);
    /* Path is emitted verbatim; the inserted separator is always '/'. */
    TEST_ASSERT_EQUAL_STRING("C:\\data/snes9x.opt", p);
    free(p);

    p = core_variables_path("/x/mgba_libretro.dylib", "/y/");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("/y/mgba.opt", p);
    free(p);
}

static void test_variables_path_handles_unusual_names(void)
{
    char *p;

    /* No _libretro suffix, but recognizable extension. */
    p = core_variables_path("/x/foo.so", "/y");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("/y/foo.opt", p);
    free(p);

    /* No extension at all. */
    p = core_variables_path("/x/bar", "/y");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("/y/bar.opt", p);
    free(p);

    /* NULL base_dir must fail cleanly. */
    TEST_ASSERT_NULL(core_variables_path("/x/foo_libretro.so", NULL));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_variable_table_set_and_get_returns_value);
    RUN_TEST(test_variable_table_get_missing_returns_null);
    RUN_TEST(test_variable_table_update_replaces_value_in_place);
    RUN_TEST(test_variable_table_keeps_items_sorted_for_bsearch);
    RUN_TEST(test_variable_table_rejects_null_inputs);

    RUN_TEST(test_core_options_table_add_and_lookup);
    RUN_TEST(test_core_options_table_add_rejects_duplicate_key);
    RUN_TEST(test_core_options_table_preserves_declaration_order);
    RUN_TEST(test_core_options_table_set_value_updates_current);
    RUN_TEST(test_core_options_table_set_visible_toggles_flag);
    RUN_TEST(test_core_options_table_null_safety);

    RUN_TEST(test_lookup_uses_default_when_no_overrides);
    RUN_TEST(test_lookup_prefers_current_over_default);
    RUN_TEST(test_lookup_prefers_disk_over_current_and_default);
    RUN_TEST(test_lookup_prefers_cli_over_disk);
    RUN_TEST(test_lookup_missing_key_returns_null);
    RUN_TEST(test_core_variable_override_populates_cli_table);

    RUN_TEST(test_variables_path_strips_libretro_suffix_and_ext);
    RUN_TEST(test_variables_path_handles_unusual_names);

    return UNITY_END();
}
