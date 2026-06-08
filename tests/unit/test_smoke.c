#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static void test_basic_arithmetic(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_basic_arithmetic);
    return UNITY_END();
}
