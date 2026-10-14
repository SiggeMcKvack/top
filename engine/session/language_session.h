/* A session's language (--language, --eu-rom, --language-pack), for the launcher and the harness alike: its pack read
 * or extracted, composed into the image, served by the guest, and said in the report.  The files are read here, with
 * the C library; engine/language opens only the pack it is given. */
#ifndef ORACLES_LANGUAGE_SESSION_H
#define ORACLES_LANGUAGE_SESSION_H

#include "guest.h"
#include "language.h"

#include <stdio.h>

typedef struct OraclesLanguageSession OraclesLanguageSession;

/* The language `code` for the image `info` describes, read before any composition: the US ROM of Ages or Seasons, or a
 * fan game made on it from the US ROM at `base_path` (NULL for an original, or a fan game's image read whole: its US ROM
 * is then the one beside its European ROM).  Its pack read from `pack_path`, else
 * extracted from the European ROM `european` of the game (the file, or a directory where it is found by its SHA-1),
 * for a fan game from that game's pack and the two images (oracles_language_pack_derive), and written to `pack_path`
 * when one is given (a pack that cannot be written serves all the same: oracles_language_session_note says so).  A pack
 * read that is not this version's, of this game or whole is extracted again when the European ROM is given, refused
 * otherwise.  *out is NULL for English.  0, or -1 with the reason. */
int oracles_language_session_start(OraclesLanguageSession **out, const char *code, const char *european, const char *pack_path,
                                   const char *base_path, const OraclesRomInfo *info, const uint8_t *rom, size_t rom_size, char *error,
                                   size_t capacity);
/* The language composed into the image (after the mods' houses), which it replaces. */
int oracles_language_session_compose(OraclesLanguageSession *session, uint8_t **rom, size_t *rom_size, char *error, size_t capacity);
/* Arms the guest's transaction on the composed image. */
int oracles_language_session_attach(OraclesLanguageSession *session, OraclesGuest *guest, char *error, size_t capacity);
/* What the player is told of a session that plays: the pack it extracted could not be written (it is extracted again
 * at the next start); "" for nothing, and without a language. */
const char *oracles_language_session_note(const OraclesLanguageSession *session);
/* "language-fr@SHA1", the language and its pack's digest, part of a savestate's identity: a state taken with another
 * pack (extracted again from other tables) is refused; "" without a language. */
const char *oracles_language_session_identity(const OraclesLanguageSession *session);
/* The language and its pack, and the texts the game looked up (oracles_guest_language_counts): the end of a session's
 * report; `prefix` starts the line. */
void oracles_language_session_report(const OraclesLanguageSession *session, const OraclesGuest *guest, const char *prefix, FILE *out);
/* The graphics that carry text the composition wrote in the language (0 before it, or without a language). */
unsigned oracles_language_session_graphics(const OraclesLanguageSession *session);
void oracles_language_session_stop(OraclesLanguageSession *session);

#endif
