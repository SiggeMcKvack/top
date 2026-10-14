/* The swim's hold (guest.h): the rules that decide a stroke, the search of a stroke's test in a ROM, and the
 * transaction on a synthetic ROM (no game data) on both cores.  At each routine's address of the game's tables, in its
 * bank,
 *   ld d,$d0 ; ld a,(wGameKeysJustPressed) ; and MASK ; ld (RESULT),a ; ret
 * (Ages' linkUpdateVelocity@mermaidSuit with the directions into $c000, both games' flippers' state 0 with A into
 * $c001) called in a loop, so that the results hold what each test saw. */
#include "core.h"
#include "guest.h"
#include "guest_struct_offsets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); } } while (0)

#define BTN_A 0x01u
#define BTN_B 0x02u
#define BTN_RIGHT 0x10u
#define BTN_LEFT 0x20u
#define BTN_UP 0x40u
#define BTN_DOWN 0x80u
#define NO_ANGLE 0xffu    /* wLinkAngle for no direction, and for two opposite ones (@directionButtonToAngle) */
#define ANGLE_RIGHT 0x08u
#define ANGLE_LEFT 0x18u
#define SPEED_120 0x2du   /* objectSpeedTable's row of SPEED_120, times five */

/* Link swimming right at his cap, his stroke spent, two frames from losing a step: a direction held asks nothing. */
static OraclesGuestStrokeState cruising(void)
{
    OraclesGuestStrokeState s = { 0, BTN_RIGHT, ANGLE_RIGHT, ANGLE_RIGHT, SPEED_120, SPEED_120, 2, 5, 0xff };
    return s;
}

static void test_mermaid_rule(void)
{
    OraclesGuestStrokeState s = cruising();
    CHECK(oracles_guest_mermaid_press(&s) == 0);
    s.step_counter = 4;   /* his next frame loses a step: a stroke keeps his speed */
    CHECK(oracles_guest_mermaid_press(&s) == BTN_RIGHT);
    s = cruising(); s.speed = SPEED_120 - 5;   /* below his cap */
    CHECK(oracles_guest_mermaid_press(&s) == BTN_RIGHT);
    s = cruising(); s.input_angle = ANGLE_LEFT;   /* the direction held is not his */
    s.pressed = BTN_LEFT;
    CHECK(oracles_guest_mermaid_press(&s) == BTN_LEFT);
    s = cruising(); s.angle = 0xff; s.speed = 0;   /* standing: the first stroke */
    CHECK(oracles_guest_mermaid_press(&s) == BTN_RIGHT);
    s.pressed = BTN_RIGHT | BTN_A;   /* the other buttons are not directions */
    CHECK(oracles_guest_mermaid_press(&s) == BTN_RIGHT);
    s.pressed = BTN_A;   /* nothing held */
    CHECK(oracles_guest_mermaid_press(&s) == 0);
    s = cruising(); s.speed = 0; s.just_pressed = BTN_RIGHT;   /* a press of the player's: the game's own stroke */
    CHECK(oracles_guest_mermaid_press(&s) == 0);
    s.just_pressed = BTN_A;   /* A pressed, the direction only held */
    CHECK(oracles_guest_mermaid_press(&s) == BTN_RIGHT);
    /* The stroke's counter, decremented before the test of its sign: from 1 to $80 it goes on this frame, from 0 or past
     * $80 it ends or has ended. */
    s = cruising(); s.speed = 0;
    for (unsigned counter = 0; counter < 0x100u; counter++) {
        s.stroke_counter = (uint8_t)counter;
        const int goes_on = counter >= 1u && counter <= 0x80u;
        CHECK(oracles_guest_mermaid_press(&s) == (goes_on ? 0u : BTN_RIGHT));
    }
    /* Two opposite directions give no angle: no stroke, whose steps would only slow Link (with a third direction too). */
    const uint8_t opposite[] = { BTN_LEFT | BTN_RIGHT, BTN_UP | BTN_DOWN, BTN_LEFT | BTN_RIGHT | BTN_UP };
    for (unsigned i = 0; i < sizeof opposite; i++) {
        s = cruising(); s.speed = 0; s.angle = NO_ANGLE; s.pressed = opposite[i]; s.input_angle = NO_ANGLE;
        CHECK(oracles_guest_mermaid_press(&s) == 0);
        s.angle = ANGLE_RIGHT; s.speed = SPEED_120;   /* swimming: none either */
        CHECK(oracles_guest_mermaid_press(&s) == 0);
    }
}

static void test_flippers_rule(void)
{
    CHECK(oracles_guest_flippers_press(0, BTN_A, NO_ANGLE) == BTN_A);             /* held: the next stroke */
    CHECK(oracles_guest_flippers_press(BTN_A, BTN_A, NO_ANGLE) == 0);             /* the player's press: the game's own */
    CHECK(oracles_guest_flippers_press(0, BTN_RIGHT, ANGLE_RIGHT) == BTN_A);      /* a direction held: the same */
    CHECK(oracles_guest_flippers_press(BTN_LEFT, BTN_LEFT, ANGLE_LEFT) == BTN_A); /* a direction newly pressed, A not */
    CHECK(oracles_guest_flippers_press(0, BTN_B, NO_ANGLE) == 0);                 /* neither: Link slows down and stops */
    CHECK(oracles_guest_flippers_press(0, 0, NO_ANGLE) == 0);
    CHECK(oracles_guest_flippers_press(BTN_RIGHT, BTN_RIGHT | BTN_A, ANGLE_RIGHT) == BTN_A);   /* a direction pressed, A held */
    /* Two opposite directions give no angle: no stroke, which would send Link the way he faces; A held still strokes. */
    CHECK(oracles_guest_flippers_press(0, BTN_LEFT | BTN_RIGHT, NO_ANGLE) == 0);
    CHECK(oracles_guest_flippers_press(0, BTN_UP | BTN_DOWN, NO_ANGLE) == 0);
    CHECK(oracles_guest_flippers_press(BTN_LEFT, BTN_LEFT | BTN_RIGHT, NO_ANGLE) == 0);
    CHECK(oracles_guest_flippers_press(0, BTN_UP | BTN_DOWN | BTN_A, NO_ANGLE) == BTN_A);
}

static void put(uint8_t *at, const uint8_t *bytes, size_t count) { memcpy(at, bytes, count); }

static void test_find(void)
{
    const OraclesGuestTables *t = &oracles_guest_tables_ages;
    const size_t size = 1024u * 1024u;
    uint8_t *rom = calloc(size, 1);
    if (!rom) { failures++; return; }
    const OraclesGuestSym routine = t->link_update_velocity_mermaid, keys = t->game_keys_just_pressed;
    const size_t base = oracles_rom_offset(routine.bank, routine.addr);
    const uint8_t test[] = { 0xfa, (uint8_t)keys.addr, (uint8_t)(keys.addr >> 8), 0xe6, ORACLES_GAME_DIRECTIONS };
    CHECK(oracles_guest_find_stroke_test(rom, size, routine, keys, ORACLES_GAME_DIRECTIONS) == 0);   /* none */
    put(rom + base + 0x1b - 3, test, sizeof test);   /* where the game has it: the `and` at $5912 */
    CHECK(oracles_guest_find_stroke_test(rom, size, routine, keys, ORACLES_GAME_DIRECTIONS) == routine.addr + 0x1bu);
    CHECK(oracles_guest_find_stroke_test(rom, size, routine, keys, ORACLES_GAME_BUTTON_A) == 0);   /* another mask */
    put(rom + base + 0x30, test, sizeof test);   /* twice: none taken */
    CHECK(oracles_guest_find_stroke_test(rom, size, routine, keys, ORACLES_GAME_DIRECTIONS) == 0);
    memset(rom + base + 0x30, 0, sizeof test);
    put(rom + base + 0x40, test, sizeof test);   /* past the routine's first $40 bytes: not looked at */
    CHECK(oracles_guest_find_stroke_test(rom, size, routine, keys, ORACLES_GAME_DIRECTIONS) == routine.addr + 0x1bu);
    const OraclesGuestSym absent = { ORACLES_GUEST_ABSENT, 0 };
    CHECK(oracles_guest_find_stroke_test(rom, size, absent, keys, ORACLES_GAME_DIRECTIONS) == 0);   /* Seasons' suit */
    CHECK(oracles_guest_find_stroke_test(rom, base + 0x10, routine, keys, ORACLES_GAME_DIRECTIONS) == 0);   /* an image too short */
    /* The flippers' test is the routine's first instruction's `and`. */
    const OraclesGuestSym flippers = t->link_update_flippers_state0;
    const uint8_t a_test[] = { 0xfa, (uint8_t)keys.addr, (uint8_t)(keys.addr >> 8), 0xe6, ORACLES_GAME_BUTTON_A };
    put(rom + oracles_rom_offset(flippers.bank, flippers.addr), a_test, sizeof a_test);
    CHECK(oracles_guest_find_stroke_test(rom, size, flippers, keys, ORACLES_GAME_BUTTON_A) == flippers.addr + 3u);
    free(rom);
}

static void emit16(uint8_t **at, uint8_t opcode, unsigned value)
{
    *(*at)++ = opcode;
    *(*at)++ = (uint8_t)value;
    *(*at)++ = (uint8_t)(value >> 8);
}

/* The test routine at `routine`: ld d,$d0 ; ld a,(wGameKeysJustPressed) ; and mask ; ld (result),a ; ret. */
static void emit_routine(uint8_t *rom, const OraclesGuestTables *t, OraclesGuestSym routine, uint8_t mask, unsigned result)
{
    uint8_t *at = rom + oracles_rom_offset(routine.bank, routine.addr);
    *at++ = 0x16; *at++ = 0xd0;                              /* ld d,$d0 : Link's object */
    emit16(&at, 0xfa, t->game_keys_just_pressed.addr);       /* ld a,(wGameKeysJustPressed) */
    *at++ = 0xe6; *at++ = mask;                              /* and mask */
    emit16(&at, 0xea, result);                               /* ld (result),a */
    *at = 0xc9;                                              /* ret */
}

static void test_transaction(OraclesCoreKind kind, OraclesGame game)
{
    const OraclesGuestTables *t = game == ORACLES_GAME_AGES ? &oracles_guest_tables_ages : &oracles_guest_tables_seasons;
    const int suit = t->link_update_velocity_mermaid.bank != ORACLES_GUEST_ABSENT;
    const size_t size = 1024u * 1024u;
    uint8_t *rom = calloc(size, 1);
    if (!rom) { failures++; return; }
    memcpy(rom + 0x134, game == ORACLES_GAME_AGES ? "ZELDA NAYRU" : "ZELDA DIN", game == ORACLES_GAME_AGES ? 11 : 9);
    rom[0x143] = 0xc0; rom[0x147] = 0x1b; rom[0x148] = 0x05; rom[0x149] = 0x02;
    rom[0x100] = 0x00; rom[0x101] = 0xc3; rom[0x102] = 0x50; rom[0x103] = 0x01;   /* nop ; jp $0150 */
    uint8_t *at = rom + 0x150;
    emit16(&at, 0x31, 0xc200);                                                   /* ld sp,$c200 */
    *at++ = 0x3e; *at++ = t->link_update_flippers_state0.bank; emit16(&at, 0xea, 0x2000);   /* ld a,bank ; ld ($2000),a */
    uint8_t *loop = at;
    if (suit) emit16(&at, 0xcd, t->link_update_velocity_mermaid.addr);          /* call linkUpdateVelocity@mermaidSuit */
    emit16(&at, 0xcd, t->link_update_flippers_state0.addr);                      /* call linkUpdateFlippersSpeed@flippersState0 */
    *at = 0x18; at[1] = (uint8_t)(loop - (at + 2));                              /* jr loop */
    if (suit) emit_routine(rom, t, t->link_update_velocity_mermaid, ORACLES_GAME_DIRECTIONS, 0xc000);
    emit_routine(rom, t, t->link_update_flippers_state0, ORACLES_GAME_BUTTON_A, 0xc001);

    const OraclesCoreOptions options = { 0, 0, kind, 0 };
    OraclesCore *core = oracles_core_create(rom, size, &options);
    free(rom);
    CHECK(core != NULL);
    if (!core) return;
    const OraclesRomInfo info = { .game = game, .revision = game == ORACLES_GAME_AGES ? ORACLES_ROM_REVISION_AGES_US : ORACLES_ROM_REVISION_SEASONS_US,
                                  .known = 1 };
    OraclesGuest *guest = oracles_guest_attach(core, oracles_compat_find(&info));
    CHECK(guest && oracles_guest_arm_swim_hold(guest) == 0);
    if (!guest) { oracles_core_destroy(core); return; }
    uint8_t *wram0 = oracles_guest_wram_writable(guest, 0), *wram1 = oracles_guest_wram_writable(guest, 1);
    uint8_t *link = wram1 + (ORACLES_OBJECTS_BASE - 0xd000u);
    const struct { uint8_t pressed, just, speed, stroke, seen, seen_a; } cases[] = {
        { BTN_RIGHT, 0, 0, 0xff, BTN_RIGHT, BTN_A },               /* a direction held, Link standing: the suit's stroke, the flippers' */
        { BTN_RIGHT, 0, SPEED_120, 0xff, 0, BTN_A },               /* cruising: none of the suit's */
        { BTN_RIGHT, 0, 0, 3, 0, BTN_A },                          /* a suit's stroke going on: none of the suit's */
        { 0, 0, 0, 0xff, 0, 0 },                                   /* nothing held: none */
        { BTN_LEFT, BTN_LEFT, 0, 0xff, BTN_LEFT, BTN_A },          /* the player's press of a direction: the suit's own, a flippers' stroke */
        { BTN_A, 0, SPEED_120, 0xff, 0, BTN_A },                   /* A held: the flippers' stroke */
        { BTN_A, BTN_A, SPEED_120, 0xff, 0, BTN_A },               /* A pressed: as it is */
        { BTN_LEFT | BTN_RIGHT, 0, 0, 0xff, 0, 0 },                /* two opposite directions: no angle, no stroke */
        { BTN_UP | BTN_DOWN, 0, SPEED_120, 0xff, 0, 0 },
    };
    unsigned mermaid_total = 0, flippers_total = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        wram0[t->game_keys_pressed.addr - 0xc000u] = cases[i].pressed;
        wram0[t->game_keys_just_pressed.addr - 0xc000u] = cases[i].just;
        const uint8_t directions = cases[i].pressed & (BTN_RIGHT | BTN_LEFT | BTN_UP | BTN_DOWN);
        wram0[t->link_angle.addr - 0xc000u] = directions == BTN_LEFT ? ANGLE_LEFT : directions == BTN_RIGHT ? ANGLE_RIGHT : NO_ANGLE;
        link[ORACLES_OBJ_ANGLE] = ANGLE_RIGHT;
        link[ORACLES_OBJ_SPEED] = cases[i].speed;
        wram1[t->link_speed_cap.addr - 0xd000u] = SPEED_120;
        wram1[t->link_speed_step_counter.addr - 0xd000u] = 0;
        wram1[t->link_speed_step_period.addr - 0xd000u] = 5;
        wram1[t->link_stroke_counter.addr - 0xd000u] = cases[i].stroke;
        wram0[0] = wram0[1] = 0x55;
        unsigned mermaid_before = 0, flippers_before = 0, mermaid = 0, flippers = 0;
        oracles_guest_swim_strokes(guest, &mermaid_before, &flippers_before);
        for (unsigned f = 0; f < (i ? 2u : 200u); f++) oracles_core_run_frame(core);   /* the boot ROM first */
        oracles_guest_swim_strokes(guest, &mermaid, &flippers);
        if (suit) CHECK(wram0[0] == cases[i].seen);
        else CHECK(wram0[0] == 0x55 && mermaid == 0);   /* no suit: no such routine called */
        CHECK(wram0[1] == cases[i].seen_a);
        /* Nothing is written: the variables are as they were set. */
        CHECK(wram0[t->game_keys_just_pressed.addr - 0xc000u] == cases[i].just && wram1[t->link_stroke_counter.addr - 0xd000u] == cases[i].stroke);
        const int mermaid_added = suit && cases[i].seen && !(cases[i].just & ORACLES_GAME_DIRECTIONS);
        const int flippers_added = cases[i].seen_a && !(cases[i].just & BTN_A);
        CHECK(mermaid_added ? mermaid - mermaid_before > 100 : mermaid == mermaid_before);   /* once a call */
        CHECK(flippers_added ? flippers - flippers_before > 100 : flippers == flippers_before);
        mermaid_total += mermaid - mermaid_before;
        flippers_total += flippers - flippers_before;
    }
    unsigned mermaid = 0, flippers = 0;
    oracles_guest_swim_strokes(guest, &mermaid, &flippers);
    CHECK(mermaid == mermaid_total && flippers == flippers_total);
    oracles_guest_detach(guest);
    oracles_core_destroy(core);
}

int main(void)
{
    test_mermaid_rule();
    test_flippers_rule();
    test_find();
    test_transaction(ORACLES_CORE_SAMEBOY, ORACLES_GAME_AGES);
    test_transaction(ORACLES_CORE_MGBA, ORACLES_GAME_AGES);
    test_transaction(ORACLES_CORE_SAMEBOY, ORACLES_GAME_SEASONS);
    test_transaction(ORACLES_CORE_MGBA, ORACLES_GAME_SEASONS);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("swim hold: ok\n");
    return failures ? 1 : 0;
}
