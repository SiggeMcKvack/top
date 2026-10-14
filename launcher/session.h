/* A game session: the user's ROM in the core, played until the player quits,
 * from `the-oracles-project --rom PATH` or from the home screen's Start game.
 *
 * The SRAM is read from and written to the save file (default: the ROM path
 * with a .sav extension, the convention of most emulators, so existing saves
 * work).  The end-of-session report goes to stderr. */
#ifndef ORACLES_LAUNCHER_SESSION_H
#define ORACLES_LAUNCHER_SESSION_H

#include "backends.h"
#include "settings.h"

#include <stddef.h>
#include <stdint.h>

struct OraclesPause;

typedef struct OraclesSessionOptions {
    const char *rom_path;
    const char *patch_path;          /* a BPS patch applied to rom_path in memory (a fan game), or NULL */
    const char *save_path;           /* NULL: next to the ROM, or to the patch when there is one */
    const char *screenshot;          /* PATH.ppm of the last frame, or NULL */
    const char *record_path, *play_path;
    const char *hotkeys_option;      /* off, use, equip, or NULL */
    const char *mods_dirs[8];        /* the mods' directories: --mods, repeated, at most ORACLES_MOD_SET_MAX */
    unsigned mods_count;
    const char *start_house;         /* --start-at-house: every file of the save starts in front of this mod's house, or NULL */
    /* --language, the European ROM it comes from (a file or a directory) and its pack (read, or written once extracted):
     * for the session alone, never stored; NULL for none. */
    const char *language, *eu_rom_path, *language_pack;
    /* The command line's --colour-correction (on, off) and --vsync (auto, on, off), or NULL for the settings': for
     * the session alone, never stored, as camera_profile. */
    const char *colour_option, *vsync_option;
    uint32_t frames;                 /* 0: no limit */
    int no_window, mute, diagnostics, native_renderer;
    int enhanced, zoom_out, continuous_transitions;
    int view;                        /* the Enhanced view's level plus one (an OraclesEnhancedLevel; --view, Display's View); 0: far with zoom_out, else near */
    int screen_4_3;                  /* the view takes the 4:3 shape (oracles_session_view_4_3) */
    int aspect;                      /* --aspect: an OraclesAspect plus one, for the run, never stored; 0: the settings' */
    int ghosts;                      /* --ghosts: auto, 1 or 2 as 1, 2 or 3, for the run, never stored; 0: the settings' ghosts= */
    int core;                        /* --core: an OraclesCoreKind plus one, for the run, never stored; 0: the settings' core= */
    int screen_margin;               /* --screen-margin: an OraclesScreenMargin plus one, for the run, never stored; 0: the settings' screen_margin= */
    int continuous_swim;             /* --continuous-swim: the transitions with Link swimming at the surface too (implies them) */
    int swim_hold;                   /* --swim-hold, or settings.txt's swim_hold=on for a game the home screen starts: a direction held
                                      * starts the swim's strokes (A's with the flippers, A held too; the direction's with Ages' mermaid suit) */
    int neighbour_objects;           /* Enhanced shows a neighbour's objects */
    int camera_profile;              /* 0: the settings' camera; 1 or 2: this one for the session, not stored */
    int pretend_slow;                /* --pretend-slow: the game counts as one that ran slowly, to try the home screen's suggestion */
    /* The window: its scale and fullscreen state, and the launcher's own window when the home screen lends it. */
    oracles_sdl_options sdl;
    /* A game the home screen started: Escape opens this pause menu (pause.h) rather than ending the session. */
    struct OraclesPause *pause;
} OraclesSessionOptions;

typedef struct OraclesSessionResult {
    int window_closed;               /* the window was closed, rather than the game left with Escape */
    char error[256];                 /* why the session could not run or stopped, empty otherwise */
    int route_written;               /* --record's route was written */
    int hotkeys_dropped;             /* a game the home screen started played without the item hotkeys: they could not attach */
    /* How the game ran, for the home screen to suggest something lighter (oracles_settings_slow_hint): in Enhanced or
     * not, the frames it played outside its pauses, and those late: the host's late frames, or under vsync, where the
     * host counts none, the frames whose work took over one frame past the first second, whichever are more. */
    int enhanced;
    uint32_t frames_played, frames_late;
    char language_note[256];         /* what the language's session says beside playing, such as a pack it could not write */
} OraclesSessionResult;

/* Defaults: Faithful, the neighbours' objects shown in Enhanced, a window at scale 4. */
void oracles_session_defaults(OraclesSessionOptions *options);
/* Plays the session with the player's settings, the options overriding them for the session alone; F2 changes and
 * stores the colour correction, the pause's Display and Controls the settings. Returns 0, or 1 on failure. */
int oracles_session_run(const OraclesSessionOptions *options, oracles_settings *settings, OraclesSessionResult *result);
/* Whether the view takes the 4:3 shape: the command line's --aspect, else the settings' aspect=, auto deciding by the
 * screen of `window` (the primary display without one). */
int oracles_session_view_4_3(const OraclesSessionOptions *options, const oracles_settings *settings, struct SDL_Window *window);

/* The screen margin the games play with, an OraclesScreenMargin: the command line's (`option`, --screen-margin's as
 * OraclesSessionOptions holds it, 0 for none), else the settings' screen_margin=. */
int oracles_session_screen_margin(int option, const oracles_settings *settings);

/* What a running game shows of the settings the pause applies at once: the colour correction as the settings had it
 * when the game last took it, Scaling's Fill, the HUD full width (-1 without the Enhanced view, which alone draws it),
 * the screen margin (an OraclesScreenMargin). */
typedef struct OraclesSessionApplied {
    int colour, fill, hud, margin;
} OraclesSessionApplied;
#define ORACLES_SESSION_CHANGED_COLOUR 1u
#define ORACLES_SESSION_CHANGED_FILL 2u
#define ORACLES_SESSION_CHANGED_HUD 4u
#define ORACLES_SESSION_CHANGED_MARGIN 8u
/* The settings after the pause's Display changed them, set against what the game shows: `applied` takes them, and the
 * ORACLES_SESSION_CHANGED_* bits say which changed.  The margin is --screen-margin's for the run when `margin_option`
 * gives one (oracles_session_screen_margin), whatever the settings say. */
unsigned oracles_session_settings_changed(const oracles_settings *settings, int margin_option, OraclesSessionApplied *applied);

/* The save file of a ROM: its path with the extension .sav. */
void oracles_session_default_save_path(const char *rom_path, char *out, size_t capacity);
/* The save a session reads and writes: --save's, else the ROM's (a fan game's patch's), NAME.mods.sav with mods, a
 * modded game's own. */
void oracles_session_save_path(const OraclesSessionOptions *options, char *out, size_t capacity);
/* The mods the home screen's Mods page made active for game `g` (settings.txt's mods_<game>=), in the order of their
 * names, as --mods gives them: their directories in the mods folder, written into `dirs`, those no longer there
 * left out.  Returns their number. */
unsigned oracles_session_home_mods(OraclesSessionOptions *options, const oracles_settings *settings, OraclesSettingsGame g,
                                   char dirs[ORACLES_SETTINGS_MODS_MAX][ORACLES_SETTINGS_PATH_LENGTH]);

#endif
