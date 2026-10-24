#include "language_session.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORIGIN_LENGTH 2200
/* The compact text's line: the game's text box, sixteen tiles of eight pixels. */
#define COMPACT_LINE_PIXELS 128u

struct OraclesLanguageSession {
    OraclesLanguage language;
    OraclesLanguagePack *pack;       /* NULL for English (US), laid out again */
    char sha1[41];                   /* the image's, as read */
    OraclesLanguageLayout layout;    /* the compact text's: a width of 0 without it */
    char origin[ORIGIN_LENGTH];      /* where the pack came from, for the report */
    char note[ORIGIN_LENGTH];        /* what the player is told: the pack could not be written */
    char identity[160];              /* language-CODE@SHA1, the pack's digest, then text-compact@SHA1, the layout's */
    OraclesLanguageReport composed;
};

/* The image of `path` when it is the European ROM of `game` (`us` 0) or its US ROM (`us` 1); NULL otherwise, quietly. */
static uint8_t *rom_at(const char *path, OraclesGame game, int us, size_t *size)
{
    char error[128];
    uint8_t *data = oracles_rom_read_file(path, size, error, sizeof error);
    OraclesRomInfo info;
    if (data && oracles_rom_identify(data, *size, &info, error, sizeof error) == 0
        && (us ? oracles_rom_is_original(&info) && info.game == game : oracles_language_source_game(&info) == game)) return data;
    free(data);
    return NULL;
}

/* Whether a file's name ends with `extension` (".gbc"), in any case. */
static int has_extension(const char *name, const char *extension)
{
    const size_t n = strlen(name), e = strlen(extension);
    if (n <= e) return 0;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)name[n - e + i]) != extension[i]) return 0;
    return 1;
}

/* The European ROM of `game` (or its US ROM): `path` itself, or the first file of the directory `path` that is it. */
static uint8_t *find_rom(const char *path, OraclesGame game, int us, size_t *size, char *found, size_t capacity)
{
    DIR *listing = opendir(path);
    if (!listing) {
        snprintf(found, capacity, "%s", path);
        return rom_at(path, game, us, size);
    }
    uint8_t *data = NULL;
    struct dirent *entry;
    while (!data && (entry = readdir(listing))) {
        if (!has_extension(entry->d_name, ".gbc") && !has_extension(entry->d_name, ".gb")) continue;
        snprintf(found, capacity, "%s/%s", path, entry->d_name);
        data = rom_at(found, game, us, size);
    }
    closedir(listing);
    return data;
}

/* The US ROM a fan game is made on: the file `base` its patch applies to, else the first in the directory `european`
 * names, or holds the European ROM; NULL with the reason. */
static uint8_t *find_us(const char *base, const char *european, OraclesGame game, size_t *size, char *error, size_t capacity)
{
    if (base) return oracles_rom_read_file(base, size, error, capacity);
    char directory[1100], found[1100];
    snprintf(directory, sizeof directory, "%s", european);
    DIR *listing = opendir(directory);
    if (listing) closedir(listing);
    else {
        char *slash = strrchr(directory, '/'), *backslash = strrchr(directory, '\\');
        if (backslash && (!slash || backslash > slash)) slash = backslash;
        if (slash) *slash = 0;
        else snprintf(directory, sizeof directory, ".");
    }
    uint8_t *data = find_rom(directory, game, 1, size, found, sizeof found);
    if (!data) snprintf(error, capacity, "a fan game's language needs the US ROM of %s it is made on, with its patch (--patch) or beside its European ROM",
                        oracles_game_name(game));
    return data;
}

static int refuse(OraclesLanguageSession *session, char *error, size_t capacity, const char *message)
{
    if (message) snprintf(error, capacity, "%s", message);
    oracles_language_session_stop(session);
    return -1;
}

/* The pack of the game `info` names from its European ROM's bytes: the game's own for an original, derived for a fan
 * game from the game's pack and its US ROM (find_us); NULL with the reason. */
static OraclesLanguagePack *extract(const uint8_t *european, size_t european_size, const char *european_path, const char *base_path,
                                    const OraclesRomInfo *info, const uint8_t *rom, size_t rom_size, char *error, size_t capacity)
{
    if (oracles_rom_is_original(info)) return oracles_language_pack_extract(european, european_size, rom, rom_size, error, capacity);
    size_t us_size = 0;
    uint8_t *us = find_us(base_path, european_path, info->game, &us_size, error, capacity);
    if (!us) return NULL;
    OraclesLanguagePack *base = oracles_language_pack_extract(european, european_size, us, us_size, error, capacity);
    OraclesLanguagePack *pack = base ? oracles_language_pack_derive(base, us, us_size, rom, rom_size, error, capacity) : NULL;
    oracles_language_pack_free(base);
    free(us);
    return pack;
}

int oracles_language_session_start(OraclesLanguageSession **out, const char *code, const char *european, const char *pack_path,
                                   const char *base_path, const OraclesRomInfo *info, const uint8_t *rom, size_t rom_size, char *error,
                                   size_t capacity)
{
    *out = NULL;
    OraclesLanguage language;
    if (oracles_language_parse(code, &language) != 0) { snprintf(error, capacity, "--language %s: not a language (en, en-eu, fr, de, it, es)", code); return -1; }
    if (language == ORACLES_LANGUAGE_ENGLISH) return 0;
    const OraclesCompatProfile *profile = oracles_compat_find(info);
    if (!oracles_compat_language_bank(profile)) {
        snprintf(error, capacity, "--language %s plays on the US ROM of Ages or Seasons, or a fan game made on it, which this ROM is not", code);
        return -1;
    }
    const char *name = oracles_compat_name(profile);
    OraclesLanguageSession *session = calloc(1, sizeof *session);
    if (!session) { snprintf(error, capacity, "out of memory"); return -1; }
    session->language = language;
    snprintf(session->sha1, sizeof session->sha1, "%s", info->sha1);
    char reason[512] = "";
    FILE *existing = pack_path ? fopen(pack_path, "rb") : NULL;
    if (existing) {
        fclose(existing);
        session->pack = oracles_language_pack_read(pack_path, reason, sizeof reason);
        if (session->pack && (oracles_language_pack_game(session->pack) != info->game || strcmp(oracles_language_pack_image(session->pack), info->sha1) != 0)) {
            oracles_language_pack_free(session->pack);
            session->pack = NULL;
            snprintf(reason, sizeof reason, "%s is the language pack of another game", pack_path);
        }
        if (session->pack) snprintf(session->origin, sizeof session->origin, "read from %s", pack_path);
        else if (!european) return refuse(session, error, capacity, reason);
    }
    if (!session->pack) {
        if (!european) {
            snprintf(error, capacity, "--language %s needs the European ROM of %s (--eu-rom) or the language pack of %s (--language-pack)", code,
                     oracles_game_name(info->game), name);
            return refuse(session, error, capacity, NULL);
        }
        char found[1100];
        size_t size = 0;
        uint8_t *data = find_rom(european, info->game, 0, &size, found, sizeof found);
        if (!data) {
            snprintf(error, capacity, "--eu-rom %s: no European ROM of %s there", european, oracles_game_name(info->game));
            return refuse(session, error, capacity, NULL);
        }
        session->pack = extract(data, size, found, base_path, info, rom, rom_size, error, capacity);
        free(data);
        if (!session->pack) return refuse(session, error, capacity, NULL);
        snprintf(session->origin, sizeof session->origin, "extracted from %s%s%s%s", found, reason[0] ? " (" : "", reason, reason[0] ? ")" : "");
        /* The pack is in memory: one that cannot be written is extracted again at the next start, and said. */
        char written[512] = "";
        if (pack_path && oracles_language_pack_write(session->pack, pack_path, written, sizeof written) != 0)
            snprintf(session->note, sizeof session->note, "The language pack could not be written (%s): it is extracted again at each start.", written);
        if (pack_path) {
            const size_t used = strlen(session->origin);
            snprintf(session->origin + used, sizeof session->origin - used, session->note[0] ? ", not written to %s" : ", written to %s", pack_path);
        }
    }
    /* A savestate holds the addresses of the language's text being shown (w7TextAddress, w7ActiveBank): it loads with
     * the same pack only. */
    char digest[41];
    oracles_language_pack_digest(session->pack, digest);
    snprintf(session->identity, sizeof session->identity, "language-%s@%s", code, digest);
    *out = session;
    return 0;
}

int oracles_language_session_lay_out(OraclesLanguageSession **session, const OraclesRomInfo *info, char *error, size_t capacity)
{
    const OraclesCompatProfile *profile = oracles_compat_find(info);
    if (!profile || !oracles_compat_language_bank(profile)) {
        snprintf(error, capacity, "the compact text lays out the texts of the US ROM of Ages or Seasons, or of a fan game made on it");
        return -1;
    }
    if (!oracles_rom_is_original(info) && *session) {
        snprintf(error, capacity, "the compact text lays out a fan game's texts in its English alone, not in a language");
        return -1;
    }
    if (!*session) {
        if (!(*session = calloc(1, sizeof **session))) { snprintf(error, capacity, "out of memory"); return -1; }
        (*session)->language = ORACLES_LANGUAGE_ENGLISH;
        snprintf((*session)->sha1, sizeof (*session)->sha1, "%s", info->sha1);
    }
    OraclesLanguageSession *s = *session;
    s->layout.width = COMPACT_LINE_PIXELS;
    /* A savestate holds the address of a text laid out again: it loads with the same layout only. */
    char digest[41];
    oracles_language_layout_digest(&s->layout, info->revision, digest);
    const size_t used = strlen(s->identity);
    snprintf(s->identity + used, sizeof s->identity - used, "%stext-compact@%s", used ? "," : "", digest);
    return 0;
}

int oracles_language_session_compose(OraclesLanguageSession *session, uint8_t **rom, size_t *rom_size, char *error, size_t capacity)
{
    uint8_t *image = NULL;
    size_t size = 0;
    const OraclesLanguageLayout *layout = session->layout.width ? &session->layout : NULL;
    const int status = session->pack ? oracles_language_compose(session->pack, session->language, *rom, *rom_size, layout, &image, &size, &session->composed, error, capacity)
                                     : oracles_language_compose_english(session->sha1, *rom, *rom_size, layout, &image, &size, &session->composed, error, capacity);
    if (status != 0) return -1;
    free(*rom);
    *rom = image;
    *rom_size = size;
    return 0;
}

int oracles_language_session_attach(OraclesLanguageSession *session, OraclesGuest *guest, char *error, size_t capacity)
{
    if (!guest || oracles_guest_arm_language(guest) != 0 || strcmp(oracles_guest_language(guest), oracles_language_code(session->language)) != 0) {
        snprintf(error, capacity, "the language cannot be served: the text's hook cannot be armed");
        return -1;
    }
    return 0;
}

const char *oracles_language_session_identity(const OraclesLanguageSession *session) { return session ? session->identity : ""; }

const char *oracles_language_session_note(const OraclesLanguageSession *session) { return session ? session->note : ""; }

void oracles_language_session_report(const OraclesLanguageSession *session, const OraclesGuest *guest, const char *prefix, FILE *out)
{
    if (!session) return;
    unsigned served = 0, english = 0, missing = 0;
    oracles_guest_language_counts(guest, &served, &english, &missing);
    const OraclesLanguageReport *c = &session->composed;
    if (session->pack)
        fprintf(out, "%slanguage %s, from the pack of the European ROM %s, %s: %u texts composed, %u kept in English, %u it lacks, "
                     "%u graphics composed; the game looked up %u texts in the language, %u kept in English, %u it lacks\n",
                prefix, oracles_language_code(session->language), oracles_language_pack_source(session->pack), session->origin,
                c->served, c->english, c->missing, c->graphics, served, english, missing);
    if (session->layout.width)
        fprintf(out, "%scompact text: the %s texts laid out again on %u pixels a line, %u of them, %u kept by their table or a call, %u that did not "
                     "hold; %u boxes for the game's %u; the game looked up %u texts laid out again\n",
                prefix, oracles_language_code(session->language), session->layout.width, c->laid_out, c->layout_kept, c->layout_failed, c->boxes_laid_out,
                c->boxes, oracles_guest_language_laid_out(guest));
}

unsigned oracles_language_session_graphics(const OraclesLanguageSession *session) { return session ? session->composed.graphics : 0; }

const OraclesLanguageReport *oracles_language_session_composed(const OraclesLanguageSession *session) { return session ? &session->composed : NULL; }

int oracles_language_session_translated(const OraclesLanguageSession *session) { return session && session->pack; }

void oracles_language_session_stop(OraclesLanguageSession *session)
{
    if (!session) return;
    oracles_language_pack_free(session->pack);
    free(session);
}
