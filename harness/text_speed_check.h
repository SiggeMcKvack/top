/* The instant text in the harness: what the text box did, read in the game's state at each frame's end (WRAM bank 7), and
 * what the guest's transaction did.  A line is followed from the frame drawLineOfText composes it (the text's state 2, 4
 * or $a at the frame's end) to the frame its last character shows (w7NextTextColumnToDisplay at the line's end), a page
 * from the box's opening, the A that turned the last page or \stop's that cleared the box, to the frame the arrow waits
 * (state 5, $f for the text's last page, or \stop's wait), in the game's frames (wFrameCounter's, which a frame the game
 * lags through leaves as it was).  Only the lines and the pages the instant text ran through whole are measured,
 * \slow's (w7TextSlowdownTimer running) apart. */
#ifndef ORACLES_HARNESS_TEXT_SPEED_CHECK_H
#define ORACLES_HARNESS_TEXT_SPEED_CHECK_H

#include "guest.h"

#include <stdio.h>

typedef struct OraclesTextSpeedCheck {
    uint8_t counter;   /* wFrameCounter at the last frame measured */
    int counter_seen;
    int line_open, line_measured, line_slow, line_state;
    unsigned line_frames, line_voices_at;
    int in_text, page_open, page_measured, page_first;
    unsigned page_frames;
    unsigned lines, lines_whole, lines_late, line_frames_max;   /* lines measured; shown whole at the first frame; later than one frame, and one per voice kept */
    unsigned pages, page_frames_max, first_pages, first_page_frames_max;
    unsigned slow_frames;                                       /* frames \slow held the text, A and B unread */
} OraclesTextSpeedCheck;

void oracles_text_speed_check_frame_end(OraclesTextSpeedCheck *check, OraclesGuest *guest);
/* Its figures and the guest's, to `out` as the summary's keys (text_speed.*), or said to the standard error. */
void oracles_text_speed_check_summary(const OraclesTextSpeedCheck *check, OraclesGuest *guest, FILE *out);
void oracles_text_speed_check_report(const OraclesTextSpeedCheck *check, OraclesGuest *guest, FILE *out);

#endif
