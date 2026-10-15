# Building and running the C# reference (AnodyneSharp)

The original fan remake we are porting lives at `/home/snake/dev/AnodyneSharp` (outside this repo). Running it side by
side is the quickest way to check how something is supposed to look or behave (title screen, menus, dialogue, doors...).

## One-time requirements

- .NET SDK 10 (installed at `~/.dotnet`, not on the default PATH).
- Nothing else: the repo checks in its compiled shaders (`ee5c7b2`), so there's no MonoGame content-pipeline step.

## Build

```
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH DOTNET_CLI_TELEMETRY_OPTOUT=1
cd /home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Multiplatform
dotnet build                     # Debug, ~10s incremental. NOT -c Release (see below)
```

Output: `bin/Debug/net10.0/AnodyneSharp` (+ `AnodyneSharp.dll`, `Content/`).

## Run

Run it from the output directory - it reads `Content/`, `Settings.json` and writes `Saves/` relative to the working dir.
`DOTNET_ROOT` must be set or the apphost fails with "You must install .NET to run this application".

```
cd /home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Multiplatform/bin/Debug/net10.0
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH
./AnodyneSharp
```

(`dotnet AnodyneSharp.dll` works too.) Needs a display (`DISPLAY=:0.0`). The harmless `AL lib: ... default-44100.mhr`
messages are OpenAL HRTF warnings.

## Useful bits

- **Use the Debug build.** The Release build compiles but exits immediately with no window and no error: its `Content/`
  output lacks the compiled `effects` (shaders), so startup fails silently. Debug's output has them.
- To check it's running: `xwininfo -root -tree | grep -i anodyne` shows a window titled "Anodyne Fan Remake" (480x540).
- `RECORDING_SYSTEM.md` in the AnodyneSharp repo describes its input record/playback (handy for comparing sequences).
- The game's assets are the same ones this repo compiles from `old/Content`.
