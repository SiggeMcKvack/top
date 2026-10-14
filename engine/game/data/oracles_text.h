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

#ifdef __cplusplus
}
#endif

#endif
