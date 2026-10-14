/* The languages on the user's ROMs, found by their SHA-1 in $ORACLES_ROM_DIR (skipped, exit 77, without them): the
 * pack of each European ROM, its glyphs, the European English against the US one, and per language the source of
 * each US text's translation (the European text of its identifier, a moved one, an adapted one, the port's own, or
 * the US English), every text served holding the US text's commands and lines; each language composed into the US
 * image, and the coverage printed; the graphics that carry text, each loaded from the composed image as the European
 * game loads it in the language, and nothing else of the US ROM changed but the font and the header's sums; every row of
 * the corrections changing every text it names; and each fan game made on the US ROM by a BPS patch of the directory,
 * its texts that are US texts translated, its own in its English, each language composed in the banks its profile
 * leaves to it.  Exit 77 when the directory holds no game's ROMs. */
#include "bps.h"
#include "language_internal.h"
#include "profile.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); } } while (0)

typedef struct rom_file { uint8_t *data; size_t size; } rom_file;

/* The ROM of `revision` in `dir`, or an empty one. */
static rom_file find(const char *dir, OraclesRomRevision revision)
{
    rom_file found = { NULL, 0 };
    DIR *listing = opendir(dir);
    struct dirent *entry;
    while (listing && !found.data && (entry = readdir(listing))) {
        const size_t n = strlen(entry->d_name);
        if (n < 4 || strcmp(entry->d_name + n - 4, ".gbc")) continue;
        char path[1024], error[128];
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        size_t size = 0;
        uint8_t *data = oracles_rom_read_file(path, &size, error, sizeof error);
        OraclesRomInfo info;
        if (data && oracles_rom_identify(data, size, &info, error, sizeof error) == 0 && info.known && info.revision == revision) found = (rom_file){ data, size };
        else free(data);
    }
    if (listing) closedir(listing);
    return found;
}

/* The ids of the US texts whose European English differs. */
static unsigned english_differences(const rom_file *us, const rom_file *eu, OraclesGame game, char *list, size_t capacity)
{
    const OraclesRom us_rom = { us->data, us->size }, eu_rom = { eu->data, eu->size };
    OraclesTextSource a, b;
    CHECK(language_source_us(&us_rom, game, &a) == 0 && language_source_european(&eu_rom, game, ORACLES_LANGUAGE_ENGLISH, &b) == 0);
    unsigned differences = 0;
    list[0] = 0;
    uint8_t x[ORACLES_TEXT_MAX], y[ORACLES_TEXT_MAX];
    const unsigned groups = oracles_text_groups(&us_rom, &a);
    for (unsigned g = ORACLES_TEXT_DICTIONARY_GROUPS; g < groups; g++) {
        const int count = oracles_text_count(&us_rom, &a, g);
        for (int i = 0; i < count; i++) {
            size_t nx = 0, ny = 0;
            CHECK(oracles_text_decode(&us_rom, &a, g, (unsigned)i, x, sizeof x, &nx) == ORACLES_OK);
            const int same = oracles_text_decode(&eu_rom, &b, g, (unsigned)i, y, sizeof y, &ny) == ORACLES_OK && nx == ny && !memcmp(x, y, nx);
            if (same) continue;
            differences++;
            const size_t used = strlen(list);
            snprintf(list + used, capacity - used, "%s%04x", used ? " " : "", (g - ORACLES_TEXT_DICTIONARY_GROUPS) << 8 | (unsigned)i);
        }
    }
    return differences;
}

/* What each game's ROMs hold, as measured: the US texts, the texts whose European English is not the US one, and per
 * language the US texts the European text of their identifier serves as it is (the others: the table's). */
typedef struct expected {
    OraclesGame game;
    OraclesRomRevision us, eu;
    unsigned texts;
    const char *english_differs;
    unsigned same[ORACLES_LANGUAGE_COUNT];
} expected;

/* Whether `bytes` are in the text's. */
static int holds(const uint8_t *text, size_t length, const uint8_t *bytes, size_t n)
{
    for (size_t at = 0; at + n <= length; at++) if (!memcmp(text + at, bytes, n)) return 1;
    return 0;
}

/* A text corrected or rewritten, first-last in a language: the words it must hold, and the wrong ones it must not, in
 * config/language_own.tsv's syntax. */
typedef struct correction {
    OraclesGame game;
    OraclesLanguage language;
    unsigned first, last;
    const char *present, *absent;
} correction;

#define AGES ORACLES_GAME_AGES
#define SEASONS ORACLES_GAME_SEASONS
#define EN ORACLES_LANGUAGE_ENGLISH_EU
#define FR ORACLES_LANGUAGE_FRENCH
#define DE ORACLES_LANGUAGE_GERMAN
#define IT ORACLES_LANGUAGE_ITALIAN
#define ES ORACLES_LANGUAGE_SPANISH

static const correction corrections[] = {
    { AGES, ES, 0x2f10, 0x2f12, "Veran", "Onox" },
    { AGES, IT, 0x3104, 0x3104, "almeno due o tre", "una o due" },
    { AGES, FR, 0x313b, 0x313b, "\\col(03)Holodrum", "Labrynna" },
    { AGES, IT, 0x550b, 0x550b, "\\col(03)Holodrum", "Labrynna" },
    { SEASONS, ES, 0x301a, 0x301a, "\\col(03)Holodrum", "Labrynna" },
    { AGES, FR, 0x003e, 0x003e, "Steak de Roc", "Filet" },
    { AGES, IT, 0x3606, 0x3606, "Bulbo\\nTokay", "Globo" },
    { AGES, IT, 0x360c, 0x360c, "Bulbo Tokay", "Globo" },
    { AGES, IT, 0x2c0d, 0x2c0d, "Radiomine", "Radiobombe" },
    { AGES, DE, 0x3606, 0x3606, "Tokay-Glasauge", "Glausauge" },
    { AGES, FR, 0x4309, 0x4309, "Noix Gasha", "Graines" },
    { AGES, FR, 0x430a, 0x430a, "Noix\\nGasha", "Graine" },
    { AGES, FR, 0x430c, 0x430c, "force des\\n\\col(01)Noix Gasha", "force des\\n\\col(01)Graines" },
    { AGES, FR, 0x430d, 0x430d, "Noix\\nGasha", "Graines" },
    { AGES, FR, 0x430b, 0x430b, "noix", "graine" },
    { AGES, FR, 0x4311, 0x4311, "noix", "graines" },
    { SEASONS, FR, 0x430d, 0x430d, "Noix\\nGasha", "Graines" },
    { SEASONS, FR, 0x3101, 0x3101, "Noix Gasha", "Graines" },
    { SEASONS, ES, 0x3101, 0x3101, "Nueces\\nGasha", "Semillas" },
    { AGES, FR, 0x1e04, 0x1e04, "Graines\\nMystiques", "Mystère" },
    { AGES, FR, 0x1e06, 0x1e06, "Graines\\nMystiques", "Mystère" },
    { AGES, ES, 0x301a, 0x301a, "búsqueda ni", "búsqueda o" },
    { AGES, ES, 0x3026, 0x3026, "búsqueda ni", "búsqueda o" },
    { SEASONS, ES, 0x301a, 0x301a, "búsqueda ni", "búsqueda o" },
    { SEASONS, ES, 0x3026, 0x3026, "búsqueda ni", "búsqueda o" },
    { AGES, FR, 0x0010, 0x0010, "obtenez\\nl'\\col(01)Echo du\\nHurlement", "Hurlement Echo" },
    { AGES, FR, 0x05b4, 0x05b4, "l'\\col(02)Echo du\\nHurlement", "Cri de" },
    { AGES, FR, 0x05c4, 0x05c4, "l'\\col(02)Echo du\\nHurlement", "Cri de" },
    { AGES, DE, 0x0071, 0x0071, "Harfe der\\nZeiten", "Harfe der Zeit" },
    { AGES, DE, 0x0078, 0x0078, "Harfe der\\nZeiten\\col(00) zurück", "Harfe der Zeit" },
    { AGES, FR, 0x0068, 0x006c, "Flûte\\nEtrange", "Etrange\\nFlûte" },
    { AGES, FR, 0x0047, 0x0047, "Ecaille de\\nZora", "Ecaille des" },
    { AGES, FR, 0x3432, 0x3432, "Ecaille de\\nZora", "Ecaille des" },
    { AGES, FR, 0x344b, 0x344b, "Ecaille de\\nZora", "Ecaille des" },
    { AGES, FR, 0x3132, 0x3132, "l'Epée Biggoron\\col(00)!", "Epée de" },
    { AGES, FR, 0x303a, 0x303a, "Boîte à Anneaux", "Anneau\\col(00)" },
    { SEASONS, FR, 0x300b, 0x300b, "à Anneaux\\col(00)", "Anneau\\col(00)" },
    { AGES, ES, 0x2d04, 0x2d04, "Nuez de Tuni", "Nuez Tuni" },
    { AGES, DE, 0x0d01, 0x0d01, "Magieflasche", "Magische" },
    { SEASONS, DE, 0x0d01, 0x0d01, "Magieflasche", "Magische" },
    { SEASONS, FR, 0x003b, 0x003b, "Bizarre", "Etrange" },
    { SEASONS, FR, 0x0069, 0x006a, "Bizarre", "Etrange" },
    { SEASONS, FR, 0x0e1b, 0x0e1b, "Bizarre", "Etrange" },
    { SEASONS, FR, 0x172b, 0x172b, "Apaisante", "Appaisante" },
    { SEASONS, FR, 0x1728, 0x1728, "Nutritive", "Nourrissante" },
    { SEASONS, FR, 0x172a, 0x172a, "Nutritive", "Nourrissante" },
    { SEASONS, FR, 0x0063, 0x0063, "Oiseau de", "Oiseau en" },
    { SEASONS, FR, 0x3b14, 0x3b14, "Minerai\\nSolide", "Minerai Dur" },
    { SEASONS, FR, 0x0032, 0x0032, "des\\n\\col(01)Missiles", "Bombes" },
    { AGES, FR, 0x541e, 0x541e, "Aucun dégât", "1/2" },
    { SEASONS, IT, 0x5330, 0x5332, "a \\col(03)Monte Goron\\col(00) da", "da \\col(03)Monte" },
    { SEASONS, FR, 0x0b3b, 0x0b3b, "chez\\n\\col(03)Malon\\col(00).", "maison" },
    { SEASONS, ES, 0x5306, 0x5306, "Hadas\\col(00) de\\n\\col(03)Labrynna", "Hadas\\col(00) me" },
    { AGES, FR, 0x280c, 0x280c, "Labrynna\\col(00)", "\\col(04)" },
    { AGES, FR, 0x094d, 0x094d, "Steak de Roc", "Filet" },
    { AGES, IT, 0x094d, 0x094d, "Petto di Roccia", "Filetto" },
    { AGES, FR, 0x4309, 0x4309, "plantée la", "planté la" },
    { SEASONS, FR, 0x3101, 0x3101, "La noix est", "La graine" },
    { AGES, FR, 0x0046, 0x0046, "Graines\\nMystiques", "Mystique\\col(00)" },
    { AGES, FR, 0x0f01, 0x0f01, "Graines\\nMystiques", "Mystique\\col(00)" },
    { AGES, FR, 0x303a, 0x303a, "Graines\\nMystiques", "Mystique\\col(00)" },
    { AGES, FR, 0x3202, 0x3202, "Graines\\nMystiques", "Mystique\\col(00)" },
    { AGES, FR, 0x320c, 0x320c, "Graines\\nMystiques", "Mystique\\col(00)" },
    { SEASONS, FR, 0x0046, 0x0046, "Graines\\nMystiques", "Mystique\\col(00)" },
    { SEASONS, FR, 0x303a, 0x303a, "Graines\\nMystiques", "Mystique\\col(00)" },
    { SEASONS, FR, 0x3202, 0x3202, "Graines\\nMystiques", "Mystique\\col(00)" },
    { SEASONS, FR, 0x320c, 0x320c, "Graines\\nMystiques", "Mystique\\col(00)" },
    { AGES, FR, 0x4807, 0x4807, "Graines\\nMystère", "Mystères" },
    { AGES, FR, 0x480c, 0x480c, "Graines\\nMystère", "Mystères" },
    { AGES, DE, 0x0046, 0x0046, "Mystische\\nKerne", "Arkanum" },
    { AGES, DE, 0x0f01, 0x0f01, "Mystische Kerne", "Arkanum" },
    { AGES, DE, 0x1e04, 0x1e04, "Mystische\\nKerne", "Arkanum" },
    { AGES, DE, 0x1e06, 0x1e06, "Mysti-\\nschen Kerne", "Arkanum" },
    { AGES, DE, 0x303a, 0x303a, "Mystischen\\nKernen", "Arkanum" },
    { SEASONS, DE, 0x0046, 0x0046, "Mystische\\nKerne", "Arkanum" },
    { SEASONS, DE, 0x303a, 0x303a, "Mystischen\\nKernen", "Arkanum" },
    { AGES, DE, 0x301a, 0x301a, "weder\\nabgeschlossen\\nnoch", "nicht\\nabgeschlossen" },
    { AGES, DE, 0x3026, 0x3026, "weder\\nabgeschlossen\\nnoch", "nicht\\nabgeschlossen" },
    { SEASONS, DE, 0x301a, 0x301a, "weder\\nabgeschlossen\\nnoch", "nicht\\nabgeschlossen" },
    { SEASONS, DE, 0x3026, 0x3026, "weder\\nabgeschlossen\\nnoch", "nicht\\nabgeschlossen" },
    { AGES, FR, 0x301a, 0x301a, "n'as ni\\nfini la quête,\\nni utilisé de", "tous" },
    { SEASONS, FR, 0x301a, 0x301a, "n'as ni\\nfini la quête,\\nni utilisé de", "tous" },
    { AGES, FR, 0x3026, 0x3026, "et que tu n'es", "ou que" },
    { SEASONS, FR, 0x3026, 0x3026, "et que tu n'es", "ou que" },
    { AGES, IT, 0x301a, 0x301a, "meta né\\nhai usato", "non hai usato" },
    { SEASONS, IT, 0x301a, 0x301a, "meta né\\nhai usato", "non hai usato" },
    { AGES, IT, 0x3026, 0x3026, "prima\\nné sei venuto", "non sei venuto" },
    { SEASONS, IT, 0x3026, 0x3026, "prima\\nné sei venuto", "non sei venuto" },
    { AGES, FR, 0x05b4, 0x05b4, "Cité\\nLynna\\col(04)", "Lynnna" },
    { AGES, FR, 0x05c4, 0x05c4, "Cité\\nLynna\\col(04)", "Lynnna" },
    { AGES, FR, 0x0068, 0x006c, "appeler", "appeller" },
    { AGES, FR, 0x3602, 0x3602, "Ecaille\\nde Zora", "Ecaille des" },
    { SEASONS, FR, 0x300c, 0x300c, "Boîte à Anneaux", "Boîte à anneaux" },
    { SEASONS, FR, 0x303b, 0x303b, "de \\col(01)Boîte à\\nAnneaux", "boîte" },
    { SEASONS, FR, 0x5306, 0x5306, "les \\col(03)fées\\col(00) du \\col(03)Bois", "de\\nLabrynna" },
    { AGES, DE, 0x3700, 0x3700, "Lynna-Stadt", "Lynna-Dorf" },
    { SEASONS, DE, 0x0b58, 0x0b58, "Ich verrate dir", "lehre" },
    { AGES, FR, 0x1615, 0x161f, "Cité\\nLynna", "Cité de" },
    { AGES, FR, 0x1d16, 0x1d16, "Cité Lynna", "Cité de" },
    { AGES, FR, 0x05b3, 0x05b3, "Ile\\nCroissant", "du Croissant" },
    { AGES, FR, 0x05c3, 0x05c3, "Ile\\nCroissant", "du Croissant" },
    { AGES, FR, 0x360c, 0x360c, "Ile\\nCroissant", "Ile du" },
    { AGES, FR, 0x3415, 0x3416, "Roi des Zora", "Roi Zora" },
    { AGES, FR, 0x3443, 0x3443, "Roi des Zora", "Roi Zora" },
    { AGES, FR, 0x3427, 0x3427, "Roi des Zora", "Rois" },
    { AGES, FR, 0x550f, 0x550f, "Roi des\\nZora\\col(00)", "Zoras" },
    { AGES, FR, 0x3448, 0x3448, "Roi des\\nZora\\col(00)", "des\\col(00)" },
    { SEASONS, FR, 0x0b58, 0x0b58, "à \\col(03)Labrynna", "dans" },
    { SEASONS, FR, 0x4c2c, 0x4c2c, "à \\col(03)Labrynna", "dans" },
    { SEASONS, FR, 0x0c10, 0x0c10, "à\\n\\col(03)Labrynna", "dans\\n\\col(03)Labrynna" },
    { SEASONS, FR, 0x3106, 0x3106, "à\\n\\col(03)Labrynna", "en\\n\\col(03)Labrynna" },
    { SEASONS, FR, 0x4408, 0x4408, "appeler", "appeller" },
    { AGES, EN, 0x541e, 0x541e, "No damage", "1/2" },
    { AGES, EN, 0x094d, 0x094d, "Rock Brisket", "Sirloin" },
    { AGES, EN, 0x05b4, 0x05b4, "Lynna City", "Lynnna" },
    { AGES, EN, 0x05c4, 0x05c4, "Lynna City", "Lynnna" },
    { AGES, EN, 0x301a, 0x301a, "Oracle of\\nSeasons game.", "Seasons here" },
    { SEASONS, EN, 0x4f00, 0x4f00, "sorrow, despair", "depair" },
    { SEASONS, EN, 0x4c12, 0x4c12, "I don't\\nunderstand...", "undertand" },
    /* The port's own texts. */
    { AGES, FR, 0x4d28, 0x4d28, "Ce n'est pas\\nn'importe quel", "vieux" },
    { AGES, FR, 0x5516, 0x5516, "Note de la\\n\\col(03)Grande Fée\\col(00)\\npour \\col(03)Tingle", "pour la" },
};

static const expected games[] = {
    { ORACLES_GAME_AGES, ORACLES_ROM_REVISION_AGES_US, ORACLES_ROM_REVISION_AGES_EU, 2824,
      "000c 0034 005d 0116 0200 0201 0202 0203 0204 0205 0206 0207 0208 0209 020a 020b 020c 020d 0505 0506 0525 0526 05b2 05c2 "
      "0700 0709 070d 070e 0711 0712 0a53 0b10 0e12 0e22 0e23 0e24 0e25 1e05 2400 2420 2492 2496 249e 2827 2a20 2f02 2f0a 2f1e "
      "2f1f 2f20 2f21 2f22 2f23 2f24 2f25 3018 301e 3024 302e 313b 3503 4509 450a 450b 450c 450d 4b02 4d23 4d25 4d26",
      { 0, 2820, 2817, 2794, 2818, 2811 } },
    { ORACLES_GAME_SEASONS, ORACLES_ROM_REVISION_SEASONS_US, ORACLES_ROM_REVISION_SEASONS_EU, 1885,
      "0034 0037 0201 0203 0204 0208 0700 0709 0b42 0c12 0c18 1100 1729 1730 230d 2600 2c00 2e0d 2f19 2f1a 2f1b 2f1c 2f1d 2f1e "
      "2f1f 2f20 2f21 2f22 2f23 2f24 2f25 3003 30ae 3210 3404 3503 3a38 4101 450b 501f 5313 5317",
      { 0, 1883, 1879, 1877, 1878, 1878 } },
};

/* ---- the graphics, loaded as the games load them (an implementation of their own, apart from language_gfx.c's) ---- */

#define SPACE (16u * 0x6000u)   /* VRAM and WRAM, $8000-$dfff, by the destination's bank (its low 4 bits) */

typedef struct gfx_load { uint16_t value[SPACE]; int16_t entry[SPACE]; unsigned count; size_t at[64]; } gfx_load;

/* Loads header h of the table at `table` as the US loadGfxHeader does (language < 0) or the European one in
 * `language`; 0, or -1 when it does not decompress. */
static int load_header(const uint8_t *rom, size_t size, size_t table, unsigned h, int language, gfx_load *m)
{
    for (unsigned i = 0; i < SPACE; i++) { m->value[i] = 0xffff; m->entry[i] = -1; }
    m->count = 0;
    size_t at = table / 0x4000u * 0x4000u + (size_t)((rom[table + 2u * h] | rom[table + 2u * h + 1u] << 8) - 0x4000u);
    for (;;) {
        const uint8_t *e = rom + at, *source = e, *rest = e + 3;
        m->at[m->count] = at;
        if (language >= 0 && !e[0]) { source = e + 1 + 3 * language; rest = e + 16; at += 19; }
        else at += 6;
        const unsigned dest = (unsigned)rest[0] << 8 | rest[1], bytes = 16u * ((rest[2] & 0x7fu) + 1u);
        uint8_t data[2048];
        if (language_gfx_decompress(rom, size, oracles_rom_offset(source[0] & 0x3fu, (unsigned)source[1] << 8 | source[2]), source[0] >> 6, data, bytes, NULL) != 0) return -1;
        for (unsigned i = 0; i < bytes; i++) {
            const unsigned address = (dest & 0xfff0u) + i;
            if (address < 0x8000u || address >= 0xe000u) return -1;
            const size_t k = (dest & 0xfu) * 0x6000u + address - 0x8000u;
            m->value[k] = data[i];
            m->entry[k] = (int16_t)m->count;
        }
        if (++m->count == 64u) return -1;
        if (!(rest[2] & 0x80u)) return 0;
    }
}

/* What the ROMs hold, as measured: the US graphics a language replaces (bank:address), and how many each replaces. */
typedef struct expected_graphics {
    const char *sources;
    unsigned count[ORACLES_LANGUAGE_COUNT];
} expected_graphics;

/* Each language's graphics: every header the European ROM chooses by language loads, from the composed image as the
 * US game does, what the European game loads in the language over every byte the replaced graphics write; the
 * replaced graphics have their US sizes; the bytes still different are those of graphics the European ROM redrew in
 * every language (the name entry's layout), printed. */
static void check_graphics(const char *name, OraclesGame game, const OraclesLanguagePack *pack, const rom_file *us, const rom_file *eu,
                           const expected_graphics *expected)
{
    static gfx_load composed, european, original;
    static char sources[4096];
    sources[0] = 0;
    static uint8_t replaced_any[0x40][0x4000];   /* the US graphics some language replaces, listed by source */
    memset(replaced_any, 0, sizeof replaced_any);
    const size_t us_table = language_gfx_headers_us(game), eu_table = language_gfx_headers_european(game);
    /* Each US graphic's largest size in the US headers, in blocks (0: none loads it). */
    static uint8_t largest[0x40][0x4000];
    memset(largest, 0, sizeof largest);
    for (unsigned h = 0; h < GFX_HEADERS; h++) {
        size_t at = us_table / 0x4000u * 0x4000u + (size_t)((us->data[us_table + 2u * h] | us->data[us_table + 2u * h + 1u] << 8) - 0x4000u);
        for (const uint8_t *x = us->data + at;; x += 6) {
            uint8_t *slot = &largest[x[0] & 0x3fu][((unsigned)x[1] << 8 | x[2]) & 0x3fffu];
            if ((x[5] & 0x7fu) + 1u > *slot) *slot = (uint8_t)((x[5] & 0x7fu) + 1u);
            if (!(x[5] & 0x80u)) break;
        }
    }
    for (OraclesLanguage language = ORACLES_LANGUAGE_ENGLISH_EU; language < ORACLES_LANGUAGE_COUNT; language++) {
        const unsigned count = oracles_language_pack_graphic_count(pack, language);
        static char replaced[4096];
        replaced[0] = 0;
        for (unsigned n = 0; n < count; n++) {
            OraclesLanguageGraphic g;
            CHECK(oracles_language_pack_graphic(pack, language, n, &g) == 0);
            char source[16];
            snprintf(source, sizeof source, "%02x:%04x", g.bank, g.address);
            snprintf(replaced + strlen(replaced), sizeof replaced - strlen(replaced), " %s", source);
            replaced_any[g.bank][g.address & 0x3fffu] = 1;
            /* Its US size: the largest any US header loads it with. */
            uint8_t data[2048];
            size_t consumed = 0;
            CHECK(language_gfx_decompress(g.data, g.length, 0, g.mode, data, 16u * (g.blocks + 1u), &consumed) == 0 && consumed == g.length);
            CHECK(largest[g.bank][g.address & 0x3fffu] == g.blocks + 1u);
        }
        uint8_t *image = NULL;
        size_t size = 0;
        OraclesLanguageReport report;
        char error[256];
        CHECK(oracles_language_compose(pack, language, us->data, us->size, &image, &size, &report, error, sizeof error) == 0);
        if (!image) continue;
        unsigned headers = 0, compared = 0, mismatched = 0, repointed = 0, left = 0;
        for (unsigned h = 0; h < GFX_HEADERS; h++) {
            const size_t at = eu_table / 0x4000u * 0x4000u + (size_t)((eu->data[eu_table + 2u * h] | eu->data[eu_table + 2u * h + 1u] << 8) - 0x4000u);
            int chosen = 0;
            for (size_t p = at; p < at + 64u * 19u; ) {
                if (!eu->data[p]) { chosen = 1; break; }
                if (!(eu->data[p + 5] & 0x80u)) break;
                p += 6;
            }
            if (!chosen) continue;
            headers++;
            CHECK(load_header(image, size, us_table, h, -1, &composed) == 0);
            CHECK(load_header(us->data, us->size, us_table, h, -1, &original) == 0);
            CHECK(load_header(eu->data, eu->size, eu_table, h, (int)language_european_index(language), &european) == 0);
            for (size_t k = 0; k < SPACE; k++) {
                if (composed.entry[k] < 0 || european.value[k] == 0xffff) continue;
                const size_t entry = composed.at[composed.entry[k]];
                char named[16];
                snprintf(named, sizeof named, "%02x:%02x%02x", us->data[entry] & 0x3fu, us->data[entry + 1u], us->data[entry + 2u]);
                const int moved = strstr(replaced, named) != NULL;
                if (moved) { compared++; mismatched += composed.value[k] != european.value[k]; }
                else left += composed.value[k] != european.value[k];
            }
            for (unsigned j = 0; j < composed.count; j++) {
                if (memcmp(image + composed.at[j], us->data + composed.at[j], 6) == 0) continue;
                repointed++;
                CHECK(!memcmp(image + composed.at[j] + 3, us->data + composed.at[j] + 3, 3));   /* the destination and the size stay */
            }
        }
        printf("%s %s graphics: %u replaced, %u compressed bytes; %u headers chosen by language, %u entries repointed, %u bytes as the "
               "European game loads them, %u different, %u left as the US game loads them\n",
               name, oracles_language_code(language), report.graphics, report.graphics_bytes, headers, repointed, compared, mismatched, left);
        CHECK(report.graphics == oracles_language_pack_graphic_count(pack, language) && report.graphics == expected->count[language]);
        CHECK(mismatched == 0 && compared > 0);
        free(image);
    }
    for (unsigned b = 0; b < 0x40u; b++)
        for (unsigned a = 0; a < 0x4000u; a++)
            if (replaced_any[b][a]) snprintf(sources + strlen(sources), sizeof sources - strlen(sources), "%s%02x:%04x", sources[0] ? " " : "", b, 0x4000u + a);
    printf("%s graphics replaced: %s\n", name, sources);
    CHECK(!strcmp(sources, expected->sources));
}

static const expected_graphics graphics_expected[] = {
    { "29:56ac 29:5a51 29:5b5f 29:5ca9 29:5de7 29:5f33 29:6071 29:61ac 29:62ff 29:644d 29:6589 29:66cc 29:67dd 37:7544 37:7cba "
      "37:7e5d 37:7edf 37:7f62 38:4470 38:456a 38:470d 38:48b5 38:4930 38:4e0a 38:5274 38:5426 38:55bb 38:5641 38:57cd 38:5982 "
      "38:5b12 38:5b90 38:5d07 38:664f",
      { 0, 22, 34, 34, 34, 34 } },
    { "28:5199 28:5542 28:5646 28:57d2 28:593e 28:5a9b 28:5c23 28:5d56 28:5e99 28:5feb 28:6184 28:628f 37:7440 37:7bbc 37:7d5f "
      "37:7de1 37:7e64 38:4372 38:446c 38:460f 38:47b7 38:4832 38:4d0c 38:5176 38:5328 38:54bd 38:5543 38:56d4 38:5889 38:5a19 "
      "38:5a97 38:5c0e 38:6556",
      { 0, 21, 33, 33, 33, 33 } },
};

/* The image composed in a language against the US ROM: in its first megabyte, nothing changed but the header's size and
 * sums, the font's glyphs, and the graphics headers' entries that load another graphic than the US one (repointed, or
 * whose graphic was written in place) with the bytes they now point to; the mods' banks empty; the language in its
 * own.  English (US) composes nothing. */
static void check_composed(const char *name, OraclesGame game, const OraclesLanguagePack *pack, const rom_file *us)
{
    static uint8_t allowed[0x100000];
    const size_t table = language_gfx_headers_us(game), font = language_font_us(game);
    char error[256];
    uint8_t *image = NULL;
    size_t size = 0;
    OraclesLanguageReport report;
    CHECK(oracles_language_compose(pack, ORACLES_LANGUAGE_ENGLISH, us->data, us->size, &image, &size, &report, error, sizeof error) == -1 && !image);
    for (OraclesLanguage language = ORACLES_LANGUAGE_ENGLISH_EU; language < ORACLES_LANGUAGE_COUNT; language++) {
        CHECK(oracles_language_compose(pack, language, us->data, us->size, &image, &size, &report, error, sizeof error) == 0);
        if (!image) continue;
        CHECK(size == 2u * us->size && us->size == sizeof allowed);
        memset(allowed, 0, sizeof allowed);
        allowed[0x148] = allowed[0x14d] = allowed[0x14e] = allowed[0x14f] = 1;
        memset(allowed + font, 1, 0x100u * GLYPH_BYTES);
        unsigned entries = 0, graphic_bytes = 0;
        for (unsigned h = 0; h < GFX_HEADERS; h++) {
            const size_t at = table / 0x4000u * 0x4000u + (size_t)((us->data[table + 2u * h] | us->data[table + 2u * h + 1u] << 8) - 0x4000u);
            for (size_t e = at;; e += 6u) {
                uint8_t data[2048], original[2048];
                size_t consumed = 0;
                const size_t bytes = 16u * ((image[e + 5u] & 0x7fu) + 1u);
                const size_t source = oracles_rom_offset(image[e] & 0x3fu, (unsigned)image[e + 1u] << 8 | image[e + 2u]);
                const size_t us_source = oracles_rom_offset(us->data[e] & 0x3fu, (unsigned)us->data[e + 1u] << 8 | us->data[e + 2u]);
                CHECK(language_gfx_decompress(image, size, source, image[e] >> 6, data, bytes, &consumed) == 0);
                CHECK(language_gfx_decompress(us->data, us->size, us_source, us->data[e] >> 6, original, bytes, NULL) == 0);
                if (memcmp(image + e, us->data + e, 3) || memcmp(data, original, bytes)) {
                    CHECK(source + consumed <= sizeof allowed);
                    memset(allowed + e, 1, 3);
                    if (source + consumed <= sizeof allowed) memset(allowed + source, 1, consumed);
                    entries++;
                    graphic_bytes += (unsigned)consumed;
                }
                if (!(us->data[e + 5u] & 0x80u)) break;
            }
        }
        unsigned changed = 0, outside = 0, mods = 0;
        for (size_t i = 0; i < us->size; i++) {
            changed += image[i] != us->data[i];
            outside += image[i] != us->data[i] && !allowed[i];
        }
        for (size_t i = us->size; i < (size_t)ORACLES_IMAGE_LANGUAGE_BANK * 0x4000u; i++) mods += image[i] != 0;
        printf("%s %s composed: %u bytes of the US ROM changed, %u of them elsewhere than the header, the font, %u entries loading the language's graphics "
               "and the %u bytes they load; %u bytes written in the mods' banks\n", name, oracles_language_code(language), changed, outside, entries,
               graphic_bytes, mods);
        CHECK(outside == 0 && mods == 0 && entries > 0);
        CHECK(!memcmp(image + (size_t)ORACLES_IMAGE_LANGUAGE_BANK * 0x4000u, ORACLES_LANGUAGE_INDEX_MAGIC, ORACLES_LANGUAGE_INDEX_MAGIC_BYTES));
        free(image);
        image = NULL;
    }
}

/* Every row of config/language_corrections.tsv changed every text it names in every language it names, and every
 * corrected text held. */
static void check_corrections_used(const char *name, const language_corrections *corrections)
{
    unsigned dead = 0;
    for (unsigned n = 0; n < language_corrections_count(corrections); n++) {
        if (language_corrections_applied(corrections, n) == language_corrections_named(corrections, n)) continue;
        fprintf(stderr, "%s: the corrections' row %u of the game changes %u of the %u texts and languages it names\n", name, n + 1u,
                language_corrections_applied(corrections, n), language_corrections_named(corrections, n));
        dead++;
    }
    printf("%s: %u rows of corrections, %u changing less than they name, %u corrected texts refused\n", name, language_corrections_count(corrections), dead,
           language_corrections_refused(corrections));
    CHECK(dead == 0 && language_corrections_refused(corrections) == 0 && language_corrections_count(corrections) > 0);
}

/* ---- the fan games ------------------------------------------------------------------------------------------------ */

/* What a fan game's image holds, as measured: its texts, those that are the US text of their identifier, of another
 * identifier, and its own; per language, the texts translated and those left in its English. */
typedef struct expected_fan {
    OraclesRomRevision revision;
    OraclesGame game;
    const char *name;
    unsigned texts, same_id, elsewhere, own;
    unsigned translated[ORACLES_LANGUAGE_COUNT], english[ORACLES_LANGUAGE_COUNT];
    unsigned glyphs;
} expected_fan;

static const expected_fan fans[] = {
    { ORACLES_ROM_REVISION_MOONRISE_1_0_6, ORACLES_GAME_AGES, "moonrise", 2838, 2534, 33, 271, { 0, 2567, 2567, 2567, 2567, 2567 }, { 0, 271, 271, 271, 271, 271 }, 35 },
    { ORACLES_ROM_REVISION_KINOMI_1_1_2, ORACLES_GAME_AGES, "kinomi", 2953, 2453, 4, 496, { 0, 2457, 2457, 2457, 2457, 2457 }, { 0, 496, 496, 496, 496, 496 }, 35 },
    { ORACLES_ROM_REVISION_TEMPLE_1_073, ORACLES_GAME_SEASONS, "temple", 1906, 1521, 37, 348, { 0, 1558, 1558, 1558, 1558, 1558 }, { 0, 348, 348, 348, 348, 348 }, 35 },
};

/* The image of fan game `revision`: a BPS patch of `dir` applied to the US ROM. */
static rom_file find_fan(const char *dir, const rom_file *us, OraclesRomRevision revision)
{
    rom_file found = { NULL, 0 };
    DIR *listing = opendir(dir);
    struct dirent *entry;
    while (listing && !found.data && (entry = readdir(listing))) {
        const size_t n = strlen(entry->d_name);
        if (n < 4 || strcmp(entry->d_name + n - 4, ".bps")) continue;
        char path[1024], error[128];
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        size_t patch_size = 0, size = 0;
        uint8_t *patch = oracles_rom_read_file(path, &patch_size, error, sizeof error);
        uint8_t *data = patch ? oracles_bps_apply(us->data, us->size, patch, patch_size, &size, error, sizeof error) : NULL;
        free(patch);
        OraclesRomInfo info;
        if (data && oracles_rom_identify(data, size, &info, error, sizeof error) == 0 && info.known && info.revision == revision) found = (rom_file){ data, size };
        else free(data);
    }
    if (listing) closedir(listing);
    return found;
}

static unsigned read16(const uint8_t *p) { return p[0] | p[1] << 8; }

/* getTextAddress, where the profile binds it: its code reads wTextIndexH and wTextIndexL, the two bases' tables and
 * the split, and writes w7ActiveBank, at the profile's addresses (textbox.s). */
static void check_hook(const char *name, const uint8_t *rom, const OraclesGuestTables *t)
{
    const uint8_t *g = rom + oracles_rom_offset(t->get_text_address.bank, t->get_text_address.addr);
    const int bound = g[0] == 0xd5 && g[10] == 0xfa && read16(g + 11) == t->text_index_h.addr && g[24] == 0xfa && read16(g + 25) == t->text_index_l.addr
                      && g[40] == 0x21 && read16(g + 41) == t->text_offset1_table.addr && g[52] == 0xfe && g[53] == t->text_offset_split_index
                      && g[60] == 0x21 && read16(g + 61) == t->text_offset2_table.addr && g[81] == 0xea && read16(g + 82) == t->active_text_bank.addr
                      && g[84] == 0xcb && g[88] == 0xd1 && g[89] == 0xc9;
    printf("%s: getTextAddress %02x:%04x, text tables %04x and %04x, split %02x: %s\n", name, t->get_text_address.bank, t->get_text_address.addr,
           t->text_offset1_table.addr, t->text_offset2_table.addr, t->text_offset_split_index, bound ? "the routine's code" : "NOT the routine");
    CHECK(bound);
}

/* Each fan game made on `us`: the profile's text symbols, its pack derived from the game's, the counts per language,
 * every text translated holding the fan game's text's commands, each language composed into its image in the banks
 * its profile leaves to it, the rest of the image unchanged but the font's glyphs and the header's sums; and its pack's
 * file read back. */
static void check_fans(const char *dir, const char *work, const OraclesLanguagePack *base, const rom_file *us)
{
    for (size_t f = 0; f < sizeof fans / sizeof fans[0]; f++) {
        const expected_fan *e = &fans[f];
        if (e->game != base->game) continue;
        rom_file fan = find_fan(dir, us, e->revision);
        if (!fan.data) { printf("%s: no patch of it in %s: not checked\n", e->name, dir); continue; }
        char error[256];
        OraclesRomInfo info;
        CHECK(oracles_rom_identify(fan.data, fan.size, &info, error, sizeof error) == 0);
        const OraclesCompatProfile *profile = oracles_compat_find(&info);
        const OraclesGuestTables *t = oracles_compat_guest_tables(profile);
        check_hook(e->name, fan.data, t);
        OraclesLanguagePack *pack = oracles_language_pack_derive(base, us->data, us->size, fan.data, fan.size, error, sizeof error);
        CHECK(pack != NULL);
        if (!pack) { fprintf(stderr, "%s: %s\n", e->name, error); free(fan.data); continue; }
        CHECK(!strcmp(oracles_language_pack_image(pack), info.sha1) && !strcmp(oracles_language_pack_source(pack), oracles_language_pack_source(base)));
        const OraclesRom fan_rom = { fan.data, fan.size }, us_rom = { us->data, us->size };
        OraclesTextSource fan_source, us_source;
        CHECK(language_source_tables(&fan_rom, t, &fan_source) == 0 && language_source_us(&us_rom, base->game, &us_source) == 0);
        /* The fan game's texts against the US ones, whatever the language. */
        unsigned texts = 0, same_id = 0, elsewhere = 0, own = 0;
        const unsigned groups = oracles_text_groups(&fan_rom, &fan_source), us_groups = oracles_text_groups(&us_rom, &us_source);
        uint8_t text[ORACLES_TEXT_MAX], original[ORACLES_TEXT_MAX];
        for (unsigned g = ORACLES_TEXT_DICTIONARY_GROUPS; g < groups; g++) {
            const int count = oracles_text_count(&fan_rom, &fan_source, g);
            for (int i = 0; i < count; i++) {
                size_t length = 0, us_length = 0;
                texts++;
                CHECK(oracles_text_decode(&fan_rom, &fan_source, g, (unsigned)i, text, sizeof text, &length) == ORACLES_OK);
                const int same = g < us_groups && i < oracles_text_count(&us_rom, &us_source, g)
                                 && oracles_text_decode(&us_rom, &us_source, g, (unsigned)i, original, sizeof original, &us_length) == ORACLES_OK
                                 && us_length == length && !memcmp(text, original, length);
                const OraclesLanguageSource source = oracles_language_pack_source_of(pack, ORACLES_LANGUAGE_FRENCH, (g - ORACLES_TEXT_DICTIONARY_GROUPS) << 8 | (unsigned)i);
                if (same) same_id++;
                else if (source == ORACLES_LANGUAGE_SOURCE_FAN) own++;
                else elsewhere++;
                /* Translated, the fan game's text's commands, as the US text's are its own. */
                for (OraclesLanguage language = ORACLES_LANGUAGE_ENGLISH_EU; language < ORACLES_LANGUAGE_COUNT; language++) {
                    size_t translated_length = 0;
                    const unsigned id = (g - ORACLES_TEXT_DICTIONARY_GROUPS) << 8 | (unsigned)i;
                    const uint8_t *translated = oracles_language_pack_text(pack, language, id, &translated_length);
                    const OraclesLanguageSource from = oracles_language_pack_source_of(pack, language, id);
                    if (translated && from != ORACLES_LANGUAGE_SOURCE_ADAPTED) CHECK(oracles_text_same_commands(text, length, translated, translated_length));
                    if (translated) CHECK(oracles_text_longest_line(translated, translated_length) <= LINE_CHARACTERS);
                }
            }
        }
        const uint8_t *characters, *bitmaps;
        const unsigned glyphs = oracles_language_pack_glyphs(pack, &characters, &bitmaps);
        printf("%s: %u texts, %u the US text of their identifier, %u of another, %u its own; %u European glyphs\n", e->name, texts, same_id, elsewhere, own, glyphs);
        CHECK(texts == e->texts && same_id == e->same_id && elsewhere == e->elsewhere && own == e->own && glyphs == e->glyphs);
        /* The texts it shares with the US ROM are those the pack translates, in every language. */
        unsigned shared = 0;
        for (unsigned n = 0; n < oracles_language_pack_count(pack, ORACLES_LANGUAGE_FRENCH); n++) {
            unsigned id = 0, every = 1;
            oracles_language_pack_nth(pack, ORACLES_LANGUAGE_FRENCH, n, &id, NULL, NULL);
            for (OraclesLanguage language = ORACLES_LANGUAGE_ENGLISH_EU; language < ORACLES_LANGUAGE_COUNT; language++)
                every &= oracles_language_pack_text(pack, language, id, NULL) != NULL;
            shared += every;
        }
        CHECK(oracles_language_pack_count(pack, ORACLES_LANGUAGE_FRENCH) == texts && shared == same_id + elsewhere);
        const size_t first = (size_t)oracles_compat_language_bank(profile) * 0x4000u, font = oracles_rom_offset(t->font_start.bank, t->font_start.addr);
        for (OraclesLanguage language = ORACLES_LANGUAGE_ENGLISH_EU; language < ORACLES_LANGUAGE_COUNT; language++) {
            unsigned counts[ORACLES_LANGUAGE_SOURCE_COUNT];
            oracles_language_pack_tally(pack, language, counts);
            const unsigned translated = counts[ORACLES_LANGUAGE_SOURCE_SAME] + counts[ORACLES_LANGUAGE_SOURCE_MOVED] + counts[ORACLES_LANGUAGE_SOURCE_ADAPTED]
                                        + counts[ORACLES_LANGUAGE_SOURCE_OWN];
            const unsigned english = counts[ORACLES_LANGUAGE_SOURCE_FAN] + counts[ORACLES_LANGUAGE_SOURCE_KEPT];
            uint8_t *image = NULL;
            size_t size = 0;
            OraclesLanguageReport report;
            CHECK(oracles_language_compose(pack, language, fan.data, fan.size, &image, &size, &report, error, sizeof error) == 0);
            unsigned changed = 0, banks = 0;
            if (image) {
                for (size_t i = 0; i < first; i++)
                    if (image[i] != fan.data[i] && !(i >= 0x14d && i <= 0x14f) && !(i >= font && i < font + 0x100u * GLYPH_BYTES)) changed++;
                for (size_t i = (LANGUAGE_LAST_BANK + 1u) * 0x4000u; i < size; i++) changed += image[i] != fan.data[i];
                CHECK(!memcmp(image + first, ORACLES_LANGUAGE_INDEX_MAGIC, ORACLES_LANGUAGE_INDEX_MAGIC_BYTES));
                for (size_t b = first / 0x4000u; b <= LANGUAGE_LAST_BANK; b++) {
                    int used = 0;
                    for (size_t i = 0; i < 0x4000u && !used; i++) used = image[b * 0x4000u + i] != 0;
                    banks += used;
                }
            }
            printf("%s %s: %u translated (%u same, %u moved, %u adapted, %u own), %u in its English; composed: %u served, %u in English, "
                   "%u bytes in %u banks from $%02x\n", e->name, oracles_language_code(language), translated, counts[ORACLES_LANGUAGE_SOURCE_SAME],
                   counts[ORACLES_LANGUAGE_SOURCE_MOVED], counts[ORACLES_LANGUAGE_SOURCE_ADAPTED], counts[ORACLES_LANGUAGE_SOURCE_OWN], english,
                   report.served, report.english, report.bytes, banks, (unsigned)(first / 0x4000u));
            CHECK(size == fan.size && changed == 0 && report.graphics == 0);
            CHECK(translated == e->translated[language] && english == e->english[language]);
            CHECK(report.served == translated && report.english == english && report.missing == 0);
            free(image);
        }
        /* The fan game's pack plays on its image only. */
        uint8_t *image = NULL;
        size_t size = 0;
        OraclesLanguageReport report;
        CHECK(oracles_language_compose(pack, ORACLES_LANGUAGE_FRENCH, us->data, us->size, &image, &size, &report, error, sizeof error) == -1);
        CHECK(oracles_language_compose(base, ORACLES_LANGUAGE_FRENCH, fan.data, fan.size, &image, &size, &report, error, sizeof error) == -1);
        CHECK(oracles_language_pack_derive(base, fan.data, fan.size, fan.data, fan.size, error, sizeof error) == NULL);
        /* Its file, read back. */
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.pack", work, e->name);
        CHECK(oracles_language_pack_write(pack, path, error, sizeof error) == 0);
        OraclesLanguagePack *back = oracles_language_pack_read(path, error, sizeof error);
        CHECK(back != NULL);
        if (back) {
            uint8_t *a = NULL, *b = NULL;
            size_t a_size = 0, b_size = 0;
            CHECK(oracles_language_compose(pack, ORACLES_LANGUAGE_GERMAN, fan.data, fan.size, &a, &a_size, &report, error, sizeof error) == 0);
            CHECK(oracles_language_compose(back, ORACLES_LANGUAGE_GERMAN, fan.data, fan.size, &b, &b_size, &report, error, sizeof error) == 0);
            CHECK(a && b && a_size == b_size && !memcmp(a, b, a_size));
            free(a);
            free(b);
            oracles_language_pack_free(back);
        }
        remove(path);
        oracles_language_pack_free(pack);
        free(fan.data);
    }
}

static int check_game(const char *dir, const char *work, const expected *e)
{
    const OraclesGame game = e->game;
    const OraclesRomRevision us_revision = e->us, eu_revision = e->eu;
    rom_file us = find(dir, us_revision), eu = find(dir, eu_revision);
    const char *name = game == ORACLES_GAME_AGES ? "ages" : "seasons";
    if (!us.data || !eu.data) { printf("%s: no US and European ROMs in %s: not checked\n", name, dir); free(us.data); free(eu.data); return 0; }
    char error[256];
    language_corrections *used = language_corrections_load(game, error, sizeof error);
    CHECK(used != NULL);
    OraclesLanguagePack *pack = used ? language_pack_extract_with(eu.data, eu.size, us.data, us.size, used, error, sizeof error) : NULL;
    CHECK(pack != NULL);
    if (!pack) { fprintf(stderr, "%s: %s\n", name, error); language_corrections_free(used); free(us.data); free(eu.data); return 1; }
    check_corrections_used(name, used);
    language_corrections_free(used);

    /* The official translations' mistakes corrected (config/language_corrections.tsv) or rewritten (the port's own
     * texts): each text served with the right words, without the wrong ones. */
    for (size_t c = 0; c < sizeof corrections / sizeof corrections[0]; c++) {
        const correction *k = &corrections[c];
        if (k->game != game) continue;
        uint8_t present[128], absent[128];
        size_t present_length = 0, absent_length = 0;
        CHECK(language_text_encode(k->present, strlen(k->present), present, sizeof present, &present_length) == 0);
        CHECK(language_text_encode(k->absent, strlen(k->absent), absent, sizeof absent, &absent_length) == 0);
        for (unsigned id = k->first; id <= k->last; id++) {
            size_t length = 0;
            const uint8_t *text = oracles_language_pack_text(pack, k->language, id, &length);
            const int served = text && holds(text, length, present, present_length - 1u) && !holds(text, length, absent, absent_length - 1u);
            if (!served) fprintf(stderr, "%s %s TX_%04x: not corrected to %s\n", name, oracles_language_code(k->language), id, k->present);
            CHECK(served);
        }
    }

    /* The glyphs: 22 in slots the US font fills with a repeated filler, 13 redrawn. */
    const uint8_t *characters, *bitmaps;
    const unsigned glyphs = oracles_language_pack_glyphs(pack, &characters, &bitmaps);
    const uint8_t *font = us.data + language_font_us(game);
    unsigned fresh = 0;
    for (unsigned g = 0; g < glyphs; g++) {
        unsigned same = 0;
        for (unsigned c = FONT_FIRST; c < 0x100u; c++) same += !memcmp(font + c * GLYPH_BYTES, font + characters[g] * GLYPH_BYTES, GLYPH_BYTES);
        fresh += same > 1u;
    }
    printf("%s: %u glyphs, %u in filler slots\n", name, glyphs, fresh);
    CHECK(glyphs == 35 && fresh == 22);

    static char list[16384];
    const unsigned differences = english_differences(&us, &eu, game, list, sizeof list);
    printf("%s: European English differs from the US on %u texts: %s\n", name, differences, list);
    CHECK(!strcmp(list, e->english_differs));

    const OraclesRom us_rom = { us.data, us.size };
    OraclesTextSource source;
    CHECK(language_source_us(&us_rom, game, &source) == 0);
    for (OraclesLanguage language = ORACLES_LANGUAGE_ENGLISH_EU; language < ORACLES_LANGUAGE_COUNT; language++) {
        const unsigned count = oracles_language_pack_count(pack, language);
        unsigned counts[ORACLES_LANGUAGE_SOURCE_COUNT], long_lines = 0, unheld = 0;
        oracles_language_pack_tally(pack, language, counts);
        list[0] = 0;
        for (unsigned n = 0; n < count; n++) {
            unsigned id = 0;
            size_t length = 0;
            OraclesLanguageSource from;
            const uint8_t *text = oracles_language_pack_nth(pack, language, n, &id, &from, &length);
            if (from != ORACLES_LANGUAGE_SOURCE_SAME) {
                const size_t used = strlen(list);
                snprintf(list + used, sizeof list - used, "%s%04x:%s", used ? " " : "", id, oracles_language_source_name(from));
            }
            if (!text) continue;
            if (oracles_text_longest_line(text, length) > 16u) long_lines++;
            /* A European text or the port's own as it is: the US text's commands, exactly. */
            uint8_t original[ORACLES_TEXT_MAX];
            size_t original_length = 0;
            const unsigned group = (id >> 8) + ORACLES_TEXT_DICTIONARY_GROUPS;
            CHECK(oracles_text_decode(&us_rom, &source, group, id & 0xffu, original, sizeof original, &original_length) == ORACLES_OK);
            if (from != ORACLES_LANGUAGE_SOURCE_ADAPTED && !oracles_text_same_commands(original, original_length, text, length)) unheld++;
        }
        const unsigned served = counts[ORACLES_LANGUAGE_SOURCE_SAME] + counts[ORACLES_LANGUAGE_SOURCE_MOVED] + counts[ORACLES_LANGUAGE_SOURCE_ADAPTED]
                                + counts[ORACLES_LANGUAGE_SOURCE_OWN];
        uint8_t *image = NULL;
        size_t size = 0;
        OraclesLanguageReport report;
        CHECK(oracles_language_compose(pack, language, us.data, us.size, &image, &size, &report, error, sizeof error) == 0);
        printf("%s %s: %u texts: %u same, %u moved, %u adapted, %u own, %u kept, %u refused, %u missing: %.2f %% translated, %.2f %% official; "
               "%u lines over 16; composed: %u served, %u in English, %u missing, %u bytes\n  not the same: %s\n",
               name, oracles_language_code(language), count, counts[ORACLES_LANGUAGE_SOURCE_SAME], counts[ORACLES_LANGUAGE_SOURCE_MOVED],
               counts[ORACLES_LANGUAGE_SOURCE_ADAPTED], counts[ORACLES_LANGUAGE_SOURCE_OWN], counts[ORACLES_LANGUAGE_SOURCE_KEPT],
               counts[ORACLES_LANGUAGE_SOURCE_REFUSED], counts[ORACLES_LANGUAGE_SOURCE_MISSING], 100.0 * served / (count ? count : 1u),
               100.0 * (served - counts[ORACLES_LANGUAGE_SOURCE_OWN]) / (count ? count : 1u), long_lines, report.served, report.english,
               report.missing, report.bytes, list);
        /* Every US text translated: none refused, none missing, none kept, the European texts as measured. */
        CHECK(count == e->texts && long_lines == 0 && unheld == 0);
        CHECK(counts[ORACLES_LANGUAGE_SOURCE_REFUSED] == 0 && counts[ORACLES_LANGUAGE_SOURCE_MISSING] == 0 && counts[ORACLES_LANGUAGE_SOURCE_KEPT] == 0);
        CHECK(counts[ORACLES_LANGUAGE_SOURCE_SAME] == e->same[language]);
        CHECK(report.served == served && report.english == 0 && report.missing == 0);
        free(image);
    }
    check_graphics(name, game, pack, &us, &eu, &graphics_expected[e - games]);
    check_composed(name, game, pack, &us);
    check_hook(name, us.data, game == ORACLES_GAME_AGES ? &oracles_guest_tables_ages : &oracles_guest_tables_seasons);
    check_fans(dir, work, pack, &us);
    /* The pack's file: read back, it composes the same image. */
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.pack", work, name);
    CHECK(oracles_language_pack_write(pack, path, error, sizeof error) == 0);
    OraclesLanguagePack *back = oracles_language_pack_read(path, error, sizeof error);
    CHECK(back != NULL);
    if (back) {
        uint8_t *a = NULL, *b = NULL;
        size_t a_size = 0, b_size = 0;
        OraclesLanguageReport report;
        CHECK(oracles_language_compose(pack, ORACLES_LANGUAGE_FRENCH, us.data, us.size, &a, &a_size, &report, error, sizeof error) == 0);
        CHECK(oracles_language_compose(back, ORACLES_LANGUAGE_FRENCH, us.data, us.size, &b, &b_size, &report, error, sizeof error) == 0);
        CHECK(a && b && a_size == b_size && !memcmp(a, b, a_size));
        free(a);
        free(b);
        oracles_language_pack_free(back);
    }
    remove(path);
    oracles_language_pack_free(pack);
    free(us.data);
    free(eu.data);
    return 1;
}

int main(int argc, char **argv)
{
    const char *dir = getenv("ORACLES_ROM_DIR");
    if (!dir || !*dir) { printf("language-roms: ORACLES_ROM_DIR is not set: skipped\n"); return 77; }
    unsigned checked = 0;
    for (size_t i = 0; i < sizeof games / sizeof games[0]; i++) checked += (unsigned)check_game(dir, argc > 1 ? argv[1] : ".", &games[i]);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    if (!failures && !checked) { printf("language-roms: no game's US and European ROMs in %s: skipped\n", dir); return 77; }
    return failures ? 1 : 0;
}
