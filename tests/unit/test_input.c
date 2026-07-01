#include <unity.h>

#include "frontend.h"
#include "input.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_state g_frontend;

static unsigned g_keyboard_calls;
static bool g_last_down;
static enum retro_key g_last_key;

static void RETRO_CALLCONV keyboard_callback(bool down, unsigned keycode,
                                             uint32_t character,
                                             uint16_t key_modifiers)
{
    (void)character;
    (void)key_modifiers;
    g_keyboard_calls++;
    g_last_down = down;
    g_last_key = (enum retro_key)keycode;
}

static void make_key_event(SDL_Event *event, SDL_EventType type,
                           SDL_Scancode scancode)
{
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->key.scancode = scancode;
    event->key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
}

static void write_keymap(const char *path, const char *contents)
{
    FILE *fp = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_size_t(strlen(contents), fwrite(contents, 1, strlen(contents), fp));
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));
}

static void temp_path(char *out, size_t out_size)
{
#ifdef _WIN32
    const char *dir = getenv("TEMP");
    const char sep = '\\';
#else
    const char *dir = getenv("TMPDIR");
    const char sep = '/';
#endif
    if (!dir || dir[0] == '\0')
        dir = "/tmp";
    snprintf(out, out_size, "%s%cpureretro_input_test_%p.cfg",
             dir, sep, (void *)out);
}

void setUp(void)
{
    memset(&g_frontend, 0, sizeof(g_frontend));
    g_keyboard_calls = 0;
    g_last_down = false;
    g_last_key = RETROK_UNKNOWN;
    input_free_keymap();
}

void tearDown(void)
{
    input_free_keymap();
}

void test_keymap_accepts_equals_and_whitespace_syntax(void)
{
    char path[512];
    temp_path(path, sizeof(path));
    write_keymap(path, "SPACE=A\nRETURN START\nRight Shift Y\n");

    TEST_ASSERT_TRUE(input_load_keymap(path));

    SDL_Event event;
    make_key_event(&event, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE);
    input_process_event(&event);
    TEST_ASSERT_EQUAL_INT16(1, input_state_joypad(0, RETRO_DEVICE_ID_JOYPAD_A));

    make_key_event(&event, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RETURN);
    input_process_event(&event);
    TEST_ASSERT_EQUAL_INT16(1, input_state_joypad(0, RETRO_DEVICE_ID_JOYPAD_START));

    make_key_event(&event, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RSHIFT);
    input_process_event(&event);
    TEST_ASSERT_EQUAL_INT16(1, input_state_joypad(0, RETRO_DEVICE_ID_JOYPAD_Y));

    remove(path);
}

void test_mapped_key_also_reaches_keyboard_callback(void)
{
    g_frontend.keyboard_callback.callback = keyboard_callback;

    SDL_Event event;
    make_key_event(&event, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Z);
    input_process_event(&event);

    TEST_ASSERT_EQUAL_INT16(1, input_state_joypad(0, RETRO_DEVICE_ID_JOYPAD_B));
    TEST_ASSERT_EQUAL_UINT(1, g_keyboard_calls);
    TEST_ASSERT_TRUE(g_last_down);
    TEST_ASSERT_EQUAL_INT(RETROK_z, g_last_key);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_keymap_accepts_equals_and_whitespace_syntax);
    RUN_TEST(test_mapped_key_also_reaches_keyboard_callback);
    return UNITY_END();
}
