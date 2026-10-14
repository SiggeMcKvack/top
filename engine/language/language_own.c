/* The port's own translations, config/language_own.tsv embedded at configure, and the syntax they are written in.
 *
 * A row: a key, a language and the text, separated by tabs; a line starting with # is a comment.  The text is the
 * font's characters in UTF-8 (ASCII, and the accented letters and marks of the European font), \n a line break,
 * \stop the end of a box, \col(N) a colour (N hexadecimal), \\ a backslash, \xNN any other byte. */
#include "language_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char oracles_language_own_texts[];

/* The font's characters past ASCII: the code point each byte from $80 prints, 0 where it prints none of a language's
 * letters (ROM_DATA_FORMATS, section 16). */
static const uint16_t letters[0x60] = {
    0x00c0, 0x00c2, 0x00c4, 0x00c6, 0x00c7, 0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00ce, 0x00cf, 0x00d1, 0x00d6, 0x0152, 0x00d9, 0x00db,   /* $80 */
    0x00dc, 0x00df, 0, 0, 0, 0, 0, 0, 0x00a1, 0x00bf, 0, 0, 0, 0, 0, 0,                                                               /* $90 */
    0x00e0, 0x00e2, 0x00e4, 0x00e6, 0x00e7, 0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ee, 0x00ef, 0x00f1, 0x00f6, 0x0153, 0x00f9, 0x00fb,   /* $a0 */
    0x00fc, 0x00b4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                                                         /* $b0 */
    0x00d4, 0x00b0, 0x00c1, 0x00cd, 0x00d3, 0x00da, 0x00cc, 0x00d2, 0x00c5, 0, 0, 0, 0, 0, 0, 0,                                     /* $c0 */
    0x00f4, 0x00aa, 0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00ec, 0x00f2, 0x00e5, 0, 0, 0, 0, 0, 0, 0,                                     /* $d0 */
};

/* The UTF-8 character at *at, its code point; *at past it.  0 for a sequence that is not one. */
static unsigned utf8(const char **at, const char *end)
{
    const unsigned char *p = (const unsigned char *)*at;
    unsigned c = p[0];
    const unsigned n = c < 0x80u ? 0u : (c & 0xe0u) == 0xc0u ? 1u : (c & 0xf0u) == 0xe0u ? 2u : (c & 0xf8u) == 0xf0u ? 3u : 4u;
    if (n > 3u || (size_t)(end - *at) <= n) return 0;
    c &= n ? 0x3fu >> n : 0x7fu;
    for (unsigned i = 1; i <= n; i++) {
        if ((p[i] & 0xc0u) != 0x80u) return 0;
        c = c << 6 | (p[i] & 0x3fu);
    }
    *at += n + 1u;
    return c;
}

static int hex_byte(const char *at, const char *end, unsigned *value)
{
    *value = 0;
    if (end - at < 2) return -1;
    for (unsigned i = 0; i < 2u; i++) {
        const int c = tolower((unsigned char)at[i]);
        if (!isxdigit(c)) return -1;
        *value = *value << 4 | (unsigned)(isdigit(c) ? c - '0' : c - 'a' + 10);
    }
    return 0;
}

int language_text_encode(const char *text, size_t length, uint8_t *out, size_t capacity, size_t *out_length)
{
    size_t n = 0;
    const char *end = text + length;
#define PUT(b) do { if (n >= capacity) return -1; out[n++] = (uint8_t)(b); } while (0)
    for (const char *at = text; at < end;) {
        if (*at == '\\') {
            const size_t left = (size_t)(end - at);
            unsigned value = 0;
            if (left >= 2u && at[1] == 'n') { PUT(0x01); at += 2; }
            else if (left >= 2u && at[1] == '\\') { PUT('\\'); at += 2; }
            else if (left >= 5u && !memcmp(at, "\\stop", 5)) { PUT(0x0c); PUT(0x18); at += 5; }
            else if (left >= 8u && !memcmp(at, "\\col(", 5) && at[7] == ')' && hex_byte(at + 5, end, &value) == 0) { PUT(0x09); PUT(value); at += 8; }
            else if (left >= 4u && at[1] == 'x' && hex_byte(at + 2, end, &value) == 0 && value) { PUT(value); at += 4; }
            else return -1;
            continue;
        }
        const unsigned c = utf8(&at, end);
        if (c >= 0x20u && c < 0x7fu && c != '\\') { PUT(c); continue; }
        unsigned byte = 0;
        for (unsigned i = 0; c && i < sizeof letters / sizeof letters[0] && !byte; i++) if (letters[i] == c) byte = 0x80u + i;
        if (!byte) return -1;
        PUT(byte);
    }
    PUT(0);
#undef PUT
    *out_length = n;
    return 0;
}

int language_own_nth(unsigned n, const char **key, size_t *key_length, OraclesLanguage *language, const char **text, size_t *text_length)
{
    unsigned row = 0;
    for (const char *line = (const char *)oracles_language_own_texts; *line;) {
        const char *end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        const char *next = *end ? end + 1 : end;
        if (end > line && end[-1] == '\r') end--;
        if (*line != '#' && end > line) {
            if (row++ == n) {
                const char *tab1 = memchr(line, '\t', (size_t)(end - line));
                const char *tab2 = tab1 ? memchr(tab1 + 1, '\t', (size_t)(end - tab1 - 1)) : NULL;
                char code[8] = { 0 };
                if (!tab2 || tab2 - tab1 < 2 || (size_t)(tab2 - tab1 - 1) >= sizeof code) return -1;
                memcpy(code, tab1 + 1, (size_t)(tab2 - tab1 - 1));
                if (oracles_language_parse(code, language) != 0) return -1;
                *key = line;
                *key_length = (size_t)(tab1 - line);
                *text = tab2 + 1;
                *text_length = (size_t)(end - tab2 - 1);
                return 0;
            }
        }
        line = next;
    }
    return -1;
}

int language_own_text(const char *key, OraclesLanguage language, uint8_t *out, size_t capacity, size_t *out_length)
{
    const char *k, *text;
    size_t k_length, text_length;
    OraclesLanguage l;
    for (unsigned n = 0; language_own_nth(n, &k, &k_length, &l, &text, &text_length) == 0; n++)
        if (l == language && k_length == strlen(key) && !memcmp(k, key, k_length)) return language_text_encode(text, text_length, out, capacity, out_length);
    return -1;
}
