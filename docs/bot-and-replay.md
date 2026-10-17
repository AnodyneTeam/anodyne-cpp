# Scripted bot, deterministic replay, and GBA parity checks

A scripted bot plays the original C# game (AnodyneSharp) and records every frame's input. The recording replays
deterministically, produces a per-frame trace (map, position, health, dialogue, inventory), and exports GBA button
runs. The GBA port is driven by the same route (headless mGBA) and compared milestone by milestone.

## Pieces

| Where | What |
|---|---|
| `src/tools/bot/csharp/*.cs` | Bot sources (copy of the new files in the C# checkout; `hooks.diff` = edits to existing C# files). Refresh with `src/tools/bot/sync_csharp.sh`. The C# checkout itself is uncommitted. |
| `src/tools/bot/run_bot.sh <part> <outdir> [realtime]` | Runs the bot on a private Xvfb, records `<part>.rec` / `.gbainput` / `.trace.tsv`, replays the recording **with no bot** and diffs the traces. Prints `PLAYBACK OK: N frames identical`. |
| `src/tools/bot/xvfb.sh` | Starts a private virtual X server (`:99`) so runs work with the desktop locked. Needs Xvfb (`apt-get download xvfb && dpkg -x ... ~/xvfb-local/root`, no root needed). |
| `src/tools/bot/gba_drive.py` | Headless mGBA driver playing the same route on the GBA ROM; writes a trace in the C# format. |
| `src/tools/bot/compare_milestones.py` | Compares a C# trace with a GBA trace by map sequence and the player's position where each warp triggered. |
| `src/tools/bot/golden/` | Known-good recordings/traces of the C# run. |

## C# command line

`-bot=<part|keys>`, `-record=<file>`, `-playback=<file>`, `-trace=<file>` (per-frame TSV), `-gbainput=<file>`
(run-length GBA masks), `-fast` (unlocked speed; omit it for a realtime run with the bot's goal shown at the bottom
of the screen - good for a video). Runs with any of these use `RNG = new Random(1234)`, a fixed 1/60 s step, and
`RecordingManager.ParseCommandLine` no longer hardcodes a playback file.

Recordings store only non-idle keys (15 KB for the 2,400-frame Nexus run instead of 2 MB); old full-dictionary files
still load.

## Writing routes

A part is a C# iterator (`Bot.cs`): it yields a frame (`KeyFunctions[]`) or a nested helper. Most parts are written
with `Exec("...")` tokens in the walkthrough's vocabulary:

`Qn/Qe/Qs/Qw` head to the neighbouring 160x160 screen (BFS over the map's collision tiles; fights what it meets once
it has the broom) - `F` fight - `P<x>,<y>` pathfind to a world position - `G<x>,<y>` straight walk - `I<x>,<y>,<side>`
walk next to the object at x,y and press Accept - `T` read dialogue - `W<n>` wait - `U/D/L/R<n>` hold - `A<n>` tap
Accept.

Debug tokens (use with `-bot=keys`, `ANODYNE_BOT_KEYS="..."`, `ANODYNE_BOT_PRE=<part>` to start from the end of a
part): `E` list the screen's entities, `M` dump tile classes, `Z` reachable screens, `S<n>` screenshot to
`/tmp/bot/shot<n>.png`. `ANODYNE_BOT_DEBUG=1` logs fights and screen arrivals.

More tokens: `H<x>,<y>` head to an absolute screen (position-independent, use this inside retry sections) - `V` save at the
checkpoint on this screen - `J<tx>,<ty>,<u|d|l|r>` stand on a tile, face, press Accept (pick up / place dust) -
`B<x>,<y>` stand on a button until a gate on the screen opens - `X0/X1` auto-fight off/on - `O0/O1` danger-dodging off/on.
Debug: `Y<tx>,<ty>` print the planned path, `N` dump gate/blocked tiles, `ANODYNE_BOT_SHOTS=<dir>` fight screenshots.

**Retry sections.** `Section(name, () => Exec("..."))` replays its steps from the top when the player dies (the game
respawns at the last checkpoint with items and keys kept) or when a step fails (timeout/no path). Steps must be harmless to
repeat, so use absolute `H` screen targets. After 40 failures the run stops (`BotFatalException`).

**Resuming experiments.** Recordings always use a fresh throwaway save directory. For development set
`ANODYNE_BOT_SAVEDIR=<dir>`: the game keeps its saves there, so a run that saved at a checkpoint (`V`) can be resumed
later without replaying the whole route (`-bot=keys` with no `ANODYNE_BOT_PRE` just boots into the save).

Newer tokens: `>x,y` walk to a world point across screens (plans over the whole map: shut gates/doors from the entity presets,
dust rafts over rivers, jumping spikes and narrow rivers once the boots are owned; fights on arrival) - `~x,y` same but
within a river crossing - `k` sweep every loose dust on the screen into the hole - `+` open every reachable card chest
(WIP) - `%`/`K`/`#` temple puzzles. Debug: `$<type>` list the map's entity presets, `&sx,sy,ex,ey` ASCII dump of screens,
`r<x>,<y>[,s]` print the permissive (or strict, raft-aware) route, `?x,y,...` tile classes, `q` carrying/raft state.
`ANODYNE_BOT_DEBUG=1` also logs the raft planner (`raft round/hop/step`).

Parts so far (each starts where the previous one ended), in `BotScripts.PartNames`:
`nexus`, `roadway`, `overworld1`, `temple` (Blue Forest: Edward card chest, Sage, into the temple), `temple1` (the whole temple:
keys, laser/dust and button puzzles, Red Slime card, Seer boss, Shieldy card), `forest2` (Blue Forest 2: Nexus Point, big card gate,
health cicada, slimes, green-key gate), `fields` (Fields: Miao, Goldman's box and the dust-into-hole cave, the Nexus round trip,
Finty, Mitra's jump boots, narrow rivers jumped to the left door into the Beach), `beach` (Beach: Nexus Point and the Fisherman
card). Goldens in `src/tools/bot/golden/`: 98,463 frames in total for `beach`, every part replays identically.

## Gotchas learned

- Locking the screen deactivates the X session and GL hangs: use the private Xvfb.
- GameFAQs blocks WebFetch (403); the walkthrough is a room-level route, not frame input.
- Interaction needs the player's hitbox within a couple of pixels of the object on the facing side.
- The screen-scroll transition and every dialogue/cutscene (`PlayState` child state) lock the player: walkers must
  not count those frames as "stuck".
- Fights can drift across screen borders; "head east" is relative to the screen the route intends, not the one the
  fight ended on.
- Floor hazards made by entities (CrackedTile, Hole) are invisible to the tile data; the pathfinder avoids them.

- Saving at a checkpoint writes a save file and the next run would "continue" it: recordings must use a clean save dir
  (done in `Program.cs`).
- Enemies need their own tactics: a Shieldy only takes damage from a downward broom swing, so the bot waits in the row
  above it and swings facing down; the broom reaches ~16px below the player.
- Gates are entities over the tile data: an opened gate is passable although the tile says wall, a closed one blocks.
  Learned "blocked" tiles expire (gates change).
- Dodging is opt-in per part (`O1`): it changes the route, so the early, verified parts keep it off.
- Temple screen (1,2): the left PewLaser column is too dense to dodge; the bot picks up the dust beside the boulders and
  places it on the beam's first tile (`K` token). Dust that falls into a hole just vanishes (it does not fill it).
- Temple screen (0,1): the patrolling Shieldy never touches the upper button, but a front hit from the broom shoves it 8px, so
  the bot hits it from below as it passes and then stands on the other button (`#` token).
- Retry sections must repeat their own prerequisites: a death resets the room (e.g. placed dust is gone).
- Experiments: `ANODYNE_BOT_SAVEDIR` runs mutate the save, so copy a base save first (`/tmp/bot/exp.sh`-style) or runs differ.

## Rules the bot runs under (current)

- **Damage is fatal.** Any hit ends the run with the ring buffer of the last 240 frames (`/tmp/bot/fail_ring.txt`: player, keys, the
  step chain, enemies, everything within 40px) and a screenshot (`/tmp/bot/fail.png`). Exceptions, all explicit:
  - `g<n>` token / `BotRunner.BossDamageBudget`: this stretch may cost n hearts (documented per section, shrinking; the Temple's beam
    column costs 2, Blue Forest 2's north rooms 3, the Seer fight 0).
  - **Recoverable hits:** a hit is tolerated if the hearts are won back (drops from kills) within `ANODYNE_BOT_RECOVER` frames
    (default 600; the clock stops while text is open). Dying is always fatal.
- **Watchdogs.** 300 unchanged frames (position, health, text, keys, screen) = stuck (`ANODYNE_BOT_STUCK`); deliberate waits and open
  dialogue/cutscenes reset it (9000-frame cap on those); the bot's own time per frame must stay under 12ms on average (a 300-frame
  window; frames over 40ms are reported with their space-time search counts). Builds are made optimised (`ensure_build.sh`, used by
  every run script): the planners are 4x slower unoptimised.
- **Soft fights** (the auto-fight when entering a screen) give up on an enemy that cannot be reached (no kill for 220 frames).

## Tooling for fast iteration

- `from.sh <snapshot|section> [TOKENS=...]`: run from a saved game (`/tmp/bot/sec/<name>`, made by `` `name `` tokens,
  `ANODYNE_BOT_SNAP_AT=<frame>:<dir>`, `SNAPS=1`) instead of replaying the route: 20 seconds per try for one room. A save respawns the
  enemies and the game RNG is not saved, so a failure that depends on RNG may not reproduce: take the snapshot from the real run.
- `part.sh <part> <from dir> <to name>`: one route part from the previous part's end state (`ANODYNE_BOT_ONLY=1`), leaving its own end
  state in `/tmp/bot/keep/<to>`. Chain: `temple_end2` -> forest2 -> `forest2_end` -> fields -> `fields_end` -> beach -> `beach_end`.
- `regress.sh`: planner unit tests (`planner_test/`: pure fight policies and the space-time planner against a fake screen with
  pseudo-random scenarios) plus the scene snapshots in `scenes/` (each must lose no health). `ANODYNE_BOT_SWING_AUDIT=1`: where the
  broom is swung with nothing near.
- Extra tokens: `` `name `` snapshot - `g<n>` heart budget - `y<percent>` scale the path cost of beam tiles - `p1` no fetching dust
  on the way - `l<col>,<r0>,<r1>` dust leapfrog up a beam column - `Pb<x>,<y>` walk only while still on the dungeon map.
- Debug output: `ANODYNE_BOT_DEBUG=1` (paths, fights, hits), `ANODYNE_FIGHT_TRACE=1` (per-frame policy traces), `SEER_WAIT` /
  `SEER_TRACE_A/B` (Seer fight timing and frame traces).

## Enemy tactics that turned out to matter

- Seer (Temple boss): never strike while it dives. Strike (a) while its wave hangs still on it during the charge (stand just under it,
  the orbs are drawn out to the sides) and (b) right after the wave, while it drifts back to its post, from just under its orbs; duck
  to the floor behind a dust when the wave falls (it dies on the first dust).
- Dog: three one-second dashes aimed at points fixed when the attack starts: predict them (`DogPath`), keep out of the way with the
  planner, hit it while it barks from a standoff.
- Burst Plant / Frog: not dangerous by touch (plant) / touch only (frog); bullets only while in flight. Stand off 3px, back away while
  bullets fly, swing when clear.
- Shieldy: never chase a patrolling one: wait still and swing when its predicted position meets the broom; push it into a beam
  with a front hit; keep clear of a patroller about to walk into us.
- Laser lanes must cover every tile row/column the emitter's 16px box touches (emitters need not be on the tile grid).

## Status

- C# bot (single-part chain, every part runs to its end with no unrecovered damage): title -> Nexus -> Roadway -> Blue Forest -> the
  whole Temple (Seer with no damage, Shieldy and Red Slime cards) -> Blue Forest 2 -> Fields -> Beach (Fisherman card, ends at the
  whirlpool screen). The goldens in `golden/` predate this work and are stale; playback/determinism checking is parked on purpose
  (`REPLAY=1 run_bot.sh` turns it back on) until the bot can finish the whole game.
- Not done: everything after the Beach (Red Sea ... the 91 parts of the walkthrough), a separate speedrun route, the 100% extras.
  The walkthrough is a room-level route, so every room needs its own puzzle/combat logic - a long, incremental job.
- GBA parity: title, intro text, BLANK consoles/gates/tutorial boxes, teleporters and the Nexus arrival match the C# run
  (`compare_milestones.py`); the Nexus Sage, Roadway and later have no behaviour on the GBA yet, so the later routes are the target
  the GBA port should reproduce as it grows.
