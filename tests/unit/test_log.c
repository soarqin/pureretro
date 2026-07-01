#include <unity.h>

#include "log.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* -------------------------------------------------------------------- */
/* log_parse_level                                                       */
/* -------------------------------------------------------------------- */

static void test_parse_level_accepts_canonical_names(void)
{
    enum log_level lvl = LOG_LEVEL_INFO;

    TEST_ASSERT_TRUE(log_parse_level("debug", &lvl));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_DEBUG, lvl);

    TEST_ASSERT_TRUE(log_parse_level("info", &lvl));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INFO, lvl);

    TEST_ASSERT_TRUE(log_parse_level("warn", &lvl));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_WARN, lvl);

    TEST_ASSERT_TRUE(log_parse_level("error", &lvl));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_ERROR, lvl);
}

static void test_parse_level_accepts_warning_alias(void)
{
    enum log_level lvl = LOG_LEVEL_INFO;
    TEST_ASSERT_TRUE(log_parse_level("warning", &lvl));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_WARN, lvl);
}

static void test_parse_level_is_case_insensitive(void)
{
    enum log_level lvl = LOG_LEVEL_INFO;

    TEST_ASSERT_TRUE(log_parse_level("DEBUG", &lvl));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_DEBUG, lvl);

    TEST_ASSERT_TRUE(log_parse_level("Info", &lvl));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INFO, lvl);

    TEST_ASSERT_TRUE(log_parse_level("WaRn", &lvl));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_WARN, lvl);

    TEST_ASSERT_TRUE(log_parse_level("ERROR", &lvl));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_ERROR, lvl);
}

static void test_parse_level_rejects_invalid_input(void)
{
    enum log_level lvl = LOG_LEVEL_INFO;

    TEST_ASSERT_FALSE(log_parse_level(NULL, &lvl));
    TEST_ASSERT_FALSE(log_parse_level("", &lvl));
    TEST_ASSERT_FALSE(log_parse_level("verbose", &lvl));
    TEST_ASSERT_FALSE(log_parse_level("trace", &lvl));
    TEST_ASSERT_FALSE(log_parse_level("info ", &lvl));  /* trailing space */
    TEST_ASSERT_FALSE(log_parse_level("infox", &lvl));
    TEST_ASSERT_FALSE(log_parse_level("de bug", &lvl));

    /* Passing NULL out must be safe. */
    TEST_ASSERT_FALSE(log_parse_level("debug", NULL));
}

/* -------------------------------------------------------------------- */
/* log_set_level / log_get_level                                         */
/* -------------------------------------------------------------------- */

static void test_set_and_get_level_round_trip(void)
{
    log_set_level(LOG_LEVEL_ERROR);
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_ERROR, log_get_level());

    log_set_level(LOG_LEVEL_DEBUG);
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_DEBUG, log_get_level());

    log_set_level(LOG_LEVEL_WARN);
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_WARN, log_get_level());

    log_set_level(LOG_LEVEL_INFO);
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INFO, log_get_level());
}

/* -------------------------------------------------------------------- */
/* log_init env-var behavior                                             */
/* -------------------------------------------------------------------- */

/* portable setenv/unsetenv wrappers (Windows uses _putenv_s) */
#if defined(_WIN32)
#include <stdio.h>
static void set_env(const char *k, const char *v) { _putenv_s(k, v); }
static void unset_env(const char *k)              { _putenv_s(k, ""); }
#else
static void set_env(const char *k, const char *v) { setenv(k, v, 1); }
static void unset_env(const char *k)              { unsetenv(k); }
#endif

/* Reset the "explicit" latch so log_init() will honor the env var again.
 * We do this by calling log_init() after clearing the env var; log_init
 * only assigns if not explicit. To reset the explicit flag we rely on
 * the fact that log_set_level() sets it, and there is no public reset —
 * so we test log_init() first, before any log_set_level() call. */
static void test_init_reads_env_when_not_explicit(void)
{
    /* Precondition: no prior log_set_level() this process. Unity runs
     * tests in the declared order, so we place this test first among the
     * env-var tests. */
    set_env("PURERETRO_LOG", "warn");
    log_init();
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_WARN, log_get_level());

    /* Unknown env value must be ignored and leave the current level intact. */
    set_env("PURERETRO_LOG", "not-a-level");
    log_init();
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_WARN, log_get_level());

    unset_env("PURERETRO_LOG");
}

static void test_init_is_noop_after_explicit_set(void)
{
    log_set_level(LOG_LEVEL_ERROR);
    set_env("PURERETRO_LOG", "debug");
    log_init();
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_ERROR, log_get_level());
    unset_env("PURERETRO_LOG");
}

int main(void)
{
    UNITY_BEGIN();
    /* Pure parser tests first — they do not mutate global state. */
    RUN_TEST(test_parse_level_accepts_canonical_names);
    RUN_TEST(test_parse_level_accepts_warning_alias);
    RUN_TEST(test_parse_level_is_case_insensitive);
    RUN_TEST(test_parse_level_rejects_invalid_input);

    /* Env-var test must run BEFORE any log_set_level() call, since
     * log_set_level() latches g_level_explicit=true and log_init() then
     * becomes a no-op for the rest of the process. */
    RUN_TEST(test_init_reads_env_when_not_explicit);

    /* set/get and the "no-op after explicit" test can follow. */
    RUN_TEST(test_set_and_get_level_round_trip);
    RUN_TEST(test_init_is_noop_after_explicit_set);
    return UNITY_END();
}
