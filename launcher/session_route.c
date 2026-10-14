/* A session's routes (docs/ROUTES.md): the one --play replays, its options taken on, and the one --record writes. */
#include "session_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int refuse(char *error, size_t capacity, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error, capacity, format, arguments);
    va_end(arguments);
    return 0;
}

int oracles_session_replay_route(session *s, const OraclesSessionOptions *o, char *error, size_t capacity)
{
    char reason[1024];
    if (oracles_route_read(o->play_path, &s->play, reason, sizeof reason) != 0) return refuse(error, capacity, "%s", reason);
    if (strcmp(s->play.header.rom_sha1, s->info.sha1) != 0 || strcmp(s->play.header.sram_sha1, s->save.sha1) != 0)
        return refuse(error, capacity, "the route was recorded with another ROM or another starting SRAM (route: rom %s, sram %s; now: rom %s, sram %s from %s)",
                      s->play.header.rom_sha1, s->play.header.sram_sha1, s->info.sha1, s->save.sha1, s->route_sram_path);
    if (!oracles_mod_session_matches_route(s->mod, s->play.header.mods, reason, sizeof reason)
        || !oracles_mod_session_matches_store(o->play_path, s->play.header.store_sha1, reason, sizeof reason)) return refuse(error, capacity, "%s", reason);
    s->playing = 1;
    fprintf(stderr, "oracles: replaying %zu input changes from %s; the session's save file is not touched\n", s->play.count, o->play_path);
    /* The route's gameplay options are part of the replay: a route played
     * with --continuous-transitions diverges without it, and the other way round. */
    /* A route recorded before the core's joypad bouncing was cut (the header's core line) ran with it and only replays with it. */
    if (oracles_route_joypad_bouncing(&s->play.header)) {
        if (oracles_core_set_joypad_bouncing(s->core, 1) == 0)
            fprintf(stderr, "oracles: the route was recorded with joypad bouncing: it is on for the replay, which may still differ from the session it was\n");
        else
            fprintf(stderr, "oracles: the route was recorded with SameBoy's joypad bouncing, which %s does not emulate: replayed without it\n",
                    oracles_core_version(s->core));
    }
    const int route_continuous = oracles_route_has_option(&s->play.header, ORACLES_ROUTE_OPTION_CONTINUOUS_TRANSITIONS);
    if (route_continuous && s->profile && !oracles_compat_continuous_transitions(s->profile))
        return refuse(error, capacity, "the route was recorded with --continuous-transitions, which %s does not support", oracles_compat_name(s->profile));
    if (route_continuous && !s->continuous_transitions) { s->continuous_transitions = 1; s->enhanced = 1; fprintf(stderr, "oracles: the route was recorded with --continuous-transitions: the option is on for the replay\n"); }
    else if (!route_continuous && s->continuous_transitions) fprintf(stderr, "oracles: the route was recorded without --continuous-transitions: the replay diverges after its first transition\n");
    const int route_swim = oracles_route_has_option(&s->play.header, ORACLES_ROUTE_OPTION_CONTINUOUS_SWIM);
    if (route_swim && s->profile && !oracles_compat_continuous_swim(s->profile))
        return refuse(error, capacity, "the route was recorded with --continuous-swim, which %s does not support", oracles_compat_name(s->profile));
    if (route_swim && !s->continuous_swim) { s->continuous_swim = s->continuous_transitions = 1; s->enhanced = 1; fprintf(stderr, "oracles: the route was recorded with --continuous-swim: the option is on for the replay\n"); }
    else if (!route_swim && s->continuous_swim) fprintf(stderr, "oracles: the route was recorded without --continuous-swim: the replay diverges after its first transition swum\n");
    char recorded[8];   /* a route played in a language replays in it; one without plays in any, as the hand-written ones do */
    if (oracles_route_language(&s->play.header, recorded, sizeof recorded)[0] && !oracles_route_language_matches(&s->play.header, o->language))
        return refuse(error, capacity, "the route was played in a language (%s): replay it with --language %s and its European ROM", recorded, recorded);
    if (!recorded[0] && o->language && strcmp(o->language, "en") != 0)
        fprintf(stderr, "oracles: the route names no language: a route played in English may diverge in another, its texts' lengths pacing the dialogues\n");
    return 1;
}

/* Whether two paths name the same file: the one to write would truncate the one being read. */
static int same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    if (strcmp(a, b) == 0) return 1;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_ino != 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

int oracles_session_record_route(session *s, const OraclesSessionOptions *o, char *error, size_t capacity)
{
    if (o->play_path) {
        /* An extension records the replayed inputs, not the replayed inventory exchanges
         * (their effects are applied, never written): such a route would lose them. */
        if (same_file(o->play_path, o->record_path)) return refuse(error, capacity, "--play and --record name the same route");
        if (s->play.action_count) return refuse(error, capacity, "%s has inventory exchanges, which an extension would not carry: it cannot be extended", o->play_path);
    }
    if (strcmp(s->save.sha1, "none") != 0) {
        const size_t size = oracles_core_sram_size(s->core);
        uint8_t *sram = malloc(size);
        const int ok = sram && oracles_core_save_sram(s->core, sram, size) == 0 && oracles_session_write_file(s->record_sram_path, sram, size);
        free(sram);
        if (!ok) return refuse(error, capacity, "cannot write the starting SRAM to %s", s->record_sram_path);
    } else {
        remove(s->record_sram_path);
    }
    OraclesRouteHeader header;
    snprintf(header.game, sizeof header.game, "%s", s->state_info.game);
    snprintf(header.rom_sha1, sizeof header.rom_sha1, "%s", s->info.sha1);
    snprintf(header.sram_sha1, sizeof header.sram_sha1, "%s", s->save.sha1);
    snprintf(header.options, sizeof header.options, "%s", s->continuous_swim ? ORACLES_ROUTE_OPTION_CONTINUOUS_TRANSITIONS "," ORACLES_ROUTE_OPTION_CONTINUOUS_SWIM
                                                          : s->continuous_transitions ? ORACLES_ROUTE_OPTION_CONTINUOUS_TRANSITIONS : "");
    if (s->language) {   /* the texts' lengths pace the dialogues: a replay plays in the same language */
        const size_t used = strlen(header.options);
        snprintf(header.options + used, sizeof header.options - used, "%s" ORACLES_ROUTE_OPTION_LANGUAGE "%s", used ? "," : "", o->language);
    }
    snprintf(header.mods, sizeof header.mods, "%s", oracles_mod_session_identity(s->mod));
    header.store_sha1[0] = 0;
    if (s->mod) {   /* the mods' storage the route starts from, as ROUTE.sram, named in the header */
        char path[PATH_MAX_LENGTH + 16];
        snprintf(path, sizeof path, "%s.store", o->record_path);
        if (!oracles_mod_session_storage_copy(s->mod, path)) return refuse(error, capacity, "cannot write %s", path);
        oracles_mod_session_file_sha1(path, header.store_sha1);
    }
    /* As every session runs, joypad bouncing off; an extension runs as the route it
     * extends, whose replay keeps the joypad bouncing of a route recorded before.  A session on mGBA, which has no
     * joypad bouncing, says so: its routes replay on mGBA. */
    if (oracles_core_kind(s->core) == ORACLES_CORE_MGBA)
        snprintf(header.core, sizeof header.core, "%s,%s", ORACLES_ROUTE_CORE_JOYPAD_BOUNCING_OFF, ORACLES_ROUTE_CORE_MGBA);
    else
        snprintf(header.core, sizeof header.core, "%s", o->play_path && oracles_route_joypad_bouncing(&s->play.header) ? "" : ORACLES_ROUTE_CORE_JOYPAD_BOUNCING_OFF);
    if (oracles_route_writer_open(&s->record, o->record_path, &header) != 0) return refuse(error, capacity, "cannot write %s", o->record_path);
    if (s->mod) {
        char path[PATH_MAX_LENGTH + 16];
        snprintf(path, sizeof path, "%s.mod.tsv", o->record_path);
        if (!oracles_mod_session_trace(s->mod, path)) return refuse(error, capacity, "cannot write %s", path);
    }
    s->recording = 1;
    return 1;
}
