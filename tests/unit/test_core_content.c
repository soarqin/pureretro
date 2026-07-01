#include <unity.h>

#include "core_content.h"

void setUp(void) {}
void tearDown(void) {}

void test_extension_extracts_last_path_component_suffix(void)
{
    TEST_ASSERT_EQUAL_STRING("sfc", core_content_extension("/tmp/game.sfc"));
    TEST_ASSERT_EQUAL_STRING("CUE", core_content_extension("C:\\roms\\disc.CUE"));
    TEST_ASSERT_EQUAL_STRING("", core_content_extension("/tmp/.hidden"));
    TEST_ASSERT_EQUAL_STRING("", core_content_extension("/tmp/noext"));
}

void test_extension_list_matches_case_insensitively(void)
{
    TEST_ASSERT_TRUE(core_content_extension_matches("sfc|smc|fig", "SMC"));
    TEST_ASSERT_TRUE(core_content_extension_matches(" cue | chd ", "cue"));
    TEST_ASSERT_FALSE(core_content_extension_matches("sfc|smc", "nes"));
    TEST_ASSERT_FALSE(core_content_extension_matches("sfc|smc", ""));
}

void test_content_override_updates_matching_policy(void)
{
    struct content_info_override_storage overrides[2] = {
        { "sfc|smc", false, true },
        { "iso|chd", true, false },
    };
    struct core_content_load_policy policy = { false, true };

    core_content_apply_overrides("/games/disc.CHD", overrides, 2, &policy);

    TEST_ASSERT_TRUE(policy.need_fullpath);
    TEST_ASSERT_FALSE(policy.persistent_data);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_extension_extracts_last_path_component_suffix);
    RUN_TEST(test_extension_list_matches_case_insensitively);
    RUN_TEST(test_content_override_updates_matching_policy);
    return UNITY_END();
}
