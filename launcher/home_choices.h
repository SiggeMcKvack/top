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
void oracles_home_store_controls(const OraclesHomeControls *c, oracles_settings *settings);
unsigned oracles_home_fitting_scale(struct SDL_Window *window, unsigned wanted, OraclesEnhancedSize surface);

#endif
