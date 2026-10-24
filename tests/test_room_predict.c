/* The prediction of the collisions of the room a scroll enters, on the synthetic tables of room_predict_fixture.h:
 * the collisions of a layout, each substitution of applyAllTileSubstitutions, the rooms left to the game's load, and
 * the whole prediction. */
#include "room_predict.h"
#include "room_predict_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t global_flags[0x10];
static uint8_t gasha_planted[2];

/* A frame of the load of room 0:12 of Ages: wScrollMode 8, collision mode 0, the overworld's flags. */
static OraclesGuestTransitionState loading(void)
{
    OraclesGuestTransitionState s;
    memset(&s, 0, sizeof s);
    s.game = ORACLES_GAME_AGES;
    s.scroll_mode = 8; s.transition_state = 3;
    s.active_group = 0; s.active_room = 0x12; s.tileset_flags = 0x01;
    s.load.global_flags = global_flags; s.load.global_flags_size = sizeof global_flags;
    s.load.gasha_spots_planted = gasha_planted; s.load.gasha_spots_planted_size = sizeof gasha_planted;
    s.load.chest_opened_tile = 0xf0; s.load.roomflag_item = 0x20; s.load.timewarp_transition = 0x06;
    s.load.pollution_fixed_flag = 0x30; s.load.finished_game_flag = 0x14; s.load.temple_lava_flag = 0xff;
    s.load.pirate_ship_flag = 0x40; s.load.intro_done_flag = 0x0a;
    s.load.portal_group = 0xff;
    return s;
}

static int count(const uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES], uint8_t tile)
{
    int n = 0;
    for (unsigned i = 0; i < ORACLES_ROOM_LAYOUT_BYTES; i++) n += layout[i] == tile;
    return n;
}

int main(void)
{
    uint8_t *rom_bytes = calloc(FIXTURE_ROM_SIZE, 1);
    CHECK(rom_bytes != NULL);
    if (!rom_bytes) return 1;
    const OraclesRom rom = { rom_bytes, FIXTURE_ROM_SIZE };
    fixture f;
    uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES], collisions[ORACLES_ROOM_LAYOUT_BYTES], table[ORACLES_TILESET_COLLISIONS_BYTES];

    /* loadRoomCollisions: each tile's collision; past a small room's row 7 and column 9, and the sixteenth column, $ff. */
    memset(table, 0, sizeof table);
    table[0x3a] = 0x0f;
    memset(layout, 0, sizeof layout);
    layout[0x23] = 0x3a;
    oracles_room_predict_collisions(table, layout, 1, collisions);
    CHECK(collisions[0x23] == 0x0f && collisions[0x22] == 0 && collisions[0x79] == 0);
    CHECK(collisions[0x0a] == 0xff && collisions[0xaa] == 0xff && collisions[0x0f] == 0xff && collisions[0xaf] == 0xff);
    CHECK(collisions[0x80] == 0xff && collisions[0x8e] == 0xff && collisions[0x90] == 0 && collisions[0xa9] == 0);
    oracles_room_predict_collisions(table, layout, 0, collisions);
    CHECK(collisions[0x0a] == 0 && collisions[0x80] == 0 && collisions[0x0f] == 0xff);

    /* The pollution (Ages): $eb outdoors becomes $fc, $3b under water, once the flag is set. */
    fixture_init(&f, rom_bytes);
    OraclesGuestTransitionState s = loading();
    memset(layout, 0, sizeof layout);
    layout[0x11] = 0xeb; layout[0x45] = 0xeb;
    CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && count(layout, 0xeb) == 2);
    global_flags[0x30 >> 3] = 1u << (0x30 & 7u);
    CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && layout[0x11] == 0xfc && layout[0x45] == 0xfc);
    layout[0x11] = 0xeb; s.tileset_flags = 0x41;
    CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && layout[0x11] == 0x3b);
    layout[0x11] = 0xeb; s.tileset_flags = 0x00;   /* indoors */
    CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && layout[0x11] == 0xeb);
    s.game = ORACLES_GAME_SEASONS; s.tileset_flags = 0x01;   /* Seasons has no pollution */
    CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && layout[0x11] == 0xeb);
    memset(global_flags, 0, sizeof global_flags);

    /* Single tile changes: the room's flags against the mask; in Ages' engine $f0 unlinked, $f1 linked, and $f2 the game
     * finished, whose absence ends the list. */
    {
        const uint8_t list[] = { 0x13, 0x40, 0x22, 0xd7,   /* another room */
                                 0x12, 0x40, 0x23, 0xd7,   /* bit 6 of the room's flags */
                                 0x12, 0xf0, 0x24, 0x64,   /* unlinked */
                                 0x12, 0xf1, 0x25, 0x65,   /* linked */
                                 0x12, 0xf2, 0x26, 0xf2,   /* finished: the end of the list unless set */
                                 0x12, 0x01, 0x27, 0x3a,
                                 0x00, 0x00 };
        fixture_init(&f, rom_bytes);
        fixture_group_lists(&f, f.tables.single_tile_change_group_table.addr, (const uint8_t[]){ 0, 0 }, 2, 0, list, sizeof list);
        s = loading();
        s.load.room_flags = 0x41;
        memset(layout, 0, sizeof layout);
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED);
        CHECK(layout[0x22] == 0 && layout[0x23] == 0xd7 && layout[0x24] == 0x64 && layout[0x25] == 0 && layout[0x26] == 0 && layout[0x27] == 0);
        global_flags[0x14 >> 3] = 1u << (0x14 & 7u);
        s.load.is_linked_game = 1;
        memset(layout, 0, sizeof layout);
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED);
        CHECK(layout[0x24] == 0 && layout[0x25] == 0x65 && layout[0x26] == 0xf2 && layout[0x27] == 0x3a);
        /* Seasons reads every mask as one of the room's flags. */
        s.game = ORACLES_GAME_SEASONS; s.load.room_flags = 0x10;
        memset(layout, 0, sizeof layout);
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED);
        CHECK(layout[0x23] == 0 && layout[0x24] == 0x64 && layout[0x25] == 0x65 && layout[0x26] == 0xf2 && layout[0x27] == 0);
        memset(global_flags, 0, sizeof global_flags);
        /* A position past the room's 176 bytes: unreadable. */
        const uint8_t past_list[] = { 0x12, 0x01, 0xc0, 0x3a, 0x00, 0x00 };
        fixture_group_lists(&f, f.tables.single_tile_change_group_table.addr, (const uint8_t[]){ 0, 0 }, 2, 0, past_list, sizeof past_list);
        s = loading(); s.load.room_flags = 0x01;
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICT_UNREADABLE);
    }

    /* Standard substitutions: each of bits 0 to 3 and 7 of the room's flags replaces its list's tiles, the list of the
     * collision mode in Ages, of the group in Seasons. */
    {
        fixture_init(&f, rom_bytes);
        const uint8_t bit1[] = { 0x34, 0x30, 0x35, 0x31, 0x00 }, bit7[] = { 0xa0, 0x1e, 0x00 };
        const unsigned at1 = fixture_bytes(&f, bit1, sizeof bit1), at7 = fixture_bytes(&f, bit7, sizeof bit7);
        fixture_word(&f, f.tables.standard_tile_substitutions.addr + 2u * (1u * 6u + 2u), at1);   /* Ages: bit 1, collision mode 2 */
        fixture_word(&f, f.tables.standard_tile_substitutions.addr + 2u * (4u * 6u + 2u), at7);   /* bit 7 */
        s = loading();
        s.active_collisions = 2; s.load.room_flags = 0x82;
        memset(layout, 0, sizeof layout);
        layout[0x05] = 0x30; layout[0x06] = 0x31; layout[0x15] = 0x30; layout[0x40] = 0x1e;
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED);
        CHECK(layout[0x05] == 0x34 && layout[0x06] == 0x35 && layout[0x15] == 0x34 && layout[0x40] == 0xa0);
        s.load.room_flags = 0x02; s.active_collisions = 3;
        memset(layout, 0, sizeof layout);
        layout[0x05] = 0x30;
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && layout[0x05] == 0x30);
        /* Seasons: by the group, eight lists a bit. */
        fixture_word(&f, f.tables.standard_tile_substitutions.addr + 2u * (1u * 8u + 3u), at1);
        s.game = ORACLES_GAME_SEASONS; s.active_group = 3; s.active_collisions = 0;
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && layout[0x05] == 0x34);
        s.game = ORACLES_GAME_AGES; s.active_collisions = 6;   /* past Ages' six lists */
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICT_UNREADABLE);
    }

    /* The opened chest: the room's flag, at the chest's position for this room, past the layout where the group lists none. */
    {
        fixture_init(&f, rom_bytes);
        const uint8_t chests[] = { 0x33, 0x11, 0x28, 0x00, 0x47, 0x12, 0x29, 0x01, 0xff };
        fixture_group_lists(&f, f.tables.chest_data_group_table.addr, (const uint8_t[]){ 0xff }, 1, 0, chests, sizeof chests);
        s = loading();
        memset(layout, 0, sizeof layout);
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && count(layout, 0xf0) == 0);
        s.load.room_flags = 0x20;
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && layout[0x47] == 0xf0 && count(layout, 0xf0) == 1);
        s.active_room = 0x14;
        memset(layout, 0, sizeof layout);
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && count(layout, 0xf0) == 0);
    }

    /* Ages' overworld: the tile under the portal and under a time warp's arrival (the entry's dictionary), and under
     * Link sent back (the return's). */
    {
        fixture_init(&f, rom_bytes);
        const uint8_t entry[] = { 0xc5, 0x3a, 0x04, 0x3a, 0x00 }, back[] = { 0xc0, 0x3a, 0xf2, 0x3a, 0x00 };
        for (unsigned i = 0; i < sizeof entry; i++) fixture_put(&f, f.tables.timewarp_entry_tile_replacement_dict.addr + i, entry[i]);
        for (unsigned i = 0; i < sizeof back; i++) fixture_put(&f, f.tables.timewarp_return_tile_replacement_dict.addr + i, back[i]);
        s = loading();
        memset(layout, 0, sizeof layout);
        layout[0x34] = 0xc5; layout[0x35] = 0x04; layout[0x36] = 0xf2; layout[0x37] = 0xc5;
        s.load.portal_group = 0; s.load.portal_room = 0x12; s.load.portal_pos = 0x34;
        s.load.warp_transition = 0x86; s.load.warp_dest_pos = 0x35;
        s.load.link_time_warp_tile = 0x37;   /* position $36, plus one */
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED);
        CHECK(layout[0x34] == 0x3a && layout[0x35] == 0x3a && layout[0x36] == 0x3a && layout[0x37] == 0xc5);
        /* Another room's portal, a warp that is no time warp, a tile neither dictionary holds: nothing. */
        memset(layout, 0, sizeof layout);
        layout[0x34] = 0xc5; layout[0x35] = 0x04; layout[0x36] = 0xc5;
        s.load.portal_room = 0x13; s.load.warp_transition = 0x05;
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED);
        CHECK(layout[0x34] == 0xc5 && layout[0x35] == 0x04 && layout[0x36] == 0xc5);
        /* Groups 2 and 3, and Seasons: none of them. */
        s.load.portal_room = 0x12; s.load.warp_transition = 0x06; s.active_group = 2; s.load.portal_group = 2;
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && layout[0x34] == 0xc5 && layout[0x35] == 0x04);
        s.active_group = 0; s.load.portal_group = 0; s.game = ORACLES_GAME_SEASONS;
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICTED && layout[0x34] == 0xc5 && layout[0x35] == 0x04);
        /* A position past the room: unreadable. */
        s.game = ORACLES_GAME_AGES; s.load.portal_pos = 0xc4;
        CHECK(oracles_room_predict_substitute(&rom, &f.tables, &s, layout) == ORACLES_ROOM_PREDICT_UNREADABLE);
    }

    /* The rooms left to the game's load: their own code before the collisions (every small group in Ages, groups 0, 1
     * and 3 in Seasons); after the graphics, but where it only draws, or a Gasha spot with nothing planted. */
    {
        fixture_init(&f, rom_bytes);
        const uint8_t code[] = { 0x12, 0x08, 0x00 }, after[] = { 0x43, 0x01, 0x44, 0x06, 0x45, 0x08, 0x46, 0x0b, 0x00 };
        fixture_group_lists(&f, f.tables.room_tile_changer_code_group_table.addr, (const uint8_t[]){ 0 }, 1, 2, code, sizeof code);
        fixture_group_lists(&f, f.tables.tile_changes_after_gfx_group_table.addr, (const uint8_t[]){ 0 }, 1, 0, after, sizeof after);
        const uint8_t spots[] = { 0x05, 0x2c, 0x45, 0x7b };
        for (unsigned i = 0; i < sizeof spots; i++) fixture_put(&f, f.tables.gasha_spot_rooms.addr + i, spots[i]);
        s = loading();
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
        s.active_group = 2;
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_ROOM_CODE);
        s.game = ORACLES_GAME_SEASONS;   /* Seasons' group 2 runs no room code */
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
        s.active_group = 4;
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_LARGE);
        s = loading();
        s.active_room = 0x43;   /* Ages' $01: a tree's graphics */
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
        s.active_room = 0x44;   /* $06: the Maku tree's puddles, in the layout */
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_AFTER_GFX);
        s.game = ORACLES_GAME_SEASONS;   /* Seasons' $06: Blaino's gloves, drawn */
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
        s.active_room = 0x46;   /* $0b in Seasons, a tree drawn; past Ages' routines */
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
        s.game = ORACLES_GAME_AGES;
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_AFTER_GFX);
        s.active_room = 0x45;   /* the Gasha spot of index 2 */
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
        gasha_planted[0] = 0x04;
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_AFTER_GFX);
        gasha_planted[0] = 0;
        /* Those that change the layout under a condition: tested first. */
        {
            const uint8_t conditional[] = { 0x47, 0x00, 0x48, 0x01, 0x49, 0x03, 0x4a, 0x0e, 0x00 };
            fixture_group_lists(&f, f.tables.tile_changes_after_gfx_group_table.addr, (const uint8_t[]){ 0 }, 1, 0, conditional, sizeof conditional);
            s = loading();
            s.active_room = 0x47;   /* Ages' $00: dungeon 2's entrance, collapsed once bit 7 is set */
            CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
            s.load.room_flags = 0x80;
            CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_AFTER_GFX);
            s.game = ORACLES_GAME_SEASONS; s.load.room_flags = 0;   /* Seasons' $00 and $01: the pirate ship, once docked */
            CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
            s.active_room = 0x48;
            CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
            global_flags[0x40 >> 3] = 1u << (0x40 & 7u);
            CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_AFTER_GFX);
            s.active_room = 0x49;   /* $03: Din's troupe's tents while bit 6 is clear */
            CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_AFTER_GFX);
            s.load.room_flags = 0x40;
            CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
            s.active_room = 0x4a;   /* $0e: the wagon until the intro is done */
            CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_AFTER_GFX);
            global_flags[0x0a >> 3] |= 1u << (0x0a & 7u);
            CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
            memset(global_flags, 0, sizeof global_flags);
        }
        /* Seasons' Temple Remains filled with lava: its layouts are other rooms'. */
        s = loading();
        s.game = ORACLES_GAME_SEASONS; s.load.temple_lava_flag = 0x15; s.active_room = 0x25;
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
        global_flags[0x15 >> 3] = 1u << (0x15 & 7u);
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICT_OTHER_LAYOUT);
        s.active_room = 0x28;
        CHECK(oracles_room_predict_excluded(&rom, &f.tables, &s) == ORACLES_ROOM_PREDICTED);
        memset(global_flags, 0, sizeof global_flags);
    }

    /* The whole prediction: the room's layout from its group's table, its substitutions, and the collisions of the
     * tileset layout named. */
    {
        fixture_init(&f, rom_bytes);
        fixture_room_tile(&f, 0x12, 3, 2, 0x3a);
        fixture_room_tile(&f, 0x12, 9, 7, 0x30);
        fixture_collision(&f, 0x3a, 0x0f);
        fixture_collision(&f, 0x34, 0x05);
        fixture_collision(&f, 0x00, 0x00);
        const uint8_t bit0[] = { 0x34, 0x30, 0x00 };
        fixture_word(&f, f.tables.standard_tile_substitutions.addr, fixture_bytes(&f, bit0, sizeof bit0));
        s = loading();
        s.load.room_flags = 0x01;
        CHECK(oracles_room_predict(&rom, &f.tables, &s, layout, collisions) == ORACLES_ROOM_PREDICTED);
        CHECK(layout[0x23] == 0x3a && layout[0x79] == 0x34 && count(layout, 0) == ORACLES_ROOM_LAYOUT_BYTES - 2);
        CHECK(collisions[0x23] == 0x0f && collisions[0x79] == 0x05 && collisions[0x22] == 0 && collisions[0x8a] == 0xff && collisions[0x9a] == 0xff);
        /* The room not named yet, a group of large rooms, an image whose tables are not bound, a tileset layout past the table. */
        s.scroll_mode = 4;
        CHECK(oracles_room_predict(&rom, &f.tables, &s, layout, collisions) == ORACLES_ROOM_PREDICT_EARLY);
        s.scroll_mode = 8; s.active_group = 5;
        CHECK(oracles_room_predict(&rom, &f.tables, &s, layout, collisions) == ORACLES_ROOM_PREDICT_LARGE);
        s.active_group = 0;
        OraclesTables unbound = f.tables;
        unbound.tileset_layout_table = (OraclesSym){ 0, 0 };
        CHECK(oracles_room_predict(&rom, &unbound, &s, layout, collisions) == ORACLES_ROOM_PREDICT_UNQUALIFIED);
        s.game = (OraclesGame)99;
        CHECK(oracles_room_predict(&rom, &f.tables, &s, layout, collisions) == ORACLES_ROOM_PREDICT_UNQUALIFIED);
        s.game = ORACLES_GAME_AGES; s.load.tileset_layout = 64;
        CHECK(oracles_room_predict(&rom, &f.tables, &s, layout, collisions) == ORACLES_ROOM_PREDICT_UNREADABLE);
        /* A large room's group table (mode 0): left to the game. */
        s.load.tileset_layout = 0; s.load.tileset_layout_group = 3;
        fixture_put(&f, 0x4000u + 8u * 3u, 0);
        CHECK(oracles_room_predict(&rom, &f.tables, &s, layout, collisions) != ORACLES_ROOM_PREDICTED);
    }
    CHECK(!strcmp(oracles_room_predict_verdict_name(ORACLES_ROOM_PREDICTED), "predicted"));
    CHECK(!strcmp(oracles_room_predict_verdict_name(ORACLES_ROOM_PREDICT_VERDICTS), "?"));

    free(rom_bytes);
    if (failures) return 1;
    puts("test_room_predict: ok");
    return 0;
}
