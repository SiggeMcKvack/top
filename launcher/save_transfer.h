/* A game's save brought in from another file, and a copy of it taken out (Cartridge's Save row).  A file comes in as
 * the save of a game when it is a save of the game's engine, Ages' or Seasons' (a fan game's, its Oracle's): an
 * SRAM image of the size the game's has, one copy of a file at least valid (save.h).  An Oracle's has its language set
 * to English, which a save of the European ROM played in German, Italian or Spanish needs; a fan game's is taken as it
 * is.  The save it replaces is kept in a Backups folder beside it, under its name and the time, never over another
 * file, and its savestate moved there with it; the new one is written under a temporary name, then renamed over the
 * save. */
#ifndef ORACLES_LAUNCHER_SAVE_TRANSFER_H
#define ORACLES_LAUNCHER_SAVE_TRANSFER_H

#include "save.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* The most a file chosen as a save is read of: a save and a byte, which tells a larger file. */
#define ORACLES_SAVE_TRANSFER_READ (ORACLES_SAVE_SIZE + 1u)

/* The file at `path` (on Android, its content:// address) into `out`, ORACLES_SAVE_TRANSFER_READ bytes, *size the bytes
 * read: a file larger than a save (a video, a disc image chosen by mistake) refused unread, with "larger than a save"
 * in `error`, and never more than ORACLES_SAVE_TRANSFER_READ bytes read of one whose size is not known.  1, or 0 with
 * the reason in `error`. */
int oracles_save_transfer_read(const char *path, uint8_t *out, size_t *size, char *error, size_t error_capacity);

/* The `size` bytes of a file as the save of a game on `engine`, into `out` (ORACLES_SAVE_SIZE bytes): 1 when they are
 * one, *english then 1 when `oracle` set a language other than English to it; 0 when they are not one. */
int oracles_save_transfer_check(const uint8_t *data, size_t size, OraclesGame engine, int oracle, uint8_t *out, int *english);

/* The save at `save` copied into its folder's Backups, as "NAME YYYY-MM-DD HH-MM.sav" at the local time `now` (or with
 * " (2)"... before the extension when a file of that name is there and holds other bytes), *backup its path, empty when
 * there was no save; its savestate (NAME.state beside NAME.sav, whichever core took it), which holds the SRAM it
 * replaces, moved there likewise, "NAME YYYY-MM-DD HH-MM.state", *state_backup its path, empty when there was none;
 * then `data` written as the save.  1, or 0 with the reason in `error`: the save replaced only once both are in
 * Backups. */
int oracles_save_transfer_import(const char *save, const uint8_t *data, size_t size, time_t now, char *backup, size_t backup_capacity,
                                 char *state_backup, size_t state_backup_capacity, char *error, size_t error_capacity);

/* The save at `save` copied to `destination` (on Android, the content:// address the document picker made).  1, or 0
 * with the reason in `error`. */
int oracles_save_transfer_export(const char *save, const char *destination, char *error, size_t error_capacity);

#endif
