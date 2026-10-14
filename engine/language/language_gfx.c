/* The graphics that carry text: found in the European ROM, served in the composed image.
 *
 * Both games load a screen's graphics through loadGfxHeader: header h is a list of entries, each a compressed graphic
 * (its bank and compression mode, its address), the place it is written to in VRAM or WRAM and its size
 * (ROM_DATA_FORMATS, sections 4.1 and 9.1).  The European loadGfxHeader reads the same headers with one more kind of
 * entry: a first byte 0, then five sources, one per language (wActiveLanguage), then the destination and the size.
 *
 * Extraction.  A header the European ROM chooses by language is loaded, on paper, as the European game does in each
 * language and as the US game does: the bytes that differ between the languages are those that carry text.  A US
 * entry that is the last to write such bytes names a US graphic (its bank and address); the language's version of
 * that graphic is the US one with every byte it is the last to write taken from the European load in the language.
 * The European ROM split some graphics (the text in one entry, the rest in another) and redrew a few in English too:
 * comparing the loads, not the entries, takes both in.  A US graphic that a header the European ROM does not choose
 * by language also loads is left alone (Seasons' castle tileset shares a header index the European ROM gave to text),
 * as is one two headers would want different.  The pack holds each graphic that differs from the US one, compressed
 * here, in the mode that makes it shortest.
 *
 * Composition.  The 6-bit bank of a header (decompressGraphics: `and $3f`) reaches only the first megabyte, not the
 * language's banks: a language's graphics go where the US graphics they replace were, every header that points to
 * one of those now pointing elsewhere, and in the free tails of the graphics banks (`tails`, below); each header
 * entry that named a replaced graphic is repointed (its bank, mode and address; the destination and the size stay).
 * Data only: no code changes, and nothing at all without a language. */
#include "language_internal.h"

#include "guest_tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BANK 0x4000u
#define ENTRY 6u                /* a header's entry */
#define CHOICE 19u              /* a European entry chosen by language: 0, five sources, destination, size */
#define MAX_ENTRIES 64u         /* a header's entries, fall-throughs included */
#define GFX_BYTES 2048u         /* a graphic's largest size: 128 blocks */
#define SPACE_BANKS 16u         /* a destination's bank (its low 4 bits): VRAM 0-1, WRAM 0-7 */
#define SPACE_FIRST 0x8000u     /* VRAM and WRAM: $8000-$dfff */
#define SPACE_BYTES 0x6000u
#define UNWRITTEN 0xffffu

typedef struct gfx_entry {
    unsigned bank, address, mode, dest, blocks;
    size_t at;                  /* where the entry lies in the ROM */
} gfx_entry;

/* ---- the compression (ROM_DATA_FORMATS, section 9.1) -------------------------------------------------------------- */

int language_gfx_decompress(const uint8_t *rom, size_t rom_size, size_t at, unsigned mode, uint8_t *out, size_t size, size_t *consumed)
{
    size_t p = at, n = 0;
#define NEXT(v) do { if (p >= rom_size) return -1; (v) = rom[p++]; } while (0)
    if (mode == 0) {
        if (size > rom_size - at || at > rom_size) return -1;
        memcpy(out, rom + at, size);
        p += size;
    } else if (mode == 2) {
        while (n < size) {
            unsigned keys[2], common = 0;
            NEXT(keys[0]);
            NEXT(keys[1]);
            if (!(keys[0] | keys[1])) {
                for (unsigned i = 0; i < 16u; i++) NEXT(out[n + i]);
            } else {
                NEXT(common);
                for (unsigned i = 0; i < 16u; i++) {
                    if (keys[i / 8u] << (i % 8u) & 0x80u) out[n + i] = (uint8_t)common;
                    else NEXT(out[n + i]);
                }
            }
            n += 16u;
        }
    } else if (mode == 1 || mode == 3) {
        unsigned flags = 0, left = 0;
        while (n < size) {
            if (!left) { NEXT(flags); left = 8; }
            left--;
            const unsigned reference = flags & 0x80u;
            flags = flags << 1 & 0xffu;
            if (!reference) { NEXT(out[n]); n++; continue; }
            unsigned distance, length, b0, b1 = 0;
            NEXT(b0);
            if (mode == 1) {
                distance = (b0 & 0x1fu) + 1u;
                length = b0 >> 5;
                if (length) length++;
                else NEXT(length);
            } else {
                NEXT(b1);
                distance = ((b1 & 0x07u) << 8 | b0) + 1u;
                length = b1 >> 3;
                if (length) length += 2u;
                else NEXT(length);
            }
            if (!length) length = 256u;
            if (distance > n) return -1;
            for (unsigned i = 0; i < length && n < size; i++, n++) out[n] = out[n - distance];
        }
    } else return -1;
#undef NEXT
    if (consumed) *consumed = p - at;
    return 0;
}

/* Mode 2: a tile, 16 bytes, is a key of 16 bits and the byte the most frequent in it, then the others; or a key of 0
 * and the 16 bytes. */
static size_t compress_common(const uint8_t *data, size_t size, uint8_t *out, size_t capacity)
{
    size_t n = 0;
    for (size_t t = 0; t < size; t += 16u) {
        const uint8_t *tile = data + t;
        unsigned best = 0, count = 0;
        for (unsigned i = 0; i < 16u; i++) {
            unsigned c = 0;
            for (unsigned j = 0; j < 16u; j++) c += tile[j] == tile[i];
            if (c > count || (c == count && tile[i] < best)) { count = c; best = tile[i]; }
        }
        if (n + 19u > capacity) return 0;
        if (count < 2u) {
            out[n++] = 0;
            out[n++] = 0;
            memcpy(out + n, tile, 16);
            n += 16u;
            continue;
        }
        unsigned key = 0;
        for (unsigned i = 0; i < 16u; i++) if (tile[i] == best) key |= 0x8000u >> i;
        out[n++] = (uint8_t)(key >> 8);
        out[n++] = (uint8_t)key;
        out[n++] = (uint8_t)best;
        for (unsigned i = 0; i < 16u; i++) if (tile[i] != best) out[n++] = tile[i];
    }
    return n;
}

/* Modes 1 and 3: the cheapest sequence of literals and references, by dynamic programming from the end; a reference
 * of length L at the nearest distance whose match is the longest.  Costs in bits: a literal 9, a reference 9 (mode 1,
 * length 2-8) or 17 (mode 3, length 3-33), 8 more with a length byte (1-256).  The distances tried are those of the
 * earlier places that start with the same two bytes, nearest first (a chain per pair of bytes); the search stops at
 * the first match as long as a match can be. */
static size_t compress_lz(const uint8_t *data, size_t size, unsigned mode, uint8_t *out, size_t capacity)
{
    const unsigned far = mode == 3 ? 0x800u : 0x20u;
    uint32_t *cost = malloc((size + 1u) * sizeof *cost);
    uint16_t *length = malloc((size + 1u) * sizeof *length), *distance = malloc((size + 1u) * sizeof *distance);
    uint16_t *earlier = malloc((size + 1u) * sizeof *earlier), *last = malloc(0x10000u * sizeof *last);
    size_t n = 0;
    if (!cost || !length || !distance || !earlier || !last) goto done;
    /* earlier[i]: the last place before i whose two bytes are those at i, UNWRITTEN for none. */
    memset(last, 0xff, 0x10000u * sizeof *last);
    for (size_t i = 0; i + 1u < size; i++) {
        const unsigned pair = (unsigned)data[i] << 8 | data[i + 1u];
        earlier[i] = last[pair];
        last[pair] = (uint16_t)i;
    }
    cost[size] = 0;
    for (size_t i = size; i-- > 0;) {
        cost[i] = 9u + cost[i + 1u];
        length[i] = 1;
        distance[i] = 0;
        unsigned longest = 0, at = 0;
        const unsigned most = size - i < 256u ? (unsigned)(size - i) : 256u;
        for (size_t j = i + 1u < size ? earlier[i] : UNWRITTEN; j != UNWRITTEN && i - j <= far && longest < most; j = earlier[j]) {
            const unsigned d = (unsigned)(i - j);
            unsigned l = 0;
            while (l < most && data[i + l] == data[i + l - d]) l++;
            if (l > longest) { longest = l; at = d; }
        }
        for (unsigned l = 2; l <= longest; l++) {
            const unsigned bits = mode == 3 ? (l >= 3u && l <= 33u ? 17u : 25u) : (l <= 8u ? 9u : 17u);
            if (bits + cost[i + l] < cost[i]) { cost[i] = bits + cost[i + l]; length[i] = (uint16_t)l; distance[i] = (uint16_t)at; }
        }
    }
    size_t flags = 0;
    unsigned item = 8;
    for (size_t i = 0; i < size; i += length[i]) {
        if (n + 4u > capacity) { n = 0; goto done; }
        if (item == 8u) { flags = n; out[n++] = 0; item = 0; }
        const unsigned l = length[i], d = distance[i];
        if (!d) out[n++] = data[i];
        else {
            out[flags] |= (uint8_t)(0x80u >> item);
            if (mode == 3) {
                const unsigned w = (d - 1u) | (l >= 3u && l <= 33u ? (l - 2u) << 11 : 0u);
                out[n++] = (uint8_t)w;
                out[n++] = (uint8_t)(w >> 8);
                if (l < 3u || l > 33u) out[n++] = (uint8_t)l;
            } else if (l <= 8u) out[n++] = (uint8_t)((d - 1u) | (l - 1u) << 5);
            else { out[n++] = (uint8_t)(d - 1u); out[n++] = (uint8_t)l; }
        }
        item++;
    }
done:
    free(cost);
    free(length);
    free(distance);
    free(earlier);
    free(last);
    return n;
}

size_t language_gfx_compress(const uint8_t *data, size_t size, uint8_t *out, size_t capacity, unsigned *mode)
{
    if (!size || size % 16u || size > GFX_BYTES || capacity < size) return 0;
    uint8_t *trial = malloc(size);
    if (!trial) return 0;
    memcpy(out, data, size);
    size_t best = size;
    *mode = 0;
    for (unsigned m = 1; m <= 3u; m++) {
        const size_t n = m == 2 ? compress_common(data, size, trial, size) : compress_lz(data, size, m, trial, size);
        if (n && n < best) { best = n; *mode = m; memcpy(out, trial, n); }
    }
    free(trial);
    return best;
}

/* ---- the headers ------------------------------------------------------------------------------------------------- */

/* The entries of header `h` of the table at `table` (physical); a European entry chosen by language gives its source in
 * the language of index `european` (language_european_index).  The number of entries, or -1 when the header leaves the
 * ROM or does not end. */
static int header_entries(const uint8_t *rom, size_t size, size_t table, unsigned h, unsigned european, gfx_entry *out)
{
    if (table + 2u * h + 2u > size) return -1;
    const unsigned pointer = rom[table + 2u * h] | rom[table + 2u * h + 1u] << 8;
    if (pointer < BANK || pointer >= 2u * BANK) return -1;
    size_t at = table / BANK * BANK + (pointer - BANK);
    for (unsigned n = 0; n < MAX_ENTRIES; n++) {
        if (at + CHOICE > size) return -1;
        const uint8_t *e = rom + at;
        gfx_entry *x = &out[n];
        x->at = at;
        const uint8_t *source = e, *rest = e + 3;
        if (!e[0]) { source = e + 1 + 3u * european; rest = e + 16; at += CHOICE; }
        else at += ENTRY;
        x->bank = source[0] & 0x3fu;
        x->mode = source[0] >> 6;
        x->address = (unsigned)source[1] << 8 | source[2];
        x->dest = (unsigned)rest[0] << 8 | rest[1];
        x->blocks = rest[2] & 0x7fu;
        if (!(rest[2] & 0x80u)) return (int)n + 1;
    }
    return -1;
}

/* Whether a header of the European ROM has an entry chosen by language. */
static int chosen_by_language(const uint8_t *rom, size_t size, size_t table, unsigned h)
{
    gfx_entry entries[MAX_ENTRIES];
    const int n = header_entries(rom, size, table, h, 0, entries);
    for (int i = 0; i < n; i++) if (!rom[entries[i].at]) return 1;
    return 0;
}

static size_t source_of(const gfx_entry *e) { return oracles_rom_offset(e->bank, e->address); }

/* The place of a destination's byte i in the memory model, or SIZE_MAX outside VRAM and WRAM. */
static size_t place(unsigned dest, unsigned i)
{
    const unsigned address = (dest & 0xfff0u) + i;
    if (address < SPACE_FIRST || address >= SPACE_FIRST + SPACE_BYTES) return (size_t)-1;
    return (size_t)(dest & 0xfu) * SPACE_BYTES + (address - SPACE_FIRST);
}

/* Loads the entries into `memory` (UNWRITTEN where nothing was written), or the index of the last entry to write each
 * byte into `writer`; 0, or -1 when an entry does not decompress or writes outside VRAM and WRAM. */
static int load(const uint8_t *rom, size_t size, const gfx_entry *entries, int count, uint16_t *memory, int16_t *writer)
{
    uint8_t data[GFX_BYTES];
    for (int j = 0; j < count; j++) {
        const gfx_entry *e = &entries[j];
        const size_t bytes = 16u * (e->blocks + 1u);
        if (!e->bank || language_gfx_decompress(rom, size, source_of(e), e->mode, data, bytes, NULL) != 0) return -1;
        for (unsigned i = 0; i < bytes; i++) {
            const size_t k = place(e->dest, i);
            if (k == (size_t)-1) return -1;
            if (memory) memory[k] = data[i];
            if (writer) writer[k] = (int16_t)j;
        }
    }
    return 0;
}

/* Clears what the entries wrote. */
static void unload(const gfx_entry *entries, int count, uint16_t *memory, int16_t *writer)
{
    for (int j = 0; j < count; j++)
        for (unsigned i = 0; i < 16u * (entries[j].blocks + 1u); i++) {
            const size_t k = place(entries[j].dest, i);
            if (k == (size_t)-1) continue;
            if (memory) memory[k] = UNWRITTEN;
            if (writer) writer[k] = -1;
        }
}

/* ---- the extraction ---------------------------------------------------------------------------------------------- */

typedef struct graphic {
    unsigned bank, address, mode, blocks;
    int excluded;                                  /* also loaded where the European ROM does not choose by language */
    int found;                                     /* a header the European ROM chooses by language loads text into it */
    int conflict;                                  /* two such headers want different bytes */
    uint8_t *data[ORACLES_LANGUAGE_COUNT];         /* the US graphic [ORACLES_LANGUAGE_ENGLISH], each language's */
    uint8_t *set[ORACLES_LANGUAGE_COUNT];          /* the bytes a European load decided */
} graphic;

typedef struct extraction {
    const OraclesRom *eu, *us;
    size_t eu_table, us_table;
    graphic *graphics;
    unsigned count, capacity;
    uint16_t *memory[EUROPEAN_LANGUAGES];          /* by the European ROM's index of a language */
    int16_t *writer;
} extraction;

static graphic *graphic_of(extraction *x, const gfx_entry *e)
{
    for (unsigned i = 0; i < x->count; i++)
        if (x->graphics[i].bank == e->bank && x->graphics[i].address == e->address) return &x->graphics[i];
    if (x->count == x->capacity) {
        const unsigned capacity = x->capacity ? 2u * x->capacity : 64u;
        graphic *grown = realloc(x->graphics, capacity * sizeof *grown);
        if (!grown) return NULL;
        x->graphics = grown;
        x->capacity = capacity;
    }
    graphic *g = &x->graphics[x->count++];
    memset(g, 0, sizeof *g);
    g->bank = e->bank;
    g->address = e->address;
    g->mode = e->mode;
    g->blocks = e->blocks;
    return g;
}

/* The US graphics every US header loads, the largest size each is loaded with, and those also loaded by a header the
 * European ROM does not choose by language. */
static int list_graphics(extraction *x, const uint8_t *language_headers)
{
    gfx_entry entries[MAX_ENTRIES];
    for (unsigned h = 0; h < GFX_HEADERS; h++) {
        const int n = header_entries(x->us->data, x->us->size, x->us_table, h, 0, entries);
        if (n < 0) return -1;
        for (int j = 0; j < n; j++) {
            graphic *g = graphic_of(x, &entries[j]);
            if (!g) return -1;
            if (entries[j].blocks > g->blocks) g->blocks = entries[j].blocks;
            if (entries[j].mode != g->mode) g->excluded = 1;
            if (!language_headers[h]) g->excluded = 1;
        }
    }
    return 0;
}

/* Header h, which the European ROM chooses by language: the bytes of each US graphic that carry text there. */
static int extract_header(extraction *x, unsigned h)
{
    gfx_entry us[MAX_ENTRIES], eu[EUROPEAN_LANGUAGES][MAX_ENTRIES];
    int eu_count[EUROPEAN_LANGUAGES];
    const int us_count = header_entries(x->us->data, x->us->size, x->us_table, h, 0, us);
    int status = us_count > 0 ? load(x->us->data, x->us->size, us, us_count, NULL, x->writer) : -1;
    for (unsigned l = 0; l < EUROPEAN_LANGUAGES; l++) {
        eu_count[l] = header_entries(x->eu->data, x->eu->size, x->eu_table, h, l, eu[l]);
        if (status == 0) status = eu_count[l] > 0 ? load(x->eu->data, x->eu->size, eu[l], eu_count[l], x->memory[l], NULL) : -1;
    }
    for (int j = 0; status == 0 && j < us_count; j++) {
        const gfx_entry *e = &us[j];
        const unsigned bytes = 16u * (e->blocks + 1u);
        int text = 0;
        for (unsigned i = 0; i < bytes && !text; i++) {
            const size_t k = place(e->dest, i);
            if (x->writer[k] != j) continue;
            for (unsigned l = 1; l < EUROPEAN_LANGUAGES; l++) text |= x->memory[l][k] != x->memory[0][k];
        }
        if (!text) continue;
        graphic *g = graphic_of(x, e);
        if (!g) { status = -1; break; }
        if (g->excluded) continue;
        const size_t size = 16u * (g->blocks + 1u);
        if (!g->found) {
            for (unsigned l = 0; l < ORACLES_LANGUAGE_COUNT && status == 0; l++) {
                g->data[l] = malloc(size);
                g->set[l] = calloc(size, 1);
                if (!g->data[l] || !g->set[l]) status = -1;
            }
            uint8_t *us_graphic = g->data[ORACLES_LANGUAGE_ENGLISH];
            if (status == 0 && language_gfx_decompress(x->us->data, x->us->size, source_of(e), g->mode, us_graphic, size, NULL) != 0) status = -1;
            for (unsigned l = ORACLES_LANGUAGE_ENGLISH_EU; l < ORACLES_LANGUAGE_COUNT && status == 0; l++) memcpy(g->data[l], us_graphic, size);
            g->found = 1;
        }
        for (unsigned i = 0; i < bytes && status == 0; i++) {
            const size_t k = place(e->dest, i);
            if (x->writer[k] != j) continue;
            for (unsigned l = ORACLES_LANGUAGE_ENGLISH_EU; l < ORACLES_LANGUAGE_COUNT; l++) {
                const uint16_t *memory = x->memory[language_european_index((OraclesLanguage)l)];
                if (memory[k] == UNWRITTEN) continue;
                if (g->set[l][i] && g->data[l][i] != memory[k]) g->conflict = 1;
                g->data[l][i] = (uint8_t)memory[k];
                g->set[l][i] = 1;
            }
        }
    }
    if (us_count > 0) unload(us, us_count, NULL, x->writer);
    for (unsigned l = 0; l < EUROPEAN_LANGUAGES; l++) if (eu_count[l] > 0) unload(eu[l], eu_count[l], x->memory[l], NULL);
    return status;
}

static int by_source(const void *a, const void *b)
{
    const graphic *x = a, *y = b;
    const unsigned p = x->bank << 16 | x->address, q = y->bank << 16 | y->address;
    return (p > q) - (p < q);
}

int language_gfx_extract(OraclesLanguagePack *pack, const OraclesRom *eu, const OraclesRom *us, char *error, size_t capacity)
{
    extraction x = { eu, us, language_gfx_headers_european(pack->game), language_gfx_headers_us(pack->game), NULL, 0, 0, { NULL }, NULL };
    uint8_t language_headers[GFX_HEADERS];
    int status = x.eu_table && x.us_table ? 0 : -1;
    for (unsigned l = 0; l < EUROPEAN_LANGUAGES && status == 0; l++) {
        x.memory[l] = malloc(SPACE_BANKS * SPACE_BYTES * sizeof *x.memory[l]);
        if (!x.memory[l]) status = -1;
        else for (size_t i = 0; i < SPACE_BANKS * SPACE_BYTES; i++) x.memory[l][i] = UNWRITTEN;
    }
    if (status == 0 && !(x.writer = malloc(SPACE_BANKS * SPACE_BYTES * sizeof *x.writer))) status = -1;
    if (status == 0) for (size_t i = 0; i < SPACE_BANKS * SPACE_BYTES; i++) x.writer[i] = -1;
    for (unsigned h = 0; h < GFX_HEADERS && status == 0; h++) language_headers[h] = (uint8_t)chosen_by_language(eu->data, eu->size, x.eu_table, h);
    if (status == 0) status = list_graphics(&x, language_headers);
    for (unsigned h = 0; h < GFX_HEADERS && status == 0; h++) if (language_headers[h]) status = extract_header(&x, h);
    uint8_t *compressed = malloc(GFX_BYTES);
    if (!compressed) status = -1;
    if (status == 0) qsort(x.graphics, x.count, sizeof *x.graphics, by_source);
    for (unsigned i = 0; i < x.count && status == 0; i++) {
        const graphic *g = &x.graphics[i];
        if (!g->found || g->conflict) continue;
        const size_t size = 16u * (g->blocks + 1u);
        for (unsigned l = ORACLES_LANGUAGE_ENGLISH_EU; l < ORACLES_LANGUAGE_COUNT && status == 0; l++) {
            if (!memcmp(g->data[l], g->data[ORACLES_LANGUAGE_ENGLISH], size)) continue;
            unsigned mode = 0;
            const size_t length = language_gfx_compress(g->data[l], size, compressed, GFX_BYTES, &mode);
            const OraclesLanguageGraphic made = { (uint8_t)g->bank, (uint16_t)g->address, (uint8_t)g->blocks, (uint8_t)mode, (uint16_t)length, compressed };
            if (!length || language_pack_add_graphic(pack, (OraclesLanguage)l, &made) != 0) status = -1;
        }
    }
    free(compressed);
    for (unsigned i = 0; i < x.count; i++)
        for (unsigned l = 0; l < ORACLES_LANGUAGE_COUNT; l++) { free(x.graphics[i].data[l]); free(x.graphics[i].set[l]); }
    free(x.graphics);
    for (unsigned l = 0; l < EUROPEAN_LANGUAGES; l++) free(x.memory[l]);
    free(x.writer);
    if (status != 0) snprintf(error, capacity, "the graphics of the European ROM cannot be read");
    return status;
}

/* ---- the composition --------------------------------------------------------------------------------------------- */

/* The free tails of the graphics banks: what follows the last graphic of a bank, filled ($00 in Ages, the bank's
 * number in Seasons) and read by nothing (ROM_DATA_FORMATS, section 15); used only when the image still has the fill. */
static const struct tail { OraclesGame game; unsigned bank, address; } tails[] = {
    { ORACLES_GAME_AGES, 0x38, 0x6aec },      /* after oth_twinrova_normal_layout */
    { ORACLES_GAME_SEASONS, 0x38, 0x7e58 },   /* after spr_caravan */
    { ORACLES_GAME_SEASONS, 0x1a, 0x70e0 },   /* after spr_magnet_gloves */
};

typedef struct range { size_t start, end; } range;

static int by_start(const void *a, const void *b)
{
    const range *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start;
}

typedef struct placed { unsigned index; size_t length, at; } placed;

static int by_length(const void *a, const void *b)
{
    const placed *x = a, *y = b;
    return x->length > y->length ? -1 : x->length < y->length ? 1 : (x->index > y->index) - (x->index < y->index);
}

int language_gfx_compose(const OraclesLanguagePack *pack, OraclesLanguage language, uint8_t *image, size_t size, OraclesLanguageReport *report,
                         char *error, size_t capacity)
{
    const unsigned count = language < ORACLES_LANGUAGE_COUNT ? pack->graphic_count[language] : 0;
    if (!count) return 0;
    const size_t table = language_gfx_headers_us(pack->game);
    const pack_graphic *graphics = pack->graphics[language];
    gfx_entry entries[MAX_ENTRIES];
    /* Every entry of every header: the replaced graphics' entries, and the places the others read. */
    size_t *repoint = malloc(GFX_HEADERS * MAX_ENTRIES * sizeof *repoint);
    unsigned *owner = malloc(GFX_HEADERS * MAX_ENTRIES * sizeof *owner);
    range *pool = malloc((count + sizeof tails / sizeof tails[0]) * sizeof *pool);
    placed *order = malloc(count * sizeof *order);
    size_t *kept = malloc(GFX_HEADERS * MAX_ENTRIES * sizeof *kept);
    unsigned entry_count = 0, kept_count = 0, ranges = 0;
    int status = repoint && owner && pool && order && kept && table ? 0 : -1;
    if (status != 0) snprintf(error, capacity, "out of memory");
    for (unsigned h = 0; h < GFX_HEADERS && status == 0; h++) {
        const int n = header_entries(image, size, table, h, 0, entries);
        if (n < 0) { snprintf(error, capacity, "the image's graphics headers are not the US ROM's"); status = -1; break; }
        for (int j = 0; j < n; j++) {
            unsigned g = 0;
            while (g < count && (graphics[g].bank != entries[j].bank || graphics[g].address != entries[j].address)) g++;
            int seen = 0;
            for (unsigned k = 0; k < entry_count && !seen; k++) seen = repoint[k] == entries[j].at;
            for (unsigned k = 0; k < kept_count && !seen; k++) seen = kept[k] == source_of(&entries[j]);
            if (seen) continue;
            if (g < count) { repoint[entry_count] = entries[j].at; owner[entry_count++] = g; }
            else kept[kept_count++] = source_of(&entries[j]);
        }
    }
    /* The pool: the replaced graphics' bytes, and the free tails. */
    uint8_t *data = malloc(GFX_BYTES);
    if (!data && status == 0) { snprintf(error, capacity, "out of memory"); status = -1; }
    for (unsigned g = 0; g < count && status == 0; g++) {
        int named = 0;
        for (unsigned k = 0; k < entry_count && !named; k++) named = owner[k] == g;
        size_t consumed = 0;
        const size_t at = oracles_rom_offset(graphics[g].bank, graphics[g].address);
        unsigned mode = 4;
        for (unsigned k = 0; k < entry_count; k++)
            if (owner[k] == g) mode = image[repoint[k]] >> 6;
        if (!named || language_gfx_decompress(image, size, at, mode, data, 16u * (graphics[g].blocks + 1u), &consumed) != 0) {
            snprintf(error, capacity, "the image's graphics headers do not load the US graphic %02x:%04x", graphics[g].bank, graphics[g].address);
            status = -1;
            break;
        }
        pool[ranges++] = (range){ at, at + consumed };
    }
    for (size_t t = 0; t < sizeof tails / sizeof tails[0] && status == 0; t++) {
        if (tails[t].game != pack->game) continue;
        const size_t start = oracles_rom_offset(tails[t].bank, tails[t].address), end = oracles_rom_offset(tails[t].bank, 0x8000u);
        const uint8_t fill = pack->game == ORACLES_GAME_SEASONS ? (uint8_t)tails[t].bank : 0;
        size_t i = start;
        while (i < end && end <= size && image[i] == fill) i++;
        if (i == end) pool[ranges++] = (range){ start, end };
    }
    free(data);
    /* Merged, and none holding a graphic the image keeps. */
    if (status == 0) {
        qsort(pool, ranges, sizeof *pool, by_start);
        unsigned merged = 0;
        for (unsigned r = 0; r < ranges; r++) {
            if (merged && pool[r].start <= pool[merged - 1u].end) { if (pool[r].end > pool[merged - 1u].end) pool[merged - 1u].end = pool[r].end; }
            else pool[merged++] = pool[r];
        }
        ranges = merged;
        for (unsigned k = 0; k < kept_count; k++)
            for (unsigned r = 0; r < ranges; r++)
                if (kept[k] >= pool[r].start && kept[k] < pool[r].end) {
                    snprintf(error, capacity, "the image keeps a graphic at %06zx among those the language replaces", kept[k]);
                    status = -1;
                }
    }
    /* The longest first, each where it first fits. */
    for (unsigned g = 0; g < count && status == 0; g++) order[g] = (placed){ g, graphics[g].length, 0 };
    if (status == 0) qsort(order, count, sizeof *order, by_length);
    for (unsigned o = 0; o < count && status == 0; o++) {
        unsigned r = 0;
        while (r < ranges && pool[r].end - pool[r].start < order[o].length) r++;
        if (r == ranges) {
            snprintf(error, capacity, "the %s graphics do not fit the image", oracles_language_code(language));
            status = -1;
            break;
        }
        order[o].at = pool[r].start;
        pool[r].start += order[o].length;
    }
    /* Written, then every entry that named a replaced graphic pointed to the language's. */
    for (unsigned o = 0; o < count && status == 0; o++) {
        const pack_graphic *g = &graphics[order[o].index];
        memcpy(image + order[o].at, pack->bytes + g->at, g->length);
        const unsigned bank = (unsigned)(order[o].at / BANK), address = BANK + (unsigned)(order[o].at % BANK);
        for (unsigned k = 0; k < entry_count; k++) {
            if (owner[k] != order[o].index) continue;
            image[repoint[k]] = (uint8_t)(bank | g->mode << 6);
            image[repoint[k] + 1u] = (uint8_t)(address >> 8);
            image[repoint[k] + 2u] = (uint8_t)address;
        }
        report->graphics++;
        report->graphics_bytes += g->length;
    }
    free(repoint);
    free(owner);
    free(pool);
    free(order);
    free(kept);
    return status;
}
