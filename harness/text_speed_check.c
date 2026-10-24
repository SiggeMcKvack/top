/* The instant text in the harness (text_speed_check.h). */
#include "text_speed_check.h"

#include <string.h>

#define LINE_COLUMNS 16u
#define STATE_TOP_LINE 2u
#define STATE_BOTTOM_LINE 4u
#define STATE_NEXT_LINE 0x0au
#define STATE_WAIT 5u
#define STATE_FINISHED 0x0fu

static void close_line(OraclesTextSpeedCheck *c, const OraclesGuestTextInstantCounts *counts)
{
    if (c->line_open && c->line_measured && !c->line_slow) {
        const unsigned voices = counts->voices_kept - c->line_voices_at;
        c->lines++;
        if (c->line_frames <= 1u) c->lines_whole++;
        if (c->line_frames > 1u + voices) c->lines_late++;
        if (c->line_frames > c->line_frames_max) c->line_frames_max = c->line_frames;
    }
    c->line_open = 0;
}

static void open_page(OraclesTextSpeedCheck *c, int instant, int first)
{
    c->page_open = 1;
    c->page_measured = instant;
    c->page_first = first;
    c->page_frames = 0;
}

void oracles_text_speed_check_frame_end(OraclesTextSpeedCheck *c, OraclesGuest *guest)
{
    if (!guest) return;
    const OraclesGuestTables *t = oracles_guest_tables(guest);
    OraclesGuestTextInstantCounts counts;
    oracles_guest_text_instant_counts(guest, &counts);
    const int instant = oracles_guest_text_instant(guest);
    const uint8_t active = oracles_guest_read8(guest, t->text_is_active);
    if (!active || active == 0x80u || oracles_guest_read8(guest, t->text_display_mode) != 0u) {
        /* The box closed, or a choice or the inventory's text: what was under way is not measured. */
        c->line_open = c->page_open = c->in_text = 0;
        return;
    }
    /* The game's frames, its main thread's (wFrameCounter): a frame the game lags through runs no text. */
    const uint8_t counter = oracles_guest_read8(guest, t->frame_counter);
    if (c->counter_seen && counter == c->counter) return;
    c->counter = counter;
    c->counter_seen = 1;
    const unsigned state = oracles_guest_read8(guest, t->text_display_state);
    const int slow = oracles_guest_read8(guest, t->text_slowdown_timer) != 0u;
    if (slow) c->slow_frames++;
    /* The line being revealed, complete once its next column is past its end. */
    const int reveal = state == STATE_TOP_LINE || state == STATE_BOTTOM_LINE || state == STATE_NEXT_LINE;
    const unsigned column = oracles_guest_read8(guest, t->next_text_column);
    OraclesGuestSym at = t->line_text_buffer;
    at.addr = (uint16_t)(at.addr + (column & (LINE_COLUMNS - 1u)));
    const int complete = column >= LINE_COLUMNS || oracles_guest_read8(guest, at) == 0u;
    if (c->line_open && (!reveal || (int)state != c->line_state || complete)) close_line(c, &counts);
    if (reveal && !complete) {
        if (!c->line_open) {
            c->line_open = 1;
            c->line_measured = instant;
            c->line_slow = 0;
            c->line_state = (int)state;
            c->line_frames = 0;
            c->line_voices_at = counts.voices_kept;
        }
        c->line_frames++;
        if (slow) c->line_slow = 1;
        if (!instant) c->line_measured = 0;
    }
    /* A page waits for A at its arrow (state 5), the text's last at its end (state $f), or a line complete still in its
     * state (\stop's wait, a heart piece's). */
    const int waiting = state == STATE_WAIT || state == STATE_FINISHED || (reveal && complete);
    if (!c->in_text) {
        c->in_text = 1;
        open_page(c, instant, 1);
    } else if (!c->page_open && !waiting) {
        open_page(c, instant, state < STATE_TOP_LINE);   /* the A that turned the page, or \stop's that cleared the box */
    }
    if (!c->page_open) return;
    if (waiting) {
        if (c->page_measured) {
            unsigned *count = c->page_first ? &c->first_pages : &c->pages, *max = c->page_first ? &c->first_page_frames_max : &c->page_frames_max;
            (*count)++;
            if (c->page_frames > *max) *max = c->page_frames;
        }
        c->page_open = 0;
        return;
    }
    c->page_frames++;
    if (slow || !instant) c->page_measured = 0;
}

void oracles_text_speed_check_summary(const OraclesTextSpeedCheck *c, OraclesGuest *guest, FILE *out)
{
    OraclesGuestTextInstantCounts counts;
    oracles_guest_text_instant_counts(guest, &counts);
    fprintf(out, "text_speed.presses=%u\ntext_speed.voices_kept=%u\ntext_speed.voices_lost=%u\ntext_speed.slow_frames=%u\n"
                 "text_speed.lines=%u\ntext_speed.lines_whole=%u\ntext_speed.lines_late=%u\ntext_speed.line_frames_max=%u\n"
                 "text_speed.first_pages=%u\ntext_speed.first_page_frames_max=%u\ntext_speed.pages=%u\ntext_speed.page_frames_max=%u\n",
            counts.presses, counts.voices_kept, counts.voices_lost, c->slow_frames, c->lines, c->lines_whole, c->lines_late, c->line_frames_max,
            c->first_pages, c->first_page_frames_max, c->pages, c->page_frames_max);
}

void oracles_text_speed_check_report(const OraclesTextSpeedCheck *c, OraclesGuest *guest, FILE *out)
{
    OraclesGuestTextInstantCounts counts;
    oracles_guest_text_instant_counts(guest, &counts);
    fprintf(out, "instant text: %u presses of A added, %u voices kept, %u lost, %u frames held by \\slow; %u lines measured, %u whole at once, %u late, "
                 "%u frames at most; %u first pages in %u frames at most, %u next pages in %u at most\n",
            counts.presses, counts.voices_kept, counts.voices_lost, c->slow_frames, c->lines, c->lines_whole, c->lines_late, c->line_frames_max,
            c->first_pages, c->first_page_frames_max, c->pages, c->page_frames_max);
}
