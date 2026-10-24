/* Display › Text, without SDL: the page Display's Text link opens in place of Display's own rows, as Advanced does, with
 * a preview of the text box beside them (16:9, under the title) or above them (4:3).
 *
 * Three rows: Text, Classic (the game's fixed-width letters) or Compact (each letter its own width), settings.txt's
 * text=; Text box, Standard (the game's) or Wide (the port's, 192 pixels of text), text_box=; Text size, Normal, Large
 * (about half as big again, in whole screen pixels) or Very large (twice), text_size=normal|large|xlarge.  They belong
 * to Enhanced: in Faithful the rows are dimmed and inert, and Display's Text link dimmed, opening all the same.  A fan
 * game has its compact text in the game's box: its Text is live, its Text box and Text size dimmed and inert, "Ages and
 * Seasons only for now.".  From the pause, a row acts at once or not at all: Text and Text box, which a game lays its
 * texts out for at its start, are dimmed and inert there, "Change it from the home screen."; Text size applies at once,
 * by the running game's profile.  A row the command line gave for the
 * run (--text, --text-box, --text-size) is dimmed and inert.  Up and down move through the rows and wrap, left and right
 * change the highlighted one, OK takes its next choice; Back returns to Display, on Text.
 *
 * The preview: the band of the view the game will play in (View's size in the screen's shape; Faithful's 160x144), at
 * its proportions, the status bar's band at its top, the box at its bottom, 8 pixels above the band's edge and centred
 * across, at the size it will take there: Text size's choice where the box so enlarged fits the view, else the next size
 * down, as the Enhanced view chooses (view.h: Very large where twice the box fits across the view; Large where the box
 * at k + ceil(k / 2) screen pixels a pixel fits the frame shown at k, text_box_enlarge.h), with a note when the choice
 * falls back.  Its text is the port's own sample, laid out in the box's two lines in the font of the game's original ROM
 * (a fan game's base ROM's), each letter at its cell (Classic) or its own width (Compact); without that ROM its words
 * are bars. */
#ifndef ORACLES_UI_PAGE_TEXT_H
#define ORACLES_UI_PAGE_TEXT_H

#include "ui_page_nav.h"

#include <stddef.h>
#include <stdint.h>

extern const char *const oracles_display_text_choices[2];       /* "Classic", "Compact": settings.txt's text= */
extern const char *const oracles_display_text_box_choices[2];   /* "Standard", "Wide": text_box= standard, wide */
#define ORACLES_DISPLAY_TEXT_SIZES 3
extern const char *const oracles_display_text_size_choices[ORACLES_DISPLAY_TEXT_SIZES];   /* "Normal", "Large", "Very large" */

/* The Text link applies in Enhanced chosen: dimmed in Faithful. */
int oracles_display_text_applies(const OraclesHomeNav *nav);
/* A Text row changes (ORACLES_DISPLAY_LETTERS, _TEXT_BOX, _TEXT_SIZE): in Enhanced (Text size, from the pause, the
 * running game's), an Oracle's page for the box and the size, Text and Text box not from the pause, and none the command
 * line gave. */
int oracles_display_text_row_live(const OraclesHomeNav *nav, unsigned row);
/* Beside the link: "Compact · Wide · Normal". */
void oracles_display_text_summary(const OraclesHomeNav *nav, char *out, size_t capacity);
/* A Text row's explanation, 16:9's beside its choices or 4:3's shorter one in the header; the link's
 * ("Letters, box and size, with a preview."). */
const char *oracles_display_text_explanation(unsigned row, int four_three);
/* A Text row's note, under its explanation (16:9) or on the help's note line (4:3), empty for none: a fan game's reason,
 * "Change it from the home screen." from the pause, Text's savestates ("Savestates taken with Compact need Compact."),
 * Text size's fallback in the view chosen ("Normal in this view: no room for a larger box."). */
const char *oracles_display_text_note(const OraclesHomeNav *nav, unsigned row);

/* The preview, in the game's pixels. */
#define ORACLES_DISPLAY_SAMPLE_W 192u   /* the widest box's text, the wide box's */
#define ORACLES_DISPLAY_SAMPLE_H 32u    /* its two lines of 16 */
typedef struct OraclesDisplayTextPreview {
    int view_w, view_h;          /* the band */
    int box_w, box_h;            /* the box: 144 x 40, the wide one 208 x 40 */
    int text_w;                  /* its text's width: 128, 192 */
    int compact;
    int size;                    /* the size it takes: 0 Normal, 1 Large, 2 Very large */
    unsigned scale_num, scale_den;   /* the box's pixels against the band's: 1, (k + ceil(k / 2)) / k, 2 */
    char label[112];             /* the view, its size and the box's, and how large it shows; in Faithful, the game's box */
    const char *note;            /* that the size chosen falls back here, else empty */
    uint8_t sample[ORACLES_DISPLAY_SAMPLE_H][ORACLES_DISPLAY_SAMPLE_W];   /* the text's ink, 1, its first `text_w` columns */
} OraclesDisplayTextPreview;
void oracles_display_text_preview(const OraclesHomeNav *nav, OraclesDisplayTextPreview *out);
/* The sample laid out in `font` (NULL, or one not read: its words as bars), Classic or `compact`, in two lines of
 * `text_w` pixels. */
void oracles_display_text_sample(const OraclesHomeFont *font, int compact, unsigned text_w,
                                 uint8_t out[ORACLES_DISPLAY_SAMPLE_H][ORACLES_DISPLAY_SAMPLE_W]);

#endif
