#include "home_choices.h"

#include "backends.h"
#include "guest_tables.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

/* Room left on the display for the window's frame and title bar, which its usable area does not count. */
#define WINDOW_FRAME 64

/* Display's choices from the settings, and the size of the display the sizes are shown for: the window's, as Play
 * reduces the window on it. */
void oracles_home_inspect_display(const oracles_settings *settings, OraclesHomeDisplay *display, struct SDL_Window *window)
{
    display->profile = settings->profile;
    display->transitions = settings->transitions;
    display->hud = settings->hud_full;
    display->view = settings->view;
    display->aspect = settings->aspect;
    display->core = oracles_settings_core(settings);
    display->window = settings->window_scale ? settings->window_scale - 2 : 3;
    display->colour = settings->colour_correction;
    display->vsync = !strcmp(settings->vsync, "on") ? 1 : !strcmp(settings->vsync, "off") ? 2 : 0;
    display->workers = settings->ghosts;
    display->scaling = settings->scaling_fill;
    display->cores = SDL_GetNumLogicalCPUCores();   /* as the session counts them for Auto */
    display->fullscreen_only = oracles_sdl_fullscreen_only();
    /* Without a window (a capture drawn offscreen), a 1080p screen. */
    SDL_Rect usable;
    const SDL_DisplayID index = window ? SDL_GetDisplayForWindow(window) : 0;
    const SDL_DisplayMode *mode = index ? SDL_GetDesktopDisplayMode(index) : NULL;
    if (mode && SDL_GetDisplayUsableBounds(index, &usable)) {
        /* In pixels, as the game's window is sized: SDL gives points on a Retina display; the frame is counted in
         * points of the launcher (oracles_sdl_point_scale). */
        const float ratio = oracles_sdl_pixel_ratio(window), frame = WINDOW_FRAME * oracles_sdl_point_scale(window);
        display->screen_w = (int)((float)mode->w * ratio + 0.5f);
        display->screen_h = (int)((float)mode->h * ratio + 0.5f);
        display->room_w = (int)((float)usable.w * ratio + 0.5f);
        display->room_h = (int)(((float)usable.h - frame) * ratio + 0.5f);   /* as fitting_scale reduces the window at Play */
    } else {
        display->screen_w = 1920;
        display->screen_h = 1080;
        display->room_w = 1920;
        display->room_h = 1080 - WINDOW_FRAME;
    }
}

/* Controls' names from the settings, each game's slots by their items' names, and Swimming. */
void oracles_home_inspect_controls(const oracles_settings *settings, OraclesHomeControls *c)
{
    for (int i = 0; i < ORACLES_HOME_BUTTONS; i++) snprintf(c->keys[i], sizeof c->keys[i], "%s", settings->key_names[i]);
    for (int i = 0; i < ORACLES_HOME_PAD_BUTTONS; i++) snprintf(c->pads[i], sizeof c->pads[i], "%s", settings->pad_names[i]);
    for (int i = 0; i < ORACLES_HOME_SLOTS; i++) {
        snprintf(c->hotkey_keys[i], sizeof c->hotkey_keys[i], "%s", settings->hotkey_key_names[i]);
        snprintf(c->hotkey_pads[i], sizeof c->hotkey_pads[i], "%s", settings->hotkey_pad_names[i]);
    }
    snprintf(c->hotkey_keys[ORACLES_HOME_SLOTS], sizeof c->hotkey_keys[0], "%s", settings->hotkey_bind_b);
    snprintf(c->hotkey_keys[ORACLES_HOME_SLOTS + 1], sizeof c->hotkey_keys[0], "%s", settings->hotkey_bind_a);
    c->swim_hold = settings->swim_hold;
    for (int g = 0; g < 2; g++) {
        const OraclesGuestTables *tables = g == 0 ? &oracles_guest_tables_ages : &oracles_guest_tables_seasons;
        for (int i = 0; i < ORACLES_HOME_SLOTS; i++) {
            const OraclesHotkeySlot *slot = &settings->hotkeys[g][i];
            const char *name = slot->set && slot->item < ORACLES_GUEST_ITEM_LABELS ? tables->item_names[slot->item] : NULL;
            snprintf(c->items[g][i], sizeof c->items[g][i], "%s", name ? name : "");
        }
    }
}

void oracles_home_store_controls(const OraclesHomeControls *c, oracles_settings *settings)
{
    for (int i = 0; i < ORACLES_HOME_BUTTONS; i++) snprintf(settings->key_names[i], sizeof settings->key_names[i], "%s", c->keys[i]);
    for (int i = 0; i < ORACLES_HOME_PAD_BUTTONS; i++) snprintf(settings->pad_names[i], sizeof settings->pad_names[i], "%s", c->pads[i]);
    for (int i = 0; i < ORACLES_HOME_SLOTS; i++) {
        snprintf(settings->hotkey_key_names[i], sizeof settings->hotkey_key_names[i], "%s", c->hotkey_keys[i]);
        snprintf(settings->hotkey_pad_names[i], sizeof settings->hotkey_pad_names[i], "%s", c->hotkey_pads[i]);
    }
    snprintf(settings->hotkey_bind_b, sizeof settings->hotkey_bind_b, "%s", c->hotkey_keys[ORACLES_HOME_SLOTS]);
    snprintf(settings->hotkey_bind_a, sizeof settings->hotkey_bind_a, "%s", c->hotkey_keys[ORACLES_HOME_SLOTS + 1]);
    settings->swim_hold = c->swim_hold;   /* a game started next takes it; a running one keeps its own */
}

/* The largest whole scale, up to the one asked for, at which the surface's window fits the display. */
unsigned oracles_home_fitting_scale(struct SDL_Window *window, unsigned wanted, OraclesEnhancedSize surface)
{
    SDL_Rect usable;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (!display || !SDL_GetDisplayUsableBounds(display, &usable)) return wanted;
    unsigned scale = wanted;
    /* The usable area in pixels, as the window is sized (points on a Retina display), less the window's frame. */
    const float ratio = oracles_sdl_pixel_ratio(window), frame = WINDOW_FRAME * oracles_sdl_point_scale(window);
    const int room_w = (int)((float)usable.w * ratio + 0.5f), room_h = (int)(((float)usable.h - frame) * ratio + 0.5f);
    while (scale > 1u && ((int)(scale * surface.width) > room_w || (int)(scale * surface.height) > room_h)) scale--;
    return scale;
}
