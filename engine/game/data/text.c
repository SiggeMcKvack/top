#include "oracles_text.h"

#include <string.h>

#define DICTIONARY_DEPTH 4u   /* fragments inserted in fragments, at most */
#define CODE_NEWLINE 0x01u
#define CODE_DICTIONARY_FIRST 0x02u
#define CODE_WITH_PARAMETER 0x06u   /* 6 to $f take one byte of parameter */
#define CODE_JUMP 0x07u
#define CODE_END_HERE 0x08u           /* cmd8: the text ends, a shop's other text follows */
#define CODE_COLOUR 0x09u
#define CODE_NAME 0x0au             /* 0 \Link, 1 \Child, 2 and 3 the secrets */
#define CODE_COMMAND 0x0cu          /* the parameter's bits 3-7: 0 speed, 1 \num1, 2 \opt, 3 \stop, 4 \pos, 5 \heartpiece, 6 \num2, 7 \slow */
#define CODE_SYMBOL 0x06u
#define CODE_FIRST_CHARACTER 0x10u
#define COMMAND_STOP 0x18u

static long base_of(const OraclesTextSource *source, unsigned group)
{
    if (group < source->splits[0]) return (long)source->bases[0];
    if (group < source->splits[1]) return (long)source->bases[1];
    return (long)source->bases[2];
}

unsigned oracles_text_groups(const OraclesRom *rom, const OraclesTextSource *source)
{
    unsigned first = 0;
    if (oracles_rom_read16le(rom, source->table, &first) != ORACLES_OK || first & 1u || first < 2u * ORACLES_TEXT_DICTIONARY_GROUPS || first > 0x200u) return 0;
    return first / 2u;
}

/* The physical offset of internal group `group`'s list. */
static long list_of(const OraclesRom *rom, const OraclesTextSource *source, unsigned group)
{
    unsigned offset = 0;
    if (group >= oracles_text_groups(rom, source)) return ORACLES_ERR_FORMAT;
    if (oracles_rom_read16le(rom, source->table + 2u * group, &offset) != ORACLES_OK) return ORACLES_ERR_RANGE;
    return (long)(source->table + offset);
}

int oracles_text_count(const OraclesRom *rom, const OraclesTextSource *source, unsigned group)
{
    const unsigned groups = oracles_text_groups(rom, source);
    if (group < ORACLES_TEXT_DICTIONARY_GROUPS || group >= groups) return ORACLES_ERR_FORMAT;
    const long start = list_of(rom, source, group), empty = list_of(rom, source, 0);
    if (start < 0 || empty < 0) return ORACLES_ERR_RANGE;
    if (start == empty) return 0;
    /* The dictionary's lists do not bound a text's: some European languages leave groups 1 to 3 pointing anywhere. */
    long end = (long)source->bases[0];
    for (unsigned g = ORACLES_TEXT_DICTIONARY_GROUPS; g < groups; g++) {
        const long other = list_of(rom, source, g);
        if (other < 0) return (int)other;
        if (other > start && other < end) end = other;
    }
    if (end <= start || (end - start) % 2 != 0 || (end - start) / 2 > 0x100) return ORACLES_ERR_FORMAT;
    return (int)((end - start) / 2);
}

long oracles_text_address(const OraclesRom *rom, const OraclesTextSource *source, unsigned group, unsigned index)
{
    const long list = list_of(rom, source, group);
    unsigned offset = 0;
    if (list < 0) return list;
    if (index > 0xffu || oracles_rom_read16le(rom, (size_t)list + 2u * index, &offset) != ORACLES_OK) return ORACLES_ERR_RANGE;
    const long at = base_of(source, group) + (long)offset;
    return (size_t)at < rom->size ? at : ORACLES_ERR_RANGE;
}

/* Appends the text at `at` to out[*length]: a fragment (depth above 0) without its terminator. */
static int decode_at(const OraclesRom *rom, const OraclesTextSource *source, size_t at, unsigned depth, uint8_t *out, size_t capacity, size_t *length)
{
    for (;;) {
        uint8_t code = 0, parameter = 0;
        if (oracles_rom_read8(rom, at, &code) != ORACLES_OK) return ORACLES_ERR_RANGE;
        if (code == 0 && depth) return ORACLES_OK;
        if (code >= CODE_DICTIONARY_FIRST && code < CODE_WITH_PARAMETER) {
            if (depth >= DICTIONARY_DEPTH || oracles_rom_read8(rom, at + 1u, &parameter) != ORACLES_OK) return ORACLES_ERR_FORMAT;
            const long fragment = oracles_text_address(rom, source, code - CODE_DICTIONARY_FIRST, parameter);
            if (fragment < 0) return (int)fragment;
            const int rc = decode_at(rom, source, (size_t)fragment, depth + 1u, out, capacity, length);
            if (rc != ORACLES_OK) return rc;
            at += 2u;
            continue;
        }
        const size_t bytes = code >= CODE_WITH_PARAMETER && code < CODE_FIRST_CHARACTER ? 2u : 1u;
        if (*length + bytes > capacity) return ORACLES_ERR_CAPACITY;
        for (size_t i = 0; i < bytes; i++)
            if (oracles_rom_read8(rom, at + i, &out[(*length)++]) != ORACLES_OK) return ORACLES_ERR_RANGE;
        if (code == 0) return ORACLES_OK;
        at += bytes;
    }
}

int oracles_text_decode(const OraclesRom *rom, const OraclesTextSource *source, unsigned group, unsigned index,
                        uint8_t *out, size_t capacity, size_t *length)
{
    *length = 0;
    const long at = oracles_text_address(rom, source, group, index);
    if (at < 0) return (int)at;
    return decode_at(rom, source, (size_t)at, 0, out, capacity, length);
}

/* The next command of a decoded text from *at that the comparison keeps, as code << 8 | parameter; -1 at its end, which
 * a \jump or a cmd8 is: the engine reads no byte of the text after them, the decoder's bytes there are another text's. */
static long next_command(const uint8_t *text, size_t length, size_t *at)
{
    while (*at < length) {
        const uint8_t code = text[*at];
        if (code < CODE_WITH_PARAMETER || code >= CODE_FIRST_CHARACTER || *at + 1u >= length) { (*at)++; continue; }
        const uint8_t parameter = text[*at + 1u];
        *at += 2u;
        if (code == CODE_COLOUR || (code == CODE_NAME && parameter < 2u) || (code == CODE_COMMAND && parameter == COMMAND_STOP)) continue;
        if (code == CODE_JUMP || code == CODE_END_HERE) *at = length;
        return (long)code << 8 | parameter;
    }
    return -1;
}

int oracles_text_same_commands(const uint8_t *a, size_t a_length, const uint8_t *b, size_t b_length)
{
    size_t i = 0, k = 0;
    for (;;) {
        const long x = next_command(a, a_length, &i), y = next_command(b, b_length, &k);
        if (x != y) return 0;
        if (x < 0) return 1;
    }
}

unsigned oracles_text_longest_line(const uint8_t *text, size_t length)
{
    unsigned longest = 0, line = 0;
    for (size_t at = 0; at < length; at++) {
        const uint8_t code = text[at];
        if (code == 0 || code == CODE_NEWLINE) { line = 0; continue; }
        if (code >= CODE_FIRST_CHARACTER) line++;
        else if (code >= CODE_WITH_PARAMETER && at + 1u < length) {
            const uint8_t parameter = text[++at];
            const unsigned command = parameter >> 3;
            if (code == CODE_SYMBOL) line++;
            else if (code == CODE_JUMP || code == CODE_END_HERE) line = 0;   /* what follows is another text's */
            else if (code == CODE_NAME && parameter < 2u) line += 5u;   /* a name of five letters at most */
            else if (code == CODE_COMMAND && (command == 1u || command == 6u)) line += 2u;   /* \num1, \num2 */
            else if (code == CODE_COMMAND && command == 2u) line++;   /* \opt's cursor */
        }
        if (line > longest) longest = line;
    }
    return longest;
}

int oracles_text_font(const OraclesRom *rom, size_t font_start, OraclesTextFont *font)
{
    memset(font, 0, sizeof *font);
    if (font_start + 16u * 0x100u > rom->size) return ORACLES_ERR_RANGE;
    for (unsigned c = ORACLES_TEXT_FIRST_CHARACTER; c < 0x100u; c++) {
        unsigned ink = 0;
        for (unsigned row = 0; row < 16u; row++) ink |= rom->data[font_start + 16u * c + row] ^ 0xffu;
        if (!ink) { font->advance[c] = ORACLES_TEXT_SPACE_ADVANCE; continue; }
        unsigned left = 0, right = ORACLES_TEXT_CELL_WIDTH - 1u;
        while (!(ink & 0x80u >> left)) left++;
        while (!(ink & 0x80u >> right)) right--;
        font->left[c] = (uint8_t)left;
        font->width[c] = (uint8_t)(right - left + 1u);
        font->advance[c] = (uint8_t)(right == ORACLES_TEXT_CELL_WIDTH - 1u ? font->width[c] : font->width[c] + 1u);
    }
    return ORACLES_OK;
}

void oracles_text_font_fixed(const OraclesTextFont *font, OraclesTextFont *fixed)
{
    OraclesTextFont out;
    memset(&out, 0, sizeof out);
    for (unsigned c = ORACLES_TEXT_FIRST_CHARACTER; c < 0x100u; c++) {
        out.width[c] = font->width[c] ? ORACLES_TEXT_CELL_WIDTH : 0u;
        out.advance[c] = ORACLES_TEXT_CELL_WIDTH;
    }
    *fixed = out;
}

void oracles_text_place(OraclesTextPen *pen, const OraclesTextFont *font, uint8_t c, uint8_t attribute, int whole, unsigned *start, unsigned *from,
                        unsigned *drawn)
{
    const unsigned cell = ORACLES_TEXT_CELL_WIDTH, tiles = ORACLES_TEXT_LINE_TILES;
    whole = whole || c < ORACLES_TEXT_FIRST_CHARACTER;
    const unsigned width = whole ? cell : font->width[c];
    if (pen->joined && !whole && width && font->left[c]) pen->pen++;   /* after a glyph that reached its cell's edge, the column this one's cell left */
    pen->joined = !whole && width && font->left[c] + width == cell;
    const unsigned tile = pen->pen / cell;
    if (pen->pen % cell && (whole || (width && tile < tiles && pen->inked[tile] && pen->attribute[tile] != attribute))) pen->pen = (tile + 1u) * cell;
    *start = pen->pen;
    *from = whole ? 0 : font->left[c];
    *drawn = width;
    for (unsigned t = pen->pen / cell; width && t <= (pen->pen + width - 1u) / cell && t < tiles; t++) {
        pen->attribute[t] = attribute;
        pen->inked[t] = pen->marked[t] = 1;
    }
    if (!width && pen->pen / cell < tiles && !pen->inked[pen->pen / cell]) {
        pen->attribute[pen->pen / cell] = attribute;
        pen->marked[pen->pen / cell] = 1;
    }
    if (width) pen->end = pen->pen + width;
    pen->pen += whole ? cell : font->advance[c];
}
