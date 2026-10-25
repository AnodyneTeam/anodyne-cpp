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
3. Applies `glue.diff` (the command line and hooks) and copies `../csharp/*.cs` plus `RecordingManager.cs` into
   `AnodyneSharp.Shared/Recording/`.
4. Sets up a 64-bit wine prefix (`~/.winemonogame`) with the x64 .NET 6 runtime and `d3dcompiler_47.dll`. MonoGame
   3.8.1's content step compiles the shaders with `mgfxc` under wine; without the prefix, `dotnet build` fails with
   `mgcb ... exited with code 6`. Plain `dotnet build` doesn't set this up.
5. Builds `AnodyneSharp.Multiplatform` (Debug, optimised).

The bot sources in `../csharp` are the source of truth; re-run the copy step (or `setup.sh`) after editing them.
Other entry points: `part.sh` (one part from a saved game), `from.sh`, `chain.sh` (whole route from a new game).
