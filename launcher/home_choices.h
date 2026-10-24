/* Display's and Controls' choices for the home screen's host (home_games.c): read from settings.txt into the pages,
 * with the size of the screen Display shows them for, and Controls' written back; and the whole scale a game's window
 * is lowered to on the display. */
#ifndef ORACLES_LAUNCHER_HOME_CHOICES_H
#define ORACLES_LAUNCHER_HOME_CHOICES_H

#include "compositor.h"
#include "settings.h"
#include "ui_home_nav.h"

struct SDL_Window;

void oracles_home_inspect_display(const oracles_settings *settings, OraclesHomeDisplay *display, struct SDL_Window *window);
void oracles_home_inspect_controls(const oracles_settings *settings, OraclesHomeControls *c);
/* menu_pad= as every screen and the session take it, once the settings are read: an unknown name the default, and
 * none, Guide alone, when the game's buttons, the d-pad or an item hotkey slot have it (oracles_controls_menu_free);
 * the standard error says which.  Controls then never gives the menu a button another cell has, nor that cell its. */
void oracles_home_resolve_menu_pad(oracles_settings *settings);
/* Display's choices written back into the settings (`in_game`: from the pause, whose core and workers are the running
 * game's and never stored); a margin --screen-margin gave (display->margin_fixed) and a text --text gave
 * (display->text_fixed) are never stored either. */
void oracles_home_store_display(const OraclesHomeDisplay *display, int in_game, oracles_settings *prefs);
void oracles_home_store_controls(const OraclesHomeControls *c, oracles_settings *settings);
unsigned oracles_home_fitting_scale(struct SDL_Window *window, unsigned wanted, OraclesEnhancedSize surface);

#endif
