# Running the C# game and bot on a headless box

The bot (`../csharp`) runs inside AnodyneSharp, the C# game. Its recording/bot hooks were never published upstream,
so a fresh machine (a Claude Code cloud session, CI) rebuilds the game from upstream plus the glue in this folder.

```
src/tools/bot/cloud/setup.sh          # once per machine, ~5 min
source src/tools/bot/cloud/headless.sh # once per shell: Xvfb on :99, pulseaudio, draw 1 frame in 60
ANODYNE_BOT_ONLY=1 src/tools/bot/run_bot.sh nexus /tmp/bot/g_nexus   # a route part from a new game
```

What `setup.sh` does:

1. Installs Xvfb, Mesa GL, OpenAL, SDL2, pulseaudio, wine and 7zip, and .NET 10 into `~/.dotnet` if missing.
2. Clones https://github.com/PixieCatSupreme/AnodyneSharp to `/home/snake/dev/AnodyneSharp` (the path every bot
   script expects) and checks out upstream commit `0b038d0` on a local `bot` branch.
3. Applies `glue.diff` (the command line and hooks), `fix16.diff` (determinism, Fix16 gameplay maths) and `wide.diff`
   (the wide view, below), and copies `../csharp/*.cs` plus `RecordingManager.cs` into `AnodyneSharp.Shared/Recording/`.
4. Sets up a 64-bit wine prefix (`~/.winemonogame`) with the x64 .NET 6 runtime and `d3dcompiler_47.dll`. MonoGame
   3.8.1's content step compiles the shaders with `mgfxc` under wine; without the prefix, `dotnet build` fails with
   `mgcb ... exited with code 6`. Plain `dotnet build` doesn't set this up.
5. Builds `AnodyneSharp.Multiplatform` (Debug, optimised).

The bot sources in `../csharp` are the source of truth; re-run the copy step (or `setup.sh`) after editing them.
Other entry points: `part.sh` (one part from a saved game), `from.sh`, `chain.sh` (whole route from a new game).

## The wide view

`wide.diff` adds a "Wide view" check box at the bottom of the graphics menu (`settings.wide_view`; `ANODYNE_WIDE=1`
turns it on for a run, `ANODYNE_WIDE=0` off). The game screen is then 240x160 instead of 160x160 (the render 240x180):
the map shows 40px of the rooms either side of the current one, and the UI stays centred over the room. Only drawing
changes, so a bot run's traces are the same either way. The port's version is `gSettings.mWideView` (anodyne/world.h).
`ANODYNE_BOT_SHOT_AT=F1,F2,...` (with `ANODYNE_BOT_DRAW_EVERY=1`) saves frame F's screen to `/tmp/bot/at_fF.png`, to set
beside SimHost's `SIMHOST_SHOT` screens of a replay.

## Wide rooms

`wide_rooms.diff`: with `ANODYNE_WIDE_ROOMS=1` the world is cut into 15x9-tile rooms (240x144) under a 16px HUD row.
The maps and Entities.xml then come from `ANODYNE_WIDE_CONTENT` (the anodyne-cpp checkout's `old/ContentWide`, made by
`src/tools/wide_rooms/remap.py`) instead of the built-in ones. Saves hold positions, so use a new game (or a wide save).
Positions the code has of its own (boss arenas, cutscene spots) were written for the 160x160 rooms: they go through
the same remap (`MapUtilities.RoomPos`/`RemapWorld`/`GetInGridPosition160`, and `RoomFrame` in place of a room corner
`Vector2`), and are the same sums without wide rooms (the bot run to REDSEA is byte-identical).

The port: `cmake -DANODYNE_WIDE_ROOMS=ON` in its own build directory (the content and the code both change). The
content compiler then reads `old/ContentWide` (`--wide-rooms`) and writes remap.txt's tables into the registry;
`anodyne/room.h` has the room size and `Room_X`/`Room_GridX160`/`RoomEdge`, the port's versions of the above. The HUD
goes back to the C#'s header row. The bot's routes are for the old rooms, so it can't play wide rooms: check them with
warps (the C#'s `scene.sh` w tokens, or the link ROM's `warp` command).
