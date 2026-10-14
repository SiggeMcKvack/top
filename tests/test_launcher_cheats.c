/* Cheats (ui_page_nav.h, ui_page_layout.h): its layout against the layout
 * reference, tests/launcher_layout_reference.json (layout_reference.h), the
 * test's argument, in its frames 22h (No damage on, highlighted) at
 * 1920x1080, 22i (Rupees never run out highlighted) in the 4:3 layout at
 * 1440x1080, and 22j (at 640x480 on Android with a controller); a text is
 * compared by its left, its width, its line box and its baseline, an element
 * by its box.  The prototype draws a fourth row, Faster walking, which the
 * launcher drops until the cheat comes: the three rows keep their places, and
 * what lies under them (16:9's panel's bottom, 4:3's explanation and note)
 * sits one row higher, by the reference's own pitch from Rupees never run out
 * to Faster walking (a row's height and the gap).  22j highlights Faster
 * walking, which the launcher has not: the page is compared on Rupees never
 * run out, the explanation by its left, top and baseline (its width is Faster
 * walking's).  And the page's keys and pointer, from the home screen and from
 * the pause. */
#include "layout_reference.h"
#include "ui_page_layout.h"
#include "ui_page_nav.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); layout_fail(); } } while (0)

static const char *const ids[ORACLES_CHEATS_ROWS] = { "damage", "ammo", "rupees" };

/* The page of Ages, played on 21 Sep 2026, opened from its menu, No damage on, the highlight on `row`. */
static void page(OraclesHomeNav *nav, unsigned row)
{
    oracles_home_init(nav);
    OraclesHomeGame *g = &nav->games[ORACLES_HOME_GAME_AGES];
    g->usable = 1;
    g->rom = ORACLES_ROM_ORIGINAL;
    snprintf(g->last_session, sizeof g->last_session, "21 Sep 2026");
    g->cheats = 1u;
    nav->focus = 5;
    CHECK(oracles_home_act(nav, ORACLES_HOME_OK) == ORACLES_HOME_STAY && nav->screen == ORACLES_SCREEN_CHEATS && nav->row == 0);
    nav->row = row;
}

/* The reference's dropped row, Faster walking's: its distance from the row above, the height of the page's rows and
 * their gap. */
static float dropped_row(const char *frame, const char *row_probe)
{
    char name[32];
    snprintf(name, sizeof name, "walk%s", row_probe);
    const float walk = layout_reference(frame, name, "y");
    snprintf(name, sizeof name, "rupees%s", row_probe);
    return walk - layout_reference(frame, name, "y");
}

/* `line` as the reference places it, `dy` lower. */
static OraclesUiLine lowered(const OraclesUiLine *line, float dy)
{
    OraclesUiLine l = *line;
    l.y += dy;
    l.baseline += dy;
    return l;
}

/* Each row's label, Off and On, the probes both layouts have. */
static void rows(const char *frame, const OraclesUiCheatsLayout *l)
{
    for (int r = 0; r < ORACLES_CHEATS_ROWS; r++) {
        char name[32];
        snprintf(name, sizeof name, "%s label", ids[r]); layout_line(frame, name, &l->labels[r]);
        snprintf(name, sizeof name, "%s off", ids[r]); layout_box(frame, name, &l->choices[r][0].box);
        snprintf(name, sizeof name, "%s on", ids[r]); layout_box(frame, name, &l->choices[r][1].box);
        snprintf(name, sizeof name, "%s off text", ids[r]); layout_line(frame, name, &l->choices[r][0].name);
        snprintf(name, sizeof name, "%s on text", ids[r]); layout_line(frame, name, &l->choices[r][1].name);
    }
}

static void frame_22h(void)
{
    OraclesHomeNav nav;
    page(&nav, 0);
    OraclesUiCheatsLayout l;
    oracles_ui_layout_cheats(ORACLES_UI_LAYOUT_16_9, &nav, &l);
    layout_line("22h", "section", &l.head.section);
    layout_line("22h", "over", &l.head.over);
    layout_line("22h", "title", &l.head.title.lines[0]);
    /* The note on the save under the title, broken in the column's width. */
    layout_line("22h", "note", &l.note.lines[0]);
    layout_near("22h", "note", "lines", (float)l.note.count);
    layout_near("22h", "note", "width", l.note.w);
    layout_near("22h", "note box", "y", l.note.lines[0].y);
    layout_near("22h", "note box", "h", l.note.h);
    /* The panel ends a row higher than the reference's, the launcher's rows as far apart as the reference's. */
    const float dropped = dropped_row("22h", " row");
    CHECK(fabsf(l.rows[2].y - l.rows[1].y - dropped) <= LAYOUT_TOLERANCE);
    OraclesUiBox panel = l.panel;
    panel.h += dropped;
    layout_box("22h", "panel", &panel);
    rows("22h", &l);
    for (int r = 0; r < ORACLES_CHEATS_ROWS; r++) {
        char name[32];
        snprintf(name, sizeof name, "%s row", ids[r]); layout_box("22h", name, &l.rows[r]);
        snprintf(name, sizeof name, "%s", ids[r]);
        layout_near("22h", name, "y", l.rows[r].y);
        layout_near("22h", name, "h", l.rows[r].h);
        if (!oracles_cheats_row_live(&nav, (unsigned)r)) continue;
        /* The explanation with its note, beside the choices, on as many lines as the browser breaks it into. */
        snprintf(name, sizeof name, "%s explanation", ids[r]);
        layout_line("22h", name, &l.explanations[r].lines[0]);
        layout_near("22h", name, "lines", (float)l.explanations[r].count);
        layout_near("22h", name, "width", l.explanations[r].w);
    }
    CHECK(l.help.h == 0.0f && l.help_note.h == 0.0f);
}

/* 4:3: one column of rows, the highlighted row's explanation and its note under them, a row higher than the
 * reference's; the head and the note on one line each; no panel, the rows and the help the mockup's panel.  `help` is
 * the reference's explanation, NULL when it is the dropped row's. */
static void frame_4_3(const char *frame, unsigned row, const char *help)
{
    OraclesHomeNav nav;
    page(&nav, row);
    nav.layout = ORACLES_UI_LAYOUT_4_3;
    OraclesUiCheatsLayout l;
    oracles_ui_layout_cheats(ORACLES_UI_LAYOUT_4_3, &nav, &l);
    layout_line(frame, "section", &l.head.section);
    layout_line(frame, "title", &l.head.title.lines[0]);
    CHECK(l.head.over.h == 0.0f && !strcmp(l.head.title.text[0], "Oracle of Ages"));
    layout_line(frame, "note", &l.note.lines[0]);
    layout_near(frame, "head", "y", l.head.section.y);
    layout_near(frame, "head", "h", l.note.lines[0].y + l.note.lines[0].h - l.head.section.y);
    const float dropped = dropped_row(frame, "");
    CHECK(fabsf(l.rows[2].y - l.rows[1].y - dropped) <= LAYOUT_TOLERANCE);
    CHECK(l.panel.w == 0.0f);
    layout_near(frame, "panel", "y", l.rows[0].y);
    layout_near(frame, "panel", "h", l.help_note.y + l.help_note.h + dropped - l.rows[0].y);
    rows(frame, &l);
    for (int r = 0; r < ORACLES_CHEATS_ROWS; r++) {
        char name[32];
        snprintf(name, sizeof name, "%s", ids[r]); layout_box(frame, name, &l.rows[r]);
    }
    const OraclesUiLine help_line = lowered(&l.help, dropped), note_line = lowered(&l.help_note, dropped);
    layout_near(frame, "help", "y", help_line.y);
    layout_near(frame, "help", "h", help_line.h);
    if (help) {
        layout_line(frame, "help text", &help_line);
        CHECK(!strcmp(oracles_cheats_texts[row], help));
    } else {
        layout_near(frame, "help text", "x", help_line.x);
        layout_near(frame, "help text", "top", help_line.y);
        layout_near(frame, "help text", "h", help_line.h);
        layout_near(frame, "help text", "baseline", help_line.baseline);
    }
    layout_near(frame, "help note", "y", note_line.y);
    layout_near(frame, "help note", "h", note_line.h);
    if (help && row == 2) layout_line(frame, "help note text", &note_line);
}

/* 22j: the help bar with a controller, the page's four hints. */
static void hints_22j(void)
{
    OraclesHomeNav nav;
    page(&nav, 2);
    nav.android = 1;
    oracles_home_input(&nav, ORACLES_INPUT_GAMEPAD);
    OraclesHomeHint hints[ORACLES_HOME_MAX_HINTS];
    OraclesUiHintLayout h[ORACLES_HOME_MAX_HINTS];
    const unsigned count = oracles_home_hints(&nav, hints);
    CHECK(count == 4 && !strcmp(hints[0].key, "D-pad \xe2\x86\x91\xe2\x86\x93") && !strcmp(hints[3].label, "Back"));
    if (count != 4) return;
    oracles_ui_layout_hints(ORACLES_UI_LAYOUT_4_3, hints, count, h);
    for (unsigned i = 0; i < count; i++) {
        char name[32];
        snprintf(name, sizeof name, "hint %u", i); layout_box("22j", name, &h[i].box);
        snprintf(name, sizeof name, "hint %u key", i); layout_box("22j", name, &h[i].key_box);
        snprintf(name, sizeof name, "hint %u key text", i); layout_line("22j", name, &h[i].key);
        snprintf(name, sizeof name, "hint %u label", i); layout_line("22j", name, &h[i].label);
    }
}

/* The keys and the pointer: up and down wrap over the three rows; left, right and OK turn a cheat off and on, each
 * change a command to store the settings; Back returns to Cheats in the menu, or in the pause from a game. */
static void keys(void)
{
    OraclesHomeNav nav;
    page(&nav, 0);
    CHECK(oracles_cheats_on(&nav, 0) && !oracles_cheats_on(&nav, 1));
    CHECK(oracles_home_act(&nav, ORACLES_HOME_UP) == ORACLES_HOME_STAY && nav.row == 2);
    CHECK(oracles_home_act(&nav, ORACLES_HOME_DOWN) == ORACLES_HOME_STAY && nav.row == 0);
    CHECK(oracles_home_act(&nav, ORACLES_HOME_RIGHT) == ORACLES_HOME_STORE && !oracles_cheats_on(&nav, 0) && nav.games[0].cheats == 0);
    CHECK(oracles_home_act(&nav, ORACLES_HOME_LEFT) == ORACLES_HOME_STORE && oracles_cheats_on(&nav, 0));
    oracles_home_act(&nav, ORACLES_HOME_DOWN);
    oracles_home_act(&nav, ORACLES_HOME_DOWN);
    CHECK(oracles_home_act(&nav, ORACLES_HOME_OK) == ORACLES_HOME_STORE && nav.games[0].cheats == 5u);
    /* Down on Rupees never run out wraps to No damage: no row after it. */
    CHECK(oracles_home_act(&nav, ORACLES_HOME_DOWN) == ORACLES_HOME_STAY && nav.row == 0);
    CHECK(!oracles_cheats_row_live(&nav, ORACLES_CHEATS_ROWS) && !oracles_cheats_on(&nav, ORACLES_CHEATS_ROWS));
    /* A click on a row highlights it; on Off or On, sets it; the same choice again changes nothing. */
    CHECK(oracles_cheats_click(&nav, 1, -1) == ORACLES_HOME_STAY && nav.row == 1);
    CHECK(oracles_cheats_click(&nav, 1, 1) == ORACLES_HOME_STORE && nav.games[0].cheats == 7u);
    CHECK(oracles_cheats_click(&nav, 1, 1) == ORACLES_HOME_STAY && oracles_cheats_click(&nav, 0, 0) == ORACLES_HOME_STORE && nav.games[0].cheats == 6u);
    CHECK(oracles_cheats_click(&nav, ORACLES_CHEATS_ROWS, 1) == ORACLES_HOME_STAY && nav.games[0].cheats == 6u);
    /* Seasons' are its own. */
    CHECK(nav.games[ORACLES_HOME_GAME_SEASONS].cheats == 0);
    CHECK(oracles_home_act(&nav, ORACLES_HOME_BACK) == ORACLES_HOME_STAY && nav.screen == ORACLES_SCREEN_HOME && oracles_home_focus(&nav) == 5);
    oracles_home_select(&nav, ORACLES_HOME_SEASONS);
    nav.focus = 5;
    CHECK(oracles_home_act(&nav, ORACLES_HOME_OK) == ORACLES_HOME_STAY && nav.screen == ORACLES_SCREEN_CHEATS && !oracles_cheats_on(&nav, 1));
    CHECK(oracles_home_act(&nav, ORACLES_HOME_OK) == ORACLES_HOME_STORE && nav.games[ORACLES_HOME_GAME_SEASONS].cheats == 1u && nav.games[0].cheats == 6u);
    /* The host writes the cheats of the games the page changed, those alone. */
    CHECK(nav.cheats_changed == (1u << ORACLES_HOME_GAME_AGES | 1u << ORACLES_HOME_GAME_SEASONS));
    /* The page's hints: move, change, select, back. */
    OraclesHomeHint hints[ORACLES_HOME_MAX_HINTS];
    CHECK(oracles_home_hints(&nav, hints) == 4 && !strcmp(hints[1].label, "Change") && hints[3].back);

    /* From the pause, on Cheats after Display; the change is the same command, which the pause applies at once. */
    oracles_home_init(&nav);
    oracles_home_pause(&nav, ORACLES_HOME_GAME_SEASONS, ORACLES_PROFILE_FAITHFUL, 0);
    nav.focus = 5;
    CHECK(oracles_home_act(&nav, ORACLES_HOME_OK) == ORACLES_HOME_STAY && nav.screen == ORACLES_SCREEN_CHEATS && nav.row == 0);
    CHECK(oracles_home_act(&nav, ORACLES_HOME_RIGHT) == ORACLES_HOME_STORE && nav.games[ORACLES_HOME_GAME_SEASONS].cheats == 1u);
    CHECK(oracles_home_act(&nav, ORACLES_HOME_BACK) == ORACLES_HOME_STAY && nav.screen == ORACLES_SCREEN_PAUSE && oracles_home_focus(&nav) == 5);
    CHECK(oracles_home_act(&nav, ORACLES_HOME_BACK) == ORACLES_HOME_RESUME);

    /* --cheats for the run: the page shows them, every row dimmed and inert, nothing to store. */
    page(&nav, 1);
    nav.cheats_fixed = 1;
    CHECK(oracles_cheats_on(&nav, 0) && !oracles_cheats_row_live(&nav, 0) && !oracles_cheats_row_live(&nav, 2));
    CHECK(oracles_home_act(&nav, ORACLES_HOME_OK) == ORACLES_HOME_STAY && oracles_cheats_click(&nav, 0, 0) == ORACLES_HOME_STAY);
    CHECK(nav.games[0].cheats == 1u && nav.cheats_changed == 0);
}

/* A ROM whose profile has no cheats, an unrecognised one: the menu has no Cheats, nor the pause of a game whose session
 * says its profile has none; their other entries as before. */
static void refused(void)
{
    OraclesHomeNav nav;
    page(&nav, 0);
    oracles_home_act(&nav, ORACLES_HOME_BACK);
    nav.games[ORACLES_HOME_GAME_AGES].rom = ORACLES_ROM_UNRECOGNISED;
    OraclesHomeItem items[ORACLES_HOME_MAX_ITEMS];
    CHECK(oracles_home_items(&nav, items) == 6 && items[4].id == ORACLES_HOME_ITEM_MODS && items[5].id == ORACLES_HOME_ITEM_EXIT);
    oracles_home_init(&nav);
    oracles_home_pause(&nav, ORACLES_HOME_GAME_AGES, ORACLES_PROFILE_FAITHFUL, 0);
    CHECK(oracles_home_items(&nav, items) == 7 && !items[5].disabled);
    nav.cheats_refused = 1;
    nav.focus = 5;
    CHECK(oracles_home_items(&nav, items) == 6 && items[5].id == ORACLES_HOME_ITEM_QUIT_GAME);
    CHECK(oracles_home_act(&nav, ORACLES_HOME_OK) == ORACLES_HOME_QUIT_GAME);
}

int main(int argc, char **argv)
{
    if (argc < 2 || !layout_reference_load(argv[1])) { fprintf(stderr, "usage: oracles-test-launcher-cheats launcher_layout_reference.json\n"); return 2; }
    if (!oracles_ui_fonts_load()) { fprintf(stderr, "FAIL the embedded fonts do not load\n"); return 1; }
    frame_22h();
    frame_4_3("22i", 2, "Buying costs nothing.");
    frame_4_3("22j", 2, NULL);
    hints_22j();
    keys();
    refused();
    if (layout_failures()) { fprintf(stderr, "%d failure(s)\n", layout_failures()); return 1; }
    printf("launcher cheats: the page matches the layout reference within %.1f px, and its keys and pointer do what they say\n", (double)LAYOUT_TOLERANCE);
    return 0;
}
