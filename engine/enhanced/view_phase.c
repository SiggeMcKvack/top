/* Link drawn in phase on a diagonal walk.  Walking diagonally, the game moves
 * Link the same 256ths of a pixel both ways each frame, but his x and his y
 * cross their whole pixels on frames of their own (their fractions differ):
 * his sprite steps across, down, then both, and the camera, which follows his
 * sprite so that he never shakes on the screen, takes the whole band through
 * the same zigzag.  Here his y is taken on the line it would follow crossing
 * its pixels on the frames his x does (an offset to his y of less than half
 * a pixel, constant while he walks at that speed both ways): his sprite is
 * drawn up to a pixel off the game's, and the camera follows it there, so
 * that the band moves along the diagonal.  The offset is taken up on a frame
 * where it moves him by nothing, and given back on such a frame too.
 * Nothing is written into the game. */
#include "view_internal.h"

void oracles_enhanced_view_set_link_phase(OraclesEnhancedView *v, int enabled) { v->link_phase = enabled != 0 && !v->subpixel; v->phase_count = 0; v->phase_latched = 0; v->phase_dy = 0; }
int oracles_enhanced_view_link_phase_dy(const OraclesEnhancedView *v) { return v->phase_dy; }
void oracles_enhanced_view_link_phase_counts(const OraclesEnhancedView *v, unsigned *moved_frames, unsigned *engaged_frames)
{
    if (moved_frames) *moved_frames = v->phase_frames;
    if (engaged_frames) *engaged_frames = v->phase_engaged_frames;
}

static int32_t floor256(int32_t x) { return x >= 0 ? x / 256 : -((-x + 255) / 256); }

/* Link's entries in the OAM on screen, all of them inside the game's window
 * both where the game drew them and `dy` lower: their mask, 0 when they are
 * not known or not wholly inside. */
static uint64_t link_entries(OraclesEnhancedView *v, int dy)
{
    const uint8_t *oam = oracles_guest_oam(v->guest), *io = oracles_guest_io(v->guest);
    if (!v->live_sprites || !oam || !io || !(io[IO_LCDC] & 0x02u)) return 0;
    const int height = (io[IO_LCDC] & 0x04u) ? 16 : 8;
    OraclesSpriteTag tags[ORACLES_SPRITE_TAGS];
    const unsigned count = oracles_sprites_tags_for_oam(v->live_sprites, oam, tags, ORACLES_SPRITE_TAGS);
    uint64_t mask = 0;
    for (unsigned i = 0; i < count; i++) {
        if (tags[i].kind != 0u || tags[i].slot != 0u) continue;
        for (unsigned n = 0; n < tags[i].count && tags[i].first + n < 40u; n++) {
            const unsigned k = tags[i].first + n;
            const uint8_t *e = oam + k * 4u;
            if (e[0] == 0u || e[0] >= 160u || e[1] == 0u || e[1] >= 168u) continue;   /* not drawn */
            const int sx = (int)e[1] - 8, sy = (int)e[0] - 16 - (int)ORACLES_ENHANCED_HUD_HEIGHT;
            const int top = sy + (dy < 0 ? dy : 0), bottom = sy + height + (dy > 0 ? dy : 0);
            if (sx < 0 || sx + 8 > (int)ORACLES_ENHANCED_CORE_WIDTH || top < 0 || bottom > (int)ORACLES_ENHANCED_AREA_HEIGHT) return 0;
            mask |= 1ull << k;
        }
    }
    return mask;
}

/* Given back on a frame where it still moves him: walking on, the camera follows him there (the band takes the
 * pixel); standing, the camera keeps its place and his sprite takes its own (`at_rest`). */
static void release(OraclesEnhancedView *v, int at_rest)
{
    if (at_rest) v->observation.drawn_link_dy_held = v->phase_last_dy;
    v->phase_latched = 0;
}

static int phase_dy(OraclesEnhancedView *v)
{
    const OraclesEnhancedObservation *ob = &v->observation;
    const uint8_t *link = oracles_guest_object(v->guest, 0, 0);
    if (!v->link_phase || !link || !ob->playing || ob->in_transition || ob->cutscene || ob->link_warping || !ob->grid || ob->large || v->have_wave) {
        v->phase_count = 0;
        release(v, 0);
        return 0;
    }
    for (unsigned i = 2; i > 0; i--) {
        v->phase_x[i] = v->phase_x[i - 1]; v->phase_y[i] = v->phase_y[i - 1];
        v->phase_epoch[i] = v->phase_epoch[i - 1]; v->phase_room[i] = v->phase_room[i - 1];
    }
    v->phase_x[0] = (int32_t)(link[ORACLES_OBJ_XH] << 8 | link[ORACLES_OBJ_X]);
    v->phase_y[0] = (int32_t)(link[ORACLES_OBJ_YH] << 8 | link[ORACLES_OBJ_Y]);
    v->phase_epoch[0] = ob->epoch; v->phase_room[0] = v->observer.ref_room;
    if (v->phase_count < 3u) v->phase_count++;
    if (v->phase_count < 3u || v->phase_epoch[2] != ob->epoch || v->phase_epoch[1] != ob->epoch
        || v->phase_room[2] != v->phase_room[0] || v->phase_room[1] != v->phase_room[0]) { release(v, 0); return 0; }
    /* The frame on screen draws him where the observation before had him, moved as he moved then. */
    const int32_t tx = v->phase_x[1], ty = v->phase_y[1];
    const int32_t vx = tx - v->phase_x[2], vy = ty - v->phase_y[2];
    if (!vx && !vy) { release(v, 1); return 0; }
    int dy = 0;
    if (vx && (vx == vy || vx == -vy)) {
        /* His x's progress to its next pixel, 0 to 255 the way he walks; his y given the same. */
        const int32_t progress = vx > 0 ? (tx & 255) : ((-tx - 1) & 255);
        int32_t c = (vy > 0 ? progress - ty : -1 - progress - ty) & 255;
        if (c > 128) c -= 256;
        dy = (int)(floor256(ty + c) - floor256(ty));
        if (!v->phase_latched && dy) return 0;   /* taken up on a frame it moves him by nothing */
        v->phase_latched = 1;
        v->phase_c = c;
        v->phase_engaged_frames++;
        v->observation.link_in_phase = 1;
    } else if (v->phase_latched) {
        dy = (int)(floor256(ty + v->phase_c) - floor256(ty));
        if (!dy) v->phase_latched = 0;          /* given back on a frame it moves him by nothing */
    }
    if (!dy) return 0;
    const uint64_t mask = link_entries(v, dy);
    if (!mask) { release(v, 0); return 0; }
    v->phase_mask = mask;
    return dy;
}

void ev_phase_observe(OraclesEnhancedView *v)
{
    v->phase_mask = 0;
    v->observation.drawn_link_dy_held = 0;
    v->observation.link_in_phase = 0;
    v->phase_dy = phase_dy(v);
    v->observation.drawn_link_dy = v->phase_dy;
    v->phase_last_dy = v->phase_dy;
}

/* His sprite moved: the window's pixels where it stands on screen and where it
 * is drawn, rendered again by the software PPU with his entries moved. */
void ev_overlay_link_phase(OraclesEnhancedView *v, int32_t camera_x, int32_t camera_y)
{
    if (!v->phase_dy || !v->phase_mask) return;
    const OraclesEnhancedObservation *ob = &v->observation;
    const uint8_t *oam = oracles_guest_oam(v->guest), *io = oracles_guest_io(v->guest), *palettes = oracles_guest_obj_palettes(v->guest);
    const uint8_t *vram0 = oracles_guest_vram(v->guest, 0), *vram1 = oracles_guest_vram(v->guest, 1);
    const OraclesGuestTables *t = oracles_guest_tables(v->guest);
    const uint8_t *regs = oracles_guest_ptr(v->guest, t->gfx_regs3, 6);
    if (!oam || !io || !palettes || !vram0 || !vram1 || !regs) return;
    const int height = (io[IO_LCDC] & 0x04u) ? 16 : 8;
    uint8_t moved[160];
    memcpy(moved, oam, sizeof moved);
    int left = (int)ORACLES_ENHANCED_CORE_WIDTH, right = 0, top = (int)ORACLES_ENHANCED_AREA_HEIGHT, bottom = 0;
    for (unsigned k = 0; k < 40u; k++) {
        if (!(v->phase_mask & (1ull << k))) continue;
        moved[k * 4u] = (uint8_t)(oam[k * 4u] + v->phase_dy);
        const int sx = (int)oam[k * 4u + 1u] - 8, sy = (int)oam[k * 4u] - 16 - (int)ORACLES_ENHANCED_HUD_HEIGHT;
        const int y0 = sy + (v->phase_dy < 0 ? v->phase_dy : 0), y1 = sy + height + (v->phase_dy > 0 ? v->phase_dy : 0);
        if (sx < left) left = sx;
        if (sx + 8 > right) right = sx + 8;
        if (y0 < top) top = y0;
        if (y1 > bottom) bottom = y1;
    }
    if (left >= right || top >= bottom) return;
    OraclesPpuInput in;
    in.vram = v->hybrid_vram;
    memcpy(v->hybrid_vram, vram0, 0x2000u);
    memcpy(v->hybrid_vram + 0x2000u, vram1, 0x2000u);
    in.oam = moved;
    in.bg_palettes = oracles_guest_bg_palettes(v->guest);
    in.obj_palettes = palettes;
    in.colours = v->colours;
    if (!in.bg_palettes) return;
    const OraclesPpuRegs r = { (uint8_t)(regs[0] | 0x80u), (uint8_t)(ob->drawn_camera_y + ob->drawn_offset_y - (int)ORACLES_ENHANCED_HUD_HEIGHT),
                               (uint8_t)(ob->drawn_camera_x + ob->drawn_offset_x), regs[3], regs[4] };
    for (unsigned ly = 0; ly < ORACLES_PPU_HEIGHT; ly++) in.lines[ly] = r;
    oracles_ppu_render(&in, v->popup_frame);
    for (int gy = top; gy < bottom; gy++)
        for (int gx = left; gx < right; gx++) {
            const int32_t px = ob->window_left + gx - camera_x, py = ob->window_top + gy - camera_y;
            if (px < 0 || px >= (int32_t)v->size.width || py < 0 || py >= (int32_t)v->band_height) continue;
            *ev_band_pixel(v, (unsigned)px, (unsigned)py) = v->popup_frame[(ORACLES_ENHANCED_HUD_HEIGHT + (unsigned)gy) * ORACLES_PPU_WIDTH + (unsigned)gx];
        }
    v->phase_frames++;
}

/* ---- the sub-pixel camera ---------------------------------------------------------------------- */

/* Walking at 0.71 of a pixel a frame each way (a diagonal), the camera in whole pixels moves 1, 1, 0 pixels on
 * the frames (3, 3, 0 on a screen at 3x), where a straight walk moves it one pixel on every frame: the band goes
 * by jerks.  The camera is the reducer's smooth position instead, to the 256th of a pixel (camera.c,
 * shown_fraction): the surface is composed at its whole pixels as before, and the band is moved by the fraction
 * when it is shown, at the screen's resolution (2, 2, 3 pixels a frame at 3x).  His sprite, drawn by the game at his
 * whole pixels, stays in the band; the view hands the backend his sprites' own pixels as a layer drawn at his exact
 * place (his own fraction of a pixel less the band's), and under them the window rendered again by the software PPU
 * without his sprites and those of what stands at his fraction (the sword in his hand), a patch moved with the band.  A frame
 * shown as it is shows him at his whole pixels.
 *
 * The band keeps its fraction through the scrolls, the text boxes and a frame Link cannot be drawn apart on: a
 * fraction dropped in one frame would move the whole picture by up to a pixel.  Where the camera's fraction itself comes or
 * goes (the reducer starting or stopping to track, Link drawn apart or not for a stretch), the band goes on from where
 * it stood at the new target's speed and takes it up or gives it back by SUB_EASE a frame.  What the view draws
 * standing on the screen (the text box, the area's name) is shown unmoved; the special objects at a fraction of their
 * own stay with the band. */
#define SUB_EASE 16
#define SUB_WAVE_HOLD 90   /* frames without the waves before the band takes a fraction again: the scrolls between the sea floor's rooms */

void oracles_enhanced_view_set_subpixel(OraclesEnhancedView *v, int enabled)
{
    v->subpixel = enabled != 0;
    v->sub_have_last = 0;
    oracles_enhanced_camera_set_exact(v->camera, v->subpixel);
    if (v->subpixel) { v->link_phase = 0; v->phase_count = 0; v->phase_latched = 0; v->phase_dy = 0; }
}

int oracles_enhanced_view_subpixel(const OraclesEnhancedView *v, int32_t *x, int32_t *y, uint32_t *band_top, uint32_t *band_height,
                                   const uint32_t **layer, int32_t *layer_x, int32_t *layer_y, uint32_t *layer_w, uint32_t *layer_h,
                                   int32_t *layer_fx, int32_t *layer_fy)
{
    *x = v->sub_x; *y = v->sub_y;
    *layer_fx = v->sub_layer_fx; *layer_fy = v->sub_layer_fy;
    *band_top = ORACLES_ENHANCED_HUD_HEIGHT; *band_height = v->band_height;
    const int has = v->sub_layer_w && v->sub_layer_h;
    *layer = has ? v->sub_layer : NULL;
    *layer_x = v->sub_layer_x; *layer_y = v->sub_layer_y; *layer_w = has ? v->sub_layer_w : 0; *layer_h = has ? v->sub_layer_h : 0;
    return v->sub_x || v->sub_y || has;
}

const uint32_t *oracles_enhanced_view_subpixel_patch(const OraclesEnhancedView *v) { return v->sub_layer_w && v->sub_layer_h ? v->sub_patch : NULL; }

unsigned oracles_enhanced_view_subpixel_fixed(const OraclesEnhancedView *v, const uint32_t (**rects)[4])
{
    *rects = v->sub_fixed;
    return v->sub_fixed_count;
}

unsigned oracles_enhanced_view_subpixel_covered_pixels(const OraclesEnhancedView *v) { return v->sub_covered_pixels; }
unsigned oracles_enhanced_view_subpixel_unexplained_frames(const OraclesEnhancedView *v) { return v->sub_unexplained_frames; }
uint64_t oracles_enhanced_view_subpixel_taken(const OraclesEnhancedView *v) { return v->sub_layer_w && v->sub_layer_h ? v->sub_taken : 0; }
unsigned oracles_enhanced_view_subpixel_objects_left(const OraclesEnhancedView *v) { return v->sub_objects_left; }

void oracles_enhanced_view_subpixel_pixel_counts(const OraclesEnhancedView *v, unsigned *left_pixels, unsigned *wrong_pixels, unsigned *alike_pixels)
{
    *left_pixels = v->sub_left_pixels; *wrong_pixels = v->sub_wrong_pixels; *alike_pixels = v->sub_alike_pixels;
}

void oracles_enhanced_view_subpixel_counts(const OraclesEnhancedView *v, unsigned *frames, unsigned *layer_frames, unsigned *refused_frames)
{
    if (frames) *frames = v->sub_frames;
    if (layer_frames) *layer_frames = v->sub_layer_frames;
    if (refused_frames) *refused_frames = v->sub_refused_frames;
}

void ev_subpixel_fixed(OraclesEnhancedView *v, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!v->subpixel) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int32_t)v->size.width) w = (int32_t)v->size.width - x;
    if (y + h > (int32_t)v->band_height) h = (int32_t)v->band_height - y;
    if (w <= 0 || h <= 0) return;
    const uint32_t rect[4] = { (uint32_t)x, ORACLES_ENHANCED_HUD_HEIGHT + (uint32_t)y, (uint32_t)w, (uint32_t)h };
    if (v->sub_fixed_count) {
        uint32_t *last = v->sub_fixed[v->sub_fixed_count - 1u];
        if (last[1] == rect[1] && last[3] == rect[3] && last[0] + last[2] == rect[0]) { last[2] += rect[2]; return; }   /* the next cell of a row */
        if (v->sub_fixed_count == ORACLES_ENHANCED_SUBPIXEL_FIXED) {   /* no room left: the last one grows to take it */
            const uint32_t x1 = last[0] + last[2] > rect[0] + rect[2] ? last[0] + last[2] : rect[0] + rect[2];
            const uint32_t y1 = last[1] + last[3] > rect[1] + rect[3] ? last[1] + last[3] : rect[1] + rect[3];
            if (rect[0] < last[0]) last[0] = rect[0];
            if (rect[1] < last[1]) last[1] = rect[1];
            last[2] = x1 - last[0]; last[3] = y1 - last[1];
            return;
        }
    }
    memcpy(v->sub_fixed[v->sub_fixed_count++], rect, sizeof rect);
}

/* One axis of the fraction the band is moved by: the camera's (`raw`) while it is `live`, else none; where that
 * changes, the band goes on from the place it stood the frame before at the new target's speed, and reaches the
 * target by SUB_EASE a frame. */
static int32_t band_fraction(int continuous, int live, int was_live, int32_t raw, int32_t last_raw, int32_t last_shown, int32_t *ease)
{
    const int32_t target = live ? raw : 0;
    int32_t d = continuous ? *ease : 0;
    if (continuous && live != was_live) d = last_shown - (live ? last_raw : 0);
    d = d > SUB_EASE ? d - SUB_EASE : d < -SUB_EASE ? d + SUB_EASE : 0;
    int32_t shown = target + d;
    if (shown > 255) shown = 255;
    if (shown < -255) shown = -255;
    *ease = shown - target;
    return shown;
}

void ev_subpixel_layer(OraclesEnhancedView *v, int tracking, int32_t camera_x, int32_t camera_y)
{
    const OraclesEnhancedObservation *ob = &v->observation;
    if (!v->subpixel) return;
    int32_t fx = 0, fy = 0;
    oracles_enhanced_camera_fraction(v->camera, &fx, &fy);
    if (!tracking) fx = fy = 0;
    if (!oracles_enhanced_camera_vertical_tracking(v->camera)) fy = 0;
    /* Where Link cannot be drawn apart for a stretch (a cutscene, a large room, the sea floor's waves and the scrolls
     * between its rooms), the band takes no fraction either, so that he keeps his place against it as without the
     * sub-pixel camera. */
    if (v->have_wave) v->sub_wave_hold = SUB_WAVE_HOLD;
    else if (v->sub_wave_hold) v->sub_wave_hold--;
    const int apart = ob->playing && !ob->cutscene && ob->grid && !ob->large && !v->have_wave;
    const int live = tracking && apart && !v->sub_wave_hold;
    const int continuous = v->sub_have_last && v->sub_last_frame + 1u == v->frame && v->sub_last_epoch == ob->epoch;
    v->sub_x = band_fraction(continuous, live, v->sub_live, fx, v->sub_raw_x, v->sub_last_x, &v->sub_ease_x);
    v->sub_y = band_fraction(continuous, live, v->sub_live, fy, v->sub_raw_y, v->sub_last_y, &v->sub_ease_y);
    v->sub_have_last = 1; v->sub_last_frame = v->frame; v->sub_last_epoch = ob->epoch;
    v->sub_live = live; v->sub_raw_x = fx; v->sub_raw_y = fy;
    v->sub_last_x = v->sub_x; v->sub_last_y = v->sub_y;
    if (v->sub_x || v->sub_y) v->sub_frames++;
    /* Link drawn apart: in play or in a scroll, outdoors, his sprites wholly inside the game's window. */
    if (!tracking || !apart || (ob->in_transition && !ob->in_scroll)) return;
    if (!v->sub_x && !v->sub_y && !ob->drawn_sub_x && !ob->drawn_sub_y) return;
    const uint8_t *oam = oracles_guest_oam(v->guest), *io = oracles_guest_io(v->guest), *palettes = oracles_guest_obj_palettes(v->guest);
    const uint8_t *vram0 = oracles_guest_vram(v->guest, 0), *vram1 = oracles_guest_vram(v->guest, 1);
    const OraclesGuestTables *t = oracles_guest_tables(v->guest);
    const uint8_t *regs = oracles_guest_ptr(v->guest, t->gfx_regs3, 6), *bg_palettes = oracles_guest_bg_palettes(v->guest);
    if (!v->live_sprites || !oam || !io || !palettes || !vram0 || !vram1 || !regs || !bg_palettes) { v->sub_refused_frames++; return; }
    /* The special objects' entries the frame shows, inside the window or past it (where the edge overlay draws them,
     * Link's below the screen too: ev_overlay_edge_sprites), placed as it places them: Link's, and those of the
     * objects that stand at his fraction of a pixel, which the game places from his (the sword in his hand, the
     * companion he rides).  Another one, a companion waiting, a bomb laid, a boomerang thrown, keeps its whole pixels
     * in the band: drawn at his fraction, it would move against the ground. */
    const uint8_t *link = oracles_guest_object(v->guest, 0, 0);
    if (!link) return;
    v->sub_taken = 0;
    const int height = (io[IO_LCDC] & 0x04u) ? 16 : 8;
    OraclesSpriteTag tags[ORACLES_SPRITE_TAGS];
    const unsigned count = (io[IO_LCDC] & 0x02u) ? oracles_sprites_tags_for_oam(v->live_sprites, oam, tags, ORACLES_SPRITE_TAGS) : 0u;
    uint8_t without[160];
    memcpy(without, oam, sizeof without);
    int left = INT32_MAX, right = INT32_MIN, top = INT32_MAX, bottom = INT32_MIN, any = 0;
    for (unsigned i = 0; i < count; i++) {
        if (tags[i].kind != 0u) continue;
        if (tags[i].slot != 0u) {
            const uint8_t *o = oracles_guest_object(v->guest, tags[i].slot, 0);
            if (!o || o[ORACLES_OBJ_X] != link[ORACLES_OBJ_X] || o[ORACLES_OBJ_Y] != link[ORACLES_OBJ_Y]) { v->sub_objects_left++; continue; }
        }
        for (unsigned n = 0; n < tags[i].count && tags[i].first + n < 40u; n++) {
            const unsigned k = tags[i].first + n;
            const uint8_t *e = oam + k * 4u;
            if (e[0] == 0u || e[1] == 0u || (e[0] >= 160u && tags[i].slot != 0u)) continue;   /* not drawn */
            v->sub_taken |= 1ull << k;
            const int sx = e[1] >= 208u ? (int)e[1] - 8 - 256 : (int)e[1] - 8;
            const int sy = (e[0] >= 224u ? (int)e[0] - 256 : (int)e[0]) - 16 - (int)ORACLES_ENHANCED_HUD_HEIGHT;
            without[k * 4u] = 0;
            if (sx < left) left = sx;
            if (sx + 8 > right) right = sx + 8;
            if (sy < top) top = sy;
            if (sy + height > bottom) bottom = sy + height;
            any = 1;
        }
    }
    if (!any) return;
    if (right - left > (int)SUB_LAYER_SIZE || bottom - top > (int)SUB_LAYER_SIZE) { v->sub_refused_frames++; return; }
    /* Their sprites' own pixels (the colours other than 0 of their tiles, flipped as they are): what the frame shows
     * there, Link or the background the game draws over him, goes to the layer and moves with him; the window
     * rendered again without them goes to the patch.  A mask of the pixels where the two differed would leave in the
     * band those of his pixels that had the background's colour, which a layer moved by a fraction then uncovers. */
    static uint8_t mask[SUB_LAYER_SIZE * SUB_LAYER_SIZE];
    memset(mask, 0, sizeof mask);
    for (unsigned k = 0; k < 40u; k++) {
        if (without[k * 4u] == oam[k * 4u]) continue;   /* not one of theirs */
        const uint8_t *e = oam + k * 4u;
        const int sx = (e[1] >= 208u ? (int)e[1] - 8 - 256 : (int)e[1] - 8) - left;
        const int sy = (e[0] >= 224u ? (int)e[0] - 256 : (int)e[0]) - 16 - (int)ORACLES_ENHANCED_HUD_HEIGHT - top;
        const uint8_t *bank = (e[3] & 0x08u) ? vram1 : vram0;
        for (int row = 0; row < height; row++) {
            const unsigned line = (e[3] & 0x40u) ? (unsigned)(height - 1 - row) : (unsigned)row;
            const unsigned tile = height == 16 ? ((e[2] & 0xfeu) + (line >> 3)) : e[2];
            const uint8_t lo = bank[tile * 16u + (line & 7u) * 2u], hi = bank[tile * 16u + (line & 7u) * 2u + 1u];
            for (int col = 0; col < 8; col++) {
                const unsigned bit = (e[3] & 0x20u) ? (unsigned)col : 7u - (unsigned)col;
                if (((lo >> bit) & 1u) | ((hi >> bit) & 1u)) mask[(unsigned)(sy + row) * SUB_LAYER_SIZE + (unsigned)(sx + col)] = 1;
            }
        }
    }
    OraclesPpuInput in;
    in.vram = v->hybrid_vram;
    memcpy(v->hybrid_vram, vram0, 0x2000u);
    memcpy(v->hybrid_vram + 0x2000u, vram1, 0x2000u);
    in.bg_palettes = bg_palettes;
    in.obj_palettes = palettes;
    in.colours = v->colours;
    const OraclesPpuRegs r = { (uint8_t)(regs[0] | 0x80u), (uint8_t)(ob->drawn_camera_y + ob->drawn_offset_y - (int)ORACLES_ENHANCED_HUD_HEIGHT),
                               (uint8_t)(ob->drawn_camera_x + ob->drawn_offset_x), regs[3], regs[4] };
    for (unsigned ly = 0; ly < ORACLES_PPU_HEIGHT; ly++) in.lines[ly] = r;
    in.oam = without;
    oracles_ppu_render(&in, v->hybrid_frame);
    /* The window rendered again must explain the frame around them, outside their sprites, but on the tiles the game
     * animates, which it may have drawn since (the sea's waves): the VRAM is the one after the frame, and a patch
     * taken from tiles it changed otherwise would put another background under him, or leave a drawing of theirs in
     * the band.  Where it does not, he stays with the band. */
    const uint8_t *animated = ev_shown_animated(v);
    unsigned unexplained = 0;
    const unsigned map = (regs[0] & 0x08u) ? 0x1c00u : 0x1800u, unsigned_tiles = (regs[0] & 0x10u) != 0;
    for (int gy = top > 0 ? top : 0; gy < bottom && gy < (int)ORACLES_ENHANCED_AREA_HEIGHT; gy++)
        for (int gx = left > 0 ? left : 0; gx < right && gx < (int)ORACLES_ENHANCED_CORE_WIDTH; gx++) {
            const int32_t px = ob->window_left + gx - camera_x, py = ob->window_top + gy - camera_y;
            if (mask[(unsigned)(gy - top) * SUB_LAYER_SIZE + (unsigned)(gx - left)] || px < 0 || px >= (int32_t)v->size.width || py < 0 || py >= (int32_t)v->band_height) continue;
            if (*ev_band_pixel(v, (unsigned)px, (unsigned)py) == v->hybrid_frame[(ORACLES_ENHANCED_HUD_HEIGHT + (unsigned)gy) * ORACLES_PPU_WIDTH + (unsigned)gx]) continue;
            const unsigned by = ((unsigned)r.scy + ORACLES_ENHANCED_HUD_HEIGHT + (unsigned)gy) & 255u, bx = ((unsigned)r.scx + (unsigned)gx) & 255u;
            const unsigned cell = map + (by >> 3) * 32u + (bx >> 3), n = vram0[cell];
            const unsigned tile = (unsigned_tiles ? n : (n < 128u ? 256u + n : n)) + ((vram1[cell] >> 3) & 1u) * 384u;
            if (animated && (animated[tile >> 3] & (1u << (tile & 7u)))) continue;
            unexplained++;
        }
    if (unexplained) { v->sub_unexplained_frames++; v->sub_refused_frames++; return; }
    memset(v->sub_layer, 0, sizeof v->sub_layer);
    memset(v->sub_patch, 0, sizeof v->sub_patch);
    memset(v->sub_pending, 0, sizeof v->sub_pending);
    for (int gy = top; gy < bottom; gy++)
        for (int gx = left; gx < right; gx++) {
            const unsigned in_layer = (unsigned)(gy - top) * SUB_LAYER_SIZE + (unsigned)(gx - left);
            const int32_t px = ob->window_left + gx - camera_x, py = ob->window_top + gy - camera_y;
            if (px < 0 || px >= (int32_t)v->size.width || py < 0 || py >= (int32_t)v->band_height) continue;
            const uint32_t band = *ev_band_pixel(v, (unsigned)px, (unsigned)py);
            if (gx < 0 || gx >= (int)ORACLES_ENHANCED_CORE_WIDTH || gy < 0 || gy >= (int)ORACLES_ENHANCED_AREA_HEIGHT) {
                /* Past the window: the band as it stands goes under him, what the edge overlay draws there to him. */
                if (mask[in_layer]) { v->sub_patch[in_layer] = band | 0xff000000u; v->sub_pending[in_layer] = 1; }
                continue;
            }
            const unsigned at = (ORACLES_ENHANCED_HUD_HEIGHT + (unsigned)gy) * ORACLES_PPU_WIDTH + (unsigned)gx;
            /* The frame's own pixel that the render without them does not explain, outside their sprites, off the
             * animated tiles: a drawing of theirs the patch would leave behind (none, such a frame left above). */
            if (!mask[in_layer] && band != v->hybrid_frame[at]) {
                const unsigned by = ((unsigned)r.scy + ORACLES_ENHANCED_HUD_HEIGHT + (unsigned)gy) & 255u, bx = ((unsigned)r.scx + (unsigned)gx) & 255u;
                const unsigned cell = map + (by >> 3) * 32u + (bx >> 3), n = vram0[cell];
                const unsigned tile = (unsigned_tiles ? n : (n < 128u ? 256u + n : n)) + ((vram1[cell] >> 3) & 1u) * 384u;
                if (!animated || !(animated[tile >> 3] & (1u << (tile & 7u)))) v->sub_left_pixels++;
            }
            if (!mask[in_layer]) continue;
            if (band == v->hybrid_frame[at]) v->sub_alike_pixels++;   /* his own, of the background's colour there */
            v->sub_layer[in_layer] = band | 0xff000000u;
            v->sub_patch[in_layer] = v->hybrid_frame[at] | 0xff000000u;
        }
    /* The layer over the band at the whole pixels: the frame as the game drew it, to the pixel. */
    for (int gy = top; gy < bottom; gy++)
        for (int gx = left; gx < right; gx++) {
            const unsigned in_layer = (unsigned)(gy - top) * SUB_LAYER_SIZE + (unsigned)(gx - left);
            const int32_t px = ob->window_left + gx - camera_x, py = ob->window_top + gy - camera_y;
            if (px < 0 || px >= (int32_t)v->size.width || py < 0 || py >= (int32_t)v->band_height || !(v->sub_layer[in_layer] >> 24)) continue;
            if ((v->sub_layer[in_layer] | 0xff000000u) != (*ev_band_pixel(v, (unsigned)px, (unsigned)py) | 0xff000000u)) v->sub_wrong_pixels++;
        }
    v->sub_layer_x = ob->window_left + left - camera_x;
    v->sub_layer_y = (int32_t)ORACLES_ENHANCED_HUD_HEIGHT + ob->window_top + top - camera_y;
    v->sub_layer_w = (uint32_t)(right - left); v->sub_layer_h = (uint32_t)(bottom - top);
    v->sub_layer_fx = ob->drawn_sub_x - v->sub_x; v->sub_layer_fy = ob->drawn_sub_y - v->sub_y;
    v->sub_layer_frames++;
}

void ev_subpixel_settle(OraclesEnhancedView *v)
{
    if (!v->sub_layer_w || !v->sub_layer_h) return;
    /* Sprites of his the edge overlay made without the OAM: the frame keeps him whole, with the band. */
    if (v->edge_link_made) { v->sub_layer_w = v->sub_layer_h = 0; v->sub_layer_frames--; v->sub_refused_frames++; return; }
    for (uint32_t row = 0; row < v->sub_layer_h; row++)
        for (uint32_t col = 0; col < v->sub_layer_w; col++) {
            const unsigned in_layer = row * SUB_LAYER_SIZE + col;
            if (v->sub_pending[in_layer]) {
                const int32_t px = v->sub_layer_x + (int32_t)col, py = v->sub_layer_y - (int32_t)ORACLES_ENHANCED_HUD_HEIGHT + (int32_t)row;
                v->sub_layer[in_layer] = *ev_band_pixel(v, (unsigned)px, (unsigned)py) | 0xff000000u;
            }
            if (!(v->sub_layer[in_layer] >> 24)) continue;
            const int32_t px = v->sub_layer_x + (int32_t)col, py = v->sub_layer_y - (int32_t)ORACLES_ENHANCED_HUD_HEIGHT + (int32_t)row;
            int fixed = 0;   /* shown unmoved, him at his whole pixels with it */
            for (unsigned f = 0; f < v->sub_fixed_count && !fixed; f++) {
                const uint32_t *rect = v->sub_fixed[f], sy = (uint32_t)py + ORACLES_ENHANCED_HUD_HEIGHT;
                fixed = (uint32_t)px >= rect[0] && (uint32_t)px < rect[0] + rect[2] && sy >= rect[1] && sy < rect[1] + rect[3];
            }
            if (!fixed && (*ev_band_pixel(v, (unsigned)px, (unsigned)py) | 0xff000000u) == v->sub_layer[in_layer]) continue;
            v->sub_layer[in_layer] = 0; v->sub_patch[in_layer] = 0;
            v->sub_covered_pixels++;
        }
}
