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
- Red Cave tokens: `s` wait for the screen's steam jets to burn and stop (~3s of safety follows) - `a1/a0` let the planner wade
  through water (short stretches only) - `G<x>,<y>,d` walk diagonally (both axes at full speed) - `B<x>,<y>,<px>,<py>` stand at an
  exact spot on a button until a gate opens - `n<tab>` pause menu, map tab, back to the dungeon entrance - `s1` wait for the
  jets to start rising - `F` in the Red boss's room runs `RedBossFight` (strike from under it between its 3s ticks, drop out of its
  lower proximity sensor for each tick). Debug: `=x0,y0,x1,y1` prints a tile rectangle with water currents (U/D/L/R, `o` still).
- Planner tokens: `b<x>,<y>` walk/raft/jump to a point, pressing reachable buttons until a route opens - `f1/f0` rafts ride along
  narrow rivers instead of jumping off across them - `v<MAP>` (dev only, `ANODYNE_BOT_DEV=1`) mark a map's Nexus pad as visited.
- Extra tokens: `` `name `` snapshot - `g<n>` heart budget - `y<percent>` scale the path cost of beam tiles - `p1` no fetching dust
  on the way - `l<col>,<r0>,<r1>` dust leapfrog up a beam column - `Pb<x>,<y>` walk only while still on the dungeon map.
- Apartments tokens: `[` get the screen's Rat onto one button while we hold the other (simulates the rat's clockwise turns, tries
  every one-dust move, sweeps the best one into place; if a dash trap kills the rat it steps out of the screen and back in for a fresh one) - `{Type,x0,y0,x1,y1[,frames]` wait until an entity of that type is in a
  world rectangle - `}` broom every Silverfish here - `/` print the screen as text (tiles, every entity's letter, then each
  entity's exact box, velocity, facing and frame); the same dump is printed on every hit and every failure, and is much cheaper to
  read than a screenshot. `/x,y` prints another screen of the map (tiles and entity presets, DEAD for killed/opened ones).
- More Apartments solvers: `]` gets the screen's Silverfish onto one button while we hold the other (BFS over fish tile x
  crystal-switch state: scare pushes from a stand tile, pillar flips; a slide into a hole or water is fatal for the fish; a fish
  stuck in a corner is reset by leaving the screen) - `_x,y` walk to a point through a crystal-pillar maze (BFS over tile x
  switch state, hitting switches on the way) - `(x,y` ride the propelled platform at that tile (jump on from behind it) - a Gas
  Guy in a room with traps or frogs is waited out at a spot off every trap line and struck when it floats into reach.
- The Watcher (Apartments boss, `WatcherFight`, run by `F`/`H` in its room): strikes from the arena's top edge while it floats
  over the holes (the broom just reaches), hits its Split pose from below and then steps out of its copies' row and beside its
  drop, and sweeps it from above/below its dash row; fire balls and bodies are predicted for the space-time planner. Budget 3
  hearts (TODO: the volley of dropped fire balls it aims at us after a dash still lands 0-2 times a fight).
- Hazard prediction in the walker: rats are replayed with their turns (walls, dust, special floors) and only their inner 8x8 bites;
  an idle on-sight dash trap's danger grows out along its lines at 80px/s from now (not the whole line forever), routes pay extra to
  cross a trap's line close to it, and a jump over a trap's line waits until it will land beyond it. Rats and Gas Guys hurt a
  player in the air, so they never trigger a jump-dodge. Jumps are planned with the real air speed (0.83x walking).
- Rats on screens without buttons are ambushed (`KillRat`: replay its loop, wait a little ahead of it facing back, swing as it
  walks up); TeleGuys are hit, then we turn our back so it reappears on the side it was hit from and charges away, and we strike
  its charge (`KillTeleGuy`); Gas Guys are predicted homing in at 12px/s and fought first when close. The fight never goes for a
  dropped heart with an enemy within 36px of us.
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
- Red Cave: lure Movers into beams/buttons instead of fighting them (`m<s>` soft-fight, `|<s>` lure tokens, `F<n>` fixed waits);
  Slashers cost 1-3 hearts each (budget with `g<n>`). Stab a room's Red_Pillar chain ("tail") with the broom before fighting the
  reaper in that room. Jump boots make the player airborne and immune except to steam, which has no AIR check. Plants, frogs,
  solid shooters and pillars count as non-walkable in the nav grid.
- Red Cave water: without a raft you sink and drown after 3s (1 heart, back to where you came in), so never wait in it. Some channels
  have a current: up a north current you wade at ~52px/s, against it you hardly move. Dust rafts float with the current (10px/s)
  and steam jets shove them; pick the dust up with the broom from the bank and drop it on the water to board it.
- Steam jets (OnOffLaser) only hurt in their 2s "steam_is_harmful" phase; then ~3s harmless (recede, 2s hidden, emerge). Cross straight
  after a burst (`s`). Four-shooter bullets are dodged while rafting too (jump or hold back).
- Enemies without a permanence flag (Annoyers, Slashers, ...) respawn whenever their screen is entered again. A pair of Annoyers at a
  doorway hits you during the frozen screen transition, every time: use another way out (the pause menu's dungeon-entrance warp).
- Slasher: 3 hits, 0.5s apart; strike for the first 70 frames of its 1.4s float, then get out of line of the cut.
  While Slashers wind up, the fight simulates each one's drift towards us and its exact cut box (long cut when we are 36px or
  more from its centre, otherwise wide) for 8 directions and a few walk lengths, and runs the way that stays clear, or else the one
  hit latest. Two hits a room are still common, so the Apartments' water and south key sections are budgeted (`g3`, `g2`).
- `X0` turns auto-fight off for the rest of a section: put `X1` back before a room whose fight should start on arrival (the Watcher).

## Status

- C# bot (single-part chain, every part runs to its end with no unrecovered damage): title -> Nexus -> Roadway -> Blue Forest -> the
  whole Temple (Seer with no damage, Shieldy and Red Slime cards) -> Blue Forest 2 -> Fields -> Beach (Fisherman card, ends at the
  whirlpool screen). The goldens in `golden/` predate this work and are stale; playback/determinism checking is parked on purpose
  (`REPLAY=1 run_bot.sh` turns it back on) until the bot can finish the whole game.
- Red Sea reaches the Red Cave door. `RedCave.cs`: part `redcave` (Part 9) runs the first rooms, the hub, the steam crossings, cuts
  the north-east tentacle in (5,1) and goes round the Red Sea to the right entrance; part `redcave2` (the right wing) takes the (6,3)
  key through the steam channel, opens the (5,5) keyblocks, cuts the east tentacle in (5,4) and goes round to the left entrance
  (verified from its start snapshot with no unrecovered damage, 1 budgeted Slasher hit). In one chain from the cave's start, `redcave`
  can still run out of its chip allowance in the (3,2)-(5,2) rooms (RNG-dependent Annoyer/Slime hits). Part `redcave3` (the left
  wing) kills the Movers in (2,5) and (0,5) by luring them into steam, takes the (1,3) and (0,3) keys, fights the (0,4) Annoyers
  out of the Four_Shooter's lines, opens the (1,5)/(1,4) keyblocks and cuts the last tentacle, which raises the north entrance
  (verified strict from `rc_left_in`, but it uses its whole 2-chip allowance: the (2,3) and (0,4) Annoyers land hits). Part
  `redcave4` goes in by the north entrance, along the top to (5,0), rides a dust raft up the channel (against its current) into the
  boss room, beats the Red boss (`RedBossFight`, no hits taken), takes the red key and the card below it and leaves by the Nexus pad
  (verified strict from `rc_pillar_14`). Part `cliff` (`ForestCliff` in `Fields.cs`) goes from the Nexus through the Fields pad, the
  east Fields' raft/button maze (planner token `b`) to the Forest door, the Forest's Nexus Point, a raft over its lake to the Cliffs,
  the Cliffs' Nexus Point and the Crowd's door (verified strict, 1 chip, from a Fields state rebuilt with the Fields part's Goldman and
  (8,2) button sections: the Red Cave snapshots descend from a save that skipped the Fields part, so they lack its pads and events).
  Part `crowd` (`Crowd` in `Fields.cs`) is the Crowd dungeon from its door to the Wall boss's door on (9,6): the four keys, the
  health chest, the keyblocks, the north-east platform rides and both fall doors. Each section is verified from the snapshot the one
  before it saved, mostly hitless. The whole part run from the door is not reliable yet: the (2,3) Dog/Annoyer/Slime fight and the
  frog rooms are RNG-dependent and can use up the chip allowance. `CrowdPlatforms` (also in `Fields.cs`, tokens `t1`..`t13`) scripts
  the rooms the planner can't do alone:
  - the propelled platforms, with dust swept onto them to start them;
  - the falling spike rollers. They are harmless while they fall and to a player in the air, so the bot gets past them as they fall
    and jumps through them as they roll. On (9,1) both rollers sweep the whole room: one from the top, and one from the bottom that
    starts when row 18 is crossed.
  The part goes on through the Wall boss (`WallBossFight`, token `x`). The bot hits the face from right under it when it is open,
  and waits between rows 3 and 5 otherwise. It dodges the bullets, the stomps' shadows and the pushes with the space-time planner,
  and it jumps the laser, timed so it lands after the laser is past. From the boss door, 6 of 7 timing variants win hitless and the
  7th takes one hit. After the boss it gets the Health Cicada, the Sage's talk, the blue key and the chest, then warps to the entrance
  and goes out to the Cliffs (verified in one run from `cr_96a`). The pit frogs are rushed (token `<x,y[,dir[j]]>`, `FrogRush`); the
  whole part passes from the chain about one run in six.
- Part `windmill` (`Windmill` in `Fields.cs`): the Fields door, the three big-key gates, the climb and the console; the card chest by
  the gates only opens once the console has run, so it is opened last. Part `temple2` (`Temple2` in `Temple.cs`): the Nexus pad to
  the Temple, a running jump (token `e<dir>[at]`) over the (5,0) holes for the key and the Pew Pew card, out through (4,4) and round
  the east woods (a lane between the laser columns, one budgeted slow-laser hit, past the Dogs) to the Annoyer card. Part `suburb`
  (`Suburb` in `Temple.cs`): Black and White Town. Its killers cost 6 hearts and never die, so they are dangers with a 30px path
  berth; a detour to the Forest's Mushroom card (raft up its drowning channel, swinging at the Slime and Annoyer from the raft) makes
  16 cards for the town's gate; then the maze's health upgrade, ten townsfolk (token `:<n>`) and the Suburbanite's Corpse card
  (17 cards). Each passes from the previous part's end. Still skipped on the route: Red Cave's (0,0) and (3,1) cards and the Crowd's
  (8,2) card. Part `apartment` (`Apartment` in `Temple.cs`): the Apartments from the town's north door: Broom Widen from Red
  Cave, the Silverfish card under the spike roller, both keys of the west wing, the Gas Guy card, then the east wing (rat and
  silverfish buttons, the platform rides, the water raft, the twin-platform room through the (7,2) door, the crystal maze), the
  Watcher, its health upgrade and the (8,7) card: all 3 of its cards, 20 in all. Next: back at the Nexus, the next area.
  The Beach 2 raft crossing still
  uses the dev `c8` token.
- Not done: everything after the Beach (Red Sea ... the 91 parts of the walkthrough), a separate speedrun route, the 100% extras.
  The walkthrough is a room-level route, so every room needs its own puzzle/combat logic - a long, incremental job.
- GBA parity: title, intro text, BLANK consoles/gates/tutorial boxes, teleporters and the Nexus arrival match the C# run
  (`compare_milestones.py`); the Nexus Sage, Roadway and later have no behaviour on the GBA yet, so the later routes are the target
  the GBA port should reproduce as it grows.
