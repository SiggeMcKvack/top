#include "update_fetch.h"

#include "update_check.h"

#include <SDL3/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#elif defined(__ANDROID__)
#include <jni.h>
#endif

#if defined(_WIN32) || defined(__linux__)
/* What the server sent so far, zero-terminated. */
typedef struct received {
    char *data;
    size_t length, capacity;
    int too_large;
} received;

static int append(received *r, const char *bytes, size_t count)
{
    if (r->length + count > ORACLES_UPDATE_MAX_BODY) { r->too_large = 1; return 0; }
    if (r->length + count + 1 > r->capacity) {
        size_t capacity = r->capacity ? r->capacity : 16384;
        while (capacity < r->length + count + 1) capacity *= 2;
        char *data = SDL_realloc(r->data, capacity);
        if (!data) return 0;
        r->data = data;
        r->capacity = capacity;
    }
    memcpy(r->data + r->length, bytes, count);
    r->length += count;
    r->data[r->length] = 0;
    return 1;
}

/* The answer handed over, an empty body included. */
static int answered(received *r, char **body, size_t *length)
{
    if (!r->data && !append(r, "", 0)) return 0;
    *body = r->data;
    *length = r->length;
    return 1;
}
#endif

#if defined(_WIN32)

static void winhttp_error(const char *step, char *error, size_t capacity)
{
    const DWORD code = GetLastError();
    const char *name = code == ERROR_WINHTTP_TIMEOUT ? ", timed out" : code == ERROR_WINHTTP_NAME_NOT_RESOLVED ? ", the name not resolved"
                       : code == ERROR_WINHTTP_CANNOT_CONNECT ? ", no connection" : code == ERROR_WINHTTP_SECURE_FAILURE ? ", the certificate refused"
                       : code == ERROR_WINHTTP_CONNECTION_ERROR ? ", the connection reset" : "";
    snprintf(error, capacity, "WinHTTP, %s: error %lu%s", step, (unsigned long)code, name);
}

int oracles_update_fetch(const char *url, int timeout_ms, SDL_AtomicInt *cancel, char **body, size_t *length, int *status,
                         char *error, size_t capacity)
{
    wchar_t wide[2048], host[256], path[2048];
    URL_COMPONENTS parts;
    memset(&parts, 0, sizeof parts);
    parts.dwStructSize = sizeof parts;
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wide, 2048) || !WinHttpCrackUrl(wide, 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        snprintf(error, capacity, "not an https:// address");
        return 0;
    }
    /* The automatic proxy from Windows 8.1, the one configured before. */
    HINTERNET session = WinHttpOpen(L"" ORACLES_UPDATE_USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"" ORACLES_UPDATE_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = NULL, request = NULL;
    received r = { NULL, 0, 0, 0 };
    int ok = 0;
    DWORD code = 0, size = sizeof code;
    if (!session) winhttp_error("open", error, capacity);
    else if (!WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms)
             || !(connection = WinHttpConnect(session, host, parts.nPort, 0))) winhttp_error("connect", error, capacity);
    else if (!(request = WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE))
             || !WinHttpAddRequestHeaders(request, L"Accept: " ORACLES_UPDATE_ACCEPT, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD)
             || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
             || !WinHttpReceiveResponse(request, NULL)) winhttp_error("request", error, capacity);
    else if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX))
        winhttp_error("status", error, capacity);
    else {
        ok = 1;
        for (;;) {
            char chunk[16384];
            DWORD read = 0;
            if (SDL_GetAtomicInt(cancel)) { snprintf(error, capacity, "cancelled"); ok = 0; break; }
            if (!WinHttpReadData(request, chunk, sizeof chunk, &read)) { winhttp_error("read", error, capacity); ok = 0; break; }
            if (!read) break;
            if (!append(&r, chunk, read)) {
                snprintf(error, capacity, r.too_large ? "an answer larger than %d bytes" : "out of memory", ORACLES_UPDATE_MAX_BODY);
                ok = 0;
                break;
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    if (ok && answered(&r, body, length)) { *status = (int)code; return 1; }
    SDL_free(r.data);
    return 0;
}

#elif defined(__ANDROID__)

int oracles_update_fetch(const char *url, int timeout_ms, SDL_AtomicInt *cancel, char **body, size_t *length, int *status,
                         char *error, size_t capacity)
{
    (void)cancel;
    JNIEnv *env = SDL_GetAndroidJNIEnv();
    jobject activity = SDL_GetAndroidActivity();
    int ok = 0;
    snprintf(error, capacity, "the activity cannot be called");
    if (!env || !activity) return 0;
    jclass class = (*env)->GetObjectClass(env, activity);
    jmethodID method = (*env)->GetStaticMethodID(env, class, "httpGet", "(Ljava/lang/String;I)Ljava/lang/String;");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    jstring argument = (*env)->NewStringUTF(env, url);
    jstring answer = method && argument ? (*env)->CallStaticObjectMethod(env, class, method, argument, (jint)timeout_ms) : NULL;
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    /* "STATUS\nBODY", or "0\nREASON". */
    const char *text = answer ? (*env)->GetStringUTFChars(env, answer, NULL) : NULL;
    if (text) {
        char *end;
        const long code = strtol(text, &end, 10);
        const char *rest = *end == '\n' ? end + 1 : end;
        received r = { NULL, 0, 0, 0 };
        if (code <= 0) snprintf(error, capacity, "%s", rest);
        else if (!append(&r, rest, strlen(rest)) || !answered(&r, body, length)) {
            snprintf(error, capacity, r.too_large ? "an answer larger than %d bytes" : "out of memory", ORACLES_UPDATE_MAX_BODY);
            SDL_free(r.data);
        } else {
            *status = (int)code;
            ok = 1;
        }
        (*env)->ReleaseStringUTFChars(env, answer, text);
    }
    if (answer) (*env)->DeleteLocalRef(env, answer);
    if (argument) (*env)->DeleteLocalRef(env, argument);
    (*env)->DeleteLocalRef(env, class);
    (*env)->DeleteLocalRef(env, activity);
    return ok;
}

#elif defined(__linux__)

/* libcurl's interface as its ABI has it, libcurl.so.4 since 2006: no header, and nothing linked, the library loaded at
 * the check, so that the launcher still runs where there is none. */
typedef struct curl_slist curl_slist;
#define CURLOPT_WRITEDATA 10001
#define CURLOPT_URL 10002
#define CURLOPT_WRITEFUNCTION 20011
#define CURLOPT_USERAGENT 10018
#define CURLOPT_HTTPHEADER 10023
#define CURLOPT_NOPROGRESS 43
#define CURLOPT_FOLLOWLOCATION 52
#define CURLOPT_XFERINFODATA 10057
#define CURLOPT_MAXREDIRS 68
#define CURLOPT_NOSIGNAL 99
#define CURLOPT_TIMEOUT_MS 155
#define CURLOPT_CONNECTTIMEOUT_MS 156
#define CURLOPT_PROTOCOLS 181
#define CURLOPT_REDIR_PROTOCOLS 182
#define CURLOPT_XFERINFOFUNCTION 20219
#define CURLINFO_RESPONSE_CODE 0x200002
#define CURLPROTO_HTTPS 2L
#define CURLE_WRITE_ERROR 23
#define CURLE_ABORTED_BY_CALLBACK 42

typedef struct curl_calls {
    void *(*easy_init)(void);
    int (*easy_setopt)(void *, int, ...);
    int (*easy_perform)(void *);
    int (*easy_getinfo)(void *, int, ...);
    void (*easy_cleanup)(void *);
    const char *(*easy_strerror)(int);
    curl_slist *(*slist_append)(curl_slist *, const char *);
    void (*slist_free_all)(curl_slist *);
} curl_calls;

/* Debian's and Ubuntu's libcurl4 (OpenSSL), else their libcurl3-gnutls; every other distribution's libcurl.so.4.  Never
 * unloaded: its TLS library may keep handlers until the process ends. */
static int load_curl(curl_calls *c)
{
    static const char *const names[] = { "libcurl.so.4", "libcurl-gnutls.so.4" };
    SDL_SharedObject *library = NULL;
    for (size_t i = 0; i < sizeof names / sizeof names[0] && !library; i++) library = SDL_LoadObject(names[i]);
    if (!library) return 0;
    c->easy_init = (void *(*)(void))SDL_LoadFunction(library, "curl_easy_init");
    c->easy_setopt = (int (*)(void *, int, ...))SDL_LoadFunction(library, "curl_easy_setopt");
    c->easy_perform = (int (*)(void *))SDL_LoadFunction(library, "curl_easy_perform");
    c->easy_getinfo = (int (*)(void *, int, ...))SDL_LoadFunction(library, "curl_easy_getinfo");
    c->easy_cleanup = (void (*)(void *))SDL_LoadFunction(library, "curl_easy_cleanup");
    c->easy_strerror = (const char *(*)(int))SDL_LoadFunction(library, "curl_easy_strerror");
    c->slist_append = (curl_slist *(*)(curl_slist *, const char *))SDL_LoadFunction(library, "curl_slist_append");
    c->slist_free_all = (void (*)(curl_slist *))SDL_LoadFunction(library, "curl_slist_free_all");
    return c->easy_init && c->easy_setopt && c->easy_perform && c->easy_getinfo && c->easy_cleanup && c->easy_strerror && c->slist_append
           && c->slist_free_all;
}

static size_t curl_received(char *data, size_t size, size_t count, void *opaque)
{
    return append(opaque, data, size * count) ? size * count : 0;
}

/* Called during the transfer, about once a second at the least: a nonzero answer ends it. */
static int curl_progress(void *opaque, int64_t down_total, int64_t down, int64_t up_total, int64_t up)
{
    (void)down_total; (void)down; (void)up_total; (void)up;
    return SDL_GetAtomicInt(opaque) != 0;
}

int oracles_update_fetch(const char *url, int timeout_ms, SDL_AtomicInt *cancel, char **body, size_t *length, int *status,
                         char *error, size_t capacity)
{
    curl_calls c;
    if (!load_curl(&c)) { snprintf(error, capacity, "no libcurl.so.4 on this system"); return 0; }
    void *curl = c.easy_init();
    curl_slist *headers = c.slist_append(NULL, "Accept: " ORACLES_UPDATE_ACCEPT);
    if (!curl || !headers) {
        if (curl) c.easy_cleanup(curl);
        if (headers) c.slist_free_all(headers);
        snprintf(error, capacity, "libcurl cannot start");
        return 0;
    }
    received r = { NULL, 0, 0, 0 };
    c.easy_setopt(curl, CURLOPT_URL, url);
    c.easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    c.easy_setopt(curl, CURLOPT_USERAGENT, ORACLES_UPDATE_USER_AGENT);
    c.easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    c.easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    c.easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
    c.easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    c.easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);   /* a thread of its own: no alarm for the name's resolution */
    c.easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    c.easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)timeout_ms);
    c.easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_received);
    c.easy_setopt(curl, CURLOPT_WRITEDATA, &r);
    c.easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    c.easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress);
    c.easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
    const int result = c.easy_perform(curl);
    long code = 0;
    c.easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    c.easy_cleanup(curl);
    c.slist_free_all(headers);
    if (result == 0 && code > 0 && answered(&r, body, length)) { *status = (int)code; return 1; }
    if (result == CURLE_WRITE_ERROR && r.too_large) snprintf(error, capacity, "an answer larger than %d bytes", ORACLES_UPDATE_MAX_BODY);
    else if (result == CURLE_ABORTED_BY_CALLBACK) snprintf(error, capacity, "cancelled");
    else snprintf(error, capacity, "libcurl: %s", result ? c.easy_strerror(result) : "no status");
    SDL_free(r.data);
    return 0;
}

#else

int oracles_update_fetch(const char *url, int timeout_ms, SDL_AtomicInt *cancel, char **body, size_t *length, int *status,
                         char *error, size_t capacity)
{
    (void)url; (void)timeout_ms; (void)cancel; (void)body; (void)length; (void)status;
    snprintf(error, capacity, "no HTTP client on this system");
    return 0;
}

#endif
