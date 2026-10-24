#include "text_box.h"

#include "ppu.h"

#include <string.h>

#define TEXT_ROWS (2u * ORACLES_TEXT_BOX_GAME_LINES)   /* the game's rows of text, before the arrow's */
#define ARROW_ROW TEXT_ROWS
#define LAST_COLUMN (ORACLES_TEXT_BOX_GAME_COLUMNS - 1u)
#define CELL_BYTES 32u
#define NO_ROW 0xffu

static int blank(uint8_t tile, uint8_t attribute) { return tile == ORACLES_TEXT_BOX_BLANK_TILE && attribute == ORACLES_TEXT_BOX_BLANK_ATTRIBUTE; }

/* A tile of the lines' VRAM, bank 0: $40 to $7f. */
static int line_tile(uint8_t tile, uint8_t attribute) { return tile >= ORACLES_TEXT_BOX_TOP_TILES && tile < 0x80u && !(attribute & 0x08u); }

OraclesTextBoxRead oracles_text_box_read(const uint8_t tiles[ORACLES_TEXT_BOX_GAME_ROWS * ORACLES_TEXT_BOX_GAME_COLUMNS],
                                         const uint8_t attributes[ORACLES_TEXT_BOX_GAME_ROWS * ORACLES_TEXT_BOX_GAME_COLUMNS],
                                         const uint8_t known[ORACLES_GUEST_TEXT_BOX_LINES], OraclesTextBoxModel *model)
{
    memset(model, 0, sizeof *model);
    for (unsigned r = 0; r < ORACLES_TEXT_BOX_GAME_ROWS; r++)
        if (!blank(tiles[r * ORACLES_TEXT_BOX_GAME_COLUMNS], attributes[r * ORACLES_TEXT_BOX_GAME_COLUMNS]) || (r < TEXT_ROWS && !blank(tiles[r * ORACLES_TEXT_BOX_GAME_COLUMNS + LAST_COLUMN], attributes[r * ORACLES_TEXT_BOX_GAME_COLUMNS + LAST_COLUMN]))) return ORACLES_TEXT_BOX_NOT_SHOWN;
    for (unsigned r = 0; r < ORACLES_TEXT_BOX_GAME_ROWS; r++) {
        OraclesTextBoxRow *row = &model->row[r];
        int gap = 0;
        for (unsigned c = 0; c < ORACLES_TEXT_BOX_GAME_COLUMNS; c++) {
            const uint8_t tile = tiles[r * ORACLES_TEXT_BOX_GAME_COLUMNS + c], attribute = attributes[r * ORACLES_TEXT_BOX_GAME_COLUMNS + c];
            const int text_column = r < TEXT_ROWS && c >= 1u && c < LAST_COLUMN;
            if (line_tile(tile, attribute)) {
                if (!text_column) return ORACLES_TEXT_BOX_UNEXPECTED;
                const unsigned j = c - 1u, line = tile >= ORACLES_TEXT_BOX_BOTTOM_TILES ? ORACLES_GUEST_TEXT_BOX_BOTTOM : ORACLES_GUEST_TEXT_BOX_TOP;
                if (((tile & 0x1fu) >> 1) != j || gap) return ORACLES_TEXT_BOX_UNEXPECTED;
                if (!row->line) { row->line = (uint8_t)line; row->half = tile & 1u; }
                else if (row->line != line || row->half != (tile & 1u)) return ORACLES_TEXT_BOX_UNEXPECTED;
                row->shown = (uint8_t)(j + 1u);
                row->pixels = (uint16_t)(8u * row->shown);
                continue;
            }
            if (blank(tile, attribute)) { gap = text_column; continue; }
            OraclesTextBoxMark *m = &model->mark[model->marks++];
            m->anchor = (uint8_t)(c == 0 ? ORACLES_TEXT_BOX_LEFT : c == LAST_COLUMN ? ORACLES_TEXT_BOX_RIGHT : ORACLES_TEXT_BOX_TEXT);
            m->column = (uint8_t)(m->anchor == ORACLES_TEXT_BOX_TEXT ? c - 1u : 0);
            m->row = (uint8_t)r;
            m->tile = tile;
            m->attribute = attribute;
        }
        if (row->line && !known[row->line]) return ORACLES_TEXT_BOX_LINE_UNKNOWN;
    }
    return ORACLES_TEXT_BOX_READ;
}

void oracles_text_box_reveal(OraclesTextBoxModel *model, const OraclesGuestTextBoxReveal *reveal, const OraclesGuestTextBoxLine *const lines[ORACLES_GUEST_TEXT_BOX_LINES])
{
    for (unsigned r = 0; r < ORACLES_TEXT_BOX_GAME_ROWS; r++) {
        OraclesTextBoxRow *row = &model->row[r];
        if (!row->line || !lines[row->line]) continue;
        const OraclesGuestTextBoxLine *line = lines[row->line];
        const unsigned whole = 8u * line->columns;
        const unsigned pixels = row->line == reveal->line ? oracles_guest_text_box_reveal_pixels(line, reveal->shown, reveal->elapsed) : ORACLES_GUEST_TEXT_BOX_WHOLE;
        row->pixels = (uint16_t)(pixels < whole ? pixels : whole);
    }
}

/* Colour `index` of BG palette `palette`, as the PPU converts it. */
static uint32_t palette_colour(const OraclesTextBoxSource *s, unsigned palette, unsigned index)
{
    const unsigned at = (palette & 7u) * 8u + index * 2u;
    const uint16_t c = (uint16_t)((s->bg_palettes[at] | s->bg_palettes[at + 1u] << 8) & 0x7fffu);
    return s->colours ? s->colours[c] : oracles_ppu_rgb555(c);
}

/* An 8 x 8 tile's row pair at `data` (its 16 bytes, or a line's cell from the half's first row) drawn at `out`. */
static void draw_tile(const uint32_t colours[32], const uint8_t *data, uint8_t attribute, uint32_t *out, unsigned stride)
{
    for (unsigned y = 0; y < 8u; y++) {
        const unsigned row = attribute & 0x40u ? 7u - y : y;
        const uint8_t lo = data[2u * row], hi = data[2u * row + 1u];
        for (unsigned x = 0; x < 8u; x++) {
            const unsigned bit = attribute & 0x20u ? x : 7u - x;
            out[y * stride + x] = colours[(attribute & 7u) * 4u + (((lo >> bit) & 1u) | ((hi >> bit) & 1u) << 1)];
        }
    }
}

static const uint8_t *game_tile(const OraclesTextBoxSource *s, uint8_t tile, uint8_t attribute)
{
    const unsigned bank = attribute & 0x08u ? 0x2000u : 0u;
    const unsigned at = s->signed_tiles ? (tile < 0x80u ? 0x1000u + 16u * tile : 0x800u + 16u * (tile - 0x80u)) : 16u * tile;
    return s->vram + bank + at;
}

void oracles_text_box_draw(const OraclesTextBoxModel *model, const OraclesTextBoxSource *source, unsigned pixels, unsigned lines,
                           uint32_t *out, unsigned stride)
{
    if (lines < ORACLES_TEXT_BOX_GAME_LINES) lines = ORACLES_TEXT_BOX_GAME_LINES;
    if (lines > ORACLES_TEXT_BOX_LINES_MAX) lines = ORACLES_TEXT_BOX_LINES_MAX;
    if (pixels > ORACLES_GUEST_TEXT_BOX_COLUMNS_MAX * 8u) pixels = ORACLES_GUEST_TEXT_BOX_COLUMNS_MAX * 8u;
    const unsigned columns = pixels / 8u + 2u, rows = 2u * lines + 1u;
    uint32_t colours[32];
    for (unsigned i = 0; i < 32u; i++) colours[i] = palette_colour(source, i >> 2, i & 3u);
    /* The marks where they go in the box drawn: by their anchor, on the row that shows theirs. */
    static const uint8_t no_mark = 0xffu;
    uint8_t marked[2u * ORACLES_TEXT_BOX_LINES_MAX + 1u][ORACLES_GUEST_TEXT_BOX_COLUMNS_MAX + 2u];
    memset(marked, no_mark, sizeof marked);
    for (unsigned i = 0; i < model->marks; i++) {
        const OraclesTextBoxMark *m = &model->mark[i];
        const unsigned row = m->row == ARROW_ROW ? rows - 1u : m->row;
        /* A mark on a line's row moves with the line: a page the game laid out, centred in a wider box. */
        unsigned shift = 0;
        if (m->anchor == ORACLES_TEXT_BOX_TEXT && m->row < TEXT_ROWS) {
            const OraclesTextBoxRow *own = &model->row[m->row], *pair = &model->row[m->row ^ 1u];
            const unsigned which = own->line ? own->line : pair->line;
            if (which && source->lines[which]) shift = source->lines[which]->offset / 8u;
        }
        const unsigned column = m->anchor == ORACLES_TEXT_BOX_LEFT ? 0u : m->anchor == ORACLES_TEXT_BOX_RIGHT ? columns - 1u : m->column + 1u + shift;
        if (column < columns - (m->anchor == ORACLES_TEXT_BOX_TEXT ? 1u : 0u)) marked[row][column] = (uint8_t)i;
    }
    for (unsigned r = 0; r < rows; r++) {
        const unsigned game_row = r == rows - 1u ? ARROW_ROW : r < TEXT_ROWS ? r : NO_ROW;
        const OraclesTextBoxRow *row = game_row == NO_ROW ? NULL : &model->row[game_row];
        const OraclesGuestTextBoxLine *line = row && row->line ? source->lines[row->line] : NULL;
        for (unsigned c = 0; c < columns; c++) {
            uint32_t *at = out + r * 8u * stride + c * 8u;
            if (marked[r][c] != no_mark) {
                const OraclesTextBoxMark *m = &model->mark[marked[r][c]];
                draw_tile(colours, game_tile(source, m->tile, m->attribute), m->attribute, at, stride);
                continue;
            }
            const unsigned j = c - 1u;
            if (line && c >= 1u && c + 1u < columns && 8u * j < row->pixels && j < line->columns) {
                const uint8_t *data = line->gfx + j * CELL_BYTES + row->half * 16u;
                uint8_t cut[16];
                if (8u * j + 8u > row->pixels) {   /* the tile the reveal stops in: its pixels after it blank, colour 3 */
                    const uint8_t blank_bits = (uint8_t)(0xffu >> (row->pixels - 8u * j));
                    for (unsigned i = 0; i < 16u; i++) cut[i] = (uint8_t)(data[i] | blank_bits);
                    data = cut;
                }
                draw_tile(colours, data, line->attribute[j], at, stride);
                continue;
            }
            draw_tile(colours, game_tile(source, ORACLES_TEXT_BOX_BLANK_TILE, ORACLES_TEXT_BOX_BLANK_ATTRIBUTE), ORACLES_TEXT_BOX_BLANK_ATTRIBUTE, at, stride);
        }
    }
}
