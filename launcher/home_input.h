/* The launcher's input, shared by the home screen and the pause menu: SDL's
 * keys and gamepad buttons as the screens' actions, and, while a cell of
 * Controls waits, as the key or the button it takes. */
#ifndef ORACLES_LAUNCHER_HOME_INPUT_H
#define ORACLES_LAUNCHER_HOME_INPUT_H

#include "ui_home_nav.h"

#include <SDL3/SDL.h>

/* Arrows, Enter or Space, Escape or Backspace; the d-pad, A, B (a button as oracles_sdl_pad_button names it), and
 * Back from the controller's Escape: Guide, and the menu's button or trigger (menu_pad=) when the d-pad, A or B are
 * not it.  A trigger goes down once past half its travel on any controller, *trigger_held following it over all of them
 * (oracles_sdl_trigger_moved). */
int oracles_home_key_action(SDL_Keycode key, OraclesHomeAction *action);
int oracles_home_button_action(const OraclesHomeNav *nav, const SDL_Event *event, int *trigger_held, OraclesHomeAction *action);
/* Where *trigger_held starts on a screen that opens, a game's pause or the home screen back from a game: the menu's
 * trigger held now (a press that opened the pause is not one that closes it). */
int oracles_home_trigger_held(const OraclesHomeNav *nav);

/* An event while a cell of Controls waits: 1 when it was the capture's (a key down, a button down), with what it
 * did in *command (STORE when the cell took it); 0 when the event is not for the capture. */
int oracles_home_capture(OraclesHomeNav *nav, const SDL_Event *event, OraclesHomeCommand *command);


#endif
