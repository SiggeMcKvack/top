/* One HTTPS GET with the platform's own client and the system's TLS, the server's certificate checked: WinHTTP on
 * Windows, NSURLSession on macOS (macos/update_fetch.m), the libcurl the system has on Linux, loaded at run time, none
 * otherwise, and HttpsURLConnection on Android (OraclesActivity.httpGet).  Nothing is sent but the request: the
 * address, an Accept header and a User-Agent without the version.  A redirection is followed, to https:// only. */
#ifndef ORACLES_LAUNCHER_UPDATE_FETCH_H
#define ORACLES_LAUNCHER_UPDATE_FETCH_H

#include <SDL3/SDL_atomic.h>

#include <stddef.h>

#define ORACLES_UPDATE_USER_AGENT "the-oracles-project"
#define ORACLES_UPDATE_ACCEPT "application/vnd.github+json"

/* Asks `url` (https://), in `timeout_ms` or about: 1 when a server answered, with *status its HTTP status and *body
 * what it sent, at most ORACLES_UPDATE_MAX_BODY bytes, zero-terminated, to free with SDL_free; 0 otherwise, with the
 * reason in `error`.  Blocks: called on the check's thread.  `cancel` set to 1 from another thread ends it early where
 * the client allows it (Linux). */
int oracles_update_fetch(const char *url, int timeout_ms, SDL_AtomicInt *cancel, char **body, size_t *length, int *status,
                         char *error, size_t capacity);

#endif
