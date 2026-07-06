/*
 * PureRetro — Input subsystem
 *
 * Keyboard-to-RetroPad mapping using SDL3.
 */

#ifndef INPUT_H
#define INPUT_H

#include "libretro.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Process an SDL input event and update the internal state. */
void input_process_event(const SDL_Event *event);

/* Poll all pending input events. Called once per frame before retro_run. */
void input_poll(void);

/* Query the state of a specific RetroPad button.
 * Returns 1 if pressed, 0 otherwise. */
int16_t input_state_joypad(unsigned port, unsigned id);

/* Query the bitmask of all pressed RetroPad buttons.
 * Returns a bitmask of RETRO_DEVICE_ID_JOYPAD_* values. */
uint16_t input_state_joypad_mask(unsigned port);

/* Load a key remapping configuration file.
 * Returns true on success, false on failure. */
bool input_load_keymap(const char *path);

/* Reset the keymap to the built-in defaults. */
void input_free_keymap(void);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_H */
