/* The view's cache: the Moonrise profile's key read through its own tables and its loaded tileset's identity, and a room waiting for a parent that is no start; no game ROM or emulation is exercised. */
#include "view_internal.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); } } while (0)

static void check_loaded_tileset_identity(OraclesEnhancedView *view)
{
    const OraclesRomInfo identity = { .game = ORACLES_GAME_AGES,
        .revision = ORACLES_ROM_REVISION_MOONRISE_1_0_6, .known = 1 };
    view->guest = oracles_guest_attach(view->core, oracles_compat_find(&identity));
    CHECK(view->guest != NULL);
    if (!view->guest) return;
    const OraclesGuestTables *t = oracles_guest_tables(view->guest);
    CHECK(t->tileset_gfx.bank == t->loaded_tileset_unique_gfx.bank);
    CHECK(t->tileset_gfx.addr == t->loaded_tileset_unique_gfx.addr);
    uint8_t *wram = oracles_guest_wram_writable(view->guest, 0);
    CHECK(wram != NULL);
    if (wram) {
        /* Only the loaded expanded identity changes; all other state,
         * including the obsolete graphics header, remains identical. */
        uint8_t *loaded = wram + t->loaded_tileset_unique_gfx.addr - 0xc000u;
        *loaded = 2;
        ev_track_animated_tiles(view);
        CHECK(view->animated_tileset_gfx == 2);
        view->animated[0] = 1;
        *loaded = 3;
        ev_track_animated_tiles(view);
        CHECK(view->animated_tileset_gfx == 3);
        CHECK(view->animated[0] == 0);
    }
    oracles_guest_detach(view->guest);
}

extern const OraclesGuestTables oracles_guest_tables_moonrise;

static int key_has(const OraclesGhostKeyRange *r, unsigned n, uint16_t address)
{
    for (unsigned i = 0; i < n; i++) if (address >= r[i].address && address < r[i].address + r[i].length) return 1;
    return 0;
}

/* Moonrise keys its neighbours as the originals do, through its own tables:
 * an address moved in the Moonrise table moves in its key, whatever Ages has. */
static void check_moonrise_traced_key(void)
{
    const OraclesRomInfo identity = { .game = ORACLES_GAME_AGES,
        .revision = ORACLES_ROM_REVISION_MOONRISE_1_0_6, .known = 1 };
    const OraclesCompatProfile *moonrise = oracles_compat_find(&identity);
    CHECK(moonrise != NULL && oracles_compat_guest_tables(moonrise) == &oracles_guest_tables_moonrise);
    if (!moonrise) return;
    OraclesGuestTables moved = oracles_guest_tables_moonrise;
    const uint16_t before = moved.toggle_blocks_state.addr, elsewhere = 0xd3f0u;   /* in no range of the key */
    moved.toggle_blocks_state.addr = elsewhere;
    OraclesCompatProfile profile = *moonrise;
    profile.guest_tables = &moved;
    OraclesGhostKeyRange own[ORACLES_GHOST_KEY_RANGES], shifted[ORACLES_GHOST_KEY_RANGES];
    const unsigned n = oracles_ghost_key_ranges(moonrise, own, ORACLES_GHOST_KEY_RANGES);
    const unsigned m = oracles_ghost_key_ranges(&profile, shifted, ORACLES_GHOST_KEY_RANGES);
    CHECK(n > 0 && key_has(own, n, before) && !key_has(own, n, elsewhere));
    CHECK(m == n && key_has(shifted, m, elsewhere) && !key_has(shifted, m, before));
}

/* A room whose parent's state is no start (Ages' grove 1:38 in the past,
 * whose entry starts the Moblins' scene), marked whether its own run was
 * refused or it found the parent marked by a sibling: settled for the fill
 * around that reference room, the parent run again for it at the pace of a
 * failure from the first time, and marked afresh around another one. */
static void check_wait_for_start(OraclesEnhancedView *view)
{
    entry *grove = &view->slots[0], *beyond = &view->slots[1];
    grove->used = beyond->used = 1;
    grove->group = beyond->group = 1;
    grove->room = 0x38; beyond->room = 0x37;
    view->observer.ref_group = 1; view->observer.ref_room = 0x39;
    view->frame = 1000;
    ev_wait_for_start(view, beyond, grove);
    CHECK(beyond->waits_start && beyond->waits_since == 1000 && beyond->waits_parent == 0x138u && beyond->waits_ref == 0x139u);
    CHECK(oracles_enhanced_fill_settled(beyond->valid, beyond->in_flight, beyond->failed, beyond->waits_start, beyond->waits_ref, 0x139u));
    view->frame = 1200;
    ev_wait_for_start(view, beyond, grove);   /* found so again: the clock runs from the first time */
    CHECK(beyond->waits_since == 1000);
    CHECK(!oracles_enhanced_parent_rerun_due(beyond->waits_start, beyond->waits_parent, beyond->waits_ref, beyond->waits_since, 0x138u, 0x139u, view->frame));
    view->observer.ref_room = 0x48;            /* Link below the grove: the mark of the other room no longer holds */
    CHECK(!oracles_enhanced_fill_settled(beyond->valid, beyond->in_flight, beyond->failed, beyond->waits_start, beyond->waits_ref, 0x148u));
    CHECK(oracles_enhanced_parent_rerun_due(beyond->waits_start, beyond->waits_parent, beyond->waits_ref, beyond->waits_since, 0x138u, 0x148u, view->frame));
    ev_wait_for_start(view, beyond, grove);
    CHECK(beyond->waits_since == 1200 && beyond->waits_ref == 0x148u);
    ev_drop_entry(beyond);
    CHECK(!beyond->waits_start);
    memset(view->slots, 0, 2 * sizeof *view->slots);
    view->frame = 0;
    view->observer.ref_group = 0; view->observer.ref_room = 0;
}

int main(void)
{
    check_moonrise_traced_key();
    const OraclesRomInfo identity = { .game = ORACLES_GAME_AGES,
        .revision = ORACLES_ROM_REVISION_AGES_US, .known = 1 };
    const OraclesCompatProfile *profile = oracles_compat_find(&identity);

    const size_t size = 1024u * 1024u;
    uint8_t *rom = calloc(size, 1);
    OraclesEnhancedView *view = calloc(1, sizeof *view);
    entry *slots = calloc(NORMAL_SLOTS, sizeof *slots);
    if (!rom || !view || !slots) { free(rom); free(view); free(slots); return 1; }
    view->slots = slots;
    view->slot_count = NORMAL_SLOTS;
    check_wait_for_start(view);
    rom[0x143] = 0xc0; rom[0x147] = 0x1b; rom[0x148] = 0x05; rom[0x149] = 0x02;
    const OraclesCoreOptions options = { 0, 0, ORACLES_CORE_SAMEBOY, 0 };
    view->core = oracles_core_create(rom, size, &options);
    view->guest = view->core ? oracles_guest_attach(view->core, profile) : NULL;
    CHECK(view->guest != NULL);
    if (view->guest) {
        oracles_guest_detach(view->guest);
        check_loaded_tileset_identity(view);
    }
    oracles_core_destroy(view->core);
    free(view);
    free(slots);
    free(rom);
    if (failures) return 1;
    puts("test_cache_key: ok");
    return 0;
}
