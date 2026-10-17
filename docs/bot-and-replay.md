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

Parts so far (each starts where the previous one ended), in `BotScripts.PartNames`:
`nexus` (title -> BLANK consoles -> Nexus), `roadway` (Nexus Sage, pad to the Roadway), `overworld1` (Roadway: broom, key,
lock, teleporter), `temple` (Blue Forest: Edward card, Sage, into the temple), `temple1` (temple: Shieldy room, two keys, the dust-blocked laser column, the Shieldy-and-button room, the first small-key gate).

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

## Status

- C# bot: title -> Nexus -> Roadway (broom) -> Blue Forest -> temple entrance (15,961 frames), then in the temple the
  Shieldy room, both keys, the laser-dust and button puzzles and the first key gate (`temple1`, 41,678 frames); every part
  replays identically.
- Not done: the upper temple rooms (2,0)/(2,1), the Red Slime card, the second key gate, the Seer boss, and everything after it; the 100% route
  and a separate speedrun route. The walkthroughs are room-level, so every room needs its own puzzle/combat logic - this is a
  long, incremental job.
- GBA parity: title, intro text, BLANK consoles/gates/tutorial boxes, teleporters and the Nexus arrival match the
  C# run (`compare_milestones.py`); the Nexus Sage, Roadway and later have no behaviour on the GBA yet.
