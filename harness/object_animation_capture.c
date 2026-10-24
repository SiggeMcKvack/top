#include "object_animation_check_internal.h"

#include "enhanced_check_internal.h"   /* ec_monotonic_us */
#include "ppu.h"

static size_t rom_offset(unsigned bank, unsigned address) { return (size_t)bank * 0x4000u + ((address | 0x4000u) & 0x7fffu) - 0x4000u; }

/* The sprites of a pose: its block's count and its first sprite in the ROM, NULL when it cannot be read. */
static const uint8_t *pose_block(const OraclesObjectAnimationCheck *c, uint16_t pose, unsigned *count)
{
    const size_t at = rom_offset(((pose & 0xc000u) >> 14) + oracles_guest_tables(c->guest)->oam_data_bank, pose);
    if (at >= c->rom_size || at + 1u + (size_t)c->rom[at] * 4u > c->rom_size) return NULL;
    *count = c->rom[at];
    return c->rom + at + 1u;
}

/* The tiles a pose draws with (8x16 sprites), in the capture and in the live VRAM, each object's own tile base and
 * flags: equal or not. */
static int pose_tiles_equal(OraclesObjectAnimationCheck *c, uint16_t pose, const OraclesObjectRecord *captured, const OraclesObjectRecord *live)
{
    unsigned count = 0;
    const uint8_t *s = pose_block(c, pose, &count);
    if (!s) return 0;
    for (unsigned i = 0; i < count; i++, s += 4) {
        const unsigned live_bank = ((live->oam_flags ^ s[3]) >> 3) & 1u, captured_bank = ((captured->oam_flags ^ s[3]) >> 3) & 1u;
        const unsigned live_tile = (uint8_t)(live->oam_tile_base + s[2]) & 0xfeu, captured_tile = (uint8_t)(captured->oam_tile_base + s[2]) & 0xfeu;
        const uint8_t *vram = oracles_guest_vram(c->guest, live_bank);
        if (!vram || memcmp(vram + live_tile * 16u, c->capture_tiles + captured_bank * ORACLES_GHOST_TILE_BYTES + captured_tile * 16u, 32u) != 0) return 0;
    }
    return 1;
}

static void note_tiles_wrong(OraclesObjectAnimationCheck *c, const captured_object *o, uint16_t pose)
{
    const size_t used = strlen(c->tiles_wrong_list);
    if (used + 32 < sizeof c->tiles_wrong_list)
        snprintf(c->tiles_wrong_list + used, sizeof c->tiles_wrong_list - used, "%s%u:%02x %u.%02x:%02x@%04x", used ? " " : "",
                 c->capture_group, c->capture_room, o->record.kind, o->record.id, o->record.subid, pose);
}

/* The room's capture is over (the room left, or the run): its poses' verdicts counted. */
void oac_finish_capture(OraclesObjectAnimationCheck *c)
{
    if (!c->have_capture) return;
    c->have_capture = 0;
    for (unsigned k = 0; k < c->captured_count; k++) {
        const captured_object *o = &c->captured[k];
        unsigned equal = 0, different = 0, unseen = 0;
        for (unsigned p = 0; p < o->pose_count; p++) {
            if (o->pose_verdict[p] == 1u) equal++;
            else if (o->pose_verdict[p] == 2u) { different++; note_tiles_wrong(c, o, o->poses[p]); }
            else unseen++;
        }
        if (!o->paired) c->captured_unpaired++;
        c->poses_checked += equal + different;
        c->poses_equal += equal;
        c->poses_different += different;
        c->poses_unseen += unseen;
        if (c->tsv)
            fprintf(c->tsv, "%u\t%u:%02x\t%u\t%02x\t%02x\t%u\t%u\t%u\t%u\t%d\t%u\t%u\t%u\t%d\n", c->capture_frame, c->capture_group, c->capture_room,
                    o->record.kind, o->record.id, o->record.subid, o->record.number, o->pose_count, o->period, o->changes, o->paired, equal, different, unseen,
                    oracles_object_animation_animated(oracles_guest_tables(c->guest), &o->record));
    }
    c->captured_count = 0;
}

/* The software PPU on the capture: the 128 lines of the game area, and the lines an object's poses cover. */
static void time_redraws(OraclesObjectAnimationCheck *c, const OraclesGhostResult *frozen)
{
    static uint8_t vram[0x4000];
    static uint32_t out[ORACLES_PPU_WIDTH * ORACLES_PPU_HEIGHT];
    memcpy(vram, frozen->tiles, ORACLES_GHOST_TILE_BYTES);
    memcpy(vram + 0x1800u, frozen->bg_map, 0x800u);
    memcpy(vram + 0x2000u, frozen->tiles + ORACLES_GHOST_TILE_BYTES, ORACLES_GHOST_TILE_BYTES);
    memcpy(vram + 0x3800u, frozen->bg_map + 0x800u, 0x800u);
    OraclesPpuInput in;
    memset(&in, 0, sizeof in);
    in.vram = vram;
    in.oam = frozen->oam;
    in.bg_palettes = frozen->bg_palettes;
    in.obj_palettes = frozen->obj_palettes;
    const OraclesPpuRegs regs = { (uint8_t)(frozen->regs3[0] | 0x80u | 0x02u), frozen->regs3[1], frozen->regs3[2], frozen->regs3[3], frozen->regs3[4] };
    for (unsigned ly = 0; ly < ORACLES_PPU_HEIGHT; ly++) in.lines[ly] = regs;
    double start = ec_monotonic_us();
    for (unsigned n = 0; n < RENDERS_TIMED; n++) oracles_ppu_render_wide_lines(&in, out, ORACLES_PPU_WIDTH, AREA_FIRST_LINE, ORACLES_PPU_HEIGHT);
    c->full_us += ec_monotonic_us() - start;
    c->fulls_timed += RENDERS_TIMED;
    for (unsigned k = 0; k < c->captured_count; k++) {
        const captured_object *o = &c->captured[k];
        if (o->pose_count < 2u) continue;
        /* Its sprites on the screen: the tags of the capture; the block's first sprite gives the object's origin. */
        int first_entry = -1;
        for (unsigned j = 0; j < frozen->tag_count && first_entry < 0; j++)
            if (!frozen->tags[j].terrain_effect && frozen->tags[j].kind == o->record.kind && frozen->tags[j].slot == o->record.slot && frozen->tags[j].count)
                first_entry = frozen->tags[j].first;
        unsigned count = 0;
        const uint8_t *s = pose_block(c, o->record.oam_data_address, &count);
        if (first_entry < 0 || first_entry >= 40 || !s || !count) continue;
        const int origin = (int)frozen->oam[first_entry * 4] - (int)(int8_t)s[0];
        int top = 1000, bottom = -1000;
        for (unsigned p = 0; p < o->pose_count; p++) {
            const uint8_t *b = pose_block(c, o->poses[p], &count);
            for (unsigned i = 0; b && i < count; i++) {
                const int y = origin + (int8_t)b[i * 4u] - 16;
                if (y < top) top = y;
                if (y + 16 > bottom) bottom = y + 16;
            }
        }
        if (top < (int)AREA_FIRST_LINE) top = (int)AREA_FIRST_LINE;
        if (bottom > ORACLES_PPU_HEIGHT) bottom = ORACLES_PPU_HEIGHT;
        if (bottom <= top) continue;
        start = ec_monotonic_us();
        for (unsigned n = 0; n < RENDERS_TIMED; n++) oracles_ppu_render_wide_lines(&in, out, ORACLES_PPU_WIDTH, (unsigned)top, (unsigned)bottom);
        c->redraw_us += ec_monotonic_us() - start;
        c->redraw_lines += (double)(bottom - top) * RENDERS_TIMED;
        c->redraws_timed += RENDERS_TIMED;
    }
}
void oracles_object_animation_check_captured(OraclesObjectAnimationCheck *c, const OraclesGhostResult *frozen, uint32_t frame)
{
    if (!c || !frozen) return;
    oac_end_tracks(c);   /* the objects of the room entered are followed afresh, and paired with the capture's */
    oac_finish_capture(c);
    const OraclesGuestTables *t = oracles_guest_tables(c->guest);
    c->captures++;
    c->have_capture = 1;
    c->capture_group = frozen->group;
    c->capture_room = frozen->room;
    c->capture_frame = frame;
    memcpy(c->capture_tiles, frozen->tiles, sizeof c->capture_tiles);
    c->captured_count = 0;
    double rate = 0.0;
    for (unsigned k = 0; k < frozen->object_count; k++) {
        const OraclesObjectRecord drawn = oracles_objects_as_drawn(&frozen->objects[k]), *r = &drawn;   /* as the view takes it */
        if (!oac_shown(t, r)) continue;
        captured_object *o = &c->captured[c->captured_count];
        memset(o, 0, sizeof *o);
        o->record = *r;
        OraclesObjectAnimation a;
        oracles_object_animation_from_record(&a, r);
        o->pose_count = oracles_object_animation_poses(&a, t, c->rom, c->rom_size, o->poses, POSES, &o->period, &o->changes);
        if (!o->pose_count) { c->captured_unreadable++; continue; }
        c->captured_count++;
        c->captured_shown[r->kind - 1u]++;
        if (o->pose_count > 1u) c->captured_multi_pose[r->kind - 1u]++;
        if (o->pose_count > 1u && oracles_object_animation_animated(t, r)) c->captured_animated[r->kind - 1u]++;
        else if (o->pose_count > 1u && r->kind != ORACLES_OBJECT_KIND_ENEMY) {
            const size_t used = strlen(c->probe_frozen_list);
            if (used + 24u < sizeof c->probe_frozen_list)
                snprintf(c->probe_frozen_list + used, sizeof c->probe_frozen_list - used, "%s%u:%02x %u.%02x:%02x", used ? " " : "", frozen->group, frozen->room,
                         r->kind, r->id, r->subid);
        }
        if (o->period) rate += (double)o->changes * FRAMES_PER_SECOND / (double)o->period;
        /* The step's cost, on a real animation. */
        if (o->pose_count > 1u) {
            OraclesObjectAnimation s = a;
            const double start = ec_monotonic_us();
            for (unsigned n = 0; n < STEPS_TIMED; n++) oracles_object_animation_step(&s, t, c->rom, c->rom_size);
            c->step_us += ec_monotonic_us() - start;
            c->steps_timed += STEPS_TIMED;
        }
    }
    if (rate > c->rate_max) { c->rate_max = rate; c->rate_max_group = frozen->group; c->rate_max_room = frozen->room; }
    time_redraws(c, frozen);
}

unsigned oac_find_captured(const OraclesObjectAnimationCheck *c, const OraclesObjectRecord *r)
{
    if (!c->have_capture || c->capture_group != c->group || c->capture_room != c->room) return 0;
    for (unsigned k = 0; k < c->captured_count; k++) {
        const OraclesObjectRecord *o = &c->captured[k].record;
        if (o->kind == r->kind && o->number == r->number && o->created_id == r->created_id && o->created_subid == r->created_subid
            && o->created_var03 == r->created_var03) return k + 1u;
    }
    return 0;
}
/* The tiles of the pose the live object shows, against the capture's, and its first state against the capture's. */
void oac_against_capture(OraclesObjectAnimationCheck *c, track *tr, const OraclesObjectRecord *r, const OraclesObjectAnimation *now)
{
    if (!tr->captured) return;
    captured_object *o = &c->captured[tr->captured - 1u];
    o->paired = 1;
    if (!o->entry_judged && c->updates_in_room == 1u) {
        /* The first update of play after the scroll: the game animates from the state it froze, the capture's. */
        o->entry_judged = 1;
        OraclesObjectAnimation captured, once;
        oracles_object_animation_from_record(&captured, &o->record);
        once = captured;
        const int stepped = oracles_object_animation_step(&once, oracles_guest_tables(c->guest), c->rom, c->rom_size) >= 0 && oac_same_state(&once, now);
        if (oac_same_state(&captured, now) || stepped) c->entry_equal++;
        else {
            c->entry_different++;
            if (c->misses)
                fprintf(c->misses, "-\t%u:%02x\t%u\t%02x\t%02x\t%u\t%d\tentry\t%02x,%04x,%04x\t%02x,%04x,%04x\t%02x,%04x,%04x\n", c->group, c->room, r->kind, r->id,
                        r->subid, r->slot, tr->moved, captured.counter, captured.pointer, captured.oam_data_address, once.counter, once.pointer, once.oam_data_address,
                        now->counter, now->pointer, now->oam_data_address);
        }
    }
    for (unsigned p = 0; p < o->pose_count; p++) {
        if (o->poses[p] != now->oam_data_address || o->pose_verdict[p] == 1u) continue;
        o->pose_verdict[p] = pose_tiles_equal(c, o->poses[p], &o->record, r) ? 1u : 2u;
    }
}
