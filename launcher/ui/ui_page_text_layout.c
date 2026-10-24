/* Display's Text link and the Display › Text page (ui_page_layout.h), without SDL: the link's summary and arrow at the
 * right of its row; the page's rows as Advanced lays its own out, and its preview, the band at its proportions in a frame
 * of at most 520x293 in 16:9's left column, under the title, its label and note under it, or of at most 400x300 at the
 * top of 4:3's rows, its label and note at its right, at its bottom. */
#include "ui_page_layout.h"

#include <math.h>
#include <string.h>

/* As ui_page_layout_internal.h's, in scene pixels. */
#define HEAD_X 96.0f
#define HEAD_W 520.0f
#define LABEL_PAD_TOP 18.0f
#define DISPLAY_LABEL_W 200.0f
#define ROW_PAD_X 20.0f
#define DISPLAY_ROW_PAD_Y 14.0f
#define DISPLAY_ROW_GAP 14.0f
#define ROW_GAP 24.0f
#define EXPLANATION_H 58.0f          /* two lines of 29 kept for each row's explanation, as Advanced's */
#define ROWS_X_4_3 64.0f
#define ROWS_Y_4_3 208.0f
#define ROWS_W_4_3 1312.0f
#define ROW_PAD_X_4_3 20.0f
#define DISPLAY_VALUE_X_4_3 408.0f
#define DISPLAY_ROW_GAP_4_3 6.0f
/* The link: its summary, then its arrow; 58 tall (16:9), 90 (4:3), the arrow centred on it. */
#define LINK_H 58.0f
#define LINK_GAP 14.0f
#define LINK_ARROW_W 9.0f
#define LINK_ARROW_H 12.0f
#define LINK_H_4_3 90.0f
#define LINK_GAP_4_3 20.0f
#define LINK_ARROW_W_4_3 13.0f
#define LINK_ARROW_H_4_3 18.0f
/* The preview: its frame's room, its border, the gaps to its label and note (16:9: under it; 4:3: beside it, their
 * column's gap and padding under it), the note's line kept, and the rows' margin under it (4:3). */
#define PREVIEW_W 520.0f
#define PREVIEW_H 293.0f
#define PREVIEW_W_4_3 400.0f
#define PREVIEW_H_4_3 300.0f
#define PREVIEW_BORDER 1.0f
#define PREVIEW_GAP 12.0f
#define PREVIEW_NOTE_H 29.0f
#define PREVIEW_GAP_4_3 28.0f
#define PREVIEW_LINE_GAP_4_3 4.0f
#define PREVIEW_NOTE_H_4_3 34.0f
#define PREVIEW_MARGIN_4_3 10.0f
/* In the game's pixels: the status bar's band, the box's height, its distance from the band's bottom edge, its text's
 * margins, the arrow's size and its distances from the box's corner. */
#define HUD_H 16.0f
#define BOX_H 40.0f
#define BOX_BOTTOM 8.0f
#define TEXT_LEFT 8.0f
#define TEXT_TOP 4.0f
#define ARROW 3.0f
#define ARROW_RIGHT 3.0f
#define ARROW_BOTTOM 2.0f

static OraclesUiLine line_at(const OraclesUiTextStyle *style, const char *text, float x, float y, float line_height)
{
    OraclesUiLine line;
    float baseline;
    oracles_ui_line_box(style, line_height, &line.h, &baseline);
    line.w = text && text[0] ? oracles_ui_text_width(style, text) : 0.0f;
    line.x = x;
    line.y = y;
    line.baseline = y + baseline;
    return line;
}

static void move_line(OraclesUiLine *line, float x, float y)
{
    line->baseline += y - line->y;
    line->x = x;
    line->y = y;
}

static OraclesUiBox box(float x, float y, float w, float h)
{
    const OraclesUiBox b = { x, y, w, h };
    return b;
}

static void move_wrapped(OraclesUiWrapped *w, float y)
{
    const float dy = y - (w->count ? w->lines[0].y : y);
    for (unsigned i = 0; i < w->count; i++) move_line(&w->lines[i], w->lines[i].x, w->lines[i].y + dy);
}

/* JavaScript's Math.round, as the mockup rounds the preview. */
static float round_half_up(float v) { return floorf(v + 0.5f); }

/* The link's summary and arrow from (x, y) in a box `h` tall, the arrow `arrow_w` x `arrow_h` `gap` after the summary,
 * both centred on it: the box. */
static OraclesUiBox link_at(const OraclesUiTextStyle *style, const char *summary, float x, float y, float h, float gap, float arrow_w, float arrow_h,
                            OraclesUiDisplayLayout *out)
{
    out->text_summary = line_at(style, summary, x, 0.0f, 0.0f);
    move_line(&out->text_summary, x, y + (h - out->text_summary.h) * 0.5f);
    out->text_arrow = box(x + out->text_summary.w + gap, y + (h - arrow_h) * 0.5f, arrow_w, arrow_h);
    out->text_link = box(x, y, out->text_summary.w + gap + arrow_w, h);
    return out->text_link;
}

float oracles_ui_text_link_16_9(const OraclesUiDisplayTexts *t, float x, float y, float w, float label_x, OraclesUiDisplayLayout *out)
{
    OraclesUiWrapped *label = &out->labels[ORACLES_DISPLAY_TEXT], *explanation = &out->explanations[8];
    oracles_ui_wrap(&oracles_ui_row_label, oracles_display_labels[ORACLES_DISPLAY_TEXT], DISPLAY_LABEL_W, label_x, y + LABEL_PAD_TOP, label);
    const float inner_x = x + ROW_PAD_X, inner_y = y + DISPLAY_ROW_PAD_Y, right = x + w - ROW_PAD_X;
    const float link_w = oracles_ui_text_width(&oracles_ui_choice, t->text_summary) + LINK_GAP + LINK_ARROW_W;
    oracles_ui_wrap(&oracles_ui_row_text, t->explanations[8], right - inner_x - ROW_GAP - link_w, inner_x, inner_y, explanation);
    const float inner_h = fmaxf(explanation->h, LINK_H);
    move_wrapped(explanation, inner_y + (inner_h - explanation->h) * 0.5f);
    link_at(&oracles_ui_choice, t->text_summary, right - link_w, inner_y + (inner_h - LINK_H) * 0.5f, LINK_H, LINK_GAP, LINK_ARROW_W, LINK_ARROW_H, out);
    out->rows[ORACLES_DISPLAY_TEXT] = box(x, y, w, inner_h + 2.0f * DISPLAY_ROW_PAD_Y);
    return fmaxf(LABEL_PAD_TOP + label->h, out->rows[ORACLES_DISPLAY_TEXT].h);
}

float oracles_ui_text_link_4_3(const OraclesUiDisplayTexts *t, float y, OraclesUiDisplayLayout *out)
{
    const float top = oracles_ui_display_row_4_3(y, oracles_display_labels[ORACLES_DISPLAY_TEXT], LINK_H_4_3, &out->labels[ORACLES_DISPLAY_TEXT],
                                                 &out->rows[ORACLES_DISPLAY_TEXT]);
    link_at(&oracles_ui_row_button_4_3, t->text_summary, DISPLAY_VALUE_X_4_3, top, LINK_H_4_3, LINK_GAP_4_3, LINK_ARROW_W_4_3, LINK_ARROW_H_4_3, out);
    return out->rows[ORACLES_DISPLAY_TEXT].y + out->rows[ORACLES_DISPLAY_TEXT].h;
}

/* The band's frame from (x, y), at most `room_w` x `room_h`, and what it holds: the status bar's band, the box at the
 * size it takes, its text's place and its arrow. */
static void preview_frame(const OraclesDisplayTextPreview *p, float x, float y, float room_w, float room_h, OraclesUiDisplayLayout *out)
{
    const float vw = (float)p->view_w, vh = (float)p->view_h;
    const float scale = vw > 0.0f && vh > 0.0f ? fminf(room_w / vw, room_h / vh) : 1.0f;
    const float unit = scale * (float)p->scale_num / (float)(p->scale_den ? p->scale_den : 1u);
    out->preview_frame = box(x, y, round_half_up(vw * scale), round_half_up(vh * scale));
    const float in_x = x + PREVIEW_BORDER, in_y = y + PREVIEW_BORDER;
    out->preview_hud = box(in_x, in_y, out->preview_frame.w - 2.0f * PREVIEW_BORDER, round_half_up(HUD_H * scale));
    const float bw = (float)p->box_w * unit;
    out->preview_box = box(in_x + round_half_up((vw * scale - bw) * 0.5f), in_y + round_half_up(vh * scale - BOX_BOTTOM * scale - BOX_H * unit),
                           round_half_up(bw), round_half_up(BOX_H * unit));
    out->pixel = unit;
    out->text_x = out->preview_box.x + round_half_up(TEXT_LEFT * unit);
    out->text_y = out->preview_box.y + round_half_up(TEXT_TOP * unit);
    const float arrow = fmaxf(ARROW, round_half_up(ARROW * unit));
    out->preview_arrow = box(out->preview_box.x + out->preview_box.w - round_half_up(ARROW_RIGHT * unit) - 2.0f * arrow,
                             out->preview_box.y + out->preview_box.h - round_half_up(ARROW_BOTTOM * unit) - arrow, 2.0f * arrow, arrow);
}

/* The rows Text, Text box and Text size. */
static const char *const *row_choices(unsigned i, unsigned *count)
{
    *count = i == 2u ? ORACLES_DISPLAY_TEXT_SIZES : 2u;
    return i == 0u ? oracles_display_text_choices : i == 1u ? oracles_display_text_box_choices : oracles_display_text_size_choices;
}

static OraclesUiOptionLayout *row_options(OraclesUiDisplayLayout *out, unsigned i)
{
    return i == 0u ? out->text : i == 1u ? out->text_box : out->text_size;
}

float oracles_ui_text_page_16_9(const OraclesUiDisplayTexts *t, float preview_y, float x, float y, float w, float label_x, OraclesUiDisplayLayout *out)
{
    preview_frame(&t->preview, HEAD_X, preview_y, PREVIEW_W, PREVIEW_H, out);
    const float label_y = out->preview_frame.y + out->preview_frame.h + PREVIEW_GAP;
    out->preview_label = line_at(&oracles_ui_diagram_label, t->preview.label, HEAD_X, label_y, 0.0f);
    out->preview_note = line_at(&oracles_ui_row_note, t->preview.note, HEAD_X, label_y + out->preview_label.h + PREVIEW_GAP, 0.0f);
    out->preview = box(HEAD_X, preview_y, HEAD_W, out->preview_note.y + PREVIEW_NOTE_H - preview_y);
    for (unsigned i = 0; i < 3u; i++) {
        const unsigned row = ORACLES_DISPLAY_LETTERS + i;
        unsigned count;
        const char *const *names = row_choices(i, &count);
        const char *const notes[1] = { t->text_notes[i] };
        OraclesUiLine *const notes_out[1] = { &out->text_notes[i] };
        if (i) y += DISPLAY_ROW_GAP;
        y += oracles_ui_display_row(x, y, w, oracles_display_labels[row], label_x, t->text_explanations[i], EXPLANATION_H, notes,
                                    t->text_notes[i] && t->text_notes[i][0] ? 1 : 0, names, NULL, count, &out->labels[row],
                                    &out->text_explanations[i], notes_out, row_options(out, i), &out->rows[row]);
    }
    return y;
}

void oracles_ui_text_page_4_3(const OraclesUiDisplayTexts *t, OraclesUiDisplayLayout *out)
{
    const float x = ROWS_X_4_3 + ROW_PAD_X_4_3;
    preview_frame(&t->preview, x, ROWS_Y_4_3, PREVIEW_W_4_3, PREVIEW_H_4_3, out);
    const float text_x = x + out->preview_frame.w + PREVIEW_GAP_4_3;
    out->preview_label = line_at(&oracles_ui_diagram_label_4_3, t->preview.label, text_x, 0.0f, 0.0f);
    out->preview_note = line_at(&oracles_ui_page_note_4_3, t->preview.note, text_x, 0.0f, 0.0f);
    const float column_h = out->preview_label.h + PREVIEW_LINE_GAP_4_3 + PREVIEW_NOTE_H_4_3 + PREVIEW_LINE_GAP_4_3;
    const float h = fmaxf(out->preview_frame.h, column_h), bottom = ROWS_Y_4_3 + h;
    /* The frame and the label's column stand on the preview's bottom. */
    out->preview_frame.y = bottom - out->preview_frame.h;
    preview_frame(&t->preview, x, out->preview_frame.y, PREVIEW_W_4_3, PREVIEW_H_4_3, out);
    move_line(&out->preview_note, text_x, bottom - PREVIEW_LINE_GAP_4_3 - PREVIEW_NOTE_H_4_3);
    move_line(&out->preview_label, text_x, out->preview_note.y - PREVIEW_LINE_GAP_4_3 - out->preview_label.h);
    out->preview = box(ROWS_X_4_3, ROWS_Y_4_3, ROWS_W_4_3, h);
    float y = bottom + PREVIEW_MARGIN_4_3 + DISPLAY_ROW_GAP_4_3;
    for (unsigned i = 0; i < 3u; i++) {
        unsigned count;
        const char *const *names = row_choices(i, &count);
        y = oracles_ui_display_options_row_4_3(y, ORACLES_DISPLAY_LETTERS + i, names, NULL, NULL, count, row_options(out, i), out) + DISPLAY_ROW_GAP_4_3;
    }
}
