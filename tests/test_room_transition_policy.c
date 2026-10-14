/* The continuous transition policy itself, frame by frame, through synthetic
 * scrolling transitions: room_transition.c built alone, with the few guest
 * calls it makes answered here, so that the policy the guest would install is
 * the one driven.  A transition is a number of frames of the room's load
 * (state 3, the collisions of the room entered given or not), the frame the
 * scroll is set up on (phase 1, its step the game's), the frame drawing its
 * first row, its steps a frame each (Link moved $80 or $60 along the axis,
 * the camera by the step the policy leaves in wcd14, a row drawn and the
 * counter decremented on each landing on a multiple of 8, the scroll over on
 * the one after it reaches zero), and frames finishing it. */
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
    int sea_floor;              /* Ages' sea floor, moving at `move_angle` and `speed` ($28, a pixel a frame, when 0) */
    uint8_t move_angle;
    int blind;                  /* the first frames of the load given no collisions (0: the first alone) */
    uint8_t speed;
    int restore;                /* the savestate carries the policy's progress, saved before the frame `reset_at` */
} run;

static OraclesRoomTransition *policy_under_test;
static uint32_t frame_counter = 100;
static int largest_across;      /* the largest move across the axis asked in a frame of the last play */
static int animated;            /* its frames that advanced Link's animation */
static int scroll_steps;        /* the machine's steps in it */

/* Link's position at the end, 8.8, unwrapped, in the room left's coordinates. */
static void play(const run *r, int *out_x, int *out_y)
{
    OraclesGuestTransitionState s;
    memset(&s, 0, sizeof s);
    s.game = ORACLES_GAME_AGES; s.game_state = 2; s.cutscene_index = 1; s.scroll_mode = 8;
    s.transition_direction = (uint8_t)r->dir; s.link_object_index = 0xd0; s.link_state = 1; s.link_visible = 0x80;
    s.link_anim_mode = 0x10; s.animation_data_bank = 6; s.link_anim_pointer = 0x4000;
    s.link_input_angle = r->angle; s.link_move_angle = r->sea_floor ? r->move_angle : r->angle; s.tile_types_bank = 0xff;
    if (r->sea_floor) { s.tileset_flags = ORACLES_TILESETFLAG_UNDERWATER; s.link_var2f = 0xc0; s.link_speed = r->speed ? r->speed : 0x28; }
    int x = r->x, y = r->y;
    const int horizontal = r->dir & 1u, rows = horizontal ? 20 : 16, step = horizontal ? 0x60 : 0x80;
    const int sign = (r->dir == 1u || r->dir == 2u) ? 1 : -1;
    uint16_t camera = 0;
    int phase = 0, counter = rows, delta = sign * 4, post = 0;
    installed_reset(installed_opaque);
    largest_across = animated = scroll_steps = 0;
    /* A frame of normal play first, as the policy sees one before any transition. */
    s.transition_state = 2; s.scroll_mode = 1; s.frame = frame_counter++;
    s.link_x = (uint16_t)x; s.link_y = (uint16_t)y;
    {
        OraclesGuestTransitionMutation m;
        memset(&m, 0, sizeof m);
        installed(installed_opaque, &s, &m);
    }
    s.scroll_mode = 8;
    for (int f = 0; post < r->post || phase < 3; f++) {
        if (f == r->reset_at) {
            uint8_t saved[ORACLES_ROOM_TRANSITION_STATE_SIZE];
            const size_t size = oracles_room_transition_save_state(policy_under_test, saved, sizeof saved);
            CHECK(size == sizeof saved);
            installed_reset(installed_opaque);
            if (r->restore) CHECK(oracles_room_transition_load_state(policy_under_test, saved, size) == 0);
            frame_counter += 7;   /* the frames the load came after, in the host's count */
        }
        int machine = 0;
        s.frame = frame_counter++;
        s.link_anim_counter = 3;
        s.room_collisions = r->unknown ? NULL : r->collisions;
        if (f < r->loading) { s.transition_state = 3; if (f < (r->blind ? r->blind : 1)) s.room_collisions = NULL; }
        else if (f == r->loading) { s.transition_state = 5; s.transition_substate = horizontal ? 2 : 1; phase = 1; }
        else if (phase == 1) { phase = 2; counter--; }
        else if (phase == 2) {
            camera = (uint16_t)(camera + delta);
            machine = step;
            scroll_steps++;
            if (!(camera & 7u)) { if (counter) counter--; else phase = 3; }
        } else post++;
        s.transition_phase = (uint8_t)phase; s.screen_scroll_counter = (uint8_t)counter; s.screen_scroll_delta = (uint8_t)delta;
        s.scroll_alignment = (uint8_t)(camera & 7u);
        if (horizontal) s.camera_x = camera; else s.camera_y = camera;
        if (horizontal) x += sign * machine; else y += sign * machine;
        s.link_x = (uint16_t)x; s.link_y = (uint16_t)y;
        OraclesGuestTransitionMutation m;
        memset(&m, 0, sizeof m);
        installed(installed_opaque, &s, &m);
        if (m.set_scroll_delta) delta = (int8_t)m.scroll_delta;
        x += m.link_x_delta; y += m.link_y_delta;
        animated += m.update_walk_animation;
        const int across = abs(horizontal ? m.link_y_delta : m.link_x_delta);
        if (across > largest_across) largest_across = across;
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
    policy_under_test = tr;
    uint8_t collisions[176];
    int x = 0, y = 0;

    /* Down from y=121 at x=40, straight, twelve frames of load: a pixel a frame up to the game's 16 px, the machine's
     * 8 px of the doubled scroll counted, and beyond them only where nothing is in his way. */
    memset(collisions, 0, sizeof collisions);
    run r = { 2u, 16u, 40 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0, 0, 0, 0 };
    play(&r, &x, &y);
    CHECK(y - (121 << 8) == (12 + 2 + 16 + 2) << 8 && scroll_steps == 16);   /* a pixel a frame, the room clear */
    /* While the room entered's collisions are not known nothing says what lies beyond: the pixel the game's distance
     * ends on at most. */
    r.unknown = 1;
    play(&r, &x, &y);
    CHECK((y >> 8) == 121 + 16);

    /* A savestate loaded in the scroll with the policy's progress: it goes on as if no load had come between. */
    r.unknown = 0; r.reset_at = 20; r.restore = 1;
    play(&r, &x, &y);
    CHECK(y - (121 << 8) == (12 + 2 + 16 + 2) << 8 && scroll_steps == 16);
    /* One without it (a state written before the progress was kept): the machine takes Link to the scroll's end alone,
     * its step still doubled, so that he goes no further than its own steps from where the load found him (20 px by
     * then, then its last ten steps of $80); the next transition is the policy's again. */
    r.restore = 0;
    play(&r, &x, &y);
    {
        OraclesRoomTransitionStats stats;
        oracles_room_transition_stats(tr, &stats);
        CHECK(stats.refused_by[10] == 12);                       /* frames 20 to 31 */
    }
    CHECK(y - (121 << 8) == (20 << 8) + 10 * 0x80 && scroll_steps == 16);
    r.reset_at = -1;
    play(&r, &x, &y);
    CHECK(y - (121 << 8) == (12 + 2 + 16 + 2) << 8);

    /* On the sea floor Link glides on with no direction held, at his speed and angle (here down and right, 45 degrees
     * off the axis): the carry goes on with him. */
    r.sea_floor = 1; r.angle = 0x80; r.move_angle = 12; r.loading = 4; r.post = 2;
    play(&r, &x, &y);
    CHECK(x - (40 << 8) == 24 * 0xb5);                                  /* the first frame's carry, withheld while the room loads, caught up */
    CHECK(y - (121 << 8) == (16 << 8) + 2 * 0xb5);                       /* the game's 16 px by the scroll's end, then his own two steps */
    CHECK(largest_across <= 0x160);
    CHECK(animated == 0);                                               /* standing, as the game moves him without a direction */
    r.angle = 12u;
    play(&r, &x, &y);
    CHECK(animated == 4 + 2 + 16 + 2);                                  /* walking, a direction held */

    /* On foot, down and right, the room's collisions unknown for three frames: the carry they withheld is caught up
     * once they are known, at most a pixel a frame across, the bus's bound; and in the water at most $160. */
    r = (run){ 2u, 12u, 40 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0, 3, 0, 0 };
    memset(collisions, 0, sizeof collisions);
    play(&r, &x, &y);
    CHECK(x - (40 << 8) == 32 * 0xb5 && y - (121 << 8) == 32 * 0xb5 && largest_across == 0x100);
    /* A savestate loaded in the load, with what is owed across: the same carry. */
    r.reset_at = 2; r.restore = 1;
    play(&r, &x, &y);
    CHECK(x - (40 << 8) == 32 * 0xb5 && y - (121 << 8) == 32 * 0xb5);
    r.reset_at = -1; r.restore = 0;
    r.sea_floor = 1; r.angle = 0x80; r.move_angle = 12; r.loading = 4; r.blind = 3;
    play(&r, &x, &y);
    CHECK(x - (40 << 8) == 24 * 0xb5 && largest_across == 0x160);
    /* A wall across, along the room entered's first row, refuses the carry when the collisions are known: what it
     * refused, the load's frames' carry with it, is never caught up, every carry after it a step of his own. */
    {
        OraclesRoomTransitionStats before, after;
        for (unsigned c = 3; c < 10u; c++) collisions[0 * 16 + c] = 0x0f;
        r = (run){ 2u, 12u, 43 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0, 3, 0, 0 };   /* his right side (x+4) at 47, column 2 */
        oracles_room_transition_stats(tr, &before);
        play(&r, &x, &y);
        oracles_room_transition_stats(tr, &after);
        CHECK(after.carry_asked_frames > before.carry_asked_frames && after.carry_withheld_frames > before.carry_withheld_frames + 3);
        CHECK(x - (43 << 8) == (int)(after.carry_asked_frames - before.carry_asked_frames) * 0xb5 && largest_across == 0xb5);
        memset(collisions, 0, sizeof collisions);
    }

    /* Slower than the machine's $80 a step ($0a, $40 a frame), straight down on the sea floor: the doubled scroll's
     * steps, his own short of them, would leave him 9.75 px on; what they leave of the game's 16 px is spread over its
     * steps, as far as those 16 px, and his own step goes on after the scroll. */
    r = (run){ 2u, 16u, 40 << 8, 121 << 8, 4, 2, collisions, 0, -1, 1, 16u, 0, 0x0a, 0 };
    play(&r, &x, &y);
    CHECK(y - (121 << 8) == (16 << 8) + 2 * 0x40 && x == 40 << 8);

    /* Beyond the game's distance in whole pixels: from y=121, the room entered's row 1 a wall, which his feet (y+7)
     * reach at y=9 already, the game's own place; past the pixel it ends on, nothing; within it, a fraction further
     * (137.80, his steps of $80 in the scroll). */
    memset(collisions, 0, sizeof collisions);
    for (unsigned c = 0; c < 10u; c++) collisions[1 * 16 + c] = 0x0f;
    r = (run){ 2u, 16u, 40 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0, 0, 0, 0 };
    play(&r, &x, &y);
    CHECK(y == (137 << 8) + 0x80);
    /* Carried down and right, a hole in row 1 under the carried path's tile (y+5) beyond the game's distance and not
     * under the straight path's: the cap holds him where his tile stays clear of it. */
    memset(collisions, 0, sizeof collisions);
    collisions[1 * 16 + 3] = 0x10;
    r = (run){ 2u, 12u, 40 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0, 0, 0, 0 };
    play(&r, &x, &y);
    CHECK(x == 0x3ea0 && y == 0x89e6);                             /* x=62.63 in column 3, y=9.90: his tile (14) still in row 0 */
    r.angle = 16u;
    play(&r, &x, &y);
    CHECK(x == 40 << 8 && (y >> 8) - 128 == 121 + 32 - 128);      /* straight, column 2: nothing in his way */

    /* With the transitions alone, the cap the routes recorded with them were played with: past the game's distance in
     * 8.8, his middle and 4 px ahead, the machine's steps not counted (the wall of row 1 under his feet from y=9, under
     * y+4 from 12). */
    oracles_room_transition_stop(tr);
    tr = oracles_room_transition_start((OraclesGuest *)rom, rom, rom_size, 0);
    CHECK(tr != NULL);
    if (!tr) return 1;
    policy_under_test = tr;
    memset(collisions, 0, sizeof collisions);
    for (unsigned c = 0; c < 10u; c++) collisions[1 * 16 + c] = 0x0f;
    r = (run){ 2u, 16u, 40 << 8, 121 << 8, 12, 2, collisions, 0, -1, 0, 0, 0, 0, 0 };
    play(&r, &x, &y);
    CHECK(y == 145 << 8);                                          /* 17 in the room entered, his feet in the wall */

    if (failures) return 1;
    puts("test_room_transition_policy: ok");
    oracles_room_transition_stop(tr);
    free(rom);
    return 0;
}
