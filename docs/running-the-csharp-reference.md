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

## Pixelate test scene (PC vs GBA, pixel by pixel)

The pixelate effect is checked with a fixed test pattern on both sides instead of by eye:

1. **PC**: the AnodyneSharp checkout has a `PixelateTest` scene (`Drawing/Effects/Pixelate.cs`, hooked into
   `AnodyneGame.Update/Draw`; not committed there). `ANODYNE_PIXELATE_TEST=<dir> ./AnodyneSharp` (from the build output
   directory, `DOTNET_ROOT` set) draws the pattern through the real shader for block sizes 1..48, writes `pc_sNN.png`
   (160x180, the 20px HUD header on top) and exits.
2. **GBA**: hold L+R at boot (`src/anodyne/pixelate_test.cpp`) and `src/tools/pixelate_test/capture_gba.py <elf> <gba> <dir>`
   writes `gba_sNN.png` (the 160x160 playfield) using the mGBA python bindings.
3. `src/tools/pixelate_test/compare.py <dir>` prints the mismatch per block size and writes `montage.png`.

Current state: block sizes 1, 2, 4, 5 and 10..48 are pixel-identical; 3 and 6..9 differ only in where the block grid
starts (the hardware mosaic grid can't be offset, and the software blocks need one palette entry each, so they stop at
11px).

## Dialogue (GBA)

`dialogue_EN.txt` is compiled by the content compiler into `build/tools/dialog_data.h` (232 scenes, 737 lines, ~75KB;
`[SOMEKEY-*]` become GBA button names, curly quotes become ASCII). `src/anodyne/dialog.cpp` is the runtime: scene lookup
(`Dialog_Get`, same progress/loop rules as `DialogueScene.GetDialogue`) and the text box, a port of
`DialogueState`/`TextBox`/`TextWriter` (typewriter + blip, hold A/B to speed up, `^` pauses, 3-line box with the
line-bump scroll, blinking arrow, intro box with a red `A` prompt). The box is 8 semi-transparent OBJs (OAM 120-127,
OBJ palette bank 15, 136 tiles of OBJ VRAM while open), so entities use OAM 10-119 and banks 4-14. While a box is
open `World_Update` freezes the player. `gDialogueOpen` (RAM) lets `src/tools/bot/gba_drive.py` mash A through it.
