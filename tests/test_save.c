/* The save's files (save.h) on SRAM images built here, without a ROM: a US save of each game, a European save in each
 * of its five languages, a damaged copy, another game's save, an image of another size. */
#include "save.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); } } while (0)

#define LANGUAGE_AT 0x7au

/* A file in both copies, its game's string, its language and some play in it; `european`: the European ROM's header,
 * with its language, before the first file. */
static void build(uint8_t *sram, OraclesGame game, unsigned file, uint8_t language, int european)
{
    for (unsigned copy = file; copy < ORACLES_SAVE_COPIES; copy += 3u) {
        const unsigned at = oracles_save_copies[copy];
        memcpy(sram + at + 2u, game == ORACLES_GAME_AGES ? "Z21216-0" : "Z11216-0", 8);
        for (unsigned i = 10u; i < 0x550u; i++) sram[at + i] = (uint8_t)(i * 7u + file);
        sram[at + LANGUAGE_AT] = language;
        oracles_save_seal(sram, at);
    }
    if (european) {
        memcpy(sram, "L_O_C_A_L_I_Z_E", 15);
        sram[0x0f] = language;
    }
}

static void us_saves(void)
{
    static uint8_t sram[ORACLES_SAVE_SIZE];
    memset(sram, 0, sizeof sram);
    build(sram, ORACLES_GAME_AGES, 0, 0, 0);
    build(sram, ORACLES_GAME_AGES, 2, 0, 0);
    CHECK(oracles_save_valid(sram, sizeof sram, ORACLES_GAME_AGES) == (1u | 4u | 8u | 32u));
    CHECK(oracles_save_valid(sram, sizeof sram, ORACLES_GAME_SEASONS) == 0u);   /* Ages' save, not Seasons' */
    CHECK(oracles_save_valid(sram, sizeof sram - 1u, ORACLES_GAME_AGES) == 0u);
    CHECK(oracles_save_valid(sram, sizeof sram, ORACLES_GAME_UNKNOWN) == 0u);
    /* English already: nothing changes. */
    static uint8_t before[ORACLES_SAVE_SIZE];
    memcpy(before, sram, sizeof sram);
    CHECK(oracles_save_set_english(sram, sizeof sram, ORACLES_GAME_AGES) == 0u);
    CHECK(!memcmp(before, sram, sizeof sram));
    /* Seasons' own. */
    memset(sram, 0, sizeof sram);
    build(sram, ORACLES_GAME_SEASONS, 1, 0, 0);
    CHECK(oracles_save_valid(sram, sizeof sram, ORACLES_GAME_SEASONS) == (2u | 16u));
    CHECK(oracles_save_valid(sram, sizeof sram, ORACLES_GAME_AGES) == 0u);
    /* An empty SRAM: the sum of zeros holds, the string does not. */
    memset(sram, 0, sizeof sram);
    CHECK(oracles_save_valid(sram, sizeof sram, ORACLES_GAME_AGES) == 0u);
}

/* A save of the European ROM in each language: every valid copy set to English and sealed again, the header and every
 * other byte kept. */
static void european_saves(void)
{
    static uint8_t sram[ORACLES_SAVE_SIZE], before[ORACLES_SAVE_SIZE];
    for (uint8_t language = 0; language <= 4u; language++)
        for (int game = ORACLES_GAME_AGES; game <= ORACLES_GAME_SEASONS; game++) {
            memset(sram, 0, sizeof sram);
            build(sram, (OraclesGame)game, 0, language, 1);
            build(sram, (OraclesGame)game, 1, language, 1);
            memcpy(before, sram, sizeof sram);
            const unsigned changed = oracles_save_set_english(sram, sizeof sram, (OraclesGame)game);
            CHECK(changed == (language ? (1u | 2u | 8u | 16u) : 0u));
            CHECK(oracles_save_valid(sram, sizeof sram, (OraclesGame)game) == (1u | 2u | 8u | 16u));
            CHECK(!memcmp(sram, "L_O_C_A_L_I_Z_E", 15) && sram[0x0f] == language);
            for (unsigned i = 0; i < sizeof sram; i++) {
                int in_place = 0;   /* the language and the checksum of a copy */
                for (unsigned c = 0; c < ORACLES_SAVE_COPIES; c++) {
                    const unsigned at = oracles_save_copies[c];
                    if (i == at || i == at + 1u || i == at + LANGUAGE_AT) in_place = 1;
                }
                if (!in_place && sram[i] != before[i]) { CHECK(!"a byte changed out of place"); break; }
            }
            for (unsigned c = 0; c < ORACLES_SAVE_COPIES; c++) CHECK(sram[oracles_save_copies[c] + LANGUAGE_AT] == 0u);
        }
}

/* A copy whose checksum fails is neither valid nor changed; the file's other copy is. */
static void damaged(void)
{
    static uint8_t sram[ORACLES_SAVE_SIZE];
    memset(sram, 0, sizeof sram);
    build(sram, ORACLES_GAME_AGES, 0, 2, 1);
    sram[oracles_save_copies[0] + 0x100u] ^= 0x40u;
    CHECK(oracles_save_valid(sram, sizeof sram, ORACLES_GAME_AGES) == 8u);
    CHECK(oracles_save_set_english(sram, sizeof sram, ORACLES_GAME_AGES) == 8u);
    CHECK(sram[oracles_save_copies[0] + LANGUAGE_AT] == 2u);
    CHECK(!oracles_save_copy_valid(sram, oracles_save_copies[0], ORACLES_GAME_AGES));
    /* Both copies damaged: no save of the game at all. */
    sram[oracles_save_copies[3] + 0x200u] ^= 0x01u;
    CHECK(oracles_save_valid(sram, sizeof sram, ORACLES_GAME_AGES) == 0u);
}

int main(void)
{
    us_saves();
    european_saves();
    damaged();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("save: all checks passed\n");
    return failures != 0;
}
