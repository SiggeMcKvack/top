/* The instant text (guest.h): the rules of its two answers, the search of its two points in a ROM, and the transaction
 * on a synthetic ROM (no game data) on both cores, for Ages, Seasons and the three fan games' tables.  At the game's
 * routines, in bank $3f, WRAM bank 7 mapped,
 *   updateCharacterDisplayTimer:  ld a,(wKeysJustPressed) ; and BTN_A|BTN_B ; ld ($c000),a ; ret
 *   displayNextTextCharacter:     nop... ; ld d,>w7d0c1 ; ld e,<w7d0c1 ; ld a,(de) ; bit 0,a ; ld ($c001),a ; ret
 * (the test of the loop where the game has it, $4d bytes in) called in a loop, so that the results hold what each test
 * saw. */
#include "core.h"
#include "guest.h"
#include "profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); } } while (0)

#define BTN_A 0x01u
#define BTN_B 0x02u
#define LOOP_AT 0x4du   /* displayNextTextCharacter's `ld e,<w7d0c1`, in the five images */

extern const OraclesGuestTables oracles_guest_tables_moonrise, oracles_guest_tables_temple, oracles_guest_tables_kinomi;

static void test_rules(void)
{
    CHECK(oracles_guest_text_skip_press(0) == BTN_A);             /* nothing pressed: A, the line whole */
    CHECK(oracles_guest_text_skip_press(BTN_A) == 0);             /* the player's own press */
    CHECK(oracles_guest_text_skip_press(BTN_B) == 0);
    CHECK(oracles_guest_text_skip_press(0xf0u) == BTN_A);         /* a direction is no press for the text */
    CHECK(oracles_guest_text_skip_flags(0x01u, 0) == 0x01u);      /* no sound effect: the loop goes on */
    CHECK(oracles_guest_text_skip_flags(0x01u, 0xa0u) == 0x00u);  /* a voice: it stops there */
    CHECK(oracles_guest_text_skip_flags(0x35u, 0x9au) == 0x34u);  /* the other bits kept */
    CHECK(oracles_guest_text_skip_flags(0x00u, 0x9au) == 0x00u);
}

static void put(uint8_t *at, const uint8_t *bytes, size_t count) { memcpy(at, bytes, count); }

static void test_find(const OraclesGuestTables *t)
{
    const size_t size = 1024u * 1024u;
    uint8_t *rom = calloc(size, 1);
    if (!rom) { failures++; return; }
    const OraclesGuestSym timer = t->update_character_display_timer, reveal = t->display_next_text_character;
    const size_t timer_at = oracles_rom_offset(timer.bank, timer.addr), reveal_at = oracles_rom_offset(reveal.bank, reveal.addr);
    const uint8_t test[] = { 0xfa, (uint8_t)t->keys_just_pressed.addr, (uint8_t)(t->keys_just_pressed.addr >> 8), 0xe6, 0x03 };
    const uint8_t loop[] = { 0x1e, (uint8_t)t->text_flags.addr, 0x1a, 0xcb, 0x47 };
    CHECK(oracles_guest_find_text_skip_test(t, rom, size) == 0 && oracles_guest_find_text_skip_loop(t, rom, size) == 0);   /* none */
    put(rom + timer_at + 0x11, test, sizeof test);   /* where the game has it: the `and` at +$14 */
    put(rom + reveal_at + LOOP_AT, loop, sizeof loop);
    CHECK(oracles_guest_find_text_skip_test(t, rom, size) == timer.addr + 0x14u);
    CHECK(oracles_guest_find_text_skip_loop(t, rom, size) == reveal.addr + LOOP_AT + 3u);
    put(rom + timer_at + 0x18, test, sizeof test);   /* twice: none taken */
    put(rom + reveal_at + 0x10, loop, sizeof loop);
    CHECK(oracles_guest_find_text_skip_test(t, rom, size) == 0 && oracles_guest_find_text_skip_loop(t, rom, size) == 0);
    memset(rom + timer_at + 0x18, 0, sizeof test);
    memset(rom + reveal_at + 0x10, 0, sizeof loop);
    put(rom + timer_at + 0x40, test, sizeof test);   /* past their windows: not looked at */
    put(rom + reveal_at + 0x60, loop, sizeof loop);
    CHECK(oracles_guest_find_text_skip_test(t, rom, size) == timer.addr + 0x14u);
    CHECK(oracles_guest_find_text_skip_loop(t, rom, size) == reveal.addr + LOOP_AT + 3u);
    rom[timer_at + 0x15] = BTN_A;   /* another mask */
    CHECK(oracles_guest_find_text_skip_test(t, rom, size) == 0);
    free(rom);
}

static void emit16(uint8_t **at, uint8_t opcode, unsigned value)
{
    *(*at)++ = opcode;
    *(*at)++ = (uint8_t)value;
    *(*at)++ = (uint8_t)(value >> 8);
}

static void test_transaction(OraclesCoreKind kind, OraclesGame game, OraclesRomRevision revision)
{
    const OraclesRomInfo info = { .game = game, .revision = revision, .known = 1 };
    const OraclesCompatProfile *profile = oracles_compat_find(&info);
    CHECK(profile && oracles_compat_text_instant(profile));
    if (!profile) return;
    const OraclesGuestTables *t = oracles_compat_guest_tables(profile);
    const size_t size = 1024u * 1024u;
    uint8_t *rom = calloc(size, 1);
    if (!rom) { failures++; return; }
    memcpy(rom + 0x134, game == ORACLES_GAME_AGES ? "ZELDA NAYRU" : "ZELDA DIN", game == ORACLES_GAME_AGES ? 11 : 9);
    rom[0x143] = 0xc0; rom[0x147] = 0x1b; rom[0x148] = 0x05; rom[0x149] = 0x02;
    rom[0x100] = 0x00; rom[0x101] = 0xc3; rom[0x102] = 0x50; rom[0x103] = 0x01;   /* nop ; jp $0150 */
    uint8_t *at = rom + 0x150;
    emit16(&at, 0x31, 0xc200);                                                         /* ld sp,$c200 */
    *at++ = 0x3e; *at++ = (uint8_t)t->update_character_display_timer.bank; emit16(&at, 0xea, 0x2000);   /* ld a,bank ; ld ($2000),a */
    *at++ = 0x3e; *at++ = 0x07; *at++ = 0xe0; *at++ = 0x70;                            /* ld a,7 ; ldh (<SVBK),a */
    uint8_t *loop = at;
    emit16(&at, 0xcd, t->update_character_display_timer.addr);                         /* call updateCharacterDisplayTimer */
    emit16(&at, 0xcd, t->display_next_text_character.addr);                            /* call displayNextTextCharacter */
    *at = 0x18; at[1] = (uint8_t)(loop - (at + 2));                                    /* jr loop */
    at = rom + oracles_rom_offset(t->update_character_display_timer.bank, t->update_character_display_timer.addr);
    emit16(&at, 0xfa, t->keys_just_pressed.addr);                                      /* ld a,(wKeysJustPressed) */
    *at++ = 0xe6; *at++ = BTN_A | BTN_B;                                               /* and BTN_A|BTN_B */
    emit16(&at, 0xea, 0xc000);                                                         /* ld ($c000),a */
    *at = 0xc9;                                                                        /* ret */
    at = rom + oracles_rom_offset(t->display_next_text_character.bank, t->display_next_text_character.addr) + LOOP_AT - 2u;
    *at++ = 0x16; *at++ = (uint8_t)(t->text_flags.addr >> 8);                          /* ld d,>w7d0c1 */
    *at++ = 0x1e; *at++ = (uint8_t)t->text_flags.addr;                                 /* ld e,<w7d0c1 */
    *at++ = 0x1a; *at++ = 0xcb; *at++ = 0x47;                                          /* ld a,(de) ; bit 0,a */
    emit16(&at, 0xea, 0xc001);                                                         /* ld ($c001),a */
    *at = 0xc9;                                                                        /* ret */

    const OraclesCoreOptions options = { 0, 0, kind, 0 };
    OraclesCore *core = oracles_core_create(rom, size, &options);
    free(rom);
    CHECK(core != NULL);
    if (!core) return;
    OraclesGuest *guest = oracles_guest_attach(core, profile);
    CHECK(guest != NULL);
    if (!guest) { oracles_core_destroy(core); return; }
    uint8_t *wram0 = oracles_guest_wram_writable(guest, 0), *wram7 = oracles_guest_wram_writable(guest, 7);
    CHECK(!oracles_guest_text_instant(guest) && !oracles_guest_text_instant_armed(guest));
    const struct { int on; uint8_t just, flags, column, sound, seen, seen_flags; } cases[] = {
        { 0, 0, 0x01, 3, 0x9a, 0, 0x01 },          /* off, armed or not: the game's own */
        { 1, 0, 0x00, 3, 0, BTN_A, 0x00 },         /* on, nothing pressed: A */
        { 1, BTN_B, 0x00, 3, 0, BTN_B, 0x00 },     /* the player's press: as it is */
        { 1, 0, 0x01, 3, 0, BTN_A, 0x01 },         /* a character without a sound effect: the loop goes on */
        { 1, 0, 0x01, 3, 0x9a, BTN_A, 0x00 },      /* with one, a voice: it stops there */
        { 1, 0, 0x21, 16, 0x9a, BTN_A, 0x20 },     /* the line's last column */
        { 1, 0, 0x00, 3, 0x9a, BTN_A, 0x00 },      /* no loop under way: nothing to stop */
        { 0, 0, 0x01, 3, 0x9a, 0, 0x01 },          /* off again: armed, answering nothing */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CHECK(oracles_guest_set_text_instant(guest, cases[i].on) == 0 && oracles_guest_text_instant(guest) == cases[i].on);
        wram0[t->keys_just_pressed.addr - 0xc000u] = cases[i].just;
        wram7[t->text_flags.addr - 0xd000u] = cases[i].flags;
        wram7[t->next_text_column.addr - 0xd000u] = cases[i].column;
        memset(wram7 + (t->line_sound_effects_buffer.addr - 0xd000u), 0, 16);
        wram7[t->line_sound_effects_buffer.addr - 0xd000u + cases[i].column - 1u] = cases[i].sound;
        wram0[0] = wram0[1] = 0x55;
        OraclesGuestTextInstantCounts before, after;
        oracles_guest_text_instant_counts(guest, &before);
        for (unsigned f = 0; f < (i ? 2u : 200u); f++) oracles_core_run_frame(core);   /* the boot ROM first */
        oracles_guest_text_instant_counts(guest, &after);
        CHECK(wram0[0] == cases[i].seen && wram0[1] == cases[i].seen_flags);
        /* Nothing is written: the variables are as they were set. */
        CHECK(wram0[t->keys_just_pressed.addr - 0xc000u] == cases[i].just && wram7[t->text_flags.addr - 0xd000u] == cases[i].flags);
        const int pressed = cases[i].on && !cases[i].just, kept = cases[i].seen_flags != cases[i].flags;
        /* Once a call; at most once otherwise, a call the frame before left between its read and its test. */
        CHECK(pressed ? after.presses - before.presses > 100 : after.presses - before.presses <= 1u);
        CHECK(kept ? after.voices_kept - before.voices_kept > 100 : after.voices_kept - before.voices_kept <= 1u);
        CHECK(after.voices_lost == 0);
    }
    CHECK(oracles_guest_text_instant_armed(guest));
    oracles_guest_detach(guest);
    oracles_core_destroy(core);
}

int main(void)
{
    test_rules();
    const OraclesGuestTables *const tables[] = { &oracles_guest_tables_ages, &oracles_guest_tables_seasons, &oracles_guest_tables_moonrise,
                                                 &oracles_guest_tables_temple, &oracles_guest_tables_kinomi };
    for (unsigned i = 0; i < sizeof tables / sizeof tables[0]; i++) test_find(tables[i]);
    static const struct { OraclesGame game; OraclesRomRevision revision; } games[] = {
        { ORACLES_GAME_AGES, ORACLES_ROM_REVISION_AGES_US }, { ORACLES_GAME_SEASONS, ORACLES_ROM_REVISION_SEASONS_US },
        { ORACLES_GAME_AGES, ORACLES_ROM_REVISION_MOONRISE_1_0_6 }, { ORACLES_GAME_SEASONS, ORACLES_ROM_REVISION_TEMPLE_1_073 },
        { ORACLES_GAME_AGES, ORACLES_ROM_REVISION_KINOMI_1_1_2 },
    };
    for (unsigned i = 0; i < sizeof games / sizeof games[0]; i++) {
        test_transaction(ORACLES_CORE_SAMEBOY, games[i].game, games[i].revision);
        test_transaction(ORACLES_CORE_MGBA, games[i].game, games[i].revision);
    }
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("text instant: ok\n");
    return failures ? 1 : 0;
}
