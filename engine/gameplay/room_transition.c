#include "room_transition.h"

#include "guest_tables.h"
#include "room_predict.h"

#include <stdlib.h>
#include <string.h>

#define STATE_PREPARE 3u
#define STATE_SCROLL 5u
#define SUBSTATE_VERTICAL 1u
#define SUBSTATE_HORIZONTAL 2u
#define PALETTE_MODE_MIX 8u            /* wPaletteThread_mode: the fade between two palettes of a smooth palette transition */
#define PHASE_SCROLLING 2u              /* wScreenTransitionState3: 0 and 1 set up, 2 scrolls, 3 to 5 finish */
#define COUNTER_HORIZONTAL 0x14u        /* columns of a small room */
#define COUNTER_VERTICAL 0x10u          /* rows */
#define NATIVE_STEP_X 0x60              /* Link's move per scroll step, 8.8 (transitionUpdateScrollAndLinkPosition) */
#define NATIVE_STEP_Y 0x80
#define ONE_PIXEL 0x100
#define NATIVE_TOTAL_X (15 * 0x100)     /* what the game moves Link over a native transition */
#define NATIVE_TOTAL_Y (16 * 0x100)
#define DIAGONAL_STEP 0xb5             /* SPEED_100 at 45 degrees, along each axis (objectSpeedTable) */
#define SMALL_ROOM_W 160
#define SMALL_ROOM_H 128
#define LARGE_ROOM_W 240
#define LARGE_ROOM_H 176
#define SPECIALCOLLISION_HOLE 0x10u     /* specialCollisionValues.s: holes, water and lava alike */
#define SPECIALCOLLISION_VERTICAL_BRIDGE 0x11u   /* $11 to $13: a bridge crossed up and down, and its two sides */
#define SPECIALCOLLISION_VERTICAL_BRIDGE_RIGHT 0x13u
#define SPECIALCOLLISION_STAIRS 0x18u   /* the stairs that slow Link down */
#define SPECIALCOLLISION_HORIZONTAL_BRIDGE 0x19u /* $19 to $1b: a bridge crossed left and right, and its two ends */
#define SPECIALCOLLISION_HORIZONTAL_BRIDGE_RIGHT 0x1bu
#define TILETYPE_WATER 0x07u            /* tileTypes.s */
#define TILETYPE_UPCURRENT 0x12u        /* the four currents, $12 to $15 */
#define TILETYPE_LEFTCURRENT 0x15u
#define TILETYPE_SEAWATER 0x17u         /* Ages: drowns Link without the mermaid suit (checkSwimmingOverSeawater) */
#define TILETYPE_WHIRLPOOL 0x18u        /* Ages */
#define VAR2F_MERMAID_SUIT 0x40u
#define REFUSAL_RESUMED 10              /* OraclesRoomTransitionStats.refused_by */

struct OraclesRoomTransition {
    OraclesGuest *guest;
    const uint8_t *rom;
    size_t rom_size;
    uint32_t last_frame;
    int have_last;
    int armed;                          /* the step is doubled for the scroll in progress */
    uint16_t last_camera;               /* hCamera along the axis at the last frame: the machine's own move shows in it */
    int in_transition;
    int start_pos;                      /* Link's position along the axis when the transition began, 8.8 */
    int start_cross;                    /* and across it: where the straight path runs */
    int doubled;                        /* the transition in progress (or the last one) had its step doubled */
    int palette_pending;                /* a palette transition's mix outlived a doubled scroll: refresh the palettes when it stops */
    int swim;                           /* --continuous-swim: Link swimming at the surface too */
    int forced;                         /* the policy raised the forced transition's bit, not yet taken */
    int resumed;                        /* a savestate loaded without the policy's progress: the transition it was in, if any, is the machine's to its end */
    int continued;                      /* a savestate loaded with it: the next frame follows the one it was saved after */
    int diagonal;                       /* --continuous-diagonal: a diagonal held carries Link across the axis too */
    int straight_known;                 /* start_cross and direction are the transition in progress's, or the last one's */
    unsigned direction;
    int owed_along;                     /* 8.8: what Link went along the axis, a carry asked, while the room entered's collisions were unknown */
    int owed;                           /* 8.8: the carry across that distance, at his own ratio, still to catch up */
    int owed_across;                    /* the direction across it is owed in, -1 none */
    const OraclesTables *data;          /* the ROM's tables the room entered's collisions are predicted from, NULL where the profile does not qualify it */
    int predicted;                      /* the room entered's: 0 not tried yet, -1 not named yet, 1 predicted and not loaded yet, 2 not predicted,
                                           3 loaded, the game's last kept (in the buffers below) */
    uint8_t predicted_layout[ORACLES_ROOM_LAYOUT_BYTES], predicted_collisions[ORACLES_ROOM_LAYOUT_BYTES];
    OraclesRoomTransitionStats stats;
};

/* ---- pure helpers ---------------------------------------------------------------- */

int oracles_room_transition_swimming(const OraclesGuestTransitionState *s)
{
    return s && s->link_swimming_state != 0u;
}

int oracles_room_transition_in_water(const OraclesGuestTransitionState *s)
{
    return s && (s->link_swimming_state != 0u || (s->tileset_flags & ORACLES_TILESETFLAG_UNDERWATER));
}

int oracles_room_transition_refusal(const OraclesGuestTransitionState *s, int swim)
{
    /* The large rooms (the dungeons) keep the game's transition,
     * deliberately: the machine is the same (see collision_free) but a
     * dungeon's rooms are meant to be crossed as the game has it. */
    if (!s || (s->game != ORACLES_GAME_AGES && s->game != ORACLES_GAME_SEASONS) || s->active_group > 5u || s->room_is_large || (s->tileset_flags & ORACLES_TILESETFLAG_SIDESCROLL) || s->game_state != 2u || s->cutscene_index > 1u || s->text_is_active) return 0;
    if (s->transition_state < STATE_PREPARE || s->transition_state > STATE_SCROLL || (s->scroll_mode != 4u && s->scroll_mode != 8u) || s->transition_direction > 3u) return 1;
    if (s->link_force_state != 0u || s->link_object_index != 0xd0u || s->link_id != 0u || s->link_state != 1u) return 2;
    if (!(s->link_visible & 0x80u)) return 3;
    if (s->link_in_air) return 4;
    /* The sea floor (Ages, --continuous-swim): the game has Link walk there, in his mermaid suit's gait. */
    if ((s->tileset_flags & ORACLES_TILESETFLAG_UNDERWATER) && !swim) return 9;
    /* Swimming (--continuous-swim): in his normal swimming state only, not
     * while he enters the water (states 1 and 2) or drowns (state 4, lava). */
    if (s->link_swimming_state) {
        if (!swim || (s->link_swimming_state & 0x4fu) != ORACLES_SWIMMING_NORMAL) return 8;
        if (s->link_anim_mode != ORACLES_LINK_ANIM_MODE_SWIM && s->link_anim_mode != ORACLES_LINK_ANIM_MODE_DIVE) return 3;
    } else if (s->link_anim_mode != ORACLES_LINK_ANIM_MODE_WALK) return 3;
    /* Pushing against the edge as he reaches it (wLinkPushingDirection) is walking. */
    if (s->link_grab_state || s->magnet_glove_state || s->link_immobilized
        || s->link_playing_instrument || s->link_turning_disabled || (s->force_link_push_animation & 0x7fu)) return 5;
    /* Invincibility alone does not block: at the start of play the counter runs from a negative value with Link free to walk. */
    if (s->using_shield || s->pegasus_seed_counter_nonzero || s->link_knockback_counter || s->link_stun_counter || s->active_item) return 6;
    if (s->animation_data_bank == 0xffu) return 7;
    return -1;
}

/* screenTransitionState2@transition (bank1.s), the direction held replaced by
 * the one Link swims in: the checks it makes before it starts a transition.
 * It tries up or down, then left or right, and when both start the second
 * overwrites the first: across a corner the horizontal one is taken. */
#define EDGE 5u                         /* yh or xh at most 5: up or left */
#define TILESETFLAG_OUTDOORS 0x01u
#define TILEINDEX_DEEP_WATER 0xfcu      /* Ages: no transition over it without the mermaid suit (@checkCanTransitionOverWater) */

/* Whether Link moves toward the edge of direction `dir`: his motion's angle
 * (32 steps, turning a step at a time) within 5 steps of the edge's own, the
 * 135 degrees convertLinkAngleToDirectionButtons gives a direction held,
 * centred on it on both sides. */
static int moving_toward(uint8_t angle, unsigned dir)
{
    if (angle & 0x80u) return 0;
    unsigned off = (unsigned)(angle - dir * 8u) & 31u;
    if (off > 16u) off = 32u - off;
    return off <= 5u;
}

/* convertLinkAngleToDirectionButtons' table on the direction held (wLinkAngle, a multiple of 4): bit n, direction n. */
static int held_toward(uint8_t angle, unsigned dir)
{
    static const uint8_t sectors[8] = { 1u, 1u | 2u, 2u, 2u | 4u, 4u, 4u | 8u, 8u, 8u | 1u };
    return !(angle & 0x80u) && (sectors[(angle >> 2) & 7u] & (1u << dir));
}

static int edge_allowed(const OraclesGuestTransitionState *s, unsigned dir)
{
    /* A direction held toward the edge is the game's own case; one held
     * away from it, the player's wish, never overridden.  One across it
     * (left along the bottom) leaves the momentum to carry him. */
    if (held_toward(s->link_input_angle, dir) || held_toward(s->link_input_angle, dir ^ 2u)) return 0;
    if (!moving_toward(s->link_move_angle, dir)) return 0;
    if (s->game == ORACLES_GAME_AGES) {
        /* Ages forbids looping around the overworld, except up. */
        if (s->tileset_flags & TILESETFLAG_OUTDOORS) {
            if (dir == 1u && (s->active_room & 0x0fu) == (unsigned)(s->map_width - 1u)) return 0;
            if (dir == 2u && s->active_room >= (unsigned)(s->map_height - 1u) * 16u) return 0;
            if (dir == 3u && (s->active_room & 0x0fu) == 0u) return 0;
        }
        if (!(s->link_var2f & VAR2F_MERMAID_SUIT) && s->active_tile_index == TILEINDEX_DEEP_WATER) return 0;
    }
    return 1;
}

int oracles_room_transition_swim_edge(const OraclesGuestTransitionState *s)
{
    if (!s || s->transition_state != 2u || s->scroll_mode != 1u || !s->link_swimming_state) return -1;
    /* Link as the policy takes him through a transition: the same checks, the transition supposed started. */
    OraclesGuestTransitionState during = *s;
    during.transition_state = STATE_PREPARE;
    during.scroll_mode = 4u;
    if (oracles_room_transition_refusal(&during, 1) >= 0) return -1;
    if (!s->link_enabled || s->disable_screen_transitions || s->screen_transition_delay) return -1;
    /* A hole under him forbids it; a current (the low bits) lets the game take it without the direction. */
    if (s->hole_or_conveyor) return -1;
    /* A stroke of the flippers (A) carries him on without the direction
     * held: the one case where he swims to the edge and the game waits for
     * it.  The mermaid suit is driven by the direction pads, and a glide
     * after they are let go is left to the game. */
    if (!s->link_speed || (s->link_move_angle & 0x80u) || (s->link_var2f & VAR2F_MERMAID_SUIT) || (s->link_stroke != 1u && s->link_stroke != 2u)) return -1;
    /* The game tests the edge after Link has moved, and puts him back on it
     * whether it starts the transition or not (@transitionUp and the others
     * write his yh or xh before @transition): at the end of a frame he stands
     * a pixel short of the test, 6 across the top or the left, the boundary
     * itself across the bottom or the right.  The forced transition skips
     * that write: it starts from where his move took him, up to a pixel
     * further. */
    const unsigned xh = s->link_x >> 8, yh = s->link_y >> 8;
    if (xh <= EDGE + 1u && edge_allowed(s, 3u)) return 3;
    if (xh > EDGE + 1u && xh >= s->boundary_x && edge_allowed(s, 1u)) return 1;
    if (yh <= EDGE + 1u && edge_allowed(s, 0u)) return 0;
    if (yh > EDGE + 1u && yh >= s->boundary_y && edge_allowed(s, 2u)) return 2;
    return -1;
}

int oracles_room_transition_eligible(const OraclesGuestTransitionState *s, int swim)
{
    return oracles_room_transition_refusal(s, swim) < 0;
}

int oracles_room_transition_step(const OraclesGuestTransitionState *s)
{
    if (!s) return 0;
    if (!oracles_room_transition_in_water(s)) return ONE_PIXEL;
    /* objectSpeedTable (bank3.s): row speed/5, $20 (8.8) a row along an axis. */
    return (s->link_speed / 5) * 0x20;
}

static int rom_byte(const uint8_t *rom, size_t rom_size, unsigned bank, uint16_t address, uint8_t *value)
{
    if (!rom || address < 0x4000u || address >= 0x8000u) return 0;
    const size_t offset = (size_t)bank * 0x4000u + (address - 0x4000u);
    if (offset >= rom_size) return 0;
    *value = rom[offset];
    return 1;
}

int oracles_room_transition_next_walk_frame(const uint8_t *rom, size_t rom_size, const OraclesGuestTransitionState *s,
                                            OraclesGuestTransitionMutation *m)
{
    if (!rom || !s || !m || s->animation_data_bank == 0xffu) return 0;
    uint8_t counter = s->link_anim_counter;
    if (counter) counter--;
    if (counter) {
        /* The current frame still has time: only the counter moves. */
        m->update_walk_animation = 1;
        m->link_anim_counter = counter;
        m->link_anim_parameter = s->link_anim_parameter;
        m->link_anim_pointer = s->link_anim_pointer;
        m->link_animation_frame = s->link_animation_frame;
        return 1;
    }
    uint16_t at = s->link_anim_pointer;
    uint8_t duration = 0;
    if (!rom_byte(rom, rom_size, s->animation_data_bank, at, &duration)) return 0;
    if (duration == 0xffu) {
        /* The end marker: the byte after it is a negative offset, taken from
         * that byte's own address (specialObjectNextAnimationFrame builds
         * bc = $ffXX and adds it to hl). */
        const uint16_t offset_at = (uint16_t)(at + 1u);
        uint8_t offset = 0;
        if (!rom_byte(rom, rom_size, s->animation_data_bank, offset_at, &offset)) return 0;
        at = (uint16_t)(offset_at + (uint16_t)(0xff00u | offset));
        if (!rom_byte(rom, rom_size, s->animation_data_bank, at, &duration)) return 0;
    }
    if (duration == 0u || duration == 0xffu || at > 0x7ffcu) return 0;
    uint8_t frame = 0, parameter = 0;
    if (!rom_byte(rom, rom_size, s->animation_data_bank, (uint16_t)(at + 1u), &frame)
        || !rom_byte(rom, rom_size, s->animation_data_bank, (uint16_t)(at + 2u), &parameter)) return 0;
    m->update_walk_animation = 1;
    m->link_anim_counter = duration;
    m->link_anim_parameter = parameter;
    m->link_anim_pointer = (uint16_t)(at + 3u);
    m->link_animation_frame = frame;
    return 1;
}

static int rom_word(const uint8_t *rom, size_t rom_size, unsigned bank, uint16_t address, uint16_t *value)
{
    uint8_t lo = 0, hi = 0;
    if (!rom_byte(rom, rom_size, bank, address, &lo) || !rom_byte(rom, rom_size, bank, (uint16_t)(address + 1u), &hi)) return 0;
    *value = (uint16_t)(lo | (hi << 8));
    return 1;
}

int oracles_room_transition_tile_type(const uint8_t *rom, size_t rom_size, const OraclesGuestTransitionState *s, uint8_t tile)
{
    /* lookupCollisionTable: the list of wActiveCollisions in tileTypesTable,
     * pairs of tile and type up to a tile $00; a tile not listed is normal. */
    if (!rom || !s || s->tile_types_bank == 0xffu || s->active_collisions > 7u) return -1;
    uint16_t at = 0;
    if (!rom_word(rom, rom_size, s->tile_types_bank, (uint16_t)(s->tile_types_table + 2u * s->active_collisions), &at)) return -1;
    for (unsigned i = 0; i < 128u; i++, at = (uint16_t)(at + 2u)) {
        uint8_t key = 0, type = 0;
        if (!rom_byte(rom, rom_size, s->tile_types_bank, at, &key)) return -1;
        if (key == 0u) return 0;
        if (!rom_byte(rom, rom_size, s->tile_types_bank, (uint16_t)(at + 1u), &type)) return -1;
        if (key == tile) return type;
    }
    return -1;
}

/* Where Link may be moved beyond the game's own distance: on foot, a tile
 * without a collision, and with `ways` stairs and a bridge he crosses along
 * the transition's axis; swimming, water he can swim in, never land: as
 * linkApplyTileTypes decides it, by the tile's type alone (water, a current,
 * Ages' sea with the mermaid suit and its whirlpools), whatever its
 * collision. */
typedef struct tile_rule { const uint8_t *rom; size_t rom_size; int swimming, ways; } tile_rule;

static int tile_allowed(const OraclesGuestTransitionState *s, const tile_rule *r, unsigned index)
{
    if (!r->swimming) {
        const uint8_t collision = s->room_collisions[index];
        if (collision == 0u) return 1;
        if (!r->ways) return 0;
        if (collision == SPECIALCOLLISION_STAIRS) return 1;
        if (s->transition_direction & 1u) return collision >= SPECIALCOLLISION_HORIZONTAL_BRIDGE && collision <= SPECIALCOLLISION_HORIZONTAL_BRIDGE_RIGHT;
        return collision >= SPECIALCOLLISION_VERTICAL_BRIDGE && collision <= SPECIALCOLLISION_VERTICAL_BRIDGE_RIGHT;
    }
    if (!s->room_layout) return 0;
    const int type = oracles_room_transition_tile_type(r->rom, r->rom_size, s, s->room_layout[index]);
    if (type == (int)TILETYPE_WATER || (type >= (int)TILETYPE_UPCURRENT && type <= (int)TILETYPE_LEFTCURRENT)) return 1;
    /* Seasons' table of tile types ends at the currents. */
    return s->game == ORACLES_GAME_AGES
        && ((type == (int)TILETYPE_SEAWATER && (s->link_var2f & VAR2F_MERMAID_SUIT)) || type == (int)TILETYPE_WHIRLPOOL);
}

/* checkTileCollisionAt_allowHoles (bank0.s) on the room entered's collisions, x and y in its coordinates: a value
 * under $10 blocks the quarter of the tile the point falls in (bit 3 the top left, 2 the top right, 1 the bottom left,
 * 0 the bottom right); a special one blocks the columns (values $10 to $17) or the rows ($18 to $1f) its byte in
 * @specialCollisions marks, two pixels a bit from bit 0 (bitTable). */
static int game_wall(const OraclesGuestTransitionState *s, int x, int y)
{
    static const uint8_t special[16] = { 0x00, 0xc3, 0x03, 0xc0, 0x00, 0xc3, 0xc3, 0x00, 0x00, 0xc3, 0x03, 0xc0, 0xc0, 0xc1, 0xff, 0x00 };
    const uint8_t v = s->room_collisions[(y >> 4) * 16 + (x >> 4)];
    if (v < 0x10u) return (v >> (((y & 8) ? 0 : 2) + ((x & 8) ? 0 : 1))) & 1;
    const unsigned index = v & 0x0fu;
    const unsigned coordinate = (unsigned)(index < 8u ? x : y);
    return (special[index] >> ((coordinate >> 1) & 7u)) & 1u;
}

/* Link's coordinates during the transition are those of the room he leaves;
 * finishScrollingTransition rebases them by the room's size along the axis
 * (160x128, or 240x176 for a large room). */
static int entered_pixel(const OraclesGuestTransitionState *s, int xh, int yh, int *ex, int *ey)
{
    const unsigned dir = s->transition_direction;
    const int w = s->room_is_large ? LARGE_ROOM_W : SMALL_ROOM_W, h = s->room_is_large ? LARGE_ROOM_H : SMALL_ROOM_H;
    int dx = 0, dy = 0;
    if (dir == 1u) dx = -w; else if (dir == 3u) dx = w;
    else if (dir == 2u) dy = -h; else dy = h;
    *ex = (xh + dx) & 0xff; *ey = (yh + dy) & 0xff;
    /* Not yet in the room entered along the axis: the room he leaves, walkable where he walked. */
    return *ex < w && *ey < h;
}

static int tile_free(const OraclesGuestTransitionState *s, const tile_rule *r, int xh, int yh)
{
    int ex = 0, ey = 0;
    return !entered_pixel(s, xh, yh, &ex, &ey) || tile_allowed(s, r, (unsigned)((ey >> 4) * 16 + (ex >> 4)));
}

static int tile_wall(const OraclesGuestTransitionState *s, int xh, int yh)
{
    int ex = 0, ey = 0;
    return entered_pixel(s, xh, yh, &ex, &ey) && game_wall(s, ex, ey);
}

/* The two points calculateAdjacentWallsBitset (link.s) tests on each side of Link, by direction: up y-3 at x-3 and
 * x+2, right x+4 at y and y+5, down y+7, left x-5. */
static const int WALL_POINTS[4][2][2] = { { { -3, -3 }, { 2, -3 } }, { { 4, 0 }, { 4, 5 } }, { { -3, 7 }, { 2, 7 } }, { { -5, 0 }, { -5, 5 } } };
#define HAZARD_DY 5                     /* the tile the game takes as Link's own (linkApplyTileTypes, objectCheckIsOverHazard: ld bc,$0500) */

static void along_pixel(const OraclesGuestTransitionState *s, int along, int *xh, int *yh)
{
    const unsigned dir = s->transition_direction;
    int x = s->link_x, y = s->link_y;
    if (dir == 1u) x += along; else if (dir == 3u) x -= along; else if (dir == 2u) y += along; else y -= along;
    *xh = (x >> 8) & 0xff; *yh = (y >> 8) & 0xff;
}

/* His leading edge by direction, on his middle: y-3 up, x+4 right, y+7 down, x-5 left (calculateAdjacentWallsBitset's). */
static const int LEAD_POINT[4][2] = { { 0, -3 }, { 4, 0 }, { 0, 7 }, { -5, 0 } };

/* Link moved `extra` (8.8) along the axis: the tile the game takes as his own and the one his leading edge reaches
 * allowed, swimming that edge in no wall, and none of his points in a wall that it is not in where he stands `from`
 * along (the rails of a bridge he walks on hold his sides all the way). */
static int reachable(const OraclesGuestTransitionState *s, const tile_rule *r, int from, int extra)
{
    const unsigned dir = s->transition_direction;
    int xh = 0, yh = 0, fx = 0, fy = 0;
    along_pixel(s, extra, &xh, &yh);
    along_pixel(s, from, &fx, &fy);
    const int lx = xh + LEAD_POINT[dir][0], ly = yh + LEAD_POINT[dir][1];
    if (!tile_free(s, r, xh, yh + HAZARD_DY) || !tile_free(s, r, lx, ly) || (r->swimming && tile_wall(s, lx, ly))) return 0;
    for (unsigned side = 0; side < 4u; side++)
        for (unsigned i = 0; i < 2u; i++) {
            const int ox = WALL_POINTS[side][i][0], oy = WALL_POINTS[side][i][1];
            if (tile_wall(s, xh + ox, yh + oy) && !tile_wall(s, fx + ox, fy + oy)) return 0;
        }
    return 1;
}

/* The cap of the routes recorded with --continuous-transitions alone: the tile under him and the one his leading edge
 * (4 px off his middle) reaches, on tiles without a collision. */
static int legacy_walkable(const OraclesGuestTransitionState *s, int extra)
{
    if (!s->room_collisions) return 1;
    const tile_rule r = { NULL, 0, 0, 0 };
    const unsigned dir = s->transition_direction;
    int xh = 0, yh = 0;
    along_pixel(s, extra, &xh, &yh);
    if (!tile_free(s, &r, xh, yh)) return 0;
    if (dir == 1u) return tile_free(s, &r, xh + 4, yh);
    if (dir == 3u) return tile_free(s, &r, xh - 4, yh);
    if (dir == 2u) return tile_free(s, &r, xh, yh + 4);
    return tile_free(s, &r, xh, yh - 4);
}

int oracles_room_transition_walkable(const OraclesGuestTransitionState *s, int from, int extra)
{
    if (!s || !s->room_collisions) return 1;
    const tile_rule r = { NULL, 0, 0, 0 };
    return reachable(s, &r, from, extra);
}

int oracles_room_transition_passable(const OraclesGuestTransitionState *s, int from, int extra)
{
    if (!s || !s->room_collisions) return 1;
    const tile_rule r = { NULL, 0, 0, 1 };
    return reachable(s, &r, from, extra);
}

int oracles_room_transition_swimmable(const uint8_t *rom, size_t rom_size, const OraclesGuestTransitionState *s, int from, int extra)
{
    if (!s || !s->room_collisions) return 1;
    const tile_rule r = { rom, rom_size, 1, 0 };
    return reachable(s, &r, from, extra);
}

int oracles_room_transition_held_across(const OraclesGuestTransitionState *s)
{
    if (!s || (s->link_input_angle & 0x80u) || s->transition_direction > 3u) return -1;
    const unsigned off = (unsigned)(s->link_input_angle - s->transition_direction * 8u) & 31u;
    if (off == 4u) return (int)((s->transition_direction + 1u) & 3u);
    if (off == 28u) return (int)((s->transition_direction + 3u) & 3u);
    return -1;
}

/* round(256 * sin(k * 11.25 degrees)), k = 0 to 8: objectSpeedTable's steps of angle */
static const int SINE_256[9] = { 0, 50, 98, 142, 181, 213, 237, 251, 256 };

int oracles_room_transition_moving_across(const OraclesGuestTransitionState *s, int step, int *along, int *across_step)
{
    if (!s || (s->link_move_angle & 0x80u) || s->transition_direction > 3u) return -1;
    const unsigned off = (unsigned)(s->link_move_angle - s->transition_direction * 8u) & 31u;
    const unsigned k = off < 16u ? off : 32u - off;
    if (k == 0u || k >= 8u) return -1;
    if (along) *along = step * SINE_256[8u - k] / 256;
    if (across_step) *across_step = step * SINE_256[k] / 256;
    return (int)((s->transition_direction + (off < 16u ? 1u : 3u)) & 3u);
}

/* A point of Link's box in the room's coordinates, the tile it falls in: along the axis, past the room's edge (the
 * room he leaves, whose collisions are gone, or the one he enters, whose collisions are not loaded yet), its first or
 * last row or column; out of the room across, -1. */
static int point_tile(const OraclesGuestTransitionState *s, int w, int h, int *px, int *py)
{
    if (s->transition_direction & 1u) { if (*py < 0 || *py >= h) return -1; if (*px < 0) *px = 0; if (*px >= w) *px = w - 1; }
    else { if (*px < 0 || *px >= w) return -1; if (*py < 0) *py = 0; if (*py >= h) *py = h - 1; }
    return (*py >> 4) * 16 + (*px >> 4);
}

/* Free where the game finds no wall: on foot no hole or water ($10) either, swimming on water he can swim in. */
static int point_free(const OraclesGuestTransitionState *s, const tile_rule *r, int w, int h, int px, int py)
{
    const int index = point_tile(s, w, h, &px, &py);
    if (index < 0 || game_wall(s, px, py)) return 0;
    return r->swimming ? tile_allowed(s, r, (unsigned)index) : s->room_collisions[index] != SPECIALCOLLISION_HOLE;
}

static int point_wall(const OraclesGuestTransitionState *s, int w, int h, int px, int py)
{
    const int index = point_tile(s, w, h, &px, &py);
    return index < 0 || game_wall(s, px, py);
}

/* What the game would not leave Link on at the point: on foot a hole, water or lava ($10); swimming anything but
 * water he can swim in, land too, where linkApplyTileTypes takes him out of the water. */
static int point_hazard(const OraclesGuestTransitionState *s, const tile_rule *r, int w, int h, int px, int py)
{
    const int index = point_tile(s, w, h, &px, &py);
    if (index < 0) return 1;
    return r->swimming ? !tile_allowed(s, r, (unsigned)index) : s->room_collisions[index] == SPECIALCOLLISION_HOLE;
}

/* Link's 8.8 coordinates in whole pixels of the room left, signed (leftward and upward his coordinate along the axis
 * runs from 5 below zero, $ff.., never as far as -128; any other stays above -16), rebased by the room's size along
 * the axis into the room entered's. */
static void room_pixel(unsigned dir, int w, int h, int x, int y, int *px, int *py)
{
    int cx = (x >> 8) & 0xff, cy = (y >> 8) & 0xff;
    if (cx >= (dir == 3u ? 0x80 : 0xf0)) cx -= 256;
    if (cy >= (dir == 0u ? 0x80 : 0xc0)) cy -= 256;
    if (dir == 1u) cx -= w; else if (dir == 3u) cx += w; else if (dir == 2u) cy -= h; else cy += h;
    *px = cx; *py = cy;
}

/* Link at (cx, cy), 8.8 in the room left's coordinates, carried `offset` (8.8, signed along x or y) off the straight
 * path across the axis, from there to `rest` further along it, a pixel at a time: every point
 * calculateAdjacentWallsBitset tests, and the tile the game takes as his own, clear on the straight path stay clear
 * carried, on the room entered's collisions.  The game resumes its wall tests and its tile's effects (a fall, a
 * drowning) wherever the scroll leaves him, and finishScrollingTransition makes that place his respawn. */
static int carry_keeps_clear(const OraclesGuestTransitionState *s, const tile_rule *r, int cx, int cy, int offset, int rest)
{
    const unsigned dir = s->transition_direction;
    const int w = s->room_is_large ? LARGE_ROOM_W : SMALL_ROOM_W, h = s->room_is_large ? LARGE_ROOM_H : SMALL_ROOM_H;
    const int sx = (dir & 1u) ? cx : cx - offset, sy = (dir & 1u) ? cy - offset : cy;
    const int dx = dir == 1u ? 1 : dir == 3u ? -1 : 0, dy = dir == 2u ? 1 : dir == 0u ? -1 : 0;
    for (int d = 0, last = 0; !last; d += ONE_PIXEL) {
        if (d >= rest) { d = rest; last = 1; }
        int px = 0, py = 0, qx = 0, qy = 0;
        room_pixel(dir, w, h, cx + dx * d, cy + dy * d, &px, &py);
        room_pixel(dir, w, h, sx + dx * d, sy + dy * d, &qx, &qy);
        for (unsigned side = 0; side < 4u; side++)
            for (unsigned i = 0; i < 2u; i++) {
                const int ox = WALL_POINTS[side][i][0], oy = WALL_POINTS[side][i][1];
                if (point_wall(s, w, h, px + ox, py + oy) && !point_wall(s, w, h, qx + ox, qy + oy)) return 0;
            }
        if (point_hazard(s, r, w, h, px, py + HAZARD_DY) && !point_hazard(s, r, w, h, qx, qy + HAZARD_DY)) return 0;
    }
    return 1;
}

/* Where the carry may take Link: his position moved `along` (8.8) in the transition's direction and `delta` in the
 * direction `across`, tested at the two points calculateAdjacentWallsBitset tests on that side.  The two of the side
 * he goes along the axis too, so that the carry never takes his leading corner into a wall the game's own move would
 * then walk him into: a point there free without the carry must stay free with it.  Then over the rest of the game's
 * own distance as well (`rest`, 8.8), all his points against the straight path, `offset` (8.8, signed along x or y)
 * across the axis from where he stands before this carry.  Against the room entered's collisions: none while the
 * transition loads them, where nothing would say where the carry leaves him. */
static int across_allowed(const OraclesGuestTransitionState *s, const tile_rule *r, unsigned across, int delta, int along, int offset, int rest)
{
    if (!s || !s->room_collisions || across > 3u || s->transition_direction > 3u) return 0;
    const unsigned dir = s->transition_direction;
    const int w = s->room_is_large ? LARGE_ROOM_W : SMALL_ROOM_W, h = s->room_is_large ? LARGE_ROOM_H : SMALL_ROOM_H;
    int x = s->link_x, y = s->link_y;
    if (dir == 1u) x += along; else if (dir == 3u) x -= along; else if (dir == 2u) y += along; else y -= along;
    int ax = 0, ay = 0, cx = 0, cy = 0;
    room_pixel(dir, w, h, x, y, &ax, &ay);
    if (across == 1u) x += delta; else if (across == 3u) x -= delta; else if (across == 2u) y += delta; else y -= delta;
    room_pixel(dir, w, h, x, y, &cx, &cy);
    for (unsigned i = 0; i < 2u; i++) {
        if (!point_free(s, r, w, h, cx + WALL_POINTS[across][i][0], cy + WALL_POINTS[across][i][1])) return 0;
        if (!point_free(s, r, w, h, cx + WALL_POINTS[dir][i][0], cy + WALL_POINTS[dir][i][1])
            && point_free(s, r, w, h, ax + WALL_POINTS[dir][i][0], ay + WALL_POINTS[dir][i][1])) return 0;
    }
    return carry_keeps_clear(s, r, x, y, offset + (across == 1u || across == 2u ? delta : -delta), rest > 0 ? rest : 0);
}

int oracles_room_transition_across_free(const OraclesGuestTransitionState *s, unsigned across, int delta, int along, int offset, int rest)
{
    const tile_rule r = { NULL, 0, 0, 0 };
    return across_allowed(s, &r, across, delta, along, offset, rest);
}

int oracles_room_transition_across_swimmable(const uint8_t *rom, size_t rom_size, const OraclesGuestTransitionState *s, unsigned across, int delta,
                                             int along, int offset, int rest)
{
    const tile_rule r = { rom, rom_size, 1, 0 };
    return across_allowed(s, &r, across, delta, along, offset, rest);
}

/* ---- the policy ----------------------------------------------------------------------- */

static uint8_t fast_delta(unsigned dir) { return (dir == 1u || dir == 2u) ? 8u : 0xf8u; }
static uint8_t native_delta(unsigned dir) { return (dir == 1u || dir == 2u) ? 4u : 0xfcu; }

static int in_scroll_substate(const OraclesGuestTransitionState *s)
{
    return s->transition_state == STATE_SCROLL
        && s->transition_substate == ((s->transition_direction & 1u) ? SUBSTATE_HORIZONTAL : SUBSTATE_VERTICAL);
}

static void move_link(OraclesRoomTransition *tr, const OraclesGuestTransitionState *s, OraclesGuestTransitionMutation *m, int machine_moved);

/* transitionUpdateScrollAndLinkPosition and screenTransitionState5's scroll: a step a frame, which draws a row or a
 * column when the camera lands on a multiple of 8 and decrements the counter, the step after it reaches zero ending
 * the scroll; the counter whole until phase 1 draws the first. */
int oracles_room_transition_machine_rest(const OraclesGuestTransitionState *s, int doubled)
{
    if (!s || s->transition_direction > 3u) return 0;
    const int horizontal = s->transition_direction & 1u;
    const int per_step = horizontal ? NATIVE_STEP_X : NATIVE_STEP_Y;
    const int rows = (int)(horizontal ? COUNTER_HORIZONTAL : COUNTER_VERTICAL);
    if (s->transition_state < STATE_SCROLL || !in_scroll_substate(s)) return rows * per_step * (doubled ? 1 : 2);
    if (s->transition_phase > PHASE_SCROLLING) return 0;
    const int counter = s->screen_scroll_counter;
    if (s->transition_phase < PHASE_SCROLLING) return counter * per_step * (doubled ? 1 : 2);
    if (doubled) return (counter + 1) * per_step;
    /* Steps of 4 px: two a row, the one that lands on a multiple of 8 drawing it. */
    const int aligned = ((horizontal ? s->camera_x : s->camera_y) & 7u) == 0;
    return (2 * counter + (aligned ? 2 : 1)) * per_step;
}

static int carry_allowed(const OraclesRoomTransition *tr, const OraclesGuestTransitionState *s, int swimming, unsigned across, int delta, int along, int offset,
                         int rest)
{
    return swimming ? oracles_room_transition_across_swimmable(tr->rom, tr->rom_size, s, across, delta, along, offset, rest)
                    : oracles_room_transition_across_free(s, across, delta, along, offset, rest);
}

/* The room entered's layout and collisions, predicted at the first frame of the transition that names it (wScrollMode
 * 8) before the game has loaded them (room_predict.h), in a room the policy takes over; compared with the game's at the
 * first frame it has them.  A vblank before the game names the room (between the transition's start and wScrollMode
 * 8) leaves the prediction to the next frame of the load. */
static void predict_room(OraclesRoomTransition *tr, const OraclesGuestTransitionState *s, int in_transition)
{
    if (in_transition && tr->predicted <= 0 && oracles_room_transition_refusal(s, tr->swim) != 0) {
        if (s->room_collisions) {
            /* Loaded by the transition's first frame, nothing to predict; after a vblank before the room was named, no
             * prediction was made. */
            if (tr->predicted < 0) { tr->stats.predict_fallbacks++; tr->stats.predict_fallback_by[ORACLES_ROOM_PREDICT_EARLY]++; }
            tr->predicted = 2;
        } else {
            const OraclesRom rom = { tr->rom, tr->rom_size };
            const OraclesRoomPredictVerdict verdict = tr->data ? oracles_room_predict(&rom, tr->data, s, tr->predicted_layout, tr->predicted_collisions)
                                                               : ORACLES_ROOM_PREDICT_UNQUALIFIED;
            if (verdict == ORACLES_ROOM_PREDICT_EARLY) tr->predicted = -1;
            else if (verdict == ORACLES_ROOM_PREDICTED) { tr->predicted = 1; tr->stats.predicted_transitions++; }
            else { tr->predicted = 2; tr->stats.predict_fallbacks++; tr->stats.predict_fallback_by[verdict]++; }
        }
    }
    if (tr->predicted == 1 && s->room_collisions && s->room_layout) {
        if (!memcmp(tr->predicted_collisions, s->room_collisions, sizeof tr->predicted_collisions)
            && !memcmp(tr->predicted_layout, s->room_layout, sizeof tr->predicted_layout)) tr->stats.predicted_matched++;
        else tr->stats.predicted_mismatched++;
        tr->predicted = 2;
    }
    /* Once the game has loaded them, a vblank of state 3 after the load's last frame (the transaction gives no
     * collisions there, wRoomCollisions possibly not the room entered's yet at such a vblank) has the ones it last
     * gave, where the profile qualifies the prediction. */
    if (in_transition && tr->data && tr->predicted >= 0 && s->room_collisions && s->room_layout) {
        memcpy(tr->predicted_collisions, s->room_collisions, sizeof tr->predicted_collisions);
        memcpy(tr->predicted_layout, s->room_layout, sizeof tr->predicted_layout);
        tr->predicted = 3;
    }
}

static void policy(void *opaque, const OraclesGuestTransitionState *s, OraclesGuestTransitionMutation *m)
{
    OraclesRoomTransition *tr = opaque;
    const int horizontal = s->transition_direction & 1u;
    const uint16_t camera = horizontal ? s->camera_x : s->camera_y;
    int camera_moved = 0;
    /* The frames the bus did not ask it about (a vblank it may not write at) are no break. */
    if (!tr->have_last || (!tr->continued && s->frame != tr->last_frame + 1u + s->frames_not_asked)) {
        /* A savestate or a break in the frames: the step byte says whether a doubled scroll is in progress. */
        tr->armed = in_scroll_substate(s) && s->screen_scroll_delta == fast_delta(s->transition_direction);
        if (tr->armed) tr->doubled = 1;
    } else {
        const int moved = (int16_t)(uint16_t)(camera - tr->last_camera);
        camera_moved = moved == 4 || moved == -4 || moved == 8 || moved == -8;
    }
    tr->have_last = 1;
    tr->continued = 0;
    tr->last_frame = s->frame;
    tr->last_camera = camera;

    const int in_transition = s->transition_state >= STATE_PREPARE && s->transition_state <= STATE_SCROLL;
    if (in_transition && !tr->in_transition) {
        tr->start_pos = horizontal ? s->link_x : s->link_y;
        tr->start_cross = horizontal ? s->link_y : s->link_x;
        tr->straight_known = 1;
        tr->direction = s->transition_direction;
        tr->doubled = 0;
        tr->owed_along = tr->owed = 0;
        tr->owed_across = -1;
        tr->predicted = 0;
    }
    /* A smooth palette transition (paletteTransitions.s) mixes BG2-7 from the
     * palette left to the one entered over about thirty frames, the loaded
     * palette header forgotten ($ff) meanwhile.  The game loads the room's
     * palettes at the end of the scroll and the refresh shows them exactly;
     * a doubled scroll ends before the mix does, the load comes first (the
     * header loaded again while the thread still mixes, a state the native
     * order never shows) and the mix's last step (1/16 of the palette left,
     * truncated) stays on screen for the whole room.  The refresh is asked
     * for again once the thread stops, in normal play (the guest's own
     * conditions, so that it is not lost); a warp or a cutscene reloads the
     * palettes itself.  A pending refresh outlives the start of another
     * transition: its scroll waits for the thread and may load nothing. */
    const int normal_play = s->game_state == 2u && s->cutscene_index <= 1u;
    if (!in_transition && tr->doubled && s->palette_thread_mode == PALETTE_MODE_MIX && s->loaded_tileset_palette == s->tileset_palette) tr->palette_pending = 1;
    if (!in_transition && s->palette_thread_mode != PALETTE_MODE_MIX) tr->doubled = 0;   /* only the mix that runs on from the scroll: not a later one of a script */
    if (!normal_play) tr->palette_pending = 0;
    if (tr->palette_pending && !in_transition && s->palette_thread_mode == 0u && s->loaded_tileset_palette == s->tileset_palette) {
        m->refresh_bg_palettes = 0xfcu;
        tr->palette_pending = 0;
        tr->stats.palette_refreshes++;
    }
    tr->in_transition = in_transition;
    if (!in_transition) tr->resumed = 0;
    if (!tr->resumed) predict_room(tr, s, in_transition);

    /* --continuous-swim: the transition a swim's momentum takes him to, the game asking the direction held. */
    if (tr->swim && !in_transition) {
        const int edge = oracles_room_transition_swim_edge(s);
        if (edge >= 0) {
            m->force_transition = 1;
            m->forced_direction = (uint8_t)edge;
            if (!(s->transition_direction_raw & 0x80u)) tr->stats.forced_transitions++;
            tr->forced = 1;
        } else if (tr->forced && (s->transition_direction_raw & 0x80u) && s->transition_state == 2u) {
            m->force_transition = 2;
            tr->forced = 0;
        }
    }
    if (in_transition) tr->forced = 0;
    const int refusal = oracles_room_transition_refusal(s, tr->swim);
    /* A state loaded in a transition without the policy's progress (one saved before it was kept): where it began, and
     * what the policy moved Link by then, are not known; the machine takes him to its end alone, its step as the
     * policy had it, doubled from the scroll's start if the load came before it, so that he goes no further than its
     * own steps from where the load found him. */
    if (tr->resumed && refusal < 0) {
        if (in_scroll_substate(s) && s->transition_phase <= PHASE_SCROLLING && !tr->armed
            && s->screen_scroll_counter == (horizontal ? COUNTER_HORIZONTAL : COUNTER_VERTICAL) && s->scroll_alignment == 0u) {
            m->set_scroll_delta = 1;
            m->scroll_delta = fast_delta(s->transition_direction);
            tr->armed = 1;
            tr->doubled = 1;
        }
        if (in_transition) { tr->stats.refused_frames++; tr->stats.refused_by[REFUSAL_RESUMED]++; }
        return;
    }
    if (refusal >= 0) {
        /* Hands off; a doubled step is given back to the game before the scroll goes on. */
        if (tr->armed && in_scroll_substate(s) && s->transition_phase <= PHASE_SCROLLING) {
            m->set_scroll_delta = 1;
            m->scroll_delta = native_delta(s->transition_direction);
        }
        tr->armed = 0;
        if (s->transition_state >= STATE_PREPARE && s->transition_state <= STATE_SCROLL) { tr->stats.refused_frames++; tr->stats.refused_by[refusal]++; }
        return;
    }

    /* The scroll step, doubled at the first frame of the scroll (the counter
     * still whole, the registers aligned), so that one column or row is drawn
     * per step. */
    if (in_scroll_substate(s) && s->transition_phase <= PHASE_SCROLLING && !tr->armed
        && s->screen_scroll_counter == (horizontal ? COUNTER_HORIZONTAL : COUNTER_VERTICAL) && s->scroll_alignment == 0u) {
        m->set_scroll_delta = 1;
        m->scroll_delta = fast_delta(s->transition_direction);
        tr->armed = 1;
        tr->doubled = 1;
        tr->stats.transitions++;
        if (oracles_room_transition_in_water(s)) tr->stats.swim_transitions++;
    }
    move_link(tr, s, m, camera_moved && in_scroll_substate(s));
}

/* Link walks a pixel a frame, or swims at his own speed: what the machine
 * moved him this frame (it moves the camera and him together, so the
 * camera's move says), topped up; his animation goes on with it. */
static void move_link(OraclesRoomTransition *tr, const OraclesGuestTransitionState *s, OraclesGuestTransitionMutation *m, int machine_moved)
{
    const int horizontal = s->transition_direction & 1u, swimming = oracles_room_transition_swimming(s), in_water = oracles_room_transition_in_water(s);
    /* With --continuous-diagonal Link goes on across the axis too, where the game would hold him for the whole
     * scroll and the view would slide along the axis alone: on foot a diagonal held, as the game walks it, 0xb5 a
     * frame along each axis; in the water his motion, its angle and speed frozen by the game through the scroll
     * and taken up again after it, split along and across as objectSpeedTable splits it. */
    int step = oracles_room_transition_step(s), across_step = 0;
    int across = -1;
    if (tr->diagonal && !in_water) { across = oracles_room_transition_held_across(s); if (across >= 0) step = across_step = DIAGONAL_STEP; }
    /* On the sea floor as at the surface he goes on with no direction held, gliding at his speed and angle
     * (linkState01 moves him either way). */
    else if (tr->diagonal) across = oracles_room_transition_moving_across(s, step, &step, &across_step);
    int extra = step - (machine_moved ? (horizontal ? NATIVE_STEP_X : NATIVE_STEP_Y) : 0);
    /* Up to the distance the game itself moves him (15 px across, 16 px up
     * or down) the tiles are the game's own choice; beyond it, only onto
     * tiles without a collision on foot, onto water swimming, where the
     * machine's own steps still to come leave him, whatever the policy adds
     * later. */
    const int pos = horizontal ? s->link_x : s->link_y;
    const int ahead = (s->transition_direction == 1u || s->transition_direction == 2u) ? pos - tr->start_pos : tr->start_pos - pos;
    /* Leftward or upward his coordinate runs below zero ($00c2 to $ffc2) before the rebase: the difference is signed
     * over 16 bits.  Without --continuous-swim it is taken as it always was, so that the routes recorded with
     * --continuous-transitions alone replay as they were played: there the distance runs negative and the cap never
     * holds Link back, leftward or upward; and the cap is the one they were played with, his middle and his leading
     * edge on the tiles reached, with no regard for the machine's steps to come. */
    const int legacy = !tr->swim && !tr->diagonal;
    const int travelled = legacy ? ahead : (int16_t)(uint16_t)ahead;
    const int native_total = horizontal ? NATIVE_TOTAL_X : NATIVE_TOTAL_Y;
    /* What the machine still moves him after this frame, the scroll doubled from its first frame unless the policy
     * found it under way (the step it arms the scroll with at its start, or hands back with control). */
    const int machine_rest = oracles_room_transition_machine_rest(s, in_scroll_substate(s) ? tr->armed : 1);
    /* Carried off the straight path across the axis (`offset`), only where the carry keeps every point of his as
     * clear as the straight path does. */
    const int offset = (int16_t)(uint16_t)((horizontal ? s->link_y : s->link_x) - tr->start_cross);
    const tile_rule rule = { tr->rom, tr->rom_size, swimming, 0 };
    const int reach = extra + machine_rest;
    const int reach_x = s->link_x + (s->transition_direction == 1u ? reach : s->transition_direction == 3u ? -reach : 0);
    const int reach_y = s->link_y + (s->transition_direction == 2u ? reach : s->transition_direction == 0u ? -reach : 0);
    /* Beyond it in whole pixels: the pixel the game's own distance ends on is its own place, a fraction short or over;
     * never while the room entered's collisions are not loaded, which would say nothing of it. */
    const int forward = s->transition_direction == 1u || s->transition_direction == 2u;
    const int beyond = forward ? ((tr->start_pos + travelled + reach) >> 8) > ((tr->start_pos + native_total) >> 8)
                               : ((tr->start_pos - travelled - reach) >> 8) < ((tr->start_pos - native_total) >> 8);
    if (legacy ? extra > 0 && travelled + extra > native_total && !legacy_walkable(s, extra)
        : extra > 0 && beyond
        && (!s->room_collisions
            || !(swimming ? oracles_room_transition_swimmable(tr->rom, tr->rom_size, s, machine_rest, reach)
                 : tr->swim ? oracles_room_transition_passable(s, machine_rest, reach) : oracles_room_transition_walkable(s, machine_rest, reach))
            || (offset && !carry_keeps_clear(s, &rule, reach_x, reach_y, offset, 0)))) {
        extra = 0;
        tr->stats.capped_frames++;
    }
    /* On foot he stops when he may not go on; swimming, his animation runs on
     * whether he moves or not, as linkUpdateDiving animates it every frame. */
    if (extra < 0) extra = 0;
    /* The doubled scroll moves him half the game's own distance, and in the water his own step may add little to the
     * machine's ($80 or $60) or nothing: what the machine's steps and his own still to come leave of that distance is
     * spread over this frame and the next ones up to the scroll's last step, as far as the game's own distance, its
     * tiles its own choice. */
    if (!legacy && in_water && in_scroll_substate(s) && s->transition_phase == PHASE_SCROLLING && tr->armed) {
        const int per_step = horizontal ? NATIVE_STEP_X : NATIVE_STEP_Y, steps = machine_rest > per_step ? machine_rest / per_step : 1;
        const int short_of = native_total - travelled - extra - machine_rest - steps * (step > per_step ? step - per_step : 0);
        const int add = short_of > 0 ? (short_of + steps - 1) / steps : 0;
        if (add) extra = extra + add < ORACLES_TRANSITION_SWIM_STEP_MAX ? extra + add : ORACLES_TRANSITION_SWIM_STEP_MAX;
    }
    /* Across, onto what he may stand on in the room entered: on foot and on the sea floor a tile without a collision,
     * swimming water. */
    int carried = 0;
    if (across >= 0 && across_step > 0) {
        /* The scroll may leave him anywhere up to the game's own distance, or the machine's steps still to come
         * beyond it; the cap holds him further on. */
        const int rest = native_total - travelled - extra > machine_rest ? native_total - travelled - extra : machine_rest;
        /* While the game loads the room entered, its collisions as predicted, where they are. */
        OraclesGuestTransitionState with_predicted;
        const OraclesGuestTransitionState *c = s;
        if (!s->room_collisions && (tr->predicted == 1 || tr->predicted == 3)) {
            with_predicted = *s;
            with_predicted.room_collisions = tr->predicted_collisions;
            with_predicted.room_layout = tr->predicted_layout;
            c = &with_predicted;
            if (tr->predicted == 1) tr->stats.carry_predicted_frames++;
        }
        if (tr->owed_across != across) { tr->owed_along = tr->owed = 0; tr->owed_across = across; }
        if (!c->room_collisions) {
            /* Nothing says yet where a carry would leave him: what he goes along the axis meanwhile is owed across. */
            tr->owed_along += extra + (machine_moved ? (horizontal ? NATIVE_STEP_X : NATIVE_STEP_Y) : 0);
            tr->stats.carry_withheld_frames++;
            tr->stats.carry_unknown_frames++;
            if (tr->predicted < 0) tr->stats.carry_unnamed_frames++;
        } else {
            /* Once they are known, the carry withheld, that distance at his own ratio across and along, is caught up
             * within the bus's bound on the axis, each step through the same tests; what a wall refuses is owed no
             * longer. */
            if (tr->owed_along && step > 0) tr->owed = tr->owed_along * across_step / step;
            tr->owed_along = 0;
            const int bound = in_water ? ORACLES_TRANSITION_SWIM_STEP_MAX : ORACLES_TRANSITION_WALK_STEP_MAX;
            const int catch_up = tr->owed < bound - across_step ? tr->owed : bound - across_step;
            if (catch_up > 0 && carry_allowed(tr, c, swimming, (unsigned)across, across_step + catch_up, extra, offset, rest)) {
                carried = across_step + catch_up;
                tr->owed -= catch_up;
            } else {
                tr->owed = 0;
                if (carry_allowed(tr, c, swimming, (unsigned)across, across_step, extra, offset, rest)) carried = across_step;
                else tr->stats.carry_withheld_frames++;
            }
        }
    }
    if (!extra && !carried && !swimming) return;
    /* On the sea floor with no direction held the game moves him standing (linkUpdateMovement's c = 2,
     * animateLinkStanding): his walk's animation stays as the transition froze it. */
    if (!(in_water && !swimming && (s->link_input_angle & 0x80u))) {
        OraclesGuestTransitionMutation walk;
        memset(&walk, 0, sizeof walk);
        if (!oracles_room_transition_next_walk_frame(tr->rom, tr->rom_size, s, &walk)) { tr->stats.unanimated_frames++; return; }   /* no animation, no move */
        m->update_walk_animation = 1;
        m->link_anim_counter = walk.link_anim_counter;
        m->link_anim_parameter = walk.link_anim_parameter;
        m->link_anim_pointer = walk.link_anim_pointer;
        m->link_animation_frame = walk.link_animation_frame;
    }
    switch (s->transition_direction) {
    case 1u: m->link_x_delta = (int16_t)extra; break;
    case 3u: m->link_x_delta = (int16_t)-extra; break;
    case 2u: m->link_y_delta = (int16_t)extra; break;
    default: m->link_y_delta = (int16_t)-extra; break;
    }
    if (carried) {
        m->carried = 1;
        switch (across) {
        case 1: m->link_x_delta = (int16_t)carried; break;
        case 3: m->link_x_delta = (int16_t)-carried; break;
        case 2: m->link_y_delta = (int16_t)carried; break;
        default: m->link_y_delta = (int16_t)-carried; break;
        }
        tr->stats.carry_asked_frames++;
    }
    if (!extra) return;
    tr->stats.walked_frames++;
    if (in_water) tr->stats.swum_frames++;
}

static void reset(void *opaque)
{
    OraclesRoomTransition *tr = opaque;
    tr->have_last = 0;
    tr->armed = 0;
    tr->in_transition = 0;
    tr->doubled = 0;
    tr->palette_pending = 0;
    tr->forced = 0;
    tr->resumed = 1;
    tr->continued = 0;
    tr->straight_known = 0;
    tr->owed_along = tr->owed = 0;
    tr->owed_across = -1;
    tr->predicted = 0;
}

#define STATE_VERSION 2u
#define STATE_V1_SIZE 53u                /* a state saved before the prediction: its fields alone */

static uint8_t *put_int(uint8_t *at, int value)
{
    const uint32_t v = (uint32_t)value;
    at[0] = (uint8_t)v; at[1] = (uint8_t)(v >> 8); at[2] = (uint8_t)(v >> 16); at[3] = (uint8_t)(v >> 24);
    return at + 4;
}

static const uint8_t *get_int(const uint8_t *at, int *value)
{
    *value = (int)(int32_t)((uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24));
    return at + 4;
}

size_t oracles_room_transition_save_state(const OraclesRoomTransition *tr, uint8_t *out, size_t capacity)
{
    if (!tr || !out || capacity < ORACLES_ROOM_TRANSITION_STATE_SIZE) return 0;
    uint8_t *at = out;
    *at++ = STATE_VERSION;
    const int fields[] = { tr->armed, tr->last_camera, tr->in_transition, tr->start_pos, tr->start_cross, tr->doubled, tr->palette_pending,
                           tr->forced, tr->straight_known, (int)tr->direction, tr->owed_along, tr->owed, tr->owed_across };
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) at = put_int(at, fields[i]);
    at = put_int(at, tr->predicted);
    memcpy(at, tr->predicted_layout, sizeof tr->predicted_layout);
    at += sizeof tr->predicted_layout;
    memcpy(at, tr->predicted_collisions, sizeof tr->predicted_collisions);
    at += sizeof tr->predicted_collisions;
    return (size_t)(at - out);
}

int oracles_room_transition_load_state(OraclesRoomTransition *tr, const uint8_t *data, size_t size)
{
    /* A state of the first version, saved before the prediction, goes on without one. */
    if (!tr || !data || !((size == ORACLES_ROOM_TRANSITION_STATE_SIZE && data[0] == STATE_VERSION) || (size == STATE_V1_SIZE && data[0] == 1u))) return -1;
    const uint8_t *at = data + 1;
    int last_camera = 0, direction = 0;
    at = get_int(at, &tr->armed); at = get_int(at, &last_camera); at = get_int(at, &tr->in_transition);
    at = get_int(at, &tr->start_pos); at = get_int(at, &tr->start_cross); at = get_int(at, &tr->doubled);
    at = get_int(at, &tr->palette_pending); at = get_int(at, &tr->forced); at = get_int(at, &tr->straight_known);
    at = get_int(at, &direction); at = get_int(at, &tr->owed_along); at = get_int(at, &tr->owed); at = get_int(at, &tr->owed_across);
    tr->predicted = 0;
    if (data[0] == STATE_VERSION) {
        at = get_int(at, &tr->predicted);
        memcpy(tr->predicted_layout, at, sizeof tr->predicted_layout);
        memcpy(tr->predicted_collisions, at + sizeof tr->predicted_layout, sizeof tr->predicted_collisions);
    }
    tr->last_camera = (uint16_t)last_camera;
    tr->direction = (unsigned)direction & 3u;
    tr->resumed = 0;
    tr->have_last = 1;
    tr->continued = 1;
    return 0;
}

OraclesRoomTransition *oracles_room_transition_start(OraclesGuest *guest, const uint8_t *rom, size_t rom_size, int swim)
{
    if (!guest || !rom || !rom_size || !oracles_compat_continuous_transitions(oracles_guest_profile(guest))) return NULL;
    if (swim && !oracles_compat_continuous_swim(oracles_guest_profile(guest))) return NULL;
    OraclesRoomTransition *tr = calloc(1, sizeof *tr);
    if (!tr) return NULL;
    tr->guest = guest;
    tr->rom = rom;
    tr->rom_size = rom_size;
    tr->swim = swim != 0;
    if (oracles_compat_predict_collisions(oracles_guest_profile(guest))) tr->data = oracles_compat_data_tables(oracles_guest_profile(guest));
    oracles_guest_set_transition_swim(guest, tr->swim);
    oracles_guest_set_transition_policy(guest, policy, reset, tr);
    return tr;
}

int oracles_room_transition_set_diagonal(OraclesRoomTransition *tr, int on)
{
    if (!tr) return 0;
    tr->diagonal = on && oracles_compat_continuous_diagonal(oracles_guest_profile(tr->guest));
    return tr->diagonal;
}

void oracles_room_transition_stop(OraclesRoomTransition *tr)
{
    if (!tr) return;
    oracles_guest_set_transition_policy(tr->guest, NULL, NULL, NULL);
    oracles_guest_set_transition_swim(tr->guest, 0);
    free(tr);
}

int oracles_room_transition_straight_cross(const OraclesRoomTransition *tr, unsigned *direction, uint16_t *cross)
{
    if (!tr || !tr->straight_known) return 0;
    if (direction) *direction = tr->direction;
    if (cross) *cross = (uint16_t)tr->start_cross;
    return 1;
}

int oracles_room_transition_water_tile(const uint8_t *rom, size_t rom_size, const OraclesGuestTransitionState *s, unsigned index)
{
    const tile_rule r = { rom, rom_size, 1, 0 };
    return s && index < 176u && tile_allowed(s, &r, index);
}

void oracles_room_transition_stats(const OraclesRoomTransition *tr, OraclesRoomTransitionStats *out)
{
    if (!out) return;
    if (tr) *out = tr->stats; else memset(out, 0, sizeof *out);
}
