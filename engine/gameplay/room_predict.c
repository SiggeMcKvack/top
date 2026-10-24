#include "room_predict.h"

#include <string.h>

#define SCROLL_MODE_LOADED 8u            /* cutscene01 writes it after loadTilesetData and before loadTilesetAndRoomLayout */
#define SMALL_GROUPS 4u                  /* NUM_SMALL_GROUPS: groups 4 to 7 are the dungeons' and the side views' */
#define TILESETFLAG_OUTDOORS 0x01u
#define POLLUTED_TILE 0xebu              /* replacePollutionWithWaterIfPollutionFixed (tileSubstitutions.s, Ages) */
#define WATER_TILE_ABOVE 0xfcu
#define WATER_TILE_BELOW 0x3bu
#define SINGLE_TILE_UNLINKED 0xf0u       /* applySingleTileChanges' masks of Ages' engine */
#define SINGLE_TILE_LINKED 0xf1u
#define SINGLE_TILE_FINISHED 0xf2u
#define STANDARD_LISTS_AGES 6u           /* standardTileSubstitutions: a list per collision mode in Ages, per group in Seasons */
#define STANDARD_LISTS_SEASONS 8u
#define LIST_LIMIT 256u                  /* entries read at most in a list before it is taken as unreadable */

/* applyRoomSpecificTileChangesAfterGfxLoad's routines that draw alone (w3VramTiles, graphics, wInShop,
 * wDiggingUpEnemiesForbidden), by bit of their index; those that write the layout and the collisions only under a
 * condition, the room's flags or a global flag, tested first: the Gasha spot's where something is planted, Ages' $00
 * (dungeon 2's entrance collapsed: bit 7), Seasons' $00 and $01 (the pirate ship docked), $03 (Din's troupe there:
 * bit 6 clear) and $0e (the wagon until the intro is done); any other writes them (roomGfxChanges.s of each game). */
#define AFTER_GFX_DRAWN_AGES 0x029eu     /* $01 to $04 the trees and the shops, $07 Crown Dungeon's entrance, $09 the mermaid statue */
#define AFTER_GFX_DRAWN_SEASONS 0xbef0u  /* $04 to $07 the shops, King Moblin's house, Blaino's gym, Vasu's sign, $09 to $0d the trees, $0f */
#define AFTER_GFX_GASHA 0x08u
#define AFTER_GFX_WING_DUNGEON 0x00u     /* Ages */
#define AFTER_GFX_PIRATE_SHIP_BOW 0x00u  /* Seasons */
#define AFTER_GFX_PIRATE_SHIP_MIDDLE 0x01u
#define AFTER_GFX_DINS_TROUPE 0x03u
#define AFTER_GFX_WAGON 0x0eu
#define AFTER_GFX_ROUTINES_AGES 0x0bu
#define AFTER_GFX_ROUTINES_SEASONS 0x10u

static int read8(const OraclesRom *rom, unsigned bank, unsigned addr, uint8_t *out)
{
    if (addr < 0x4000u || addr >= 0x8000u) return 0;
    return oracles_rom_read8(rom, oracles_rom_offset(bank, addr), out) == ORACLES_OK;
}

static int read16(const OraclesRom *rom, unsigned bank, unsigned addr, unsigned *out)
{
    if (addr < 0x4000u || addr >= 0x7fffu) return 0;
    return oracles_rom_read16le(rom, oracles_rom_offset(bank, addr), out) == ORACLES_OK;
}

static int bound(OraclesSym sym) { return sym.addr != 0; }

static int flag_set(const uint8_t *flags, unsigned size, uint8_t flag)
{
    return flag != 0xffu && (unsigned)(flag >> 3) < size && flags && ((flags[flag >> 3] >> (flag & 7u)) & 1u);
}

/* findRoomSpecificData: the group's list of pairs of room and value, ended by a room $00; -1 when the room is not in
 * it, -2 when the list cannot be read. */
static int room_value(const OraclesRom *rom, OraclesSym table, unsigned group, unsigned room)
{
    unsigned list = 0;
    if (!read16(rom, table.bank, table.addr + 2u * group, &list)) return -2;
    for (unsigned i = 0; i < LIST_LIMIT; i++, list += 2u) {
        uint8_t key = 0, value = 0;
        if (!read8(rom, table.bank, list, &key)) return -2;
        if (key == 0u) return -1;
        if (!read8(rom, table.bank, list + 1u, &value)) return -2;
        if (key == room) return value;
    }
    return -2;
}

/* replaceTiles: pairs of the tile to put and the tile it replaces, everywhere in the room's 176 bytes, ended by $00. */
static int replace_tiles(const OraclesRom *rom, unsigned bank, unsigned list, uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES])
{
    for (unsigned i = 0; i < LIST_LIMIT; i++, list += 2u) {
        uint8_t put = 0, old = 0;
        if (!read8(rom, bank, list, &put)) return 0;
        if (put == 0u) return 1;
        if (!read8(rom, bank, list + 1u, &old)) return 0;
        for (unsigned k = 0; k < ORACLES_ROOM_LAYOUT_BYTES; k++)
            if (layout[k] == old) layout[k] = put;
    }
    return 0;
}

static void replace_pair(uint8_t put, uint8_t old, uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES])
{
    for (unsigned k = 0; k < ORACLES_ROOM_LAYOUT_BYTES; k++)
        if (layout[k] == old) layout[k] = put;
}

/* lookupKey on a dictionary of pairs of key and value ended by a key $00: the tile at `pos` replaced by its value. */
static int replace_by_key(const OraclesRom *rom, OraclesSym dict, unsigned pos, uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES])
{
    if (!bound(dict) || pos >= ORACLES_ROOM_LAYOUT_BYTES) return 0;
    unsigned at = dict.addr;
    for (unsigned i = 0; i < LIST_LIMIT; i++, at += 2u) {
        uint8_t key = 0, value = 0;
        if (!read8(rom, dict.bank, at, &key)) return 0;
        if (key == 0u) return 1;
        if (!read8(rom, dict.bank, at + 1u, &value)) return 0;
        if (key == layout[pos]) { layout[pos] = value; return 1; }
    }
    return 0;
}

/* applySingleTileChanges (commonTileSubstitutions.s): the group's entries of room, mask of the room's flags (in Ages'
 * engine $f0 unlinked, $f1 linked, $f2 the game finished, whose absence ends the list), position and tile. */
static int single_tile_changes(const OraclesRom *rom, const OraclesTables *data, const OraclesGuestTransitionState *s, uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES])
{
    const OraclesSym t = data->single_tile_change_group_table;
    unsigned at = 0;
    if (!read16(rom, t.bank, t.addr + 2u * s->active_group, &at)) return 0;
    for (unsigned i = 0; i < LIST_LIMIT; i++) {
        uint8_t room = 0, mask = 0, pos = 0, tile = 0;
        if (!read8(rom, t.bank, at, &room) || !read8(rom, t.bank, at + 1u, &mask)) return 0;
        int match = room == s->active_room && (mask & s->load.room_flags) != 0;
        if (room == s->active_room && s->game == ORACLES_GAME_AGES) {
            if (mask == SINGLE_TILE_UNLINKED) match = !s->load.is_linked_game;
            else if (mask == SINGLE_TILE_LINKED) match = s->load.is_linked_game != 0;
            else if (mask == SINGLE_TILE_FINISHED) {
                if (s->load.finished_game_flag == 0xffu) return 0;
                if (!flag_set(s->load.global_flags, s->load.global_flags_size, s->load.finished_game_flag)) return 1;
                match = 1;
            }
        }
        if (match) {
            if (!read8(rom, t.bank, at + 2u, &pos) || !read8(rom, t.bank, at + 3u, &tile)) return 0;
            if (pos >= ORACLES_ROOM_LAYOUT_BYTES) return 0;
            layout[pos] = tile;
        } else if (mask == 0u) return 1;
        at += 4u;
    }
    return 0;
}

/* applyStandardTileSubstitutions: for bits 0 to 3 and 7 of the room's flags, the list of that bit for the collision
 * mode (Ages) or the group (Seasons). */
static int standard_substitutions(const OraclesRom *rom, const OraclesTables *data, const OraclesGuestTransitionState *s, uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES])
{
    static const uint8_t bits[5] = { 0, 1, 2, 3, 7 };
    const OraclesSym t = data->standard_tile_substitutions;
    const unsigned lists = s->game == ORACLES_GAME_AGES ? STANDARD_LISTS_AGES : STANDARD_LISTS_SEASONS;
    const unsigned index = s->game == ORACLES_GAME_AGES ? s->active_collisions : s->active_group;
    for (unsigned b = 0; b < 5u; b++) {
        if (!((s->load.room_flags >> bits[b]) & 1u)) continue;
        if (index >= lists) return 0;
        unsigned list = 0;
        if (!read16(rom, t.bank, t.addr + 2u * (b * lists + index), &list) || !replace_tiles(rom, t.bank, list, layout)) return 0;
    }
    return 1;
}

/* replaceOpenedChest and getChestData: the group's entries of position, room and contents (two bytes), ended by a
 * position $ff; a room without one has its opened chest put past the layout. */
static int opened_chest(const OraclesRom *rom, const OraclesTables *data, const OraclesGuestTransitionState *s, uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES])
{
    if (s->load.roomflag_item == 0xffu || s->load.chest_opened_tile == 0xffu) return 0;
    if (!(s->load.room_flags & s->load.roomflag_item)) return 1;
    const OraclesSym t = data->chest_data_group_table;
    unsigned at = 0;
    if (!read16(rom, t.bank, t.addr + 2u * s->active_group, &at)) return 0;
    for (unsigned i = 0; i < LIST_LIMIT; i++, at += 4u) {
        uint8_t pos = 0, room = 0;
        if (!read8(rom, t.bank, at, &pos)) return 0;
        if (pos == 0xffu) return 1;
        if (!read8(rom, t.bank, at + 1u, &room)) return 0;
        if (room != s->active_room) continue;
        if (pos < ORACLES_ROOM_LAYOUT_BYTES) layout[pos] = s->load.chest_opened_tile;
        return 1;
    }
    return 0;
}

OraclesRoomPredictVerdict oracles_room_predict_substitute(const OraclesRom *rom, const OraclesTables *data, const OraclesGuestTransitionState *s,
                                                          uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES])
{
    if (!rom || !data || !s || !layout) return ORACLES_ROOM_PREDICT_UNQUALIFIED;
    const int ages = s->game == ORACLES_GAME_AGES;
    if (ages && flag_set(s->load.global_flags, s->load.global_flags_size, s->load.pollution_fixed_flag) && (s->tileset_flags & TILESETFLAG_OUTDOORS))
        replace_pair((s->tileset_flags & ORACLES_TILESETFLAG_UNDERWATER) ? WATER_TILE_BELOW : WATER_TILE_ABOVE, POLLUTED_TILE, layout);
    if (!single_tile_changes(rom, data, s, layout) || !standard_substitutions(rom, data, s, layout) || !opened_chest(rom, data, s, layout))
        return ORACLES_ROOM_PREDICT_UNREADABLE;
    /* Groups 4 and 5 run the shutters, switches, toggle blocks and Jabu-Jabu's water here: never predicted.  The
     * overworld of Ages then clears the tile under the portal, under a time warp's arrival and under Link sent back. */
    if (ages && s->active_group < 2u) {
        if (s->load.portal_group == s->active_group && s->load.portal_room == s->active_room
            && !replace_by_key(rom, data->timewarp_entry_tile_replacement_dict, s->load.portal_pos, layout)) return ORACLES_ROOM_PREDICT_UNREADABLE;
        if (s->load.timewarp_transition != 0xffu && (s->load.warp_transition & 0x0fu) == s->load.timewarp_transition
            && !replace_by_key(rom, data->timewarp_entry_tile_replacement_dict, s->load.warp_dest_pos, layout)) return ORACLES_ROOM_PREDICT_UNREADABLE;
        if (s->load.link_time_warp_tile && !replace_by_key(rom, data->timewarp_return_tile_replacement_dict, s->load.link_time_warp_tile - 1u, layout))
            return ORACLES_ROOM_PREDICT_UNREADABLE;
    }
    return ORACLES_ROOM_PREDICTED;
}

/* checkIsTempleRemains (Seasons' loadTilesetData.s): rooms $14 to $17 and $24 to $27 of group 0. */
static int temple_remains(unsigned group, unsigned room)
{
    return group == 0u && room >= 0x14u && room - 4u < 0x30u && ((room - 4u) & 0x0fu) < 4u;
}

OraclesRoomPredictVerdict oracles_room_predict_excluded(const OraclesRom *rom, const OraclesTables *data, const OraclesGuestTransitionState *s)
{
    if (!rom || !data || !s) return ORACLES_ROOM_PREDICT_UNQUALIFIED;
    const int ages = s->game == ORACLES_GAME_AGES;
    const unsigned group = s->active_group & 7u, room = s->active_room;
    if (group >= SMALL_GROUPS) return ORACLES_ROOM_PREDICT_LARGE;
    /* applyRoomSpecificTileChanges: every small group in Ages, groups 0, 1 and 3 in Seasons (tileSubstitutions.s). */
    if (ages || group != 2u) {
        const int code = room_value(rom, data->room_tile_changer_code_group_table, group, room);
        if (code == -2) return ORACLES_ROOM_PREDICT_UNREADABLE;
        if (code >= 0) return ORACLES_ROOM_PREDICT_ROOM_CODE;
    }
    const int after = room_value(rom, data->tile_changes_after_gfx_group_table, group, room);
    if (after == -2) return ORACLES_ROOM_PREDICT_UNREADABLE;
    if (after >= 0) {
        const unsigned routines = ages ? AFTER_GFX_ROUTINES_AGES : AFTER_GFX_ROUTINES_SEASONS;
        const unsigned drawn = ages ? AFTER_GFX_DRAWN_AGES : AFTER_GFX_DRAWN_SEASONS;
        if ((unsigned)after >= routines) return ORACLES_ROOM_PREDICT_AFTER_GFX;
        if (after == AFTER_GFX_GASHA) {
            /* getIndexOfGashaSpotInRoom_body: the spot's index in gashaSpotRooms (low bytes), its bit planted or not. */
            const unsigned spots = s->load.gasha_spots_planted_size * 8u;
            unsigned index = 0;
            for (; index < spots; index++) {
                uint8_t at = 0;
                if (!read8(rom, data->gasha_spot_rooms.bank, data->gasha_spot_rooms.addr + index, &at)) return ORACLES_ROOM_PREDICT_UNREADABLE;
                if (at == room) break;
            }
            if (index == spots) return ORACLES_ROOM_PREDICT_UNREADABLE;
            if (flag_set(s->load.gasha_spots_planted, s->load.gasha_spots_planted_size, (uint8_t)index)) return ORACLES_ROOM_PREDICT_AFTER_GFX;
        } else if (ages && after == AFTER_GFX_WING_DUNGEON) {
            if (s->load.room_flags & 0x80u) return ORACLES_ROOM_PREDICT_AFTER_GFX;
        } else if (!ages && (after == AFTER_GFX_PIRATE_SHIP_BOW || after == AFTER_GFX_PIRATE_SHIP_MIDDLE)) {
            if (s->load.pirate_ship_flag == 0xffu || flag_set(s->load.global_flags, s->load.global_flags_size, s->load.pirate_ship_flag)) return ORACLES_ROOM_PREDICT_AFTER_GFX;
        } else if (!ages && after == AFTER_GFX_DINS_TROUPE) {
            if (!(s->load.room_flags & 0x40u)) return ORACLES_ROOM_PREDICT_AFTER_GFX;
        } else if (!ages && after == AFTER_GFX_WAGON) {
            if (s->load.intro_done_flag == 0xffu || !flag_set(s->load.global_flags, s->load.global_flags_size, s->load.intro_done_flag)) return ORACLES_ROOM_PREDICT_AFTER_GFX;
        } else if (!((drawn >> after) & 1u)) return ORACLES_ROOM_PREDICT_AFTER_GFX;
    }
    /* @adjustLoadingRoomForTempleRemains: the lava-filled layouts are other rooms' (Seasons). */
    if (!ages && temple_remains(group, room) && (s->load.temple_lava_flag == 0xffu || flag_set(s->load.global_flags, s->load.global_flags_size, s->load.temple_lava_flag)))
        return ORACLES_ROOM_PREDICT_OTHER_LAYOUT;
    return ORACLES_ROOM_PREDICTED;
}

void oracles_room_predict_collisions(const uint8_t table[ORACLES_TILESET_COLLISIONS_BYTES], const uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES],
                                     int small, uint8_t collisions[ORACLES_ROOM_LAYOUT_BYTES])
{
    for (unsigned i = 0; i < ORACLES_ROOM_LAYOUT_BYTES; i++) collisions[i] = table[layout[i]];
    /* @blankDataAroundCollisions: the sixteenth column; in a small room the ninth row and the eleventh column. */
    for (unsigned row = 0; row < ORACLES_LARGE_ROOM_HEIGHT; row++) {
        collisions[row * 16u + 15u] = 0xffu;
        if (small) collisions[row * 16u + ORACLES_SMALL_ROOM_WIDTH] = 0xffu;
    }
    if (small)
        for (unsigned col = 0; col < 16u; col++) collisions[ORACLES_SMALL_ROOM_HEIGHT * 16u + col] = 0xffu;
}

OraclesRoomPredictVerdict oracles_room_predict(const OraclesRom *rom, const OraclesTables *data, const OraclesGuestTransitionState *s,
                                               uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES], uint8_t collisions[ORACLES_ROOM_LAYOUT_BYTES])
{
    if (!rom || !data || !s || !layout || !collisions || (s->game != ORACLES_GAME_AGES && s->game != ORACLES_GAME_SEASONS)) return ORACLES_ROOM_PREDICT_UNQUALIFIED;
    if (!bound(data->room_layout_group_table) || !bound(data->tileset_layout_table) || !bound(data->single_tile_change_group_table)
        || !bound(data->standard_tile_substitutions) || !bound(data->chest_data_group_table) || !bound(data->room_tile_changer_code_group_table)
        || !bound(data->tile_changes_after_gfx_group_table) || !bound(data->gasha_spot_rooms)) return ORACLES_ROOM_PREDICT_UNQUALIFIED;
    if (s->scroll_mode != SCROLL_MODE_LOADED) return ORACLES_ROOM_PREDICT_EARLY;
    const OraclesRoomPredictVerdict excluded = oracles_room_predict_excluded(rom, data, s);
    if (excluded != ORACLES_ROOM_PREDICTED) return excluded;
    /* loadRoomLayout: the 192 bytes cleared, the room of wLoadingRoom (wActiveRoom) decoded in wTilesetLayoutGroup's table. */
    int large = 0;
    memset(layout, 0, ORACLES_ROOM_LAYOUT_BYTES);
    if (oracles_decode_room_layout(rom, data, s->load.tileset_layout_group & 7u, s->active_room, layout, &large) != ORACLES_OK) return ORACLES_ROOM_PREDICT_UNREADABLE;
    if (large) return ORACLES_ROOM_PREDICT_LARGE;
    const OraclesRoomPredictVerdict substituted = oracles_room_predict_substitute(rom, data, s, layout);
    if (substituted != ORACLES_ROOM_PREDICTED) return substituted;
    /* loadTilesetLayout: w3TileCollisions, the tileset layout's own (the one loaded already when it is the same). */
    uint8_t table[ORACLES_TILESET_COLLISIONS_BYTES];
    int have = 0;
    if (data->num_tileset_layouts && s->load.tileset_layout >= data->num_tileset_layouts) return ORACLES_ROOM_PREDICT_UNREADABLE;
    if (oracles_decode_tileset_layout(rom, data, s->load.tileset_layout, NULL, table, NULL, &have) != ORACLES_OK || !have) return ORACLES_ROOM_PREDICT_UNREADABLE;
    oracles_room_predict_collisions(table, layout, 1, collisions);
    return ORACLES_ROOM_PREDICTED;
}

const char *oracles_room_predict_verdict_name(OraclesRoomPredictVerdict verdict)
{
    static const char *const names[ORACLES_ROOM_PREDICT_VERDICTS] = { "predicted", "image not qualified", "the room not named yet", "a large room or a dungeon's group",
                                                                      "the room's own code", "its own code after the graphics", "another room's layout", "a table unreadable" };
    return (unsigned)verdict < ORACLES_ROOM_PREDICT_VERDICTS ? names[verdict] : "?";
}
