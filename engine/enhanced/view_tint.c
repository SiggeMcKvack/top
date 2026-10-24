/* A scene's tint on the neighbouring rooms (view.h, view_internal.h): the transform from the room's palettes to the
 * scene's, fitted on the live room, kept per room, and the tint followed frame by frame. */
#include "view_internal.h"

#define TINT_MIN_PALETTES 4u              /* the room's palettes a scene's tint changes at least: the screen, not an object's palette */
#define PALETTE_THREAD_MIX 0x08u          /* wPaletteThread_mode of the fade between two palettes (paletteFadeHandler08) */
#define TINT_FIT_ERROR 4                 /* the farthest a channel of the scene's palettes stands from its transform's */
#define TINT_SETTLE_FRAMES 4u             /* after a fade, a scroll or a palette transition: the palettes on screen a frame or two behind the thread */

static unsigned palette_colour(const uint8_t *palettes, unsigned i) { return (unsigned)(palettes[i * 2u] | (palettes[i * 2u + 1u] << 8)) & 0x7fffu; }

/* A colour through the transform, its channels rounded and kept to 0..31. */
static unsigned tinted_colour(const int32_t *k, unsigned colour)
{
    const int32_t r = (int32_t)(colour & 31u), g = (int32_t)((colour >> 5) & 31u), b = (int32_t)((colour >> 10) & 31u);
    unsigned out = 0;
    for (unsigned ch = 0; ch < 3u; ch++) {
        const int32_t value = k[ch * 4u] * r + k[ch * 4u + 1u] * g + k[ch * 4u + 2u] * b + k[ch * 4u + 3u];
        const int32_t c = value <= 0 ? 0 : (value + 512) >> 10;
        out |= (unsigned)(c > 31 ? 31 : c) << (ch * 5u);
    }
    return out;
}

/* The colour transform that takes the colours of `slots` in `reference` to
 * those in `scene`, least squares by output channel (out = a.r + b.g + c.b + d),
 * in 1/1024, and how far from the scene's each palette's colours come
 * through it, the farthest channel; 0 when there is none. */
static int fit_tint(const uint8_t *reference, const uint8_t *scene, uint8_t slots, int32_t out[12], int error[8])
{
    int64_t sums[4][4] = { { 0 } }, targets[3][4] = { { 0 } };
    for (unsigned i = 0; i < 32u; i++) {
        if (!(slots & (1u << (i / 4u)))) continue;
        const unsigned from = palette_colour(reference, i), to = palette_colour(scene, i);
        const int64_t x[4] = { from & 31u, (from >> 5) & 31u, (from >> 10) & 31u, 1 };
        for (unsigned r = 0; r < 4u; r++) {
            for (unsigned c = 0; c < 4u; c++) sums[r][c] += x[r] * x[c];
            for (unsigned ch = 0; ch < 3u; ch++) targets[ch][r] += x[r] * (int64_t)((to >> (ch * 5u)) & 31u);
        }
    }
    /* The normal equations, a little weight against steep slopes: palettes of
     * few distinct colours leave the system short of rank. */
    double m[4][7];
    for (unsigned r = 0; r < 4u; r++) {
        for (unsigned c = 0; c < 4u; c++) m[r][c] = (double)sums[r][c] + (r == c && r < 3u ? 0.01 : 0.0);
        for (unsigned ch = 0; ch < 3u; ch++) m[r][4u + ch] = (double)targets[ch][r];
    }
    for (unsigned c = 0; c < 4u; c++) {
        unsigned pivot = c;
        for (unsigned r = c + 1u; r < 4u; r++) if ((m[r][c] < 0 ? -m[r][c] : m[r][c]) > (m[pivot][c] < 0 ? -m[pivot][c] : m[pivot][c])) pivot = r;
        if (m[pivot][c] > -1e-9 && m[pivot][c] < 1e-9) return 0;
        if (pivot != c) for (unsigned k = 0; k < 7u; k++) { const double s = m[c][k]; m[c][k] = m[pivot][k]; m[pivot][k] = s; }
        for (unsigned r = 0; r < 4u; r++) {
            if (r == c) continue;
            const double f = m[r][c] / m[c][c];
            for (unsigned k = c; k < 7u; k++) m[r][k] -= f * m[c][k];
        }
    }
    for (unsigned ch = 0; ch < 3u; ch++)
        for (unsigned k = 0; k < 4u; k++) {
            const double scaled = m[k][4u + ch] / m[k][k] * 1024.0;
            if (scaled > 1e8 || scaled < -1e8) return 0;
            out[ch * 4u + k] = scaled >= 0 ? (int32_t)(scaled + 0.5) : -(int32_t)(-scaled + 0.5);
        }
    for (unsigned p = 0; p < 8u; p++) error[p] = 0;
    for (unsigned i = 0; i < 32u; i++) {
        if (!(slots & (1u << (i / 4u)))) continue;
        const unsigned got = tinted_colour(out, palette_colour(reference, i)), want = palette_colour(scene, i);
        for (unsigned shift = 0; shift < 15u; shift += 5u) {
            const int d = (int)((got >> shift) & 31u) - (int)((want >> shift) & 31u);
            if (d > error[i / 4u]) error[i / 4u] = d;
            if (-d > error[i / 4u]) error[i / 4u] = -d;
        }
    }
    return 1;
}

/* Palettes 2 to 7 of `a` within 2 of `b` on every channel: a palette
 * transition's last mix, or none. */
static int within_two(const uint8_t *a, const uint8_t *b)
{
    for (unsigned c = 8u; c < 32u; c++) {
        const unsigned x = palette_colour(a, c), y = palette_colour(b, c);
        for (unsigned shift = 0; shift < 15u; shift += 5u) {
            const int d = (int)((x >> shift) & 31u) - (int)((y >> shift) & 31u);
            if (d > 2 || d < -2) return 0;
        }
    }
    return 1;
}

/* The scene's tint of `scene` over `reference` (oracles_enhanced_scene_tint, view.h). */
int oracles_enhanced_scene_tint(const uint8_t reference[64], const uint8_t scene[64], uint8_t *slots_out, uint8_t *changed_out, int32_t coefficients[12])
{
    uint8_t changed = 0, beyond = 0;   /* changed by more than a palette transition's last mix, 2 */
    unsigned count = 0;
    for (unsigned p = 0; p < 8u; p++) {
        if (memcmp(scene + p * 8u, reference + p * 8u, 8u) == 0) continue;
        changed |= (uint8_t)(1u << p);
        count++;
        for (unsigned c = p * 4u; c < p * 4u + 4u; c++) {
            const unsigned a = palette_colour(scene, c), b = palette_colour(reference, c);
            for (unsigned shift = 0; shift < 15u; shift += 5u) {
                const int d = (int)((a >> shift) & 31u) - (int)((b >> shift) & 31u);
                if (d > 2 || d < -2) beyond |= (uint8_t)(1u << p);
            }
        }
    }
    /* The palettes one transform explains: the farthest left out, one at a
     * time, while enough remain (Seasons' Onox turns two palettes red as the
     * others go to night); one of those left beyond a transition's mix. */
    uint8_t slots = changed;
    while (count >= TINT_MIN_PALETTES && (slots & beyond)) {
        int error[8];
        if (!fit_tint(reference, scene, slots, coefficients, error)) return 0;
        unsigned worst = 8u;
        for (unsigned p = 0; p < 8u; p++)
            if ((slots & (1u << p)) && error[p] > TINT_FIT_ERROR && (worst == 8u || error[p] > error[worst])) worst = p;
        if (worst == 8u) {
            *slots_out = slots;
            *changed_out = changed;
            return 1;
        }
        slots &= (uint8_t)~(1u << worst);
        count--;
    }
    return 0;
}

int oracles_enhanced_scene_tint_after_fade(const uint8_t reference[64], const uint8_t scene[64], uint8_t slots)
{
    unsigned beyond = 0;
    for (unsigned p = 0; p < 8u; p++) {
        if (!(slots & (1u << p))) continue;
        int apart = 0;
        for (unsigned c = p * 4u; c < p * 4u + 4u; c++) {
            const unsigned a = palette_colour(scene, c), b = palette_colour(reference, c);
            for (unsigned shift = 0; shift < 15u; shift += 5u) {
                const int d = (int)((a >> shift) & 31u) - (int)((b >> shift) & 31u);
                if (d > 2 || d < -2) apart = 1;
            }
        }
        beyond += (unsigned)apart;
    }
    return beyond >= TINT_MIN_PALETTES;
}

void oracles_enhanced_scene_tinted(const uint8_t own[64], const uint8_t reference[64], const uint8_t scene[64], uint8_t changed,
                                   const int32_t coefficients[12], uint8_t out[64])
{
    memcpy(out, own, 64u);
    for (unsigned p = 0; p < 8u; p++) {
        if (!(changed & (1u << p))) continue;
        if (memcmp(own + p * 8u, scene + p * 8u, 8u) == 0) continue;   /* run during the scene: its palette already */
        if (memcmp(own + p * 8u, reference + p * 8u, 8u) == 0) { memcpy(out + p * 8u, scene + p * 8u, 8u); continue; }
        for (unsigned c = p * 4u; c < p * 4u + 4u; c++) {
            const unsigned colour = tinted_colour(coefficients, palette_colour(own, c));
            out[c * 2u] = (uint8_t)colour; out[c * 2u + 1u] = (uint8_t)(colour >> 8);
        }
    }
}

/* The room's own palettes a scene's tint was taken from (view.h). */
void oracles_enhanced_tint_forget(OraclesTintMemory memories[ORACLES_TINT_MEMORIES])
{
    memset(memories, 0, ORACLES_TINT_MEMORIES * sizeof *memories);
}

static OraclesTintMemory *tint_memory_of(OraclesTintMemory memories[ORACLES_TINT_MEMORIES], uint8_t group, uint8_t room, uint8_t header)
{
    for (unsigned i = 0; i < ORACLES_TINT_MEMORIES; i++)
        if (memories[i].valid && memories[i].group == group && memories[i].room == room && memories[i].header == header) return &memories[i];
    return NULL;
}

const uint8_t *oracles_enhanced_tint_reference(OraclesTintMemory memories[ORACLES_TINT_MEMORIES], uint8_t group, uint8_t room, uint8_t header,
                                               uint32_t frame, const uint8_t *entry, const uint8_t scene[64])
{
    for (unsigned i = 0; i < ORACLES_TINT_MEMORIES; i++)
        if (memories[i].valid && frame - memories[i].used_at > ORACLES_TINT_MEMORY_FRAMES) memories[i].valid = 0;
    OraclesTintMemory *kept = tint_memory_of(memories, group, room, header);
    if (!kept) return entry;
    /* The scene over: the room's entry, the palettes on screen and the kept
     * ones the same.  The palettes alone are not enough: a room the scene
     * goes to loads its palettes a frame before it becomes the active room. */
    if (entry && memcmp(entry, scene, 64u) == 0 && memcmp(scene, kept->reference, 64u) == 0) { kept->valid = 0; return entry; }
    return kept->reference;
}

void oracles_enhanced_tint_kept(OraclesTintMemory memories[ORACLES_TINT_MEMORIES], uint8_t group, uint8_t room, uint8_t header, uint32_t frame,
                                const uint8_t reference[64])
{
    OraclesTintMemory *kept = tint_memory_of(memories, group, room, header);
    if (!kept) {
        kept = &memories[0];
        for (unsigned i = 1; i < ORACLES_TINT_MEMORIES && kept->valid; i++)
            if (!memories[i].valid || memories[i].used_at < kept->used_at) kept = &memories[i];
        kept->valid = 1; kept->group = group; kept->room = room; kept->header = header;
        memcpy(kept->reference, reference, 64u);
    }
    kept->used_at = frame;
}

/* A cutscene may tint the game's screen with palettes of its own and no
 * fade: in Ages over the room's base palettes (w2TilesetBgPalettes),
 * straight (loadPaletteHeader) or through the palette thread's fade between
 * two palettes (startFadeBetweenTwoPalettes), the night of Nayru's
 * possession (CUTSCENE_NAYRU_SINGING, PALH_99 over palettes 2 to 7); in
 * Seasons the same fade mixes into the palettes on screen, the base left as
 * it was, as Onox comes for Din (cutscene06Func4).  The game's screen is then
 * all of the scene, and the other rooms take its tint: the transform from the
 * palettes the live room's own load gives (its entry, which the ghost loaded
 * as the game loads it) to those on screen, while the view measures no fade.
 * Only a change of most of the room's palettes that one transform explains,
 * more than the tint a palette transition leaves (2): a palette or two of an
 * object, or palettes of another kind, are not the scene's tint.  Not during
 * a scroll, nor while the palette transition of a scroll mixes the palettes
 * of the room left and the room entered; and in the few frames after one, or
 * after a fade, only a change of most palettes beyond that mix: the last step
 * of a scroll's fade into Lynna's 0:6b stays on screen two frames after the
 * palette thread is done, within 2 of the room's palettes but for the one it
 * shows white, and passed for a tint of four palettes, while a scene that
 * opens as a fade ends (the night of Nayru's possession, at its return from
 * white) changes them all. */
void ev_track_scene_tint(OraclesEnhancedView *v)
{
    const OraclesGuestTables *t = oracles_guest_tables(v->guest);
    const uint8_t *scene = oracles_guest_bg_palettes(v->guest);
    v->scene_tint = 0;
    const uint8_t group = oracles_guest_read8(v->guest, t->active_group), room = oracles_guest_read8(v->guest, t->active_room);
    /* A warp or a load: what was kept is about another moment. */
    if (v->observation.epoch != v->tint_epoch) { oracles_enhanced_tint_forget(v->tint_memories); v->tint_epoch = v->observation.epoch; }
    /* The palette thread's fade between two palettes run by a palette
     * transition: started with the scroll (wLoadedTilesetPalette set to $ff),
     * it may go on after the scroll's end with the continuous transitions, and
     * a fade may cut it short, its mix left on screen.  Over once the
     * palettes on screen are back within 2 of the room's base, or in another
     * room. */
    const uint8_t thread_mode = oracles_guest_read8(v->guest, t->palette_thread_mode);
    const uint8_t *base = oracles_guest_ptr(v->guest, t->tileset_bg_palettes, 64u);
    const unsigned place = (unsigned)group << 8 | room;
    if (thread_mode == PALETTE_THREAD_MIX && (v->observation.in_scroll || oracles_guest_read8(v->guest, t->loaded_tileset_palette) == 0xffu)) {
        v->transition_mix = 1;
        v->transition_mix_room = place;
    } else if (v->transition_mix && thread_mode != PALETTE_THREAD_MIX && (place != v->transition_mix_room || (scene && base && within_two(scene, base)))) v->transition_mix = 0;
    if (v->fade_effective != 0 || v->observation.in_scroll || v->transition_mix) { v->tint_fade_at = v->frame; return; }
    if (!scene) return;
    const uint8_t header = oracles_guest_read8(v->guest, t->tileset_palette);
    /* The palettes of the room's last delivery, its entry dropped since or
     * not: a key byte the scene changes (Din's dance in Seasons) drops it
     * with the same palettes. */
    const entry *e = ev_find_entry(v, group, room);
    const uint8_t *reference = e && e->accepted_at && e->tileset_palette == header ? e->base_bg_palettes : NULL;
    reference = oracles_enhanced_tint_reference(v->tint_memories, group, room, header, v->frame, reference, scene);
    if (!reference) return;
    uint8_t slots = 0, changed = 0;
    if (!oracles_enhanced_scene_tint(reference, scene, &slots, &changed, v->tint_coefficients)) return;
    if (v->frame - v->tint_fade_at <= TINT_SETTLE_FRAMES && !oracles_enhanced_scene_tint_after_fade(reference, scene, slots)) return;
    oracles_enhanced_tint_kept(v->tint_memories, group, room, header, v->frame, reference);
    v->scene_tint = 1;
    v->tint_slots = slots;
    v->tint_changed = changed;
    memcpy(v->tint_reference, reference, 64u);
    memcpy(v->tint_scene, scene, 64u);
    const uint8_t sets[2] = { slots, changed };
    uint64_t key = oracles_guest_hash(sets, sizeof sets, ORACLES_HASH_SEED);
    key = oracles_guest_hash(v->tint_reference, 64u, key);
    key = oracles_guest_hash(v->tint_scene, 64u, key);
    v->tint_key = oracles_guest_hash((const uint8_t *)v->tint_coefficients, sizeof v->tint_coefficients, key);
    if (!v->scene_tint_frames) v->scene_tint_first = v->frame;
    v->scene_tint_last = v->frame;
    v->scene_tint_frames++;
}

void oracles_enhanced_view_scene_tint_counts(const OraclesEnhancedView *v, unsigned *frames, uint32_t *first, uint32_t *last)
{
    if (frames) *frames = v->scene_tint_frames;
    if (first) *first = v->scene_tint_first;
    if (last) *last = v->scene_tint_last;
}
