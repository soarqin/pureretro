/*
 * PureRetro — Input subsystem
 *
 * Keyboard mapping to libretro's RetroPad abstraction.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "input.h"
#include "frontend.h"

/* ------------------------------------------------------------------ */
/* RetroPad button name lookup                                         */
/* ------------------------------------------------------------------ */

static struct {
    const char *name;
    unsigned id;
} g_button_names[] = {
    { "B", RETRO_DEVICE_ID_JOYPAD_B },
    { "Y", RETRO_DEVICE_ID_JOYPAD_Y },
    { "SELECT", RETRO_DEVICE_ID_JOYPAD_SELECT },
    { "START", RETRO_DEVICE_ID_JOYPAD_START },
    { "UP", RETRO_DEVICE_ID_JOYPAD_UP },
    { "DOWN", RETRO_DEVICE_ID_JOYPAD_DOWN },
    { "LEFT", RETRO_DEVICE_ID_JOYPAD_LEFT },
    { "RIGHT", RETRO_DEVICE_ID_JOYPAD_RIGHT },
    { "A", RETRO_DEVICE_ID_JOYPAD_A },
    { "X", RETRO_DEVICE_ID_JOYPAD_X },
    { "L", RETRO_DEVICE_ID_JOYPAD_L },
    { "R", RETRO_DEVICE_ID_JOYPAD_R },
    { "L2", RETRO_DEVICE_ID_JOYPAD_L2 },
    { "R2", RETRO_DEVICE_ID_JOYPAD_R2 },
    { "L3", RETRO_DEVICE_ID_JOYPAD_L3 },
    { "R3", RETRO_DEVICE_ID_JOYPAD_R3 },
};

/* ------------------------------------------------------------------ */
/* Built-in scancode → RetroPad mapping                                */
/* ------------------------------------------------------------------ */

static const struct
{
    SDL_Scancode scancode;
    unsigned retro_id;
} g_default_keymap[] = {
    { SDL_SCANCODE_UP,     RETRO_DEVICE_ID_JOYPAD_UP     },
    { SDL_SCANCODE_DOWN,   RETRO_DEVICE_ID_JOYPAD_DOWN   },
    { SDL_SCANCODE_LEFT,   RETRO_DEVICE_ID_JOYPAD_LEFT   },
    { SDL_SCANCODE_RIGHT,  RETRO_DEVICE_ID_JOYPAD_RIGHT  },
    { SDL_SCANCODE_RETURN, RETRO_DEVICE_ID_JOYPAD_START  },
    { SDL_SCANCODE_RSHIFT, RETRO_DEVICE_ID_JOYPAD_SELECT },
    { SDL_SCANCODE_Z,      RETRO_DEVICE_ID_JOYPAD_B      },
    { SDL_SCANCODE_X,      RETRO_DEVICE_ID_JOYPAD_A      },
    { SDL_SCANCODE_A,      RETRO_DEVICE_ID_JOYPAD_Y      },
    { SDL_SCANCODE_S,      RETRO_DEVICE_ID_JOYPAD_X      },
    { SDL_SCANCODE_Q,      RETRO_DEVICE_ID_JOYPAD_L      },
    { SDL_SCANCODE_W,      RETRO_DEVICE_ID_JOYPAD_R      },
};

#define KEYMAP_UNMAPPED 0xFFu
static uint8_t g_scancode_to_retro[SDL_SCANCODE_COUNT];
static bool g_scancode_table_built = false;

static void build_scancode_table(void)
{
    for (size_t i = 0; i < SDL_SCANCODE_COUNT; ++i)
        g_scancode_to_retro[i] = KEYMAP_UNMAPPED;
    for (size_t i = 0; i < sizeof(g_default_keymap) / sizeof(g_default_keymap[0]); ++i)
        g_scancode_to_retro[g_default_keymap[i].scancode] = (uint8_t)g_default_keymap[i].retro_id;
    g_scancode_table_built = true;
}

/* ------------------------------------------------------------------ */
/* SDL scancode → retro_key mapping                                    */
/* ------------------------------------------------------------------ */

static unsigned g_sdl_to_retro_key[SDL_SCANCODE_COUNT];
static bool g_retro_key_table_built = false;

static void build_retro_key_table(void)
{
    for (size_t i = 0; i < SDL_SCANCODE_COUNT; ++i)
        g_sdl_to_retro_key[i] = RETROK_UNKNOWN;

    g_sdl_to_retro_key[SDL_SCANCODE_A] = RETROK_a;
    g_sdl_to_retro_key[SDL_SCANCODE_B] = RETROK_b;
    g_sdl_to_retro_key[SDL_SCANCODE_C] = RETROK_c;
    g_sdl_to_retro_key[SDL_SCANCODE_D] = RETROK_d;
    g_sdl_to_retro_key[SDL_SCANCODE_E] = RETROK_e;
    g_sdl_to_retro_key[SDL_SCANCODE_F] = RETROK_f;
    g_sdl_to_retro_key[SDL_SCANCODE_G] = RETROK_g;
    g_sdl_to_retro_key[SDL_SCANCODE_H] = RETROK_h;
    g_sdl_to_retro_key[SDL_SCANCODE_I] = RETROK_i;
    g_sdl_to_retro_key[SDL_SCANCODE_J] = RETROK_j;
    g_sdl_to_retro_key[SDL_SCANCODE_K] = RETROK_k;
    g_sdl_to_retro_key[SDL_SCANCODE_L] = RETROK_l;
    g_sdl_to_retro_key[SDL_SCANCODE_M] = RETROK_m;
    g_sdl_to_retro_key[SDL_SCANCODE_N] = RETROK_n;
    g_sdl_to_retro_key[SDL_SCANCODE_O] = RETROK_o;
    g_sdl_to_retro_key[SDL_SCANCODE_P] = RETROK_p;
    g_sdl_to_retro_key[SDL_SCANCODE_Q] = RETROK_q;
    g_sdl_to_retro_key[SDL_SCANCODE_R] = RETROK_r;
    g_sdl_to_retro_key[SDL_SCANCODE_S] = RETROK_s;
    g_sdl_to_retro_key[SDL_SCANCODE_T] = RETROK_t;
    g_sdl_to_retro_key[SDL_SCANCODE_U] = RETROK_u;
    g_sdl_to_retro_key[SDL_SCANCODE_V] = RETROK_v;
    g_sdl_to_retro_key[SDL_SCANCODE_W] = RETROK_w;
    g_sdl_to_retro_key[SDL_SCANCODE_X] = RETROK_x;
    g_sdl_to_retro_key[SDL_SCANCODE_Y] = RETROK_y;
    g_sdl_to_retro_key[SDL_SCANCODE_Z] = RETROK_z;

    g_sdl_to_retro_key[SDL_SCANCODE_0] = RETROK_0;
    g_sdl_to_retro_key[SDL_SCANCODE_1] = RETROK_1;
    g_sdl_to_retro_key[SDL_SCANCODE_2] = RETROK_2;
    g_sdl_to_retro_key[SDL_SCANCODE_3] = RETROK_3;
    g_sdl_to_retro_key[SDL_SCANCODE_4] = RETROK_4;
    g_sdl_to_retro_key[SDL_SCANCODE_5] = RETROK_5;
    g_sdl_to_retro_key[SDL_SCANCODE_6] = RETROK_6;
    g_sdl_to_retro_key[SDL_SCANCODE_7] = RETROK_7;
    g_sdl_to_retro_key[SDL_SCANCODE_8] = RETROK_8;
    g_sdl_to_retro_key[SDL_SCANCODE_9] = RETROK_9;

    g_sdl_to_retro_key[SDL_SCANCODE_F1]  = RETROK_F1;
    g_sdl_to_retro_key[SDL_SCANCODE_F2]  = RETROK_F2;
    g_sdl_to_retro_key[SDL_SCANCODE_F3]  = RETROK_F3;
    g_sdl_to_retro_key[SDL_SCANCODE_F4]  = RETROK_F4;
    g_sdl_to_retro_key[SDL_SCANCODE_F5]  = RETROK_F5;
    g_sdl_to_retro_key[SDL_SCANCODE_F6]  = RETROK_F6;
    g_sdl_to_retro_key[SDL_SCANCODE_F7]  = RETROK_F7;
    g_sdl_to_retro_key[SDL_SCANCODE_F8]  = RETROK_F8;
    g_sdl_to_retro_key[SDL_SCANCODE_F9]  = RETROK_F9;
    g_sdl_to_retro_key[SDL_SCANCODE_F10] = RETROK_F10;
    g_sdl_to_retro_key[SDL_SCANCODE_F11] = RETROK_F11;
    g_sdl_to_retro_key[SDL_SCANCODE_F12] = RETROK_F12;

    g_sdl_to_retro_key[SDL_SCANCODE_UP]    = RETROK_UP;
    g_sdl_to_retro_key[SDL_SCANCODE_DOWN]  = RETROK_DOWN;
    g_sdl_to_retro_key[SDL_SCANCODE_LEFT]  = RETROK_LEFT;
    g_sdl_to_retro_key[SDL_SCANCODE_RIGHT] = RETROK_RIGHT;

    g_sdl_to_retro_key[SDL_SCANCODE_RETURN]     = RETROK_RETURN;
    g_sdl_to_retro_key[SDL_SCANCODE_ESCAPE]     = RETROK_ESCAPE;
    g_sdl_to_retro_key[SDL_SCANCODE_SPACE]      = RETROK_SPACE;
    g_sdl_to_retro_key[SDL_SCANCODE_BACKSPACE]  = RETROK_BACKSPACE;
    g_sdl_to_retro_key[SDL_SCANCODE_TAB]        = RETROK_TAB;
    g_sdl_to_retro_key[SDL_SCANCODE_CAPSLOCK]   = RETROK_CAPSLOCK;

    g_sdl_to_retro_key[SDL_SCANCODE_LSHIFT] = RETROK_LSHIFT;
    g_sdl_to_retro_key[SDL_SCANCODE_RSHIFT] = RETROK_RSHIFT;
    g_sdl_to_retro_key[SDL_SCANCODE_LCTRL]  = RETROK_LCTRL;
    g_sdl_to_retro_key[SDL_SCANCODE_RCTRL]  = RETROK_RCTRL;
    g_sdl_to_retro_key[SDL_SCANCODE_LALT]   = RETROK_LALT;
    g_sdl_to_retro_key[SDL_SCANCODE_RALT]   = RETROK_RALT;
    g_sdl_to_retro_key[SDL_SCANCODE_LGUI]   = RETROK_LMETA;
    g_sdl_to_retro_key[SDL_SCANCODE_RGUI]   = RETROK_RMETA;

    g_sdl_to_retro_key[SDL_SCANCODE_PAGEUP]   = RETROK_PAGEUP;
    g_sdl_to_retro_key[SDL_SCANCODE_PAGEDOWN] = RETROK_PAGEDOWN;
    g_sdl_to_retro_key[SDL_SCANCODE_HOME]     = RETROK_HOME;
    g_sdl_to_retro_key[SDL_SCANCODE_END]      = RETROK_END;
    g_sdl_to_retro_key[SDL_SCANCODE_INSERT]   = RETROK_INSERT;
    g_sdl_to_retro_key[SDL_SCANCODE_DELETE]   = RETROK_DELETE;

    g_sdl_to_retro_key[SDL_SCANCODE_KP_0] = RETROK_KP0;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_1] = RETROK_KP1;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_2] = RETROK_KP2;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_3] = RETROK_KP3;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_4] = RETROK_KP4;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_5] = RETROK_KP5;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_6] = RETROK_KP6;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_7] = RETROK_KP7;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_8] = RETROK_KP8;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_9] = RETROK_KP9;

    g_sdl_to_retro_key[SDL_SCANCODE_KP_ENTER]    = RETROK_KP_ENTER;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_PLUS]     = RETROK_KP_PLUS;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_MINUS]    = RETROK_KP_MINUS;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_MULTIPLY] = RETROK_KP_MULTIPLY;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_DIVIDE]   = RETROK_KP_DIVIDE;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_PERIOD]   = RETROK_KP_PERIOD;
    g_sdl_to_retro_key[SDL_SCANCODE_KP_EQUALS]   = RETROK_KP_EQUALS;

    g_sdl_to_retro_key[SDL_SCANCODE_MINUS]        = RETROK_MINUS;
    g_sdl_to_retro_key[SDL_SCANCODE_EQUALS]       = RETROK_EQUALS;
    g_sdl_to_retro_key[SDL_SCANCODE_LEFTBRACKET]  = RETROK_LEFTBRACKET;
    g_sdl_to_retro_key[SDL_SCANCODE_RIGHTBRACKET] = RETROK_RIGHTBRACKET;
    g_sdl_to_retro_key[SDL_SCANCODE_SEMICOLON]    = RETROK_SEMICOLON;
    g_sdl_to_retro_key[SDL_SCANCODE_APOSTROPHE]   = RETROK_QUOTE;
    g_sdl_to_retro_key[SDL_SCANCODE_GRAVE]        = RETROK_BACKQUOTE;
    g_sdl_to_retro_key[SDL_SCANCODE_BACKSLASH]    = RETROK_BACKSLASH;
    g_sdl_to_retro_key[SDL_SCANCODE_COMMA]        = RETROK_COMMA;
    g_sdl_to_retro_key[SDL_SCANCODE_PERIOD]       = RETROK_PERIOD;
    g_sdl_to_retro_key[SDL_SCANCODE_SLASH]        = RETROK_SLASH;

    g_sdl_to_retro_key[SDL_SCANCODE_NUMLOCKCLEAR] = RETROK_NUMLOCK;
    g_sdl_to_retro_key[SDL_SCANCODE_SCROLLLOCK]   = RETROK_SCROLLOCK;
    g_sdl_to_retro_key[SDL_SCANCODE_PRINTSCREEN]  = RETROK_PRINT;
    g_sdl_to_retro_key[SDL_SCANCODE_PAUSE]        = RETROK_PAUSE;
    g_sdl_to_retro_key[SDL_SCANCODE_APPLICATION]  = RETROK_MENU;

    g_retro_key_table_built = true;
}

/* ------------------------------------------------------------------ */
/* Keymap config file parser                                           */
/* ------------------------------------------------------------------ */

static char *trim_whitespace(char *str)
{
    char *end;

    while (isspace((unsigned char)*str))
        str++;

    if (*str == '\0')
        return str;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;

    end[1] = '\0';
    return str;
}

bool input_load_keymap(const char *path)
{
    FILE *fp;
    char line[256];
    unsigned loaded = 0;

    if (!path)
        return false;

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open keymap config: %s\n", path);
        return false;
    }

    if (!g_scancode_table_built)
        build_scancode_table();

    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim_whitespace(line);
        char *eq;
        char *left;
        char *right;
        SDL_Scancode scancode;
        unsigned retro_id = KEYMAP_UNMAPPED;
        size_t j;

        if (trimmed[0] == '\0' || trimmed[0] == '#')
            continue;

        eq = strchr(trimmed, '=');
        if (!eq) {
            fprintf(stderr, "Warning: invalid keymap line (missing '='): %s\n", trimmed);
            continue;
        }

        *eq = '\0';
        left = trim_whitespace(trimmed);
        right = trim_whitespace(eq + 1);

        if (left[0] == '\0' || right[0] == '\0') {
            fprintf(stderr, "Warning: invalid keymap line (empty side)\n");
            continue;
        }

        scancode = SDL_GetScancodeFromName(left);
        if (scancode == SDL_SCANCODE_UNKNOWN) {
            fprintf(stderr, "Warning: unknown SDL scancode name: %s\n", left);
            continue;
        }

        for (j = 0; j < sizeof(g_button_names) / sizeof(g_button_names[0]); ++j) {
            if (strcmp(right, g_button_names[j].name) == 0) {
                retro_id = g_button_names[j].id;
                break;
            }
        }

        if (retro_id == KEYMAP_UNMAPPED) {
            fprintf(stderr, "Warning: unknown RetroPad button name: %s\n", right);
            continue;
        }

        if ((unsigned)scancode < SDL_SCANCODE_COUNT)
            g_scancode_to_retro[scancode] = (uint8_t)retro_id;

        loaded++;
    }

    fclose(fp);
    fprintf(stderr, "Loaded %u keymap entries from %s\n", loaded, path);
    return true;
}

void input_free_keymap(void)
{
    build_scancode_table();
}

/* ------------------------------------------------------------------ */
/* Event dispatch                                                      */
/* ------------------------------------------------------------------ */

void input_process_event(const SDL_Event *event)
{
    bool pressed;
    SDL_Scancode scancode;

    if (!event)
        return;

    if (event->type != SDL_EVENT_KEY_DOWN && event->type != SDL_EVENT_KEY_UP)
        return;

    if (!g_scancode_table_built)
        build_scancode_table();

    if (!g_retro_key_table_built)
        build_retro_key_table();

    scancode = event->key.scancode;
    if ((unsigned)scancode >= SDL_SCANCODE_COUNT)
        return;

    uint8_t retro_id = g_scancode_to_retro[scancode];
    if (retro_id != KEYMAP_UNMAPPED) {
        pressed = (event->type == SDL_EVENT_KEY_DOWN);
        g_frontend.joypad_state[retro_id] = pressed ? 1 : 0;
        return;
    }

    if (g_frontend.keyboard_callback.callback) {
        unsigned retro_key = g_sdl_to_retro_key[scancode];
        enum retro_mod mod = RETROKMOD_NONE;
        SDL_Keymod sdl_mod = event->key.mod;

        if (sdl_mod & SDL_KMOD_SHIFT) mod |= RETROKMOD_SHIFT;
        if (sdl_mod & SDL_KMOD_CTRL)  mod |= RETROKMOD_CTRL;
        if (sdl_mod & SDL_KMOD_ALT)   mod |= RETROKMOD_ALT;
        if (sdl_mod & SDL_KMOD_GUI)   mod |= RETROKMOD_META;

        pressed = (event->type == SDL_EVENT_KEY_DOWN);
        g_frontend.keyboard_callback.callback(
            pressed, retro_key, (uint32_t)event->key.key, mod);
    }
}

void input_poll(void)
{
    /* Input state is updated via SDL events in the main loop.
     * This function exists to satisfy the libretro input poll contract. */
}

int16_t input_state_joypad(unsigned port, unsigned id)
{
    if (port != 0)
        return 0;

    if (id > RETRO_DEVICE_ID_JOYPAD_R3)
        return 0;

    return g_frontend.joypad_state[id] ? 1 : 0;
}

uint16_t input_state_joypad_mask(unsigned port)
{
    uint16_t mask = 0;
    unsigned i;

    (void)port;

    for (i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; ++i) {
        if (g_frontend.joypad_state[i])
            mask |= (1 << i);
    }

    return mask;
}
