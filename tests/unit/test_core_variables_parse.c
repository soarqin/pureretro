#include <unity.h>

#include "core_variables_parse.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* -------------------------------------------------------------------- */
/* core_var_parse_default                                                */
/* -------------------------------------------------------------------- */

static void test_parse_default_returns_first_choice(void)
{
    char out[32];

    TEST_ASSERT_TRUE(core_var_parse_default("Speed hack; enabled|disabled",
                                             out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("enabled", out);

    /* Multiple pipe-separated choices, whitespace after ';'. */
    TEST_ASSERT_TRUE(core_var_parse_default("Resolution;   640x480|1280x960|1920x1440",
                                             out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("640x480", out);

    /* Single-choice list (no pipes). */
    TEST_ASSERT_TRUE(core_var_parse_default("Region; NTSC",
                                             out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("NTSC", out);
}

static void test_parse_default_handles_missing_or_empty_choice(void)
{
    char out[32];

    /* No ';' at all — v0 spec allows this; there is no default. */
    memset(out, 'X', sizeof(out));
    TEST_ASSERT_FALSE(core_var_parse_default("description only",
                                              out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);

    /* Semicolon present but empty choices. */
    memset(out, 'X', sizeof(out));
    TEST_ASSERT_FALSE(core_var_parse_default("desc; ", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_parse_default_null_and_size_safety(void)
{
    char out[8];

    TEST_ASSERT_FALSE(core_var_parse_default(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);

    /* out=NULL or out_len=0 must fail without crashing. */
    TEST_ASSERT_FALSE(core_var_parse_default("desc; a|b", NULL, sizeof(out)));
    TEST_ASSERT_FALSE(core_var_parse_default("desc; a|b", out, 0));
}

static void test_parse_default_truncates_to_output_buffer(void)
{
    /* Buffer only holds 4 chars + NUL. Default is "abcdef". */
    char out[5];
    TEST_ASSERT_TRUE(core_var_parse_default("d; abcdef|xyz",
                                             out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abcd", out);
    TEST_ASSERT_EQUAL_CHAR('\0', out[4]);
}

/* -------------------------------------------------------------------- */
/* core_var_parse_description                                            */
/* -------------------------------------------------------------------- */

static void test_parse_description_takes_text_before_semicolon(void)
{
    char out[64];

    core_var_parse_description("Speed hack; enabled|disabled",
                                out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Speed hack", out);

    core_var_parse_description("Region; NTSC", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Region", out);
}

static void test_parse_description_trims_trailing_whitespace(void)
{
    char out[64];

    core_var_parse_description("Speed hack   ; enabled|disabled",
                                out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Speed hack", out);

    /* Trailing tabs too. */
    core_var_parse_description("Foo\t\t; bar", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Foo", out);
}

static void test_parse_description_no_semicolon_is_empty(void)
{
    char out[64];
    memset(out, 'X', sizeof(out));
    core_var_parse_description("plain text no semicolon",
                                out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_parse_description_null_and_size_safety(void)
{
    char out[64];

    core_var_parse_description(NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);

    /* Must not crash with NULL out or zero size. */
    core_var_parse_description("desc; a", NULL, sizeof(out));
    core_var_parse_description("desc; a", out, 0);
}

static void test_parse_description_truncates_to_output_buffer(void)
{
    char out[6];  /* holds 5 chars + NUL */
    core_var_parse_description("HelloWorld; a|b", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Hello", out);
    TEST_ASSERT_EQUAL_CHAR('\0', out[5]);
}

/* -------------------------------------------------------------------- */
/* core_var_choices_begin                                                */
/* -------------------------------------------------------------------- */

static void test_choices_begin_points_after_semicolon_and_space(void)
{
    const char *raw = "desc; enabled|disabled";
    const char *begin = core_var_choices_begin(raw);
    TEST_ASSERT_NOT_NULL(begin);
    TEST_ASSERT_EQUAL_STRING("enabled|disabled", begin);
}

static void test_choices_begin_skips_multiple_spaces(void)
{
    const char *begin = core_var_choices_begin("desc;      a|b");
    TEST_ASSERT_NOT_NULL(begin);
    TEST_ASSERT_EQUAL_STRING("a|b", begin);
}

static void test_choices_begin_returns_null_on_empty_choices(void)
{
    /* Semicolon present but only whitespace after. */
    TEST_ASSERT_NULL(core_var_choices_begin("desc;"));
    TEST_ASSERT_NULL(core_var_choices_begin("desc; "));
    TEST_ASSERT_NULL(core_var_choices_begin("desc;    "));
}

static void test_choices_begin_returns_input_when_no_semicolon(void)
{
    /* Documented behavior: raw is returned as-is when ';' is absent. */
    const char *raw = "no-semicolon-at-all";
    TEST_ASSERT_EQUAL_PTR(raw, core_var_choices_begin(raw));
}

static void test_choices_begin_null_input(void)
{
    TEST_ASSERT_NULL(core_var_choices_begin(NULL));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_parse_default_returns_first_choice);
    RUN_TEST(test_parse_default_handles_missing_or_empty_choice);
    RUN_TEST(test_parse_default_null_and_size_safety);
    RUN_TEST(test_parse_default_truncates_to_output_buffer);

    RUN_TEST(test_parse_description_takes_text_before_semicolon);
    RUN_TEST(test_parse_description_trims_trailing_whitespace);
    RUN_TEST(test_parse_description_no_semicolon_is_empty);
    RUN_TEST(test_parse_description_null_and_size_safety);
    RUN_TEST(test_parse_description_truncates_to_output_buffer);

    RUN_TEST(test_choices_begin_points_after_semicolon_and_space);
    RUN_TEST(test_choices_begin_skips_multiple_spaces);
    RUN_TEST(test_choices_begin_returns_null_on_empty_choices);
    RUN_TEST(test_choices_begin_returns_input_when_no_semicolon);
    RUN_TEST(test_choices_begin_null_input);

    return UNITY_END();
}
