/*
 * PureRetro — Input subsystem
 *
 * Keyboard mapping to libretro's RetroPad abstraction.
 */

#include <string.h>
#include <SDL3/SDL.h>
#include "input.h"
#include "frontend.h"

/* Map SDL scancodes to RetroPad button IDs. */
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

void input_process_event(const SDL_Event *event)
{
    unsigned i;
    bool pressed;
    SDL_Scancode scancode;

    if (!event)
        return;

    if (event->type != SDL_EVENT_KEY_DOWN && event->type != SDL_EVENT_KEY_UP)
        return;

    scancode = event->key.scancode;
    pressed = (event->type == SDL_EVENT_KEY_DOWN);

    for (i = 0; i < sizeof(g_keymap) / sizeof(g_keymap[0]); ++i) {
        if (g_keymap[i].scancode == scancode) {
            if (pressed)
                g_frontend.joypad_state[g_keymap[i].retro_id] = 1;
            else
                g_frontend.joypad_state[g_keymap[i].retro_id] = 0;
            break;
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

    if (id > RETRO_DEVICE_ID_JOYPAD_R3 && id != RETRO_DEVICE_ID_JOYPAD_MASK)
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
