/* What a session plays with, from its options and the settings (session.h): its defaults, its screen margin, what the
 * pause changes of the settings, its save, and the mods of the Mods page. */
#include "session.h"

#include "compositor.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void oracles_session_defaults(OraclesSessionOptions *options)
{
    memset(options, 0, sizeof *options);
    options->neighbour_objects = 1;
    options->sdl.scale = 4;
}

int oracles_session_screen_margin(int option, const oracles_settings *settings)
{
    if (option > 0 && option <= ORACLES_SCREEN_MARGINS) return option - 1;
    return settings->screen_margin >= 0 && settings->screen_margin < ORACLES_SCREEN_MARGINS ? settings->screen_margin : ORACLES_SCREEN_MARGIN_OFF;
}

void oracles_session_far_screen(const OraclesSessionOptions *o, const oracles_settings *settings, struct SDL_Window *window, unsigned *width, unsigned *height)
{
    *width = *height = 0;
    const int aspect = o->aspect ? o->aspect - 1 : settings->aspect;
    if (settings->far_fixed || aspect != ORACLES_ASPECT_AUTO) return;
    oracles_sdl_screen_size(window, width, height);
}

OraclesEnhancedSize oracles_session_view_size(const OraclesSessionOptions *o, int level)
{
    /* A screen given is one far fits to, its shape the view's; else the options' shape. */
    const OraclesEnhancedAspect aspect = o->screen_w && o->screen_h ? ORACLES_ENHANCED_ASPECTS : o->screen_4_3 ? ORACLES_ENHANCED_4_3 : ORACLES_ENHANCED_16_9;
    return oracles_enhanced_screen_view_size((OraclesEnhancedLevel)level, aspect, 0, o->screen_w, o->screen_h);
}

unsigned oracles_session_settings_changed(const oracles_settings *settings, int margin_option, OraclesSessionApplied *applied)
{
    unsigned changed = 0;
    if (settings->colour_correction != applied->colour) { applied->colour = settings->colour_correction; changed |= ORACLES_SESSION_CHANGED_COLOUR; }
    if (settings->scaling_fill != applied->fill) { applied->fill = settings->scaling_fill; changed |= ORACLES_SESSION_CHANGED_FILL; }
    if (applied->hud >= 0 && settings->hud_full != applied->hud) { applied->hud = settings->hud_full; changed |= ORACLES_SESSION_CHANGED_HUD; }
    const int margin = oracles_session_screen_margin(margin_option, settings);
    if (margin != applied->margin) { applied->margin = margin; changed |= ORACLES_SESSION_CHANGED_MARGIN; }
    return changed;
}

void oracles_session_default_save_path(const char *rom_path, char *out, size_t capacity)
{
    snprintf(out, capacity, "%s", rom_path);
    char *dot = strrchr(out, '.');
    const char *slash = strrchr(out, '/');
    const char *backslash = strrchr(out, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    if (dot && (!slash || dot > slash)) *dot = 0;
    const size_t length = strlen(out);
    snprintf(out + length, capacity - length, ".sav");
}

void oracles_session_save_path(const OraclesSessionOptions *o, char *out, size_t capacity)
{
    if (o->save_path) { snprintf(out, capacity, "%s", o->save_path); return; }
    oracles_session_default_save_path(o->patch_path ? o->patch_path : o->rom_path, out, capacity);
    const size_t length = strlen(out);
    if (o->mods_count && length >= 4u) snprintf(out + length - 4u, capacity - (length - 4u), ".mods.sav");   /* for ".sav" */
}

unsigned oracles_session_home_mods(OraclesSessionOptions *o, const oracles_settings *settings, OraclesSettingsGame g,
                                   char dirs[ORACLES_SETTINGS_MODS_MAX][ORACLES_SETTINGS_PATH_LENGTH])
{
    char folder[ORACLES_SETTINGS_PATH_LENGTH], names[ORACLES_SETTINGS_MODS_MAX][ORACLES_SETTINGS_MOD_NAME];
    oracles_settings_mods_folder(settings, folder, sizeof folder);
    const size_t count = folder[0] ? oracles_settings_mod_names(settings->mods[g], names, ORACLES_SETTINGS_MODS_MAX) : 0;
    o->mods_count = 0;
    for (size_t i = 0; i < count; i++) {
        struct stat info;
        char *dir = dirs[o->mods_count];
        if (snprintf(dir, ORACLES_SETTINGS_PATH_LENGTH, "%s/%s", folder, names[i]) >= ORACLES_SETTINGS_PATH_LENGTH) continue;
        if (stat(dir, &info) != 0 || !S_ISDIR(info.st_mode)) { fprintf(stderr, "oracles: mod %s is no longer in %s: it plays without it\n", names[i], folder); continue; }
        o->mods_dirs[o->mods_count++] = dir;
    }
    return o->mods_count;
}
