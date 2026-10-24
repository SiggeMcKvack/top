/* The game's texts, read from a ROM image (docs/ROM_DATA_FORMATS.md, section 8).
 *
 * A text is found as getTextAddress finds it: the language's table gives, per
 * internal group, the offset of a list of 16-bit offsets, one per text, which
 * count from one of up to three bases chosen by the group.  Internal groups 0
 * to 3 are the dictionary: the codes 2 to 5 insert one of their fragments; a
 * text TX_GGII is internal group GG + 4, index II.  The decoder expands the
 * dictionary and keeps every other byte, the control codes and their
 * parameter included, as the game's text engine reads them.  Every address is
 * a physical offset in the image: a list or a text that crosses a bank goes on
 * in the next one. */
#ifndef ORACLES_DATA_TEXT_H
#define ORACLES_DATA_TEXT_H

#include "oracles_rom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORACLES_TEXT_DICTIONARY_GROUPS 4u   /* internal groups of the dictionary, before the texts' */
#define ORACLES_TEXT_MAX 4096u              /* a decoded text's bytes, its terminator included, at most */

typedef struct OraclesTextSource {
    size_t table;           /* the language's table of groups */
    size_t bases[3];        /* where the texts' offsets count from */
    unsigned splits[2];     /* the first internal group counted from bases[1], from bases[2]; 0x100: none is */
} OraclesTextSource;

/* The number of internal groups of the table (its first list follows it), or 0 when the table cannot be read. */
unsigned oracles_text_groups(const OraclesRom *rom, const OraclesTextSource *source);
/* The number of texts of internal group `group` (ORACLES_TEXT_DICTIONARY_GROUPS or more): 0 for a group that shares
 * group 0's list, as the game's empty groups do; a list ends where the next text group's begins, the last one where
 * the first base begins.  Negative on an error (ORACLES_ERR_*). */
int oracles_text_count(const OraclesRom *rom, const OraclesTextSource *source, unsigned group);
/* The physical offset of a text or a fragment, or a negative ORACLES_ERR_*. */
long oracles_text_address(const OraclesRom *rom, const OraclesTextSource *source, unsigned group, unsigned index);
/* Text `index` of internal group `group` decoded into `out`: the dictionary expanded, a 0 at its end, its length
 * with that 0 in *length.  ORACLES_ERR_CAPACITY past `capacity` bytes or a dictionary nested too deep. */
int oracles_text_decode(const OraclesRom *rom, const OraclesTextSource *source, unsigned group, unsigned index,
                        uint8_t *out, size_t capacity, size_t *length);

/* Whether two decoded texts give the text engine the same commands, in the same order: every control code with its
 * parameter, but those that only lay the text out or name the hero (the line break, \stop, which opens the next box,
 * \Link and \Child, and the colours), up to the first \jump or cmd8, which end the text as the engine reads it. */
int oracles_text_same_commands(const uint8_t *a, size_t a_length, const uint8_t *b, size_t b_length);
/* The longest line of a decoded text, in characters: a control code that prints in the line (\Link, \Child, \num1,
 * \num2, \opt, \item, \sym) counts as the game draws it; \heartpiece draws apart; the bytes after a \jump or a cmd8,
 * which end the text, are another text's. */
unsigned oracles_text_longest_line(const uint8_t *text, size_t length);

/* The text box's font, as the compact text lays a line out: character c, from ORACLES_TEXT_FIRST_CHARACTER, is a cell of
 * 8 x 16 pixels at the font's start + 16 c in the ROM, a byte a row, its bit 7 the left column, a clear bit ink
 * (retrieveTextCharacter).  A character's width is its columns from the first to the last of ink; it moves the pen by
 * its width and a column, a blank character by ORACLES_TEXT_SPACE_ADVANCE, and one whose ink reaches its cell's right
 * edge by its width alone: it goes on in the next cell, as the halves of the A and B buttons ($b8 $b9, $ba $bb) do. */
#define ORACLES_TEXT_FIRST_CHARACTER 0x10u
#define ORACLES_TEXT_CELL_WIDTH 8u
#define ORACLES_TEXT_SPACE_ADVANCE 5u
typedef struct OraclesTextFont {
    uint8_t left[256];      /* a character's first column of ink in its cell */
    uint8_t width[256];     /* its columns from the first to the last of ink; 0 for a blank one and below the first character */
    uint8_t advance[256];   /* how far it moves the pen; 0 below the first character */
} OraclesTextFont;
/* The widths of the font at physical offset `font_start` (gfx_font_start's, where character 0 would be);
 * ORACLES_ERR_RANGE when its characters are not all in the image. */
int oracles_text_font(const OraclesRom *rom, size_t font_start, OraclesTextFont *font);

/* A line as the compact text lays it out, its characters placed one after the other at a pen, in pixels: a character
 * moves it by its advance; a whole one (a symbol, $06: an icon or a kanji, or a choice's cursor place) takes a whole tile
 * from the next tile's edge; a character with ink starts the next tile when the tile it would share holds ink of another
 * attribute (a tile has one palette).  After a character whose ink reaches its cell's right edge, the next one joins it
 * when its own ink starts its cell, and keeps a column before it otherwise.  A tile column takes its ink's attribute,
 * else a blank character's on it.  The
 * guest's transaction draws a line so and the layout of the texts (language_wrap.c) measures it so: one rule for both. */
#define ORACLES_TEXT_LINE_TILES 32u   /* a line's tile columns at most, 256 pixels */
#define ORACLES_TEXT_LINE_CHARACTERS_MAX 64u   /* a line's characters the compact text keeps at most: 128 pixels, two a character */
typedef struct OraclesTextPen {
    unsigned pen;                                  /* where the next character goes */
    unsigned end;                                  /* past the last column drawn */
    uint8_t attribute[ORACLES_TEXT_LINE_TILES];    /* each tile column's */
    uint8_t inked[ORACLES_TEXT_LINE_TILES];        /* the tile column holds ink */
    uint8_t marked[ORACLES_TEXT_LINE_TILES];       /* it has an attribute */
    int joined;                                    /* the last character's ink reached its cell's right edge */
} OraclesTextPen;
/* Character `c` of `attribute` placed at the pen, `whole` for a symbol or a cursor's place: its first column drawn in
 * *start, the first column of its cell drawn in *from and how many in *drawn (its ink's, 0 for a blank character, a
 * cell for a whole one); the pen moved on.  The pen starts zeroed. */
void oracles_text_place(OraclesTextPen *pen, const OraclesTextFont *font, uint8_t c, uint8_t attribute, int whole, unsigned *start, unsigned *from,
                        unsigned *drawn);

#ifdef __cplusplus
}
#endif

#endif
