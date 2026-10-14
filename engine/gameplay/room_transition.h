/* Continuous room transitions: an opt-in gameplay policy
 * that keeps Link walking through the game's own scrolling transition.
 *
 * The game freezes Link for the whole transition (linkState01 skips his
 * movement while wScrollMode is in transition) and moves him itself by
 * 15 px over about forty frames, one tile column every two 4 px steps; in
 * the wide world of the Enhanced profile that freeze is the only thing the
 * player sees of the transition.  The policy, at the return of
 * drawAllSprites on every frame of the transition (states 3 to 5), asks the
 * guest's closed transaction for three things: the scroll step doubled to
 * 8 px (a column per step, twenty steps), Link moved so that he advances
 * one pixel a frame in the direction of the transition, his walking
 * animation advanced from the ROM's animation data, as animateLinkWalking
 * would.  The extra distance is stopped short of any tile of the room being
 * entered that has a collision, so Link never arrives inside a wall.  The
 * core and the audio keep their cadence; the guest state of a session with
 * the option differs from a native one by design, and a route recorded
 * without it diverges after the first transition.  Both games, the small
 * rooms of groups 0 to 5 (the overworlds and the interiors), Link on foot;
 * the large rooms (dungeons) and the sidescrolling areas deliberately keep
 * their native transition (room_transition.c says why).  With `swim` (--continuous-swim) Link
 * swimming at the surface goes on too, in his normal swimming state, diving
 * or not: at his own speed (w1Link.speed, frozen by the game through the
 * transition), his swimming or diving animation advanced, and beyond the
 * game's own distance only into water (the ROM's tile types); a current's
 * push is not reproduced.  On Ages' sea floor, where the game has him walk in
 * his mermaid suit's gait, he goes on at his own speed too, his walking
 * animation advanced, onto tiles without a collision beyond the game's
 * distance, as on foot.  With it, the cap beyond the game's distance holds
 * leftward and upward too, on foot as swimming; without it, the distance
 * wraps there below zero and the cap never holds, as the routes recorded
 * with the transitions alone were played; and on foot, beyond the game's
 * distance, he walks onto stairs and onto a bridge along its way, which the
 * rule of no collision alone took for walls; with it or `diagonal`, the cap
 * counts the machine's own steps still to come, which nothing holds back, and
 * tests the game's own points: the tile it takes as his (y+5), his leading
 * edge, and none of the points it tests for walls newly in one.  With `diagonal`
 * (--continuous-diagonal), on foot, a diagonal held toward the edge goes on
 * as the game walks it, 0xb5 a frame along each axis: across the axis too,
 * onto tiles of the room entered without a collision, where the game holds
 * him for the whole scroll and the view would slide along the axis alone,
 * only where every point the game tests for walls, and the tile it takes as
 * his, stay as clear as the straight path's wherever the scroll may leave
 * him;
 * swimming, or on Ages' sea floor, his motion goes on at its own angle and
 * speed, which the game freezes through the scroll and takes up after it,
 * across the axis only into water swimming; a route recorded without it
 * replays without it, its diagonal crossings elsewhere otherwise. */
#ifndef ORACLES_GAMEPLAY_ROOM_TRANSITION_H
#define ORACLES_GAMEPLAY_ROOM_TRANSITION_H

#include "guest.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OraclesRoomTransition OraclesRoomTransition;

/* Installs the policy on the guest; `rom` must outlive it (the animation data, the tile types). NULL when the guest's
 * game has no tables, or `swim` is asked of a profile without it. */
OraclesRoomTransition *oracles_room_transition_start(OraclesGuest *guest, const uint8_t *rom, size_t rom_size, int swim);
void oracles_room_transition_stop(OraclesRoomTransition *transition);
/* --continuous-diagonal: a diagonal held carries Link across the transition's axis too (off at the start), where the
 * guest's profile qualifies it; returns whether it is on. */
int oracles_room_transition_set_diagonal(OraclesRoomTransition *transition, int on);

typedef struct OraclesRoomTransitionStats {
    unsigned transitions;        /* transitions the policy took over (the step doubled) */
    unsigned swim_transitions;   /* of them, those Link swam through, at the surface or on the sea floor */
    unsigned walked_frames;      /* frames Link was moved by the policy */
    unsigned swum_frames;        /* of them, frames he swam */
    unsigned forced_transitions; /* transitions a swim's momentum took Link to at an edge, the direction not held (--continuous-swim) */
    unsigned capped_frames;      /* frames the extra move was withheld: a collision ahead in the room entered */
    unsigned refused_frames;     /* frames in a transition the policy left alone (Link not on foot, an item out) */
    unsigned refused_by[11];     /* by reason: 0 game/room, 1 state, 2 Link's state, 3 not walking, 4 airborne, 5 holding/pushing, 6 item, shield, seeds, knocked or stunned, 7 animation data, 8 swimming, 9 under water, 10 a savestate loaded in the transition */
    unsigned unanimated_frames;  /* frames Link was not moved because his animation stream could not be read */
    unsigned palette_refreshes;  /* palettes refreshed after a palette transition's fade outlived the doubled scroll */
    unsigned carry_asked_frames; /* frames the policy asked to move Link across the axis too (--continuous-diagonal): a diagonal held on foot, his motion in the water; those the bus applied are oracles_guest_transition_carried */
    unsigned carry_withheld_frames; /* frames it did not: a collision across in the room entered, or its collisions not loaded */
} OraclesRoomTransitionStats;
/* The first reason a state is not eligible (the index of refused_by), -1 when eligible; `swim` as at the start. */
int oracles_room_transition_refusal(const OraclesGuestTransitionState *state, int swim);
void oracles_room_transition_stats(const OraclesRoomTransition *transition, OraclesRoomTransitionStats *out);

/* Pure helpers, testable without a ROM. */
/* Whether the policy applies to this state at all (game, group, room size, transition, Link on foot or swimming). */
int oracles_room_transition_eligible(const OraclesGuestTransitionState *state, int swim);
/* The transition the game would start now if the player held the direction Link swims in at the surface, during a
 * stroke of the flippers (screenTransitionState2@transition, its checks in its order, the direction held replaced by
 * his motion's angle), when the direction held does not take him there already: 0 up, 1 right, 2 down, 3 left, -1 none. */
int oracles_room_transition_swim_edge(const OraclesGuestTransitionState *state);
/* The direction across the transition's axis a diagonal held toward the edge adds (wLinkAngle, a step of 45 degrees
 * off the axis): 0 up, 1 right, 2 down, 3 left; -1 for none, straight or away. */
int oracles_room_transition_held_across(const OraclesGuestTransitionState *state);
/* Whether Link, moved by `along` (8.8) in the transition's direction and `delta` in the direction `across`, is free
 * there on that side at the two points the game tests for walls (calculateAdjacentWallsBitset), in the room being
 * entered: no wall, no hole or water; and the two of the side he goes along the axis, free without the move across,
 * stay free with it; while a point is still in the room he leaves along the axis, the room entered's first row or
 * column; and from there to `rest` (8.8, what remains of the game's own distance) further along the axis: Link
 * `offset` (8.8, signed along x or y) off the straight path across it before this move, carried, keeps every point
 * the game tests for walls, and the tile it takes as his own (y+5: a hole, water or lava), as clear as the straight
 * path does.  0 while the room entered's collisions are not loaded, or when a point leaves the room across. */
int oracles_room_transition_across_free(const OraclesGuestTransitionState *state, unsigned across, int delta, int along, int offset, int rest);
/* The same for Link swimming: its points on water he can swim in (as oracles_room_transition_swimmable), and his own
 * tile out of water he can swim in (land, a hole, lava, the sea without the mermaid suit) only where the straight
 * path's is. */
int oracles_room_transition_across_swimmable(const uint8_t *rom, size_t rom_size, const OraclesGuestTransitionState *state, unsigned across, int delta,
                                             int along, int offset, int rest);
/* In the water, his motion's angle (w1Link.angle, 32 steps) off the transition's direction by 1 to 7 steps: the
 * direction across it (0 up, 1 right, 2 down, 3 left), and `step` (8.8) split along and across it as
 * objectSpeedTable splits a speed; -1 for none, straight, along the edge or away. */
int oracles_room_transition_moving_across(const OraclesGuestTransitionState *state, int step, int *along, int *across_step);
/* What the machine still moves Link along the transition's axis after this frame (8.8): a step for each row or column
 * the scroll has yet to draw, the counter whole until its phase 1, decremented at each step landing the camera on a
 * multiple of 8, the last step taken at zero; with `doubled`, steps of 8 px, one a row; without, steps of 4 px, two a
 * row; before the scroll, all of them; 0 once it is over. */
int oracles_room_transition_machine_rest(const OraclesGuestTransitionState *state, int doubled);
/* Whether Link swims at the surface (wLinkSwimmingState). */
int oracles_room_transition_swimming(const OraclesGuestTransitionState *state);
/* Whether he is in the water: swimming at the surface, or on Ages' sea floor. */
int oracles_room_transition_in_water(const OraclesGuestTransitionState *state);
/* Link's own move a frame along an axis, 8.8: a pixel on foot, his speed in the water (objectSpeedTable). */
int oracles_room_transition_step(const OraclesGuestTransitionState *state);
/* The tile type of `tile` in the list the room's collisions select (lookupCollisionTable over tileTypesTable), -1
 * when the table cannot be read. */
int oracles_room_transition_tile_type(const uint8_t *rom, size_t rom_size, const OraclesGuestTransitionState *state, uint8_t tile);
/* The next frame of Link's walking animation from the ROM's animation data
 * (specialObjectNextAnimationFrame: duration, base frame, parameter, `$ff`
 * then a signed offset to loop).  Fills the animation fields of `mutation`;
 * 0 when the data cannot be read. */
int oracles_room_transition_next_walk_frame(const uint8_t *rom, size_t rom_size, const OraclesGuestTransitionState *state,
                                            OraclesGuestTransitionMutation *mutation);
/* Whether Link, moved by `extra` (8.8) along the transition's axis, stands on
 * collision-free tiles of the room being entered (or still in the room he
 * leaves): the tile the game takes as his own (y+5) and the one his leading
 * edge reaches (y-3 up, y+7 down, x+4 right, x-5 left), and none of the eight
 * points the game tests for walls in a wall it is not in moved by `from`.  1
 * when the room's collisions are not loaded yet. */
int oracles_room_transition_walkable(const OraclesGuestTransitionState *state, int from, int extra);
/* The same with --continuous-swim: stairs, and a bridge crossed along the transition's axis, are walked on too. */
int oracles_room_transition_passable(const OraclesGuestTransitionState *state, int from, int extra);
/* The same for Link swimming: onto water he can swim in (the collision of holes, water and lava, and the tile type of
 * water, a current, or the sea with the mermaid suit). */
int oracles_room_transition_swimmable(const uint8_t *rom, size_t rom_size, const OraclesGuestTransitionState *state, int from, int extra);

#ifdef __cplusplus
}
#endif

#endif
