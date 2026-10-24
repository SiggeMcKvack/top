/* A language composed into the image the core runs.
 *
 * The image is the one whose texts the pack translates: the US ROM of the pack's game, alone or with the mods' houses
 * (mod_compose.c), which leave the language's banks empty, and grows to 2 MiB; or a fan game made on it, which keeps
 * its size and leaves its own empty banks to the language (its profile names the first, profile.h).  From that bank
 * (ORACLES_IMAGE_LANGUAGE_BANK in an original) to bank $7f: the index (oracles_rom.h), then the language's texts, each within one bank and identical texts stored once, as the game reads them: a text crosses
 * no bank, so nothing depends on how the game steps from one bank to the next.  An entry goes to every text the US
 * game has: the text the pack serves for it, or English when the pack keeps it or has none (language_extract.c says
 * which, and checked every text it serves against the US one).  The glyphs of the European font that differ from the US one are written
 * over it, where the game reads each character as it prints it (ROM_DATA_FORMATS, section 8.1).  The graphics that
 * carry text go in the first megabyte of an original, their headers repointed (language_gfx.c); a fan game keeps its
 * own.  Data only: the game's code is the image's, and so is every byte it reads in English.
 *
 * For the compact text, the texts are laid out again (language_wrap.c) in the font of the image composed, its
 * European glyphs written first: a text laid out again is stored so, its entry marked (ORACLES_LANGUAGE_LAID_OUT); one
 * the game's tables keep (config/vwf_keep_<game>.tsv), or that another text calls or jumps to within a line, keeps its
 * layout.  English (US) has no pack: its index serves the US texts laid out again, the others kept in English; a fan
 * game's English is so laid out, its texts read in its image (the compact text lays it out in its English alone). */
#include "language_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BANK 0x4000u
#define ENTRY_BYTES 3u
#define STORED_SLOTS 8192u   /* a power of two, above the texts of a language */
#define GROUP_TEXTS 0x100u
#define INVENTORY_GROUP 0x09u   /* TX_09xx: the inventory's names and descriptions, which scroll on a line of their own */

typedef struct composition {
    uint8_t *im;
    size_t size;                  /* the image's bytes */
    size_t next;                  /* the next free byte of the language's banks */
    size_t end;                   /* past their last */
    language_slot slots[STORED_SLOTS];   /* the texts stored, by the hash of their bytes */
    /* The compact text's layout: NULL without it. */
    language_wrap *wrap;
    language_keep *keep;
    uint8_t inline_target[0x100][GROUP_TEXTS / 8u];   /* by internal group: the texts called or jumped to within a line */
    uint8_t laid[ORACLES_TEXT_MAX];
} composition;

/* Where the text lies in the image, stored there now if it was not; 0 when the banks are full. */
static size_t store(composition *c, const uint8_t *text, size_t length, unsigned *bytes)
{
    language_slot *slot = language_slot_of(c->slots, STORED_SLOTS, c->im, text, length);
    if (slot->length) return slot->at;
    if (c->next % BANK + length > BANK) c->next = (c->next / BANK + 1u) * BANK;
    if (c->next + length > c->end) return 0;
    const size_t at = c->next;
    memcpy(c->im + at, text, length);
    c->next += length;
    *slot = (language_slot){ (uint32_t)at, (uint16_t)length, 0 };
    *bytes += (unsigned)length;
    return at;
}

static void put_entry(uint8_t *entry, unsigned bank, unsigned addr)
{
    entry[0] = (uint8_t)bank;
    entry[1] = (uint8_t)addr;
    entry[2] = (uint8_t)(addr >> 8);
}

/* The text of TX_`id` the language has: the pack's, or for English (US) the US ROM's (`us`, decoded into `buffer`);
 * NULL when it has none. */
static const uint8_t *text_of(const composition *c, const OraclesLanguagePack *pack, OraclesLanguage language, const OraclesTextSource *us,
                              unsigned groups, unsigned id, uint8_t *buffer, size_t *length)
{
    if (language != ORACLES_LANGUAGE_ENGLISH) return oracles_language_pack_text(pack, language, id, length);
    const OraclesRom rom = { c->im, c->size };
    return language_text_of(&rom, us, groups, id, buffer, length) == 0 ? buffer : NULL;
}

/* The text served for TX_`id` laid out again: `text` itself, or c->laid; *laid_out set when it is. */
static const uint8_t *laid_out(composition *c, OraclesLanguage language, unsigned group, unsigned id, const uint8_t *text, size_t *length,
                               int *laid, OraclesLanguageReport *report)
{
    *laid = 0;
    if (group == ORACLES_TEXT_DICTIONARY_GROUPS + INVENTORY_GROUP) return text;
    const unsigned before = language_text_boxes(text, *length);
    report->boxes += before;
    const uint8_t *served = text;
    if (language_keep_has(c->keep, language, id) || c->inline_target[group][(id & 0xffu) / 8u] & 1u << (id & 7u)) report->layout_kept++;
    else {
        size_t n = 0;
        const language_wrap_result result = language_wrap_text(c->wrap, text, *length, c->laid, sizeof c->laid, &n);
        if (result == WRAP_DONE) {
            served = c->laid;
            *length = n;
            *laid = 1;
            report->laid_out++;
        } else if (result == WRAP_FAILED) report->layout_failed++;
    }
    report->boxes_laid_out += served == text ? before : language_text_boxes(served, *length);
    return served;
}

/* The entries of one internal group of the US text, written at `entries`. */
static int compose_group(composition *c, const OraclesLanguagePack *pack, OraclesLanguage language, const OraclesTextSource *us, unsigned groups,
                         unsigned group, unsigned count, uint8_t *entries, OraclesLanguageReport *report, char *error, size_t capacity)
{
    uint8_t buffer[ORACLES_TEXT_MAX];
    for (unsigned index = 0; index < count; index++) {
        const unsigned id = (group - ORACLES_TEXT_DICTIONARY_GROUPS) << 8 | index;
        uint8_t *entry = entries + ENTRY_BYTES * index;
        size_t length = 0;
        const uint8_t *text = text_of(c, pack, language, us, groups, id, buffer, &length);
        int laid = 0;
        if (text && c->wrap) text = laid_out(c, language, group, id, text, &length, &laid, report);
        if (!text || (language == ORACLES_LANGUAGE_ENGLISH && !laid)) {
            const int kept = language == ORACLES_LANGUAGE_ENGLISH || oracles_language_pack_source_of(pack, language, id) != ORACLES_LANGUAGE_SOURCE_MISSING;
            put_entry(entry, 0, kept ? ORACLES_LANGUAGE_KEPT : 0u);
            if (kept) report->english++;
            else report->missing++;
            continue;
        }
        const size_t at = store(c, text, length, &report->bytes);
        if (!at) { snprintf(error, capacity, "the %s texts do not fit the image's language banks", oracles_language_code(language)); return -1; }
        put_entry(entry, (unsigned)(at / BANK), (BANK + (unsigned)(at % BANK)) | (laid ? ORACLES_LANGUAGE_LAID_OUT : 0u));
        report->served++;
    }
    return 0;
}

/* The compact text's layout of a language, in the font of the image as composed: its words written with a hyphen
 * learned and the texts other texts call or jump to within a line found, over all its texts. */
static int prepare_layout(composition *c, const OraclesLanguagePack *pack, OraclesLanguage language, const OraclesTextSource *us, unsigned groups,
                          const language_target *target, const OraclesLanguageLayout *layout, char *error, size_t capacity)
{
    const OraclesRom rom = { c->im, c->size };
    OraclesTextFont font;
    if (oracles_text_font(&rom, oracles_rom_offset(target->tables->font_start.bank, target->tables->font_start.addr), &font) != ORACLES_OK
        || !(c->wrap = language_wrap_new(&font, layout->width, language))) {
        snprintf(error, capacity, "the compact text's layout of %u pixels a line cannot be made", layout->width);
        return -1;
    }
    if (!(c->keep = language_keep_load(target->revision, error, capacity))) return -1;
    uint8_t buffer[ORACLES_TEXT_MAX], targets[64];
    for (unsigned group = ORACLES_TEXT_DICTIONARY_GROUPS; group < groups; group++) {
        const int count = oracles_text_count(&rom, us, group);
        for (int index = 0; index < count; index++) {
            size_t length = 0;
            const uint8_t *text = text_of(c, pack, language, us, groups, (group - ORACLES_TEXT_DICTIONARY_GROUPS) << 8 | (unsigned)index, buffer, &length);
            if (!text) continue;
            language_wrap_learn(c->wrap, text, length);
            const unsigned n = language_text_inline_targets(text, length, targets, sizeof targets);
            for (unsigned i = 0; i < n; i++) c->inline_target[group][targets[i] / 8u] |= (uint8_t)(1u << (targets[i] & 7u));
        }
    }
    return 0;
}

static void free_composition(composition *c)
{
    language_wrap_free(c->wrap);
    language_keep_free(c->keep);
    free(c);
}

/* The composition of `language` (English (US): the US texts laid out again, `pack` NULL). */
static int compose(const OraclesLanguagePack *pack, OraclesLanguage language, const language_target *target, const uint8_t *image, size_t size,
                   const OraclesLanguageLayout *layout, uint8_t **out, size_t *out_size, OraclesLanguageReport *report, char *error, size_t capacity)
{
    const size_t first = (size_t)target->first_bank * BANK, end = (LANGUAGE_LAST_BANK + 1u) * BANK;
    size_t composed = ORACLES_IMAGE_SIZE;
    if (target->original) {
        if (size != ORACLES_IMAGE_SIZE / 2u && size != ORACLES_IMAGE_SIZE) { snprintf(error, capacity, "a language plays on the US ROM only"); return -1; }
    } else {
        OraclesRomInfo info;
        char reason[128];
        if (oracles_rom_identify(image, size, &info, reason, sizeof reason) != 0 || strcmp(info.sha1, target->sha1) != 0 || size < end) {
            snprintf(error, capacity, "the pack's language plays on %s only", target->name);
            return -1;
        }
        composed = size;
    }
    for (size_t i = first; i < size && i < end; i++)
        if (image[i]) { snprintf(error, capacity, "the image's language banks are taken"); return -1; }
    composition *c = calloc(1, sizeof *c);
    if (!c || !(c->im = calloc(1, composed))) { free(c); snprintf(error, capacity, "out of memory"); return -1; }
    memcpy(c->im, image, size);
    c->size = composed;
    c->end = end;
    const OraclesRom rom = { c->im, composed };
    OraclesTextSource us;
    const unsigned groups = language_source_tables(&rom, target->tables, &us) == 0 ? oracles_text_groups(&rom, &us) : 0;
    unsigned entries = 0;
    for (unsigned group = ORACLES_TEXT_DICTIONARY_GROUPS; group < groups; group++) {
        const int count = oracles_text_count(&rom, &us, group);
        entries += count > 0 ? (unsigned)count : 0u;
    }
    c->next = first + ORACLES_LANGUAGE_INDEX_GROUPS + 4u * groups + ENTRY_BYTES * entries;
    if (!groups || groups > 0xffu || c->next > first + BANK) {
        snprintf(error, capacity, "the image's text tables are not those of %s", target->name);
        free(c->im);
        free_composition(c);
        return -1;
    }
    /* The glyphs first: the layout measures the texts in the font the game will print them in. */
    const uint8_t *characters = NULL, *bitmaps = NULL;
    const unsigned glyphs = pack ? oracles_language_pack_glyphs(pack, &characters, &bitmaps) : 0;
    const size_t font = oracles_rom_offset(target->tables->font_start.bank, target->tables->font_start.addr);
    for (unsigned g = 0; g < glyphs; g++) memcpy(c->im + font + characters[g] * GLYPH_BYTES, bitmaps + g * GLYPH_BYTES, GLYPH_BYTES);
    int status = layout && layout->width ? prepare_layout(c, pack, language, &us, groups, target, layout, error, capacity) : 0;
    uint8_t *index = c->im + first;
    memcpy(index, ORACLES_LANGUAGE_INDEX_MAGIC, ORACLES_LANGUAGE_INDEX_MAGIC_BYTES);
    index[16] = ORACLES_LANGUAGE_INDEX_VERSION;
    index[17] = (uint8_t)groups;
    memcpy(index + ORACLES_LANGUAGE_INDEX_CODE, oracles_language_code(language), strlen(oracles_language_code(language)));
    size_t at = ORACLES_LANGUAGE_INDEX_GROUPS + 4u * groups;
    for (unsigned group = ORACLES_TEXT_DICTIONARY_GROUPS; group < groups && status == 0; group++) {
        const int count = oracles_text_count(&rom, &us, group);
        uint8_t *row = index + ORACLES_LANGUAGE_INDEX_GROUPS + 4u * group;
        if (count <= 0) continue;
        row[0] = (uint8_t)at; row[1] = (uint8_t)(at >> 8); row[2] = (uint8_t)count; row[3] = (uint8_t)(count >> 8);
        status = compose_group(c, pack, language, &us, groups, group, (unsigned)count, index + at, report, error, capacity);
        at += ENTRY_BYTES * (unsigned)count;
    }
    if (status == 0 && pack && target->original) status = language_gfx_compose(pack, language, c->im, composed, report, error, capacity);
    if (status == 0) {
        oracles_image_finish(c->im, composed);
        *out = c->im;
        *out_size = composed;
    } else free(c->im);
    free_composition(c);
    return status;
}

int oracles_language_compose(const OraclesLanguagePack *pack, OraclesLanguage language, const uint8_t *image, size_t size, const OraclesLanguageLayout *layout,
                             uint8_t **out, size_t *out_size, OraclesLanguageReport *report, char *error, size_t capacity)
{
    *out = NULL;
    memset(report, 0, sizeof *report);
    language_target target;
    if (language_target_of(pack->image, &target) != 0 || target.game != pack->game) {
        snprintf(error, capacity, "the pack translates an image the port does not know (SHA-1 %s)", pack->image);
        return -1;
    }
    if (language == ORACLES_LANGUAGE_ENGLISH || !oracles_language_pack_count(pack, language)) {
        snprintf(error, capacity, "the pack has no %s text", oracles_language_code(language));
        return -1;
    }
    if (layout && layout->width && !target.original) { snprintf(error, capacity, "the compact text lays out a fan game's texts in its English alone"); return -1; }
    return compose(pack, language, &target, image, size, layout, out, out_size, report, error, capacity);
}

int oracles_language_compose_english(const char *sha1, const uint8_t *image, size_t size, const OraclesLanguageLayout *layout, uint8_t **out,
                                     size_t *out_size, OraclesLanguageReport *report, char *error, size_t capacity)
{
    *out = NULL;
    memset(report, 0, sizeof *report);
    language_target target;
    if (language_target_of(sha1, &target) != 0) {
        snprintf(error, capacity, "the compact text lays out the texts of the US ROM of Ages or Seasons, or of a fan game the port binds the texts of");
        return -1;
    }
    if (!layout || !layout->width) { snprintf(error, capacity, "English (US) is composed only laid out again"); return -1; }
    return compose(NULL, ORACLES_LANGUAGE_ENGLISH, &target, image, size, layout, out, out_size, report, error, capacity);
}
