# Routes

A route is a recording of a player's inputs, frame by frame, with the header that says which game, which ROM (by SHA-1) and which options it was played with; its starting save sits beside it (`.route.sram`, a player's save, no data of the ROM). Replayed through the harness, a route drives the game exactly as it was played, and the checks of its directory's `checks.tsv` say what must hold on every replay.

The format, the harness and the suite are described in [`docs/ROUTES.md`](../docs/ROUTES.md).

## Replaying the suite

Put your ROMs in one directory and name it in `ORACLES_ROM_DIR`; the routes find their ROM by its SHA-1. A fan game's route names the SHA-1 of the patched image: put the game's BPS patch beside the base ROM it applies to, and the suite applies it, or put the patched image (a `.gbc`) there. Then, from a build of the repository:

```
ORACLES_ROM_DIR=/path/to/roms ctest --test-dir build -R '^oracles-routes$'
ORACLES_ROM_DIR=/path/to/roms python3 tools/check_routes.py --harness build/oracles-harness
```

Without `ORACLES_ROM_DIR` the test is skipped. `tools/check_routes.py` reads every `checks.tsv` under `routes/`, and replays on SameBoy unless given `--core mgba` (`oracles-routes-mgba` in `ctest`), each row saying the core it holds on; `--only TEXT` replays the rows whose route or label contains the text (`ages/intro`, `seasons-dungeon-enhanced-zoom`).

## The routes

The routes recorded before SameBoy's joypad bouncing was cut (`ages/advanced`, `ages/dungeon-sideview`, `ages/overworld`, `ages/underwater`, `seasons/continuous-transitions`, `seasons/hotkeys-equip`, `seasons/lost-woods`, `seasons/neighbour-objects`, `seasons/overworld`) were rewritten by `tools/debounce_route.py`: in the frames the game read the keys, their keys are those it read on SameBoy, bouncing included; in the others, those recorded. Each replays the same game without the bouncing, and keeps its recording as `#recorded` comments. `ages/overworld-continuous.route` is the same session's keys as the game read them with `--continuous-transitions`.

| Route | Covers | Modes |
| --- | --- | --- |
| `ages/overworld.route` | the native renderer on Ages: Lynna, interiors, warps | faithful, faithful-cc, enhanced, enhanced-threaded, enhanced-zoom, enhanced-zoom-threaded |
| `ages/overworld-continuous.route` | the same session as the game plays it with `--continuous-transitions` (its keys as the game read them then: `tools/debounce_route.py`) | enhanced |
| `ages/advanced.route` | the past, the sea and the rooms under water, a dungeon of the past | faithful, faithful-cc, enhanced, enhanced-threaded |
| `ages/dungeon-sideview.route` | a dungeon of the present and its side-view section | faithful, faithful-cc, enhanced, enhanced-threaded |
| `ages/underwater.route` | under water | faithful, faithful-cc, enhanced, enhanced-threaded, enhanced-zoom, enhanced-zoom-threaded |
| `ages/intro.route` | Ages' introduction and cutscenes in the drawn-back view | faithful, enhanced-zoom, enhanced-zoom-threaded |
| `ages/hotkeys-use.route` | item hotkeys in `use` mode, a session equal to its replay | faithful, enhanced, enhanced-threaded, enhanced-zoom |
| `ages/zoom-out.route` | the drawn-back view on Ages: dialogues, time travel, interiors | faithful, enhanced-zoom, enhanced-zoom-threaded |
| `ages/mods-claw-game.route` | Lua mods: a house, its characters, a minigame, `mod.storage`, the save | faithful, enhanced |
| `ages/swim.route` | the continuous transitions through a swim: the sea with the mermaid suit, a dive and the sea floor, an underwater building | enhanced-zoom, enhanced-zoom-threaded |
| `ages/mermaid-hold.route` | the swim's hold with the mermaid suit: `ages/underwater.route`'s start, then directions held on the sea floor, written by hand; with `--continuous-swim --continuous-diagonal`, the diagonal on foot, the route staying on land | faithful, enhanced |
| `ages/seafloor-diagonal.route` | the transitions' diagonal on the sea floor with the mermaid suit, tapped and held with the swim's hold, across the edges of 2:b7, 2:b6, 2:a6 and 2:a7, written by hand | faithful, enhanced |
| `ages/diagonal-crossings.route` | the transitions' diagonal on foot: the edges of 0:6c, 0:6b and 0:5c crossed diagonally, walls across included, written by hand | faithful, enhanced, enhanced-threaded, enhanced-zoom |
| `ages/swim-diagonal.route` | the transitions' diagonal swimming with the mermaid suit across the edge between 0:c4 and 0:b4, written by hand | faithful, enhanced, enhanced-threaded, enhanced-zoom |
| `ages/stairs-diagonal.route` | Link's sprite steady on the screen on stairs and walking diagonally, at every size of the view and with both camera profiles, written by hand | faithful, enhanced, enhanced-threaded, enhanced-zoom |
| `ages/save.route`, `ages/load.route` | the game's own save, written by hand after `ages/overworld.route`'s keys: the same cartridge RAM on both cores, and the file opened the same on both | faithful |
| `ages/shop.route` | a purchase: `ages/overworld.route`'s keys into Lynna's shop, then hearts bought for 10 rupees, written by hand; with `--cheats rupees` it costs nothing | faithful |
| `ages/shop-cheat.route`, `ages/shop-cheat-off.route` | `ages/shop.route`'s keys with `cheat` lines, written by hand: the cheat of the rupees turned on at frame 6228, the frame the purchase is paid at, which costs nothing, then off; on from the start, turned off at that frame, which costs 10 | faithful |
| `seasons/overworld.route` | Enhanced on Seasons: Holodrum, vertical scrolls, interiors, a dungeon | faithful, faithful-cc, enhanced, enhanced-threaded, enhanced-zoom, enhanced-zoom-threaded |
| `seasons/continuous-transitions.route` | continuous transitions, Subrosia, white fades, large rooms | faithful, faithful-cc, enhanced, enhanced-threaded |
| `seasons/neighbour-objects.route` | the neighbouring rooms' objects handed over to the live room | faithful, enhanced |
| `seasons/lost-woods.route` | rooms whose transitions the game routes itself | faithful, enhanced, enhanced-threaded, enhanced-zoom, enhanced-zoom-threaded |
| `seasons/hotkeys-equip.route` | item hotkeys in `equip` mode, the A target and its variants | faithful, enhanced, enhanced-threaded |
| `seasons/scroll-animation.route` | animated tiles during a scroll | faithful, enhanced-zoom, enhanced-zoom-threaded |
| `seasons/intro.route` | Seasons' introduction and cutscenes in the drawn-back view | faithful, enhanced-zoom, enhanced-zoom-threaded |
| `seasons/dungeon.route` | a dungeon in the drawn-back view, large rooms whole | faithful, enhanced-zoom, enhanced-zoom-threaded |
| `seasons/swim.route` | the continuous transitions through a swim with the flippers, strokes carrying Link across an edge | faithful, enhanced-zoom, enhanced-zoom-threaded |
| `seasons/swim-hold.route` | the swim's hold with the flippers: `seasons/swim.route`'s start, Link entering the water, then a direction held alone, with A, and nothing, written by hand (SameBoy only) | faithful, enhanced-zoom |
| `seasons/shop.route` | a purchase: `seasons/overworld.route`'s keys to Horon's shop, then a bomb dropped and its blast taken outside, and ten bombs bought for 20 rupees, written by hand; with the cheats the blast costs no heart, the purchase nothing, and the bomb is kept | faithful |
| `seasons/diagonal-crossings.route` | the transitions' diagonal on foot: the edges of 0:e7, 0:e8, 0:d7 and 0:d8 crossed diagonally in the eight ways, written by hand | faithful, enhanced, enhanced-threaded |
| `seasons/stairs-diagonal.route` | Link's sprite steady on the screen on stairs and walking diagonally, at every size of the view and with both camera profiles, written by hand | faithful, enhanced, enhanced-threaded, enhanced-zoom |
| `moonrise-regalia/exploration.route` | a fan game from its BPS patch: a new game, continuous transitions | faithful, faithful-cc, enhanced, enhanced-threaded, enhanced-zoom, enhanced-zoom-threaded; with `--continuous-diagonal`, faithful and enhanced |
| `temple-of-seasons/subrosia-and-dungeon.route` | Temple of Seasons: Subrosia, its north and a dungeon, with continuous transitions, the drawn-back view and dialogues | faithful, faithful-cc, enhanced, enhanced-threaded, enhanced-zoom, enhanced-zoom-threaded; with `--continuous-diagonal`, faithful and enhanced |
| `gifts-of-kinomi/castle-and-west.route` | Gifts of Kinomi: the starting castle, its way out and the west, in the drawn-back view with continuous transitions | faithful, faithful-cc, enhanced, enhanced-threaded, enhanced-zoom, enhanced-zoom-threaded; with `--continuous-diagonal`, faithful and enhanced |

The modes: `faithful` checks the native renderer against the core and the ghost instance against every scrolling transition; `faithful-cc` the renderer with colour correction; `enhanced` and `enhanced-zoom` the Enhanced view at its two sizes, the ghost synchronous, with a run hash; the `-threaded` modes the same with the ghost in its thread, as played. For every route, the live state is compared with and without the Enhanced view.
