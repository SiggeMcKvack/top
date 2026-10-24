/* The text box drawn by the host, from what it knows: the box as the game's map shows it, read into a model, and the
 * host's lines (guest.h, the host's text box).
 *
 * The game's box is 18 tile columns by 5 tile rows of its background map (initTextboxMapping, textbox.s): a margin, 16
 * columns of text, a margin; two lines of two tile rows each, then the row of the arrow.  initTextboxMapping fills it
 * with tile $02 of attribute $80, the box's blank.  displayNextTextCharacter shows a line's column j, a cell of 8 x 16
 * pixels, as tiles base + 2j (its upper half) and base + 2j + 1 (its lower) in the column j + 1, base $40 the top line's
 * tiles ($9400), $60 the bottom's ($9600); shiftTextboxMapUp moves the rows up a tile and subFirstRowOfTextMapBy20 points
 * the moved rows at the top line, so that a row shows one half of one line, whichever, from its first column.
 * updateTextboxArrow puts the arrow (tile $03, or the blank) in the last row's last column, and
 * updateSelectedTextPosition a choice's cursor (tile $04) over a line's cell.
 *
 * The model says what the box shows, not where: each row's line and half and how many of its columns, and the marks
 * (the arrow, a cursor, any other tile the game puts in the box) held to the left margin, to a text column or to the
 * right margin.  Drawn back at the game's width and lines, it is the game's box; drawn at another width or with another
 * number of lines, the rows keep their lines, the text runs on in the host's lines, the arrow stays at the right. */
#ifndef ORACLES_ENHANCED_TEXT_BOX_H
#define ORACLES_ENHANCED_TEXT_BOX_H

#include "guest.h"
#include "text_box_enlarge.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORACLES_TEXT_BOX_GAME_COLUMNS 18u        /* a margin, 16 text columns, a margin */
#define ORACLES_TEXT_BOX_GAME_ROWS 5u            /* two lines of two tile rows, the arrow's row */
#define ORACLES_TEXT_BOX_GAME_PIXELS 128u        /* the game's text width */
#define ORACLES_TEXT_BOX_GAME_LINES 2u
#define ORACLES_TEXT_BOX_LINES_MAX 3u
#define ORACLES_TEXT_BOX_WIDE_PIXELS 192u        /* the wide box's text width: its box of 208 the widest every Enhanced view holds (Near 4:3, 213) */
#define ORACLES_TEXT_BOX_BLANK_TILE 0x02u        /* initTextboxMapping */
#define ORACLES_TEXT_BOX_BLANK_ATTRIBUTE 0x80u
#define ORACLES_TEXT_BOX_TOP_TILES 0x40u         /* the top line's first tile, $9400 */
#define ORACLES_TEXT_BOX_BOTTOM_TILES 0x60u      /* the bottom line's, $9600 */
/* The box drawn at most: 256 pixels of text and its margins, three lines and the arrow's row. */
#define ORACLES_TEXT_BOX_WIDTH_MAX ((ORACLES_GUEST_TEXT_BOX_COLUMNS_MAX + 2u) * 8u)
#define ORACLES_TEXT_BOX_HEIGHT_MAX ((2u * ORACLES_TEXT_BOX_LINES_MAX + 1u) * 8u)

typedef enum OraclesTextBoxAnchor {
    ORACLES_TEXT_BOX_LEFT,     /* the left margin */
    ORACLES_TEXT_BOX_TEXT,     /* a text column */
    ORACLES_TEXT_BOX_RIGHT     /* the right margin */
} OraclesTextBoxAnchor;

typedef struct OraclesTextBoxRow {
    uint8_t line;      /* ORACLES_GUEST_TEXT_BOX_TOP or _BOTTOM, 0 for none */
    uint8_t half;      /* 0 the line's upper tile row, 1 its lower */
    uint8_t shown;     /* its tile columns shown, from the first */
    uint16_t pixels;   /* its pixels drawn, from the first: the columns shown, or the reveal read in the text's state (a line
                        * wider than the game's map says, oracles_text_box_reveal) */
} OraclesTextBoxRow;

typedef struct OraclesTextBoxMark {
    uint8_t anchor;      /* OraclesTextBoxAnchor */
    uint8_t column;      /* the text column of a mark held to one */
    uint8_t row;         /* the game's row */
    uint8_t tile, attribute;
} OraclesTextBoxMark;

typedef struct OraclesTextBoxModel {
    OraclesTextBoxRow row[ORACLES_TEXT_BOX_GAME_ROWS];
    unsigned marks;
    OraclesTextBoxMark mark[ORACLES_TEXT_BOX_GAME_ROWS * ORACLES_TEXT_BOX_GAME_COLUMNS];
} OraclesTextBoxModel;

typedef enum OraclesTextBoxRead {
    ORACLES_TEXT_BOX_READ = 0,       /* the model holds the box */
    ORACLES_TEXT_BOX_NOT_SHOWN,      /* the cells are not a box: its margins are not the box's blank */
    ORACLES_TEXT_BOX_LINE_UNKNOWN,   /* a row shows a line the host does not know */
    ORACLES_TEXT_BOX_UNEXPECTED      /* a cell the model does not express: a line's tile out of its column or row, a gap */
} OraclesTextBoxRead;

/* Pure: the model of the game's box from its cells in the map (tiles and attributes, a row of 18 after the other), the host's lines
 * known for ORACLES_GUEST_TEXT_BOX_TOP and _BOTTOM in `known`. */
OraclesTextBoxRead oracles_text_box_read(const uint8_t tiles[ORACLES_TEXT_BOX_GAME_ROWS * ORACLES_TEXT_BOX_GAME_COLUMNS],
                                         const uint8_t attributes[ORACLES_TEXT_BOX_GAME_ROWS * ORACLES_TEXT_BOX_GAME_COLUMNS],
                                         const uint8_t known[ORACLES_GUEST_TEXT_BOX_LINES], OraclesTextBoxModel *model);

/* Pure: a box wider than the game's, its rows' reveal from the text's state rather than the game's map, which shows only
 * a line's first 128 pixels: the rows of the line the game reveals (reveal->line) to oracles_guest_text_box_reveal_pixels,
 * every other line shown whole.  `lines` the host's lines, ORACLES_GUEST_TEXT_BOX_*. */
void oracles_text_box_reveal(OraclesTextBoxModel *model, const OraclesGuestTextBoxReveal *reveal, const OraclesGuestTextBoxLine *const lines[ORACLES_GUEST_TEXT_BOX_LINES]);

typedef struct OraclesTextBoxSource {
    const uint8_t *vram;          /* 16 KiB, bank 0 then bank 1: the game's tiles, the box's blank, the arrow, the cursor */
    int signed_tiles;             /* LCDC bit 4 clear: tile 0 at $9000, $80 to $ff at $8800 */
    const uint8_t *bg_palettes;   /* 64 bytes, RGB555 little-endian */
    const uint32_t *colours;      /* ORACLES_PPU_COLOURS pixels, one per RGB555 colour; NULL: the raw conversion */
    const OraclesGuestTextBoxLine *lines[ORACLES_GUEST_TEXT_BOX_LINES];   /* the host's lines, ORACLES_GUEST_TEXT_BOX_* */
} OraclesTextBoxSource;

/* Pure: the box drawn from its model at `pixels` of text (a multiple of 8, ORACLES_TEXT_BOX_GAME_PIXELS to
 * ORACLES_GUEST_TEXT_BOX_COLUMNS_MAX * 8) and `lines` lines (2 or 3): (pixels / 8 + 2) x (2 lines + 1) tiles into `out`,
 * whose rows are `stride` pixels apart, as 0xffRRGGBB.  A row of the model's lines goes to the same row, its `pixels`
 * drawn and the rest blank; the arrow's row goes last; a third line, which the game's model never holds, is blank. */
void oracles_text_box_draw(const OraclesTextBoxModel *model, const OraclesTextBoxSource *source, unsigned pixels, unsigned lines,
                           uint32_t *out, unsigned stride);

#ifdef __cplusplus
}
#endif

#endif
