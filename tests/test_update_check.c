/* The update check without the network: GitHub's answer for the latest release (update_release.json, as the API gave
 * it for v1.1.0, and variants of it) read to its tag, its page and whether it is a draft or a pre-release, the nested
 * objects' fields of the same names skipped; the versions compared; what each answer shows, HTTP errors and the rate
 * limit, malformed answers, drafts, pre-releases and tags older or the same showing nothing; and the check run on its
 * thread from a file, as --update-check-url gives one. */
#include "update_check.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

static char *release;   /* update_release.json */
static size_t release_length;

/* The release with the first `from` replaced by `to`; to free. */
static char *variant(const char *from, const char *to)
{
    const char *at = strstr(release, from);
    if (!at) { fprintf(stderr, "FAIL: %s not in the release\n", from); failures++; at = release + release_length; from = ""; }
    const size_t before = (size_t)(at - release), length = release_length - strlen(from) + strlen(to);
    char *text = malloc(length + 1);
    if (!text) exit(1);
    memcpy(text, release, before);
    memcpy(text + before, to, strlen(to));
    strcpy(text + before + strlen(to), at + strlen(from));
    return text;
}

static OraclesUpdateState evaluate(const char *running, int http_status, const char *body, OraclesUpdateStatus *status, char *reason)
{
    return oracles_update_evaluate(running, http_status, "the reason given", body, body ? strlen(body) : 0, status, reason, 256);
}

static int parses(const char *json, OraclesRelease *r)
{
    return oracles_update_parse_release(json, strlen(json), r);
}

static void versions(void)
{
    unsigned v[3], w[3];
    CHECK(oracles_update_parse_version("v1.2.3", v) == 1 && v[0] == 1 && v[1] == 2 && v[2] == 3);
    CHECK(oracles_update_parse_version("1.2.3", v) == 1);
    CHECK(oracles_update_parse_version("v10.0.12+build.5", v) == 1 && v[0] == 10 && v[2] == 12);
    CHECK(oracles_update_parse_version("v1.2.0-rc.1", v) == -1);
    CHECK(oracles_update_parse_version("v1.2", v) == 0 && oracles_update_parse_version("v1.2.3.4", v) == 0);
    CHECK(oracles_update_parse_version("V1.2.3", v) == 0 && oracles_update_parse_version("release-1", v) == 0 && oracles_update_parse_version("", v) == 0);
    CHECK(oracles_update_parse_version("v01.2.3", v) == 0 && oracles_update_parse_version("v1..3", v) == 0 && oracles_update_parse_version("v1.2.3-", v) == 0);
    CHECK(oracles_update_parse_version("v1.2.3 ", v) == 0 && oracles_update_parse_version("v1.2.x", v) == 0 && oracles_update_parse_version("v1.2.3+", v) == 0);
    CHECK(oracles_update_parse_version("v1.0.0", v) == 1 && v[0] == 1 && v[1] == 0 && v[2] == 0);
    CHECK(oracles_update_parse_version("v1.2.9999999", v) == 0);   /* past the five digits a part keeps */
    /* By number, part by part: 1.10.0 after 1.9.0. */
    oracles_update_parse_version("1.10.0", v);
    oracles_update_parse_version("1.9.0", w);
    CHECK(oracles_update_compare(v, w) > 0 && oracles_update_compare(w, v) < 0 && oracles_update_compare(v, v) == 0);
    oracles_update_parse_version("2.0.0", v);
    oracles_update_parse_version("1.99.99", w);
    CHECK(oracles_update_compare(v, w) > 0);
    oracles_update_parse_version("1.1.1", v);
    oracles_update_parse_version("1.1.0", w);
    CHECK(oracles_update_compare(v, w) > 0);
}

static void parsing(void)
{
    OraclesRelease r;
    /* GitHub's answer: the top level's fields, not the author's html_url nor the assets' uploaders'. */
    CHECK(oracles_update_parse_release(release, release_length, &r));
    CHECK(r.has_tag && !strcmp(r.tag, "v1.1.0") && r.has_url && !strcmp(r.url, "https://github.com/thibautbus/top/releases/tag/v1.1.0"));
    CHECK(!r.draft && !r.prerelease);
    char *text = variant("\"html_url\":\"https://github.com/thibautbus/top/releases/tag/v1.1.0\",", "");
    CHECK(parses(text, &r) && !r.has_url && r.has_tag);
    free(text);
    text = variant("\"tag_name\":\"v1.1.0\",", "");
    CHECK(parses(text, &r) && !r.has_tag && r.has_url);
    free(text);
    text = variant("\"draft\":false", "\"draft\":true");
    CHECK(parses(text, &r) && r.draft && !r.prerelease);
    free(text);
    text = variant("\"prerelease\":false", "\"prerelease\":true");
    CHECK(parses(text, &r) && r.prerelease && !r.draft);
    free(text);
    /* Pretty-printed, as another server may send it. */
    CHECK(parses(" {\n  \"tag_name\" : \"v1.2.0\" ,\r\n\t\"draft\": false,\n  \"html_url\": \"https://example.org/r\"\n}\n", &r) && !strcmp(r.tag, "v1.2.0")
          && !strcmp(r.url, "https://example.org/r"));
    /* Escapes: the notes quoting a tag, accents and an emoji (a surrogate pair), and a field after them. */
    CHECK(parses("{\"body\":\"Say \\\"tag_name\\\":\\\"v9.9.9\\\" \\ud83c\\udf89 \\u00e9t\\u00e9 \\\\ \\/ \\n\",\"tag_name\":\"v1.2.0\",\"html_url\":\"https:\\/\\/x\\/a\"}", &r)
          && !strcmp(r.tag, "v1.2.0") && !strcmp(r.url, "https://x/a"));
    CHECK(parses("{\"tag_name\":\"v\\u0031.2.0\"}", &r) && !strcmp(r.tag, "v1.2.0"));
    CHECK(parses("{\"tag_name\":\"\\u00e9\"}", &r) && !strcmp(r.tag, "\xc3\xa9"));
    CHECK(parses("{}", &r) && !r.has_tag);
    CHECK(parses("{\"a\":[1,-2.5e3,true,false,null,{\"b\":{}},[]],\"tag_name\":\"v1.0.0\"}", &r) && r.has_tag);
    /* A key longer than any read, skipped. */
    CHECK(parses("{\"a_key_much_longer_than_the_check_reads_at_all\":1,\"tag_name\":\"v1.0.0\"}", &r) && r.has_tag);
    /* Malformed: cut, not an object, a field of the wrong type, garbage after, an unpaired surrogate, a raw control. */
    CHECK(!oracles_update_parse_release(release, release_length / 2, &r));
    CHECK(!oracles_update_parse_release(release, release_length - 1, &r));
    CHECK(!parses("", &r) && !parses("   ", &r) && !parses("[]", &r) && !parses("null", &r) && !parses("\"v1.2.0\"", &r));
    CHECK(!parses("{", &r) && !parses("{\"tag_name\"}", &r) && !parses("{\"tag_name\":}", &r) && !parses("{\"tag_name\":\"v1\",}", &r));
    CHECK(!parses("{\"tag_name\":5}", &r) && !parses("{\"draft\":\"false\"}", &r) && !parses("{\"prerelease\":null}", &r) && !parses("{\"html_url\":[]}", &r));
    CHECK(!parses("{\"tag_name\":\"v1.2.0\"} x", &r) && !parses("{\"tag_name\":\"v1.2.0\"}{}", &r));
    CHECK(!parses("{\"tag_name\":\"\\ud83c\"}", &r) && !parses("{\"tag_name\":\"\\udf89\"}", &r) && !parses("{\"tag_name\":\"\\x\"}", &r));
    CHECK(!parses("{\"tag_name\":\"a\nb\"}", &r) && !parses("{\"a\":tru}", &r) && !parses("{\"a\":-}", &r) && !parses("{'a':1}", &r));
    /* A tag or a page longer than the check keeps: refused, never cut. */
    char long_tag[200];
    snprintf(long_tag, sizeof long_tag, "{\"tag_name\":\"v1.2.0-%0100d\"}", 0);
    CHECK(!parses(long_tag, &r));
    /* Nesting: GitHub's three levels and far more read, beyond 64 refused. */
    char deep[400];
    memset(deep, 0, sizeof deep);
    strcpy(deep, "{\"a\":");
    for (int i = 0; i < 70; i++) strcat(deep, "[");
    for (int i = 0; i < 70; i++) strcat(deep, "]");
    strcat(deep, "}");
    CHECK(!parses(deep, &r));
    memset(deep, 0, sizeof deep);
    strcpy(deep, "{\"a\":");
    for (int i = 0; i < 60; i++) strcat(deep, "[");
    for (int i = 0; i < 60; i++) strcat(deep, "]");
    strcat(deep, "}");
    CHECK(parses(deep, &r));
}

static void answers(void)
{
    OraclesUpdateStatus s;
    char reason[256];
    /* The release as published, for the version that is it, an older one and a newer one. */
    CHECK(evaluate("1.1.0", 200, release, &s, reason) == ORACLES_UPDATE_CURRENT && s.version[0] == 0 && s.url[0] == 0);
    CHECK(strstr(reason, "v1.1.0") != NULL);
    CHECK(evaluate("1.0.0", 200, release, &s, reason) == ORACLES_UPDATE_NEWER && s.state == ORACLES_UPDATE_NEWER);
    CHECK(!strcmp(s.version, "1.1.0") && !strcmp(s.url, "https://github.com/thibautbus/top/releases/tag/v1.1.0"));
    CHECK(evaluate("1.0.9", 200, release, &s, reason) == ORACLES_UPDATE_NEWER);
    CHECK(evaluate("1.1.1", 200, release, &s, reason) == ORACLES_UPDATE_CURRENT);
    CHECK(evaluate("2.0.0", 200, release, &s, reason) == ORACLES_UPDATE_CURRENT);
    /* A later release, its tag with or without its v. */
    char *text = variant("\"tag_name\":\"v1.1.0\"", "\"tag_name\":\"v1.10.0\"");
    CHECK(evaluate("1.9.0", 200, text, &s, reason) == ORACLES_UPDATE_NEWER && !strcmp(s.version, "1.10.0"));
    CHECK(evaluate("1.10.0", 200, text, &s, reason) == ORACLES_UPDATE_CURRENT);
    free(text);
    text = variant("\"tag_name\":\"v1.1.0\"", "\"tag_name\":\"1.2.0\"");
    CHECK(evaluate("1.1.0", 200, text, &s, reason) == ORACLES_UPDATE_NEWER && !strcmp(s.version, "1.2.0"));
    free(text);
    /* Drafts and pre-releases, by their fields or their tag, show nothing. */
    text = variant("\"draft\":false", "\"draft\":true");
    CHECK(evaluate("1.0.0", 200, text, &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "draft") && s.url[0] == 0);
    free(text);
    text = variant("\"prerelease\":false", "\"prerelease\":true");
    CHECK(evaluate("1.0.0", 200, text, &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "pre-release"));
    free(text);
    text = variant("\"tag_name\":\"v1.1.0\"", "\"tag_name\":\"v1.2.0-rc.1\"");
    CHECK(evaluate("1.1.0", 200, text, &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "pre-release"));
    free(text);
    text = variant("\"tag_name\":\"v1.1.0\"", "\"tag_name\":\"nightly\"");
    CHECK(evaluate("1.1.0", 200, text, &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "not a version"));
    free(text);
    text = variant("\"tag_name\":\"v1.1.0\",", "");
    CHECK(evaluate("1.0.0", 200, text, &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "tag_name"));
    free(text);
    /* A newer release offers only an https:// page. */
    text = variant("\"html_url\":\"https://github.com/thibautbus/top/releases/tag/v1.1.0\"", "\"html_url\":\"file:///etc/passwd\"");
    CHECK(evaluate("1.0.0", 200, text, &s, reason) == ORACLES_UPDATE_FAILED && s.url[0] == 0);
    CHECK(evaluate("1.1.0", 200, text, &s, reason) == ORACLES_UPDATE_CURRENT);   /* no page needed */
    free(text);
    text = variant("\"html_url\":\"https://github.com/thibautbus/top/releases/tag/v1.1.0\",", "");
    CHECK(evaluate("1.0.0", 200, text, &s, reason) == ORACLES_UPDATE_FAILED && s.url[0] == 0);
    free(text);
    /* No answer, HTTP errors and the rate limit: nothing shown, the reason said. */
    CHECK(evaluate("1.0.0", 0, NULL, &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "the reason given"));
    CHECK(evaluate("1.0.0", 403, "{\"message\":\"API rate limit exceeded\"}", &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "rate limit") && strstr(reason, "403"));
    CHECK(evaluate("1.0.0", 429, "", &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "429"));
    CHECK(evaluate("1.0.0", 404, "{\"message\":\"Not Found\"}", &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "no release"));
    CHECK(evaluate("1.0.0", 500, release, &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "HTTP 500"));
    CHECK(evaluate("1.0.0", 301, release, &s, reason) == ORACLES_UPDATE_FAILED);
    /* A malformed answer, or none, with a 200. */
    CHECK(evaluate("1.0.0", 200, "", &s, reason) == ORACLES_UPDATE_FAILED && strstr(reason, "malformed"));
    CHECK(evaluate("1.0.0", 200, NULL, &s, reason) == ORACLES_UPDATE_FAILED);
    CHECK(evaluate("1.0.0", 200, "<html>Service unavailable</html>", &s, reason) == ORACLES_UPDATE_FAILED);
    CHECK(oracles_update_evaluate("1.0.0", 200, "", release, release_length - 10, &s, reason, sizeof reason) == ORACLES_UPDATE_FAILED);
    /* A running version that is not one compares with nothing. */
    CHECK(evaluate("dev", 200, release, &s, reason) == ORACLES_UPDATE_FAILED);
}

/* The check on its thread, from a file as --update-check-url gives it: checking, then the newer release. */
static void checked(const char *path)
{
    OraclesUpdateStatus s;
    CHECK(strncmp(oracles_update_default_url(), "https://api.github.com/repos/", 29) == 0);
    CHECK(strstr(oracles_update_default_url(), "/releases/latest") != NULL);
    oracles_update_check_status(&s);
    CHECK(s.state == ORACLES_UPDATE_UNKNOWN && oracles_update_check_event() == 0);
    char *text = variant("\"tag_name\":\"v1.1.0\"", "\"tag_name\":\"v99.0.0\"");
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) { fputs(text, f); fclose(f); }
    free(text);
    oracles_update_check_start(path, "1.1.0");
    CHECK(oracles_update_check_event() != 0);
    CHECK(oracles_update_check_wait(10000));
    oracles_update_check_status(&s);
    CHECK(s.state == ORACLES_UPDATE_NEWER && !strcmp(s.version, "99.0.0") && !strcmp(s.url, "https://github.com/thibautbus/top/releases/tag/v1.1.0"));
    /* Where no browser takes the page (a console without one), the screen says its address. */
    char message[256];
    oracles_update_open_failed(s.url, message, sizeof message);
    CHECK(!strcmp(message, "No browser opened the release page: github.com/thibautbus/top/releases/tag/v1.1.0"));
    /* Once a run: a second start changes nothing. */
    oracles_update_check_start("no such file", "1.1.0");
    oracles_update_check_status(&s);
    CHECK(s.state == ORACLES_UPDATE_NEWER);
    oracles_update_check_close();
    remove(path);
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s update_release.json TEMPORARY\n", argv[0]); return 2; }
    release = SDL_LoadFile(argv[1], &release_length);
    if (!release) { fprintf(stderr, "%s: %s\n", argv[1], SDL_GetError()); return 1; }
    versions();
    parsing();
    answers();
    checked(argv[2]);
    SDL_free(release);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("update check: all checks passed\n");
    return 0;
}
