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
#include "log.h"

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

#define KEYMAP_UNMAPPED 0xFFu

static unsigned parse_button_name(const char *name)
{
    for (size_t i = 0;
         i < sizeof(g_button_names) / sizeof(g_button_names[0]); ++i) {
        if (strcmp(g_button_names[i].name, name) == 0)
            return g_button_names[i].id;
    }
    return KEYMAP_UNMAPPED;
}

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

    /* letters */
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

    /* digits */
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

    /* function keys */
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

    /* navigation and common keys */
    g_sdl_to_retro_key[SDL_SCANCODE_UP]        = RETROK_UP;
    g_sdl_to_retro_key[SDL_SCANCODE_DOWN]      = RETROK_DOWN;
    g_sdl_to_retro_key[SDL_SCANCODE_LEFT]      = RETROK_LEFT;
    g_sdl_to_retro_key[SDL_SCANCODE_RIGHT]     = RETROK_RIGHT;
    g_sdl_to_retro_key[SDL_SCANCODE_RETURN]    = RETROK_RETURN;
    g_sdl_to_retro_key[SDL_SCANCODE_ESCAPE]    = RETROK_ESCAPE;
    g_sdl_to_retro_key[SDL_SCANCODE_SPACE]     = RETROK_SPACE;
    g_sdl_to_retro_key[SDL_SCANCODE_BACKSPACE] = RETROK_BACKSPACE;
    g_sdl_to_retro_key[SDL_SCANCODE_TAB]       = RETROK_TAB;
    g_sdl_to_retro_key[SDL_SCANCODE_INSERT]    = RETROK_INSERT;
    g_sdl_to_retro_key[SDL_SCANCODE_DELETE]    = RETROK_DELETE;
    g_sdl_to_retro_key[SDL_SCANCODE_HOME]      = RETROK_HOME;
    g_sdl_to_retro_key[SDL_SCANCODE_END]       = RETROK_END;
    g_sdl_to_retro_key[SDL_SCANCODE_PAGEUP]    = RETROK_PAGEUP;
    g_sdl_to_retro_key[SDL_SCANCODE_PAGEDOWN]  = RETROK_PAGEDOWN;

    /* modifiers */
    g_sdl_to_retro_key[SDL_SCANCODE_LSHIFT] = RETROK_LSHIFT;
    g_sdl_to_retro_key[SDL_SCANCODE_RSHIFT] = RETROK_RSHIFT;
    g_sdl_to_retro_key[SDL_SCANCODE_LCTRL]  = RETROK_LCTRL;
    g_sdl_to_retro_key[SDL_SCANCODE_RCTRL]  = RETROK_RCTRL;
    g_sdl_to_retro_key[SDL_SCANCODE_LALT]   = RETROK_LALT;
    g_sdl_to_retro_key[SDL_SCANCODE_RALT]   = RETROK_RALT;
    g_sdl_to_retro_key[SDL_SCANCODE_LGUI]   = RETROK_LMETA;
    g_sdl_to_retro_key[SDL_SCANCODE_RGUI]   = RETROK_RMETA;

    /* numpad */
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

    /* punctuation */
    g_sdl_to_retro_key[SDL_SCANCODE_COMMA]      = RETROK_COMMA;
    g_sdl_to_retro_key[SDL_SCANCODE_PERIOD]     = RETROK_PERIOD;
    g_sdl_to_retro_key[SDL_SCANCODE_SLASH]      = RETROK_SLASH;
    g_sdl_to_retro_key[SDL_SCANCODE_SEMICOLON]  = RETROK_SEMICOLON;
    g_sdl_to_retro_key[SDL_SCANCODE_APOSTROPHE] = RETROK_QUOTE;
    g_sdl_to_retro_key[SDL_SCANCODE_LEFTBRACKET]  = RETROK_LEFTBRACKET;
    g_sdl_to_retro_key[SDL_SCANCODE_RIGHTBRACKET] = RETROK_RIGHTBRACKET;
    g_sdl_to_retro_key[SDL_SCANCODE_BACKSLASH]    = RETROK_BACKSLASH;
    g_sdl_to_retro_key[SDL_SCANCODE_MINUS]        = RETROK_MINUS;
    g_sdl_to_retro_key[SDL_SCANCODE_EQUALS]       = RETROK_EQUALS;
    g_sdl_to_retro_key[SDL_SCANCODE_GRAVE]        = RETROK_BACKQUOTE;

    g_retro_key_table_built = true;
}

/* ------------------------------------------------------------------ */
/* Keymap config file parser                                           */
/* ------------------------------------------------------------------ */

bool input_load_keymap(const char *path)
{
    if (!path)
        return false;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        LOG_ERROR("Failed to open keymap config: %s", path);
        return false;
    }

    if (!g_scancode_table_built)
        build_scancode_table();

    char line[256];
    size_t loaded = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        /* skip comments and blank lines */
        char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '\0' || *p == '#')
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = '\0';

        char *sc_name = p;
        char *btn_name = eq + 1;

        /* trim whitespace */
        size_t sl = strlen(sc_name);
        while (sl > 0 && (sc_name[sl - 1] == ' ' || sc_name[sl - 1] == '\t'))
            sc_name[--sl] = '\0';
        while (*btn_name == ' ' || *btn_name == '\t')
            ++btn_name;

        SDL_Scancode sc = SDL_GetScancodeFromName(sc_name);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            LOG_WARN("unknown scancode '%s' in keymap", sc_name);
            continue;
        }
        unsigned btn = parse_button_name(btn_name);
        if (btn == KEYMAP_UNMAPPED) {
            LOG_WARN("unknown button '%s' in keymap", btn_name);
            continue;
        }

        g_scancode_to_retro[sc] = (uint8_t)btn;
        loaded++;
    }

    fclose(fp);
    LOG_INFO("Loaded %zu keymap entries from %s", loaded, path);
    return true;
}

void input_free_keymap(void)
{
    build_scancode_table(); /* resets to built-in defaults */
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

    pressed = (event->type == SDL_EVENT_KEY_DOWN);

    /* 1. Joypad mapping takes priority. */
    uint8_t retro_id = g_scancode_to_retro[scancode];
    if (retro_id != KEYMAP_UNMAPPED) {
        g_frontend.joypad_state[retro_id] = pressed ? 1 : 0;
        return;
    }

    /* 2. Not mapped to joypad: send to keyboard callback if registered. */
    if (g_frontend.keyboard_callback.callback) {
        enum retro_key rk = g_sdl_to_retro_key[scancode];
        if (rk != RETROK_UNKNOWN) {
            /* Use an unsigned bitmask, not `enum retro_mod`: combining
             * RETROKMOD_* values via |= produces values outside the
             * enum's named members and trips Clang's -Wassign-enum.
             * The callback takes a uint16_t bitfield anyway. */
            unsigned mod = RETROKMOD_NONE;
            SDL_Keymod sdl_mod = event->key.mod;
            if (sdl_mod & SDL_KMOD_SHIFT) mod |= RETROKMOD_SHIFT;
            if (sdl_mod & SDL_KMOD_CTRL)  mod |= RETROKMOD_CTRL;
            if (sdl_mod & SDL_KMOD_ALT)   mod |= RETROKMOD_ALT;
            if (sdl_mod & SDL_KMOD_GUI)   mod |= RETROKMOD_META;

            g_frontend.keyboard_callback.callback(
                pressed, rk, (uint32_t)event->key.key, (uint16_t)mod);
        }
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
