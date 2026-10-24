/* An object's animation advanced from the game's data, without a ROM: the
 * counter counted down, the entry read at zero and the pose its index names
 * in the OAM data table of the object's id, with the bits of each kind; the
 * loop marker's offset back, read once; a pose held 256 calls by a duration
 * of zero; the loop of poses and its period; an animation chosen by the
 * object's code told from a step; a kind whose tables are not resolved, or
 * data out of its bank, left as it was; one call of the step told from
 * other changes, and the objects a neighbour animates by it; a record as
 * its capture draws it; a
 * pose's own sprites at a place given.
 * The data is built here in the game's format (interactionAnimations.s,
 * interactionOamData.s), in a buffer standing for the ROM. */
#include "object_animation.h"
#include "object_sprites.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); } } while (0)

#define ROM_SIZE 0x10000u
#define BANK 2u
#define ANIMATIONS 0x4000u   /* the animation table: a pointer per id to its list of animations */
#define OAM_TABLE 0x4100u    /* the OAM data table: a pointer per id to its list of blocks */
#define LISTS 0x4200u
#define DATA 0x4400u
#define ID 3u

static uint8_t rom[ROM_SIZE];
static OraclesGuestTables tables;

static uint8_t *at(unsigned addr) { return rom + BANK * 0x4000u + (addr - 0x4000u); }
static void word(unsigned addr, unsigned value) { at(addr)[0] = (uint8_t)value; at(addr)[1] = (uint8_t)(value >> 8); }

/* Animation 0: an entry outside the loop (pose 0 for 2 calls), then a loop of
 * pose 2 for 3 calls and pose 4 for 1 call, back by the marker.  Animation 1:
 * pose 6 held by a duration of zero, then the marker back to it.  The blocks
 * the indices name: $5123, $9134 (its two high bits cut, $40 added for an
 * interaction), $4145, $e156. */
static void build(void)
{
    memset(rom, 0, sizeof rom);
    memset(&tables, 0xff, sizeof tables);
    tables.interaction_animation_table = (OraclesGuestSym){ BANK, ANIMATIONS };
    tables.interaction_oam_data_table = (OraclesGuestSym){ BANK, OAM_TABLE };
    tables.enemy_animation_table = (OraclesGuestSym){ BANK, ANIMATIONS };
    tables.enemy_oam_data_table = (OraclesGuestSym){ BANK, OAM_TABLE };
    tables.part_animation_table = (OraclesGuestSym){ ORACLES_GUEST_ABSENT, 0 };
    tables.part_oam_data_table = (OraclesGuestSym){ ORACLES_GUEST_ABSENT, 0 };
    word(ANIMATIONS + 2u * ID, LISTS);
    word(LISTS, DATA);
    word(LISTS + 2u, DATA + 0x20u);
    static const uint8_t first[] = { 2, 0, 0x11, 3, 2, 0x22, 1, 4, 0x33, 0xff, 0 };
    memcpy(at(DATA), first, sizeof first);
    at(DATA + 10u)[0] = (uint8_t)((DATA + 3u) - (DATA + 10u) - 0xff00u);   /* hl = marker + 1 + $ff00 + c = the loop's first entry */
    static const uint8_t second[] = { 0, 6, 0x44, 0xff, 0 };
    memcpy(at(DATA + 0x20u), second, sizeof second);
    at(DATA + 0x24u)[0] = (uint8_t)((DATA + 0x20u) - (DATA + 0x24u) - 0xff00u);
    word(OAM_TABLE + 2u * ID, LISTS + 0x100u);
    word(LISTS + 0x100u, 0x5123u);
    word(LISTS + 0x102u, 0x9134u);
    word(LISTS + 0x104u, 0x4145u);
    word(LISTS + 0x106u, 0xe156u);
}

/* The object as xxxSetAnimation leaves it with animation `n`: its first entry read. */
static OraclesObjectAnimation set(unsigned kind, unsigned n)
{
    OraclesObjectAnimation a;
    memset(&a, 0, sizeof a);
    a.kind = (uint8_t)kind;
    a.id = ID;
    a.counter = 1;
    a.pointer = (uint16_t)(n ? DATA + 0x20u : DATA);
    CHECK(oracles_object_animation_step(&a, &tables, rom, ROM_SIZE) == 1);
    return a;
}

int main(void)
{
    build();
    CHECK(oracles_object_animation_resolved(&tables, ORACLES_OBJECT_KIND_INTERACTION));
    CHECK(oracles_object_animation_resolved(&tables, ORACLES_OBJECT_KIND_ENEMY));
    CHECK(!oracles_object_animation_resolved(&tables, ORACLES_OBJECT_KIND_PART));

    /* The entries in turn: pose, counter, parameter, pointer past the entry; the counter counted down between. */
    OraclesObjectAnimation a = set(ORACLES_OBJECT_KIND_INTERACTION, 0);
    CHECK(a.oam_data_address == 0x5123u && a.counter == 2 && a.parameter == 0x11 && a.pointer == DATA + 3u);
    CHECK(oracles_object_animation_step(&a, &tables, rom, ROM_SIZE) == 0 && a.counter == 1 && a.oam_data_address == 0x5123u);
    CHECK(oracles_object_animation_step(&a, &tables, rom, ROM_SIZE) == 1);
    CHECK(a.oam_data_address == 0x5134u && a.counter == 3 && a.parameter == 0x22 && a.pointer == DATA + 6u);
    for (unsigned n = 0; n < 2u; n++) CHECK(oracles_object_animation_step(&a, &tables, rom, ROM_SIZE) == 0);
    CHECK(oracles_object_animation_step(&a, &tables, rom, ROM_SIZE) == 1 && a.oam_data_address == 0x4145u && a.counter == 1 && a.pointer == DATA + 9u);
    /* The marker: back to the loop's first entry, read at once. */
    CHECK(oracles_object_animation_step(&a, &tables, rom, ROM_SIZE) == 1 && a.oam_data_address == 0x5134u && a.counter == 3 && a.pointer == DATA + 6u);

    /* An enemy: the same data, the two high bits cut and no bit added. */
    OraclesObjectAnimation e = set(ORACLES_OBJECT_KIND_ENEMY, 0);
    CHECK(e.oam_data_address == 0x1123u);
    for (unsigned n = 0; n < 2u; n++) oracles_object_animation_step(&e, &tables, rom, ROM_SIZE);
    CHECK(e.oam_data_address == 0x1134u);

    /* The poses from the first entry: the one outside the loop, then the loop's two; a loop of 4 calls, 2 changes. */
    uint16_t poses[ORACLES_OBJECT_ANIMATION_POSES];
    unsigned period = 0, changes = 0;
    OraclesObjectAnimation p = set(ORACLES_OBJECT_KIND_INTERACTION, 0);
    CHECK(oracles_object_animation_poses(&p, &tables, rom, ROM_SIZE, poses, ORACLES_OBJECT_ANIMATION_POSES, &period, &changes) == 3u);
    CHECK(poses[0] == 0x5123u && poses[1] == 0x5134u && poses[2] == 0x4145u && period == 4u && changes == 2u);

    /* A duration of zero holds the pose 256 calls; a loop of one pose has no change. */
    OraclesObjectAnimation h = set(ORACLES_OBJECT_KIND_INTERACTION, 1);
    CHECK(h.oam_data_address == 0x6156u && h.counter == 0);
    unsigned calls = 0;
    int rc;
    do { rc = oracles_object_animation_step(&h, &tables, rom, ROM_SIZE); calls++; } while (rc == 0);
    CHECK(rc == 1 && calls == 256u && h.pointer == DATA + 0x23u && h.oam_data_address == 0x6156u);
    CHECK(oracles_object_animation_poses(&h, &tables, rom, ROM_SIZE, poses, ORACLES_OBJECT_ANIMATION_POSES, &period, &changes) == 1u);
    CHECK(period == 256u && changes == 0u);

    /* An animation chosen by the code, told from a step of the loop. */
    CHECK(oracles_object_animation_is_set(&h, DATA + 3u, &tables, rom, ROM_SIZE));
    CHECK(oracles_object_animation_is_set(&h, DATA + 0x23u, &tables, rom, ROM_SIZE));
    CHECK(!oracles_object_animation_is_set(&h, DATA + 6u, &tables, rom, ROM_SIZE));

    /* Not resolved, or a pointer out of the bank: left as it was. */
    OraclesObjectAnimation q = a;
    q.kind = ORACLES_OBJECT_KIND_PART;
    q.counter = 1;
    const OraclesObjectAnimation q0 = q;
    CHECK(oracles_object_animation_step(&q, &tables, rom, ROM_SIZE) == -1 && memcmp(&q, &q0, sizeof q) == 0);
    OraclesObjectAnimation bad = a;
    bad.counter = 1;
    bad.pointer = 0x8000u;
    const OraclesObjectAnimation bad0 = bad;
    CHECK(oracles_object_animation_step(&bad, &tables, rom, ROM_SIZE) == -1 && memcmp(&bad, &bad0, sizeof bad) == 0);
    CHECK(oracles_object_animation_poses(&bad, &tables, rom, ROM_SIZE, poses, ORACLES_OBJECT_ANIMATION_POSES, &period, &changes) == 0u);

    /* One call of the step from a state, told from what a code that animates otherwise leaves (the capture's probe):
     * the counter counted down, the entry read at zero; not a state left as it was (a code that never animates the
     * object, or holds it), set back to its animation's start every update (Vasu's snakes), stepped twice, or
     * another object in the slot. */
    const OraclesObjectAnimation start = set(ORACLES_OBJECT_KIND_INTERACTION, 0);
    OraclesObjectAnimation next = start;
    oracles_object_animation_step(&next, &tables, rom, ROM_SIZE);
    CHECK(oracles_object_animation_one_step(&start, &next, &tables, rom, ROM_SIZE));
    OraclesObjectAnimation read = next;
    oracles_object_animation_step(&read, &tables, rom, ROM_SIZE);
    CHECK(oracles_object_animation_one_step(&next, &read, &tables, rom, ROM_SIZE) && read.oam_data_address != next.oam_data_address);
    CHECK(!oracles_object_animation_one_step(&start, &start, &tables, rom, ROM_SIZE));
    CHECK(!oracles_object_animation_one_step(&next, &start, &tables, rom, ROM_SIZE));
    CHECK(!oracles_object_animation_one_step(&start, &read, &tables, rom, ROM_SIZE));
    OraclesObjectAnimation other = next;
    other.id = ID + 1u;
    CHECK(!oracles_object_animation_one_step(&start, &other, &tables, rom, ROM_SIZE));
    OraclesObjectAnimation part = start, part_next = next;
    part.kind = part_next.kind = ORACLES_OBJECT_KIND_PART;
    CHECK(!oracles_object_animation_one_step(&part, &part_next, &tables, rom, ROM_SIZE));   /* its tables not resolved here */

    /* Which objects a neighbour animates: interactions and parts of a kind resolved whose code advanced their
     * animation so at the probe; never an enemy. */
    OraclesObjectRecord r;
    memset(&r, 0, sizeof r);
    r.kind = ORACLES_OBJECT_KIND_INTERACTION;
    r.id = r.created_id = 0x89u;
    CHECK(!oracles_object_animation_animated(&tables, &r));
    r.animated_by_code = 1;
    CHECK(oracles_object_animation_animated(&tables, &r));
    r.kind = ORACLES_OBJECT_KIND_ENEMY;
    CHECK(!oracles_object_animation_animated(&tables, &r));
    r.kind = ORACLES_OBJECT_KIND_PART;
    CHECK(!oracles_object_animation_animated(&tables, &r));   /* its tables not resolved here */
    /* The probe asks about interactions and parts alone: a capture of enemies runs none. */
    CHECK(oracles_object_animation_probed(ORACLES_OBJECT_KIND_INTERACTION) && oracles_object_animation_probed(ORACLES_OBJECT_KIND_PART));
    CHECK(!oracles_object_animation_probed(ORACLES_OBJECT_KIND_ENEMY) && !oracles_object_animation_probed(0u));

    /* A record as the frame its capture keeps draws it: the place and the look noted then, not the latched ones. */
    OraclesObjectRecord latched;
    memset(&latched, 0, sizeof latched);
    latched.yh = 0x10u;
    latched.drawn_yh = 0x18u;
    latched.drawn.visible = 0x82u;
    latched.drawn.anim_pointer = 0x4321u;
    OraclesObjectRecord drawn = oracles_objects_as_drawn(&latched);
    CHECK(drawn.yh == 0x18u && drawn.visible == 0x82u && drawn.anim_pointer == 0x4321u && latched.visible == 0u);
    latched.anim_pointer = 0x4400u;   /* an animation set when it was latched: the one the game goes on from */
    drawn = oracles_objects_as_drawn(&latched);
    CHECK(drawn.yh == 0x18u && drawn.visible == 0x82u && drawn.anim_pointer == 0x4400u);

    /* A pose's own sprites at a place given: the block at $5123 (bank 2, its high bits adding 1 to the first bank),
     * offsets added to the position less the camera (y 16 down for the OAM), tiles to the base, flags xored; a sprite
     * from x $a8 on is outside the window. */
    tables.oam_data_bank = 1;
    static const uint8_t block[] = { 2, 0x10, 0x08, 0x02, 0x00, 0x10, 0x58, 0x04, 0x20 };
    memcpy(at(0x5123u), block, sizeof block);
    const OraclesObjectPlace place = { 0x40, 0x50, 0x00, 0x00, 0x00, 0x03, 0x20 };
    OraclesObjectSprite s[ORACLES_OBJECT_SPRITES_MAX];
    CHECK(oracles_object_sprites_pose(&tables, rom, ROM_SIZE, &place, 0x5123u, s, ORACLES_OBJECT_SPRITES_MAX) == 2);
    CHECK(s[0].oam_y == 0x60 && s[0].oam_x == 0x58 && s[0].tile == 0x22 && s[0].attr == 0x03 && s[0].in_window && s[0].effect == ORACLES_SPRITE_OWN);
    CHECK(s[1].oam_y == 0x60 && s[1].oam_x == 0xa8 && s[1].tile == 0x24 && s[1].attr == 0x23 && !s[1].in_window);
    const OraclesObjectPlace raised = { 0x40, 0x50, 0x01, 0x10, 0x00, 0x03, 0x20 };   /* a puddle's pixel, the camera 16 down */
    CHECK(oracles_object_sprites_pose(&tables, rom, ROM_SIZE, &raised, 0x5123u, s, ORACLES_OBJECT_SPRITES_MAX) == 2 && s[0].oam_y == 0x51);
    CHECK(oracles_object_sprites_pose(&tables, rom, 0x9000u, &place, 0x5123u, s, ORACLES_OBJECT_SPRITES_MAX) == -1);   /* out of the ROM */

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("object animation: ok\n");
    return 0;
}
