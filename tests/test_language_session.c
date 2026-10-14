/* A session's language (language_session.c), on the user's ROMs found by their SHA-1 in $ORACLES_ROM_DIR (skipped, exit
 * 77, without them): English gives no session; a pack is extracted from the European ROM found in a directory, whatever
 * the case of its extension, and written; read back; a pack of another fingerprint or of another game is extracted again
 * when the European ROM is given and refused without it; without the European ROM or its pack, or on another ROM, the
 * language is refused; a pack that cannot be written serves all the same, and the session says so. */
#include "language_internal.h"
#include "language_session.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#define make_dir(path) mkdir(path, 0777)
#endif

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); } } while (0)

typedef struct rom_file { uint8_t *data; size_t size; } rom_file;

/* The ROM of `revision` in `dir`, or an empty one. */
static rom_file find(const char *dir, OraclesRomRevision revision)
{
    rom_file found = { NULL, 0 };
    DIR *listing = opendir(dir);
    struct dirent *entry;
    while (listing && !found.data && (entry = readdir(listing))) {
        const size_t n = strlen(entry->d_name);
        if (n < 4 || (strcmp(entry->d_name + n - 4, ".gbc") && strcmp(entry->d_name + n - 4, ".GBC"))) continue;
        char path[1024], error[128];
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        size_t size = 0;
        uint8_t *data = oracles_rom_read_file(path, &size, error, sizeof error);
        OraclesRomInfo info;
        if (data && oracles_rom_identify(data, size, &info, error, sizeof error) == 0 && info.known && info.revision == revision) found = (rom_file){ data, size };
        else free(data);
    }
    if (listing) closedir(listing);
    return found;
}

static int write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    const int written = fwrite(data, 1, size, f) == size;
    return (fclose(f) == 0) & written;
}

/* The session's report, where it says where its pack came from. */
static const char *report(const OraclesLanguageSession *session)
{
    static char text[4096];
    text[0] = 0;
    FILE *f = tmpfile();
    if (!f) return text;
    oracles_language_session_report(session, NULL, "", f);
    rewind(f);
    const size_t n = fread(text, 1, sizeof text - 1u, f);
    text[n] = 0;
    fclose(f);
    return text;
}

/* Starts Ages in French: the European ROM `european`, the pack `pack`; 0 or -1, the session in *session. */
static int start(OraclesLanguageSession **session, const char *european, const char *pack, const OraclesRomInfo *info, const rom_file *us,
                 char *error, size_t capacity)
{
    error[0] = 0;
    return oracles_language_session_start(session, "fr", european, pack, NULL, info, us->data, us->size, error, capacity);
}

int main(int argc, char **argv)
{
    const char *work = argc > 1 ? argv[1] : ".";
    char error[1024];
    OraclesLanguageSession *session = (OraclesLanguageSession *)&failures;
    OraclesRomInfo none = { 0 };
    /* English (US) is the cartridge's own: no session, whatever is given. */
    CHECK(oracles_language_session_start(&session, "en", NULL, NULL, NULL, &none, NULL, 0, error, sizeof error) == 0 && session == NULL);
    CHECK(oracles_language_session_start(&session, "klingon", NULL, NULL, NULL, &none, NULL, 0, error, sizeof error) == -1 && session == NULL);

    const char *dir = getenv("ORACLES_ROM_DIR");
    rom_file ages = { NULL, 0 }, ages_eu = { NULL, 0 }, seasons = { NULL, 0 }, seasons_eu = { NULL, 0 };
    if (dir && *dir) {
        ages = find(dir, ORACLES_ROM_REVISION_AGES_US);
        ages_eu = find(dir, ORACLES_ROM_REVISION_AGES_EU);
        seasons = find(dir, ORACLES_ROM_REVISION_SEASONS_US);
        seasons_eu = find(dir, ORACLES_ROM_REVISION_SEASONS_EU);
    }
    if (!ages.data || !ages_eu.data || !seasons.data || !seasons_eu.data) {
        printf("language-session: no US and European ROMs of both games in ORACLES_ROM_DIR: the ROMs' part skipped\n");
        free(ages.data); free(ages_eu.data); free(seasons.data); free(seasons_eu.data);
        if (failures) fprintf(stderr, "%d failure(s)\n", failures);
        return failures ? 1 : 77;
    }
    OraclesRomInfo info, eu_info;
    CHECK(oracles_rom_identify(ages.data, ages.size, &info, error, sizeof error) == 0);
    CHECK(oracles_rom_identify(ages_eu.data, ages_eu.size, &eu_info, error, sizeof error) == 0);
    char eu_dir[1024], empty[1024], pack[1100], eu_path[1100], unwritable[1100];
    snprintf(eu_dir, sizeof eu_dir, "%s/language-session-eu", work);
    snprintf(empty, sizeof empty, "%s/language-session-empty", work);
    snprintf(pack, sizeof pack, "%s/language-session-ages.pack", work);
    snprintf(eu_path, sizeof eu_path, "%s/AGES EU.GBC", eu_dir);
    snprintf(unwritable, sizeof unwritable, "%s/language-session-none/ages.pack", work);
    make_dir(eu_dir);
    make_dir(empty);
    CHECK(write_file(eu_path, ages_eu.data, ages_eu.size));
    remove(pack);
    /* English (US) on the US ROM, with all a language needs: no session, the image stays the ROM. */
    CHECK(oracles_language_session_start(&session, "en", eu_dir, pack, NULL, &info, ages.data, ages.size, error, sizeof error) == 0 && session == NULL);

    /* Neither the European ROM nor a pack; a directory without the European ROM; another ROM than the US one. */
    CHECK(start(&session, NULL, pack, &info, &ages, error, sizeof error) == -1 && session == NULL && strstr(error, "needs the European ROM"));
    CHECK(start(&session, empty, pack, &info, &ages, error, sizeof error) == -1 && strstr(error, "no European ROM"));
    CHECK(start(&session, eu_dir, pack, &eu_info, &ages_eu, error, sizeof error) == -1 && strstr(error, "US ROM of Ages or Seasons"));

    /* Extracted from the European ROM found in the directory (GBC in capitals), written; then read back. */
    CHECK(start(&session, eu_dir, pack, &info, &ages, error, sizeof error) == 0 && session != NULL);
    CHECK(strstr(report(session), "extracted from") && strstr(report(session), "written to") && !oracles_language_session_note(session)[0]);
    char identity[128];
    snprintf(identity, sizeof identity, "%s", oracles_language_session_identity(session));
    CHECK(!strncmp(identity, "language-fr@", 12) && strlen(identity) == 52u);
    oracles_language_session_stop(session);
    CHECK(start(&session, NULL, pack, &info, &ages, error, sizeof error) == 0 && session != NULL && strstr(report(session), "read from"));
    CHECK(!strcmp(oracles_language_session_identity(session), identity));   /* the same pack: a state taken with one loads with the other */
    oracles_language_session_stop(session);

    /* Another pack (a text changed, its fingerprint the same): another savestate identity. */
    OraclesLanguagePack *changed = oracles_language_pack_read(pack, error, sizeof error);
    CHECK(changed != NULL);
    if (changed) {
        changed->bytes[changed->texts[ORACLES_LANGUAGE_FRENCH][0].at] ^= 0x01u;
        CHECK(oracles_language_pack_write(changed, pack, error, sizeof error) == 0);
        oracles_language_pack_free(changed);
    }
    CHECK(start(&session, NULL, pack, &info, &ages, error, sizeof error) == 0 && session != NULL);
    CHECK(strcmp(oracles_language_session_identity(session), identity) != 0 && !strncmp(oracles_language_session_identity(session), "language-fr@", 12));
    oracles_language_session_stop(session);

    /* A pack of another fingerprint: refused without the European ROM, extracted again with it, and rewritten. */
    OraclesLanguagePack *stale = oracles_language_pack_read(pack, error, sizeof error);
    CHECK(stale != NULL);
    if (stale) {
        stale->fingerprint[0] = stale->fingerprint[0] == '0' ? '1' : '0';
        CHECK(oracles_language_pack_write(stale, pack, error, sizeof error) == 0);
        oracles_language_pack_free(stale);
    }
    CHECK(start(&session, NULL, pack, &info, &ages, error, sizeof error) == -1 && strstr(error, "another version"));
    CHECK(start(&session, eu_dir, pack, &info, &ages, error, sizeof error) == 0 && session != NULL);
    CHECK(strstr(report(session), "extracted from") && strstr(report(session), "another version"));
    oracles_language_session_stop(session);
    OraclesLanguagePack *again = oracles_language_pack_read(pack, error, sizeof error);
    CHECK(again != NULL);
    oracles_language_pack_free(again);

    /* The pack of another game: refused without the European ROM, extracted again with it. */
    OraclesLanguagePack *other = oracles_language_pack_extract(seasons_eu.data, seasons_eu.size, seasons.data, seasons.size, error, sizeof error);
    CHECK(other != NULL && oracles_language_pack_write(other, pack, error, sizeof error) == 0);
    oracles_language_pack_free(other);
    CHECK(start(&session, NULL, pack, &info, &ages, error, sizeof error) == -1 && strstr(error, "another game"));
    CHECK(start(&session, eu_dir, pack, &info, &ages, error, sizeof error) == 0 && session != NULL && strstr(report(session), "extracted from"));
    oracles_language_session_stop(session);

    /* A pack that cannot be written: the session plays with the pack in memory, and says so. */
    CHECK(start(&session, eu_path, unwritable, &info, &ages, error, sizeof error) == 0 && session != NULL);
    CHECK(strstr(oracles_language_session_note(session), "could not be written") && strstr(report(session), "not written to"));
    oracles_language_session_stop(session);

    remove(pack);
    remove(eu_path);
    free(ages.data); free(ages_eu.data); free(seasons.data); free(seasons_eu.data);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
