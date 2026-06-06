/*
 * PureRetro — Input subsystem
 *
 * Keyboard mapping to libretro's RetroPad abstraction.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "input.h"
#include "frontend.h"

/* Map SDL scancodes to RetroPad button IDs.
 *
 * Built into a direct lookup table at first use so input_process_event
 * is O(1) per key event instead of scanning g_keymap linearly. The
 * sentinel value 0xFF marks "unmapped". */
static const struct
{
    SDL_Scancode scancode;
    unsigned retro_id;
} g_keymap[] = {
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
    for (size_t i = 0; i < sizeof(g_keymap) / sizeof(g_keymap[0]); ++i)
        g_scancode_to_retro[g_keymap[i].scancode] = (uint8_t)g_keymap[i].retro_id;
    g_scancode_table_built = true;
}

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

    scancode = event->key.scancode;
    if ((unsigned)scancode >= SDL_SCANCODE_COUNT)
        return;

    uint8_t retro_id = g_scancode_to_retro[scancode];
    if (retro_id == KEYMAP_UNMAPPED)
        return;

    pressed = (event->type == SDL_EVENT_KEY_DOWN);
    g_frontend.joypad_state[retro_id] = pressed ? 1 : 0;
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
