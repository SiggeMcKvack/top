/* A save brought in and taken out (save_transfer.h), on SRAM images built here and files in the folder the test's
 * argument names, emptied first: a file chosen read, refused unread when larger than a save; an Oracle's European save
 * set to English, a fan game's taken as it is, another game's save, a file of another size or that is no save refused;
 * the save replaced kept in Backups under its name and the time, a backup of that name already there kept too, the new
 * one numbered; no backup without a save; the savestate moved there with it; a copy out. */
#include "save_transfer.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); } } while (0)

#define LANGUAGE_AT 0x7au

static void build(uint8_t *sram, OraclesGame game, uint8_t language, uint8_t play)
{
    memset(sram, 0, ORACLES_SAVE_SIZE);
    memcpy(sram, "L_O_C_A_L_I_Z_E", 15);
    sram[0x0f] = language;
    for (unsigned copy = 0; copy < ORACLES_SAVE_COPIES; copy += 3u) {
        const unsigned at = oracles_save_copies[copy];
        memcpy(sram + at + 2u, game == ORACLES_GAME_AGES ? "Z21216-0" : "Z11216-0", 8);
        for (unsigned i = 10u; i < 0x550u; i++) sram[at + i] = (uint8_t)(i + play);
        sram[at + LANGUAGE_AT] = language;
        oracles_save_seal(sram, at);
    }
}

static int holds(const char *path, const void *data, size_t size)
{
    size_t length = 0;
    void *there = SDL_LoadFile(path, &length);
    const int same = there && length == size && !memcmp(there, data, size);
    SDL_free(there);
    return same;
}

static void check(void)
{
    static uint8_t sram[ORACLES_SAVE_SIZE], out[ORACLES_SAVE_SIZE];
    int english;
    /* An Oracle's European save in German: set to English, still valid. */
    build(sram, ORACLES_GAME_AGES, 2, 0);
    CHECK(oracles_save_transfer_check(sram, sizeof sram, ORACLES_GAME_AGES, 1, out, &english) && english);
    CHECK(out[oracles_save_copies[0] + LANGUAGE_AT] == 0u && oracles_save_valid(out, sizeof out, ORACLES_GAME_AGES) == (1u | 8u));
    /* In English: taken as it is, nothing said. */
    build(sram, ORACLES_GAME_SEASONS, 0, 0);
    CHECK(oracles_save_transfer_check(sram, sizeof sram, ORACLES_GAME_SEASONS, 1, out, &english) && !english && !memcmp(out, sram, sizeof sram));
    /* A fan game's, on its Oracle's engine, in Spanish: copied as it is. */
    build(sram, ORACLES_GAME_AGES, 4, 0);
    CHECK(oracles_save_transfer_check(sram, sizeof sram, ORACLES_GAME_AGES, 0, out, &english) && !english && !memcmp(out, sram, sizeof sram));
    /* Another game's save, a file of another size, a file that is no save: refused. */
    CHECK(!oracles_save_transfer_check(sram, sizeof sram, ORACLES_GAME_SEASONS, 1, out, &english));
    CHECK(!oracles_save_transfer_check(sram, sizeof sram - 16u, ORACLES_GAME_AGES, 1, out, &english));
    memset(sram, 0xff, sizeof sram);
    CHECK(!oracles_save_transfer_check(sram, sizeof sram, ORACLES_GAME_AGES, 1, out, &english));
}

static void import(const char *folder)
{
    static uint8_t first[ORACLES_SAVE_SIZE], second[ORACLES_SAVE_SIZE], third[ORACLES_SAVE_SIZE], other[16] = "another backup";
    char save[1024], backup[1100], state_backup[1100], state[1100], expected[1100], error[256];
    static const uint8_t old_state[] = "a savestate of the first";
    snprintf(save, sizeof save, "%s/Oracle of Ages (USA).sav", folder);
    build(first, ORACLES_GAME_AGES, 0, 1);
    build(second, ORACLES_GAME_AGES, 0, 2);
    build(third, ORACLES_GAME_AGES, 0, 3);
    struct tm when = { .tm_year = 126, .tm_mon = 9, .tm_mday = 20, .tm_hour = 14, .tm_min = 22, .tm_isdst = -1 };
    const time_t now = mktime(&when);

    /* No save yet: none kept, the new one written. */
    CHECK(oracles_save_transfer_import(save, first, sizeof first, now, backup, sizeof backup, state_backup, sizeof state_backup, error, sizeof error));
    CHECK(!backup[0] && !state_backup[0] && holds(save, first, sizeof first));
    snprintf(expected, sizeof expected, "%s/Backups", folder);
    CHECK(!SDL_GetPathInfo(expected, NULL));
    /* The savestate beside the save, which holds the SRAM replaced: moved to Backups with it, under the same name and
     * time, so that no Load state brings the old save back. */
    snprintf(state, sizeof state, "%s/Oracle of Ages (USA).state", folder);
    CHECK(SDL_SaveFile(state, old_state, sizeof old_state));

    /* A file of the backup's name already there, holding other bytes: kept, the backup numbered after it. */
    SDL_CreateDirectory(expected);
    snprintf(expected, sizeof expected, "%s/Backups/Oracle of Ages (USA) 2026-10-20 14-22.sav", folder);
    CHECK(SDL_SaveFile(expected, other, sizeof other));
    CHECK(oracles_save_transfer_import(save, second, sizeof second, now, backup, sizeof backup, state_backup, sizeof state_backup, error, sizeof error));
    snprintf(expected, sizeof expected, "%s/Backups/Oracle of Ages (USA) 2026-10-20 14-22 (2).sav", folder);
    CHECK(!strcmp(backup, expected) && holds(backup, first, sizeof first));
    snprintf(expected, sizeof expected, "%s/Backups/Oracle of Ages (USA) 2026-10-20 14-22.sav", folder);
    CHECK(holds(expected, other, sizeof other));
    CHECK(holds(save, second, sizeof second));
    snprintf(expected, sizeof expected, "%s/Backups/Oracle of Ages (USA) 2026-10-20 14-22.state", folder);
    CHECK(!strcmp(state_backup, expected) && holds(state_backup, old_state, sizeof old_state) && !SDL_GetPathInfo(state, NULL));

    /* Again in the same minute: a third name, the two before kept; no savestate now, none moved. */
    CHECK(oracles_save_transfer_import(save, third, sizeof third, now, backup, sizeof backup, state_backup, sizeof state_backup, error, sizeof error));
    CHECK(!state_backup[0]);
    snprintf(expected, sizeof expected, "%s/Backups/Oracle of Ages (USA) 2026-10-20 14-22 (3).sav", folder);
    CHECK(!strcmp(backup, expected) && holds(backup, second, sizeof second));
    snprintf(expected, sizeof expected, "%s/Backups/Oracle of Ages (USA) 2026-10-20 14-22 (2).sav", folder);
    CHECK(holds(expected, first, sizeof first));
    CHECK(holds(save, third, sizeof third));
    snprintf(expected, sizeof expected, "%s.partial", save);
    CHECK(!SDL_GetPathInfo(expected, NULL));

    /* The new save cannot be written (its temporary name taken by a folder): the save and its savestate stay in place. */
    CHECK(SDL_SaveFile(state, old_state, sizeof old_state) && SDL_CreateDirectory(expected));
    CHECK(!oracles_save_transfer_import(save, first, sizeof first, now, backup, sizeof backup, state_backup, sizeof state_backup, error, sizeof error));
    CHECK(error[0] && holds(save, third, sizeof third) && holds(state, old_state, sizeof old_state));
    CHECK(SDL_RemovePath(expected) && SDL_RemovePath(state));

    /* Out: a copy of the save where the player chose. */
    snprintf(expected, sizeof expected, "%s/copy.sav", folder);
    CHECK(oracles_save_transfer_export(save, expected, error, sizeof error) && holds(expected, third, sizeof third));
    snprintf(save, sizeof save, "%s/none.sav", folder);
    CHECK(!oracles_save_transfer_export(save, expected, error, sizeof error) && error[0]);
}

/* A file chosen as a save: a save read whole, a smaller file too (the check refuses it), a larger one refused unread. */
static void read_chosen(const char *folder)
{
    static uint8_t sram[ORACLES_SAVE_SIZE], out[ORACLES_SAVE_TRANSFER_READ];
    static uint8_t large[2u * 1024u * 1024u];
    char path[1024], error[256];
    size_t size = 1;
    build(sram, ORACLES_GAME_AGES, 0, 4);
    snprintf(path, sizeof path, "%s/chosen.sav", folder);
    CHECK(SDL_SaveFile(path, sram, sizeof sram) && oracles_save_transfer_read(path, out, &size, error, sizeof error));
    CHECK(size == sizeof sram && !memcmp(out, sram, sizeof sram));
    CHECK(SDL_SaveFile(path, sram, 100) && oracles_save_transfer_read(path, out, &size, error, sizeof error) && size == 100u);
    CHECK(SDL_SaveFile(path, large, ORACLES_SAVE_SIZE + 1u));
    CHECK(!oracles_save_transfer_read(path, out, &size, error, sizeof error) && size == 0u && !strcmp(error, "larger than a save"));
    CHECK(SDL_SaveFile(path, large, sizeof large) && !oracles_save_transfer_read(path, out, &size, error, sizeof error) && !strcmp(error, "larger than a save"));
    snprintf(path, sizeof path, "%s/none.sav", folder);
    CHECK(!oracles_save_transfer_read(path, out, &size, error, sizeof error) && error[0]);
}

static SDL_EnumerationResult SDLCALL removed(void *userdata, const char *dirname, const char *fname)
{
    (void)userdata;
    char path[2048];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(path, removed, NULL);
    SDL_RemovePath(path);
    return SDL_ENUM_CONTINUE;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s FOLDER\n", argv[0]); return 2; }
    SDL_EnumerateDirectory(argv[1], removed, NULL);
    SDL_CreateDirectory(argv[1]);
    check();
    import(argv[1]);
    read_chosen(argv[1]);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("save transfer: all checks passed\n");
    return failures != 0;
}
