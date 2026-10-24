/* The collisions of the room a scroll enters, predicted while the game loads them.
 *
 * The game decides a scrolling transition and loads the room entered in the same frame (cutscene01, bank1.s):
 * getNextActiveRoom names it, loadTilesetData its tileset, wScrollMode is set to 8, then loadTilesetAndRoomLayout
 * decodes its layout and applies its tile substitutions, loadRoomCollisions maps each tile through the tileset's
 * collisions, and generateVramTilesWithRoomChanges draws it.  That frame outlasts two or three vblanks, through which
 * the room's collisions are not known yet, and the continuous transitions' diagonal (room_transition.h) would withhold
 * Link's carry across the axis.  Everything that load reads is fixed once wScrollMode is 8: the layout and the
 * collisions are the ROM's tables (the room's layout, its tileset's collisions) through substitutions driven by bytes
 * of the RAM the load leaves alone (the room's flags, the global flags, the portal).  The prediction rebuilds them
 * from those tables and bytes, as the game does, for the rooms whose load is only that; a room whose own code changes
 * its tiles (applyRoomSpecificTileChanges, applyRoomSpecificTileChangesAfterGfxLoad but where it only draws), a large
 * room, an image not qualified, is left to the game's own load.  The prediction is compared with what the game
 * loads at every transition it is made for (oracles_room_transition_stats). */
#ifndef ORACLES_GAMEPLAY_ROOM_PREDICT_H
#define ORACLES_GAMEPLAY_ROOM_PREDICT_H

#include "guest.h"
#include "oracles_rom.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Why a room's collisions are not predicted, ORACLES_ROOM_PREDICTED when they are. */
typedef enum OraclesRoomPredictVerdict {
    ORACLES_ROOM_PREDICTED = 0,
    ORACLES_ROOM_PREDICT_UNQUALIFIED,   /* the image is not qualified (its tables not bound), or not Ages or Seasons */
    ORACLES_ROOM_PREDICT_EARLY,         /* wScrollMode not 8 yet: the room entered not named yet */
    ORACLES_ROOM_PREDICT_LARGE,         /* a large room, or a group of large rooms (4 to 7) */
    ORACLES_ROOM_PREDICT_ROOM_CODE,     /* the room's own code changes its tiles (applyRoomSpecificTileChanges) */
    ORACLES_ROOM_PREDICT_AFTER_GFX,     /* or changes them after its graphics (applyRoomSpecificTileChangesAfterGfxLoad), a Gasha spot planted */
    ORACLES_ROOM_PREDICT_OTHER_LAYOUT,  /* Seasons' Temple Remains filled with lava: the layout is another room's */
    ORACLES_ROOM_PREDICT_UNREADABLE,    /* a table could not be read, or points out of the room */
    ORACLES_ROOM_PREDICT_VERDICTS
} OraclesRoomPredictVerdict;

/* The room entered's layout (wRoomLayout) and collisions (wRoomCollisions) as the game loads them, ORACLES_ROOM_LAYOUT_BYTES
 * each, from `state` at a frame of the load (wScrollMode 8, the room entered named and its tileset data loaded) and
 * the ROM's tables `data`; the verdict.  Pure: the ROM and the state alone. */
OraclesRoomPredictVerdict oracles_room_predict(const OraclesRom *rom, const OraclesTables *data, const OraclesGuestTransitionState *state,
                                               uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES], uint8_t collisions[ORACLES_ROOM_LAYOUT_BYTES]);

/* The steps, testable alone. */
/* The substitutions of applyAllTileSubstitutions on `layout`, the room's as loadRoomLayout decoded it, a small room of
 * groups 0 to 3; the verdict (ORACLES_ROOM_PREDICT_UNREADABLE when a table cannot be read). */
OraclesRoomPredictVerdict oracles_room_predict_substitute(const OraclesRom *rom, const OraclesTables *data, const OraclesGuestTransitionState *state,
                                                          uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES]);
/* Whether the room's own code leaves its tiles to the prediction: ORACLES_ROOM_PREDICTED, or why not. */
OraclesRoomPredictVerdict oracles_room_predict_excluded(const OraclesRom *rom, const OraclesTables *data, const OraclesGuestTransitionState *state);
/* loadRoomCollisions: each tile's collision in `table` (w3TileCollisions), the bytes past a small room's last row and
 * column, and past a row's sixteenth, $ff. */
void oracles_room_predict_collisions(const uint8_t table[ORACLES_TILESET_COLLISIONS_BYTES], const uint8_t layout[ORACLES_ROOM_LAYOUT_BYTES],
                                     int small, uint8_t collisions[ORACLES_ROOM_LAYOUT_BYTES]);
/* The verdict's name, for the reports. */
const char *oracles_room_predict_verdict_name(OraclesRoomPredictVerdict verdict);

#ifdef __cplusplus
}
#endif

#endif
