/* Text size Large and Very large in whole screen pixels: where the presenter shows the text box again larger above the
 * frame.  Header alone, for the presenter and the checks, which link no more of the Enhanced view. */
#ifndef ORACLES_ENHANCED_TEXT_BOX_ENLARGE_H
#define ORACLES_ENHANCED_TEXT_BOX_ENLARGE_H

#include <stdint.h>

/* The sizes the presenter shows a box at: Large, k + ceil(k / 2) screen pixels a pixel, k the frame's whole scale
 * (4: 6, three halves; 3: 5; 2: 3; 1: 2); Very large, 2 k.  A size that does not fit falls to the one below. */
#define ORACLES_TEXT_BOX_ENLARGE_LARGE 1u
#define ORACLES_TEXT_BOX_ENLARGE_XLARGE 2u

static inline unsigned oracles_text_box_enlarge_factor(unsigned size, unsigned k)
{
    return size >= ORACLES_TEXT_BOX_ENLARGE_XLARGE ? 2u * k : k + (k + 1u) / 2u;
}

/* Pure: a frame shown at `scale` screen pixels a frame pixel (a whole number, or a Fill's real one), its top-left at
 * (fx, fy), `fw` x `fh` screen pixels; the box of `bw` x `bh` frame pixels at (bx, by) in it drawn at `size`'s factor of
 * k, the scale's whole part, or the next size down where that does not fit, centred on the box across, against its
 * bottom edge when `bottom`, else its top: its top-left and size in `out` (x, y, w, h), the factor in `factor`.  1 when
 * a size lies within the frame shown, 0 when none does (Normal then). */
static inline int oracles_text_box_enlarge(double fx, double fy, double fw, double fh, double scale, uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh,
                                           unsigned size, int bottom, int32_t out[4], unsigned *factor)
{
    const unsigned k = scale >= 1.0 ? (unsigned)(scale + 1e-6) : 1u;
    for (unsigned s = size > ORACLES_TEXT_BOX_ENLARGE_XLARGE ? ORACLES_TEXT_BOX_ENLARGE_XLARGE : size; ; s--) {
        const unsigned f = oracles_text_box_enlarge_factor(s, k);
        const double left = fx + ((double)bx + (double)bw / 2.0) * scale - (double)(bw * f) / 2.0;
        const double edge = bottom ? fy + (double)(by + bh) * scale - (double)(bh * f) : fy + (double)by * scale;
        out[0] = (int32_t)(left >= 0.0 ? left + 0.5 : left - 0.5);
        out[1] = (int32_t)(edge >= 0.0 ? edge + 0.5 : edge - 0.5);
        out[2] = (int32_t)(bw * f);
        out[3] = (int32_t)(bh * f);
        *factor = f;
        if (bw && bh && (double)out[0] >= fx - 0.5 && (double)(out[0] + out[2]) <= fx + fw + 0.5 && (double)out[1] >= fy - 0.5
            && (double)(out[1] + out[3]) <= fy + fh + 0.5) return 1;
        if (s <= ORACLES_TEXT_BOX_ENLARGE_LARGE) return 0;
    }
}

/* Pure: the scale a part of `w` x `h` frame pixels is shown at in `area_w` x `area_h` screen pixels: the largest whole
 * one, or with Fill the real one; 0 when the area does not hold it once. */
static inline double oracles_text_box_part_scale(uint32_t w, uint32_t h, uint32_t area_w, uint32_t area_h, int fill)
{
    if (!w || !h) return 0.0;
    const double across = (double)area_w / (double)w, down = (double)area_h / (double)h, scale = across < down ? across : down;
    if (fill) return scale;
    return scale >= 1.0 ? (double)(unsigned)(scale + 1e-9) : 0.0;
}

/* Pure: the part shown alone (the large menus), `w` x `h` frame pixels at `x`, widened to take in a box of `reserve_w`
 * frame pixels shown at `size` (or the next size down) at the part's own scale in `area_w` x `area_h`, never lowering
 * that scale: as wide as the box at that size's factor needs, within the frame's `frame_w` and as wide as the area holds
 * at that scale, centred on the part; left as it is when no size fits (the box at the part's scale, Normal), or with no
 * box reserved. */
static inline void oracles_text_box_reserve(uint32_t frame_w, uint32_t area_w, uint32_t area_h, int fill, uint32_t reserve_w, unsigned size, uint32_t *x,
                                            uint32_t *w, uint32_t h)
{
    const double scale = oracles_text_box_part_scale(*w, h, area_w, area_h, fill);
    if (!reserve_w || scale <= 0.0) return;
    const unsigned k = (unsigned)(scale + 1e-6);
    for (unsigned s = size > ORACLES_TEXT_BOX_ENLARGE_XLARGE ? ORACLES_TEXT_BOX_ENLARGE_XLARGE : size; ; s--) {
        const double box = (double)(reserve_w * oracles_text_box_enlarge_factor(s, k)) / scale;
        uint32_t wanted = (uint32_t)(box - 1e-6) + 1u;
        if (wanted < *w) wanted = *w;
        if ((wanted - *w) & 1u) wanted++;   /* the same centre */
        if (wanted <= frame_w && (double)wanted * scale <= (double)area_w + 1e-6) {
            const int64_t left = (int64_t)*x - (int64_t)((wanted - *w) / 2u);
            *x = (uint32_t)(left < 0 ? 0 : left + wanted > frame_w ? frame_w - wanted : left);
            *w = wanted;
            return;
        }
        if (s <= ORACLES_TEXT_BOX_ENLARGE_LARGE) return;
    }
}

#endif
