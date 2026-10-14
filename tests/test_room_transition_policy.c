/* The continuous transition policy itself, frame by frame, through synthetic
 * scrolling transitions: room_transition.c built alone, with the few guest
 * calls it makes answered here, so that the policy the guest would install is
 * the one driven.  A transition is a number of frames of the room's load
 * (state 3, the collisions of the room entered given or not), a frame setting
 * the scroll up, a step a frame of the machine's (Link moved $80 or $60 along
 * the axis, the counter decremented), and frames finishing it. */
#include "room_transition.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static OraclesGuestTransitionFn installed;
static OraclesGuestTransitionResetFn installed_reset;
static void *installed_opaque;
const OraclesCompatProfile *oracles_guest_profile(const OraclesGuest *guest) { (void)guest; return NULL; }
int oracles_compat_continuous_transitions(const OraclesCompatProfile *profile) { (void)profile; return 1; }
int oracles_compat_continuous_swim(const OraclesCompatProfile *profile) { (void)profile; return 1; }
int oracles_compat_continuous_diagonal(const OraclesCompatProfile *profile) { (void)profile; return 1; }
void oracles_guest_set_transition_swim(OraclesGuest *guest, int on) { (void)guest; (void)on; }
void oracles_guest_set_transition_policy(OraclesGuest *guest, OraclesGuestTransitionFn policy, OraclesGuestTransitionResetFn reset, void *opaque)
{
    (void)guest;
    installed = policy; installed_reset = reset; installed_opaque = opaque;
}

typedef struct run {
    unsigned dir;
    uint8_t angle;              /* wLinkAngle held */
    int x, y;                   /* 8.8, in the room left */
    int loading, post;          /* frames of the room's load, frames after the scroll */
    const uint8_t *collisions;  /* the room entered's */
    int unknown;                /* the collisions given NULL at every frame */
    int reset_at;               /* a savestate's load before that frame (-1 none) */
    int sea_floor;              /* Ages' sea floor, moving at `move_angle` and speed $28 (a pixel a frame) */
    uint8_t move_angle;
} run;

static uint32_t frame_counter = 100;

/* Link's position at the end, 8.8, unwrapped, in the room left's coordinates. */
static void play(const run *r, int *out_x, int *out_y)
{
    OraclesGuestTransitionState s;
    memset(&s, 0, sizeof s);
    s.game = ORACLES_GAME_AGES; s.game_state = 2; s.cutscene_index = 1; s.scroll_mode = 8;
    s.transition_direction = (uint8_t)r->dir; s.link_object_index = 0xd0; s.link_state = 1; s.link_visible = 0x80;
    s.link_anim_mode = 0x10; s.animation_data_bank = 6; s.link_anim_pointer = 0x4000;
    s.link_input_angle = r->angle; s.link_move_angle = r->sea_floor ? r->move_angle : r->angle; s.tile_types_bank = 0xff;
    if (r->sea_floor) { s.tileset_flags = ORACLES_TILESETFLAG_UNDERWATER; s.link_var2f = 0xc0; s.link_speed = 0x28; }
    int x = r->x, y = r->y;
    const int horizontal = r->dir & 1u, rows = horizontal ? 20 : 16, step = horizontal ? 0x60 : 0x80;
    const int sign = (r->dir == 1u || r->dir == 2u) ? 1 : -1;
    uint16_t camera = 0;
    installed_reset(installed_opaque);
    /* A frame of normal play first, as the policy sees one before any transition. */
    s.transition_state = 2; s.scroll_mode = 1; s.frame = frame_counter++;
    s.link_x = (uint16_t)x; s.link_y = (uint16_t)y;
    {
        OraclesGuestTransitionMutation m;
        memset(&m, 0, sizeof m);
        installed(installed_opaque, &s, &m);
    }
    s.scroll_mode = 8;
    const int frames = r->loading + 1 + rows + r->post;
    for (int f = 0; f < frames; f++) {
        if (f == r->reset_at) installed_reset(installed_opaque);
        int machine = 0;
        s.frame = frame_counter++;
        s.link_anim_counter = 3;
        s.room_collisions = r->unknown ? NULL : r->collisions;
        if (f < r->loading) { s.transition_state = 3; if (f == 0) s.room_collisions = NULL; }
        else if (f == r->loading) {
            s.transition_state = 5; s.transition_substate = horizontal ? 2 : 1; s.transition_phase = 1;
            s.screen_scroll_counter = (uint8_t)rows; s.scroll_alignment = 0; s.screen_scroll_delta = (uint8_t)(sign * 4);
        } else if (f <= r->loading + rows) {
            const int k = f - r->loading;
            s.transition_phase = k < rows ? 2 : 3;
            s.screen_scroll_counter = (uint8_t)(k < rows ? rows - 1 - k : 0);
            s.screen_scroll_delta = (uint8_t)(sign * 8);
            camera = (uint16_t)(camera + sign * 8);
            machine = step;
        } else s.transition_phase = 3;
        if (horizontal) s.camera_x = camera; else s.camera_y = camera;
        if (horizontal) x += sign * machine; else y += sign * machine;
        s.link_x = (uint16_t)x; s.link_y = (uint16_t)y;
        OraclesGuestTransitionMutation m;
        memset(&m, 0, sizeof m);
        installed(installed_opaque, &s, &m);
        x += m.link_x_delta; y += m.link_y_delta;
    }
    *out_x = x; *out_y = y;
    frame_counter += 10;
}

int main(void)
{
    const size_t rom_size = 8u * 0x4000u;
    uint8_t *rom = calloc(rom_size, 1);
    CHECK(rom != NULL);
    if (!rom) return 1;
    OraclesRoomTransition *tr = oracles_room_transition_start((OraclesGuest *)rom, rom, rom_size, 1);
    CHECK(tr != NULL && installed != NULL);
    if (!tr || !installed) return 1;
    CHECK(oracles_room_transition_set_diagonal(tr, 1));
    uint8_t collisions[176];
    int x = 0, y = 0;

    /* Down from y=121 at x=40, straight, twelve frames of load: a pixel a frame up to the game's 16 px, the machine's
     * 8 px of the doubled scroll counted, and beyond them only where nothing is in his way. */
    memset(collisions, 0, sizeof collisions);
    run r = { 2u, 16u, 40 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0 };
    play(&r, &x, &y);
    CHECK(y - (121 << 8) == (12 + 1 + 16 + 2) << 8);          /* a pixel a frame, the room clear */
    /* While the room entered's collisions are not known nothing says what lies beyond: the pixel the game's distance
     * ends on at most. */
    r.unknown = 1;
    play(&r, &x, &y);
    CHECK((y >> 8) == 121 + 16);

    /* A savestate loaded in the transition: the policy hands it to the game to its end, Link moved by the machine alone
     * from there; the next transition is the policy's again. */
    r.unknown = 0; r.reset_at = 20;
    play(&r, &x, &y);
    {
        OraclesRoomTransitionStats stats;
        oracles_room_transition_stats(tr, &stats);
        CHECK(stats.refused_by[10] == 11);                       /* frames 20 to 30 */
    }
    CHECK(y - (121 << 8) == (20 << 8) + 9 * 0x80);             /* 20 px by then, then the machine's last nine steps alone */
    r.reset_at = -1;
    play(&r, &x, &y);
    CHECK(y - (121 << 8) == (12 + 1 + 16 + 2) << 8);

    /* On the sea floor Link glides on with no direction held, at his speed and angle (here down and right, 45 degrees
     * off the axis): the carry goes on with him. */
    r.sea_floor = 1; r.angle = 0x80; r.move_angle = 12; r.loading = 4; r.post = 2;
    play(&r, &x, &y);
    CHECK(y - (121 << 8) == 23 * 0xb5 && x - (40 << 8) == 22 * 0xb5);   /* across from the second frame: the first loads the room */

    /* Beyond the game's distance in whole pixels: from y=121, the room entered's row 1 a wall, which his feet (y+7)
     * reach at y=9 already, the game's own place; past the pixel it ends on, nothing; within it, a fraction further
     * (137.80, his steps of $80 in the scroll). */
    memset(collisions, 0, sizeof collisions);
    for (unsigned c = 0; c < 10u; c++) collisions[1 * 16 + c] = 0x0f;
    r = (run){ 2u, 16u, 40 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0 };
    play(&r, &x, &y);
    CHECK(y == (137 << 8) + 0x80);
    /* Carried down and right, a hole in row 1 under the carried path's tile (y+5) beyond the game's distance and not
     * under the straight path's: the cap holds him where his tile stays clear of it. */
    memset(collisions, 0, sizeof collisions);
    collisions[1 * 16 + 3] = 0x10;
    r = (run){ 2u, 12u, 40 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0 };
    play(&r, &x, &y);
    CHECK(x == 0x3d36 && y == 0x8a31);                             /* x=61 in column 3, y=10: his tile (15) still in row 0 */
    r.angle = 16u;
    play(&r, &x, &y);
    CHECK(x == 40 << 8 && (y >> 8) - 128 == 121 + 31 - 128);      /* straight, column 2: nothing in his way */

    /* With the transitions alone, the cap the routes recorded with them were played with: past the game's distance in
     * 8.8, his middle and 4 px ahead, the machine's steps not counted (the wall of row 1 under his feet from y=9, under
     * y+4 from 12). */
    oracles_room_transition_stop(tr);
    tr = oracles_room_transition_start((OraclesGuest *)rom, rom, rom_size, 0);
    CHECK(tr != NULL);
    if (!tr) return 1;
    memset(collisions, 0, sizeof collisions);
    for (unsigned c = 0; c < 10u; c++) collisions[1 * 16 + c] = 0x0f;
    r = (run){ 2u, 16u, 40 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0 };
    play(&r, &x, &y);
    CHECK(y == (144 << 8) + 0x80);                                 /* 16.5 in the room entered, his feet in the wall */

    if (failures) return 1;
    puts("test_room_transition_policy: ok");
    oracles_room_transition_stop(tr);
    free(rom);
    return 0;
}
