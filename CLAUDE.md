# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## About the project

A from-scratch C/C++ rewrite of AnodyneSharp (a fan remake of the game *Anodyne*), aimed at running on constrained
targets — currently the **Game Boy Advance**, with **PS1** planned as a second port target (no PS1 platform code
exists yet; only `anodyne/platform/gba` is implemented so far).

`old/` contains the original AnodyneSharp assets and content (maps, dialogue, sprites, bgm) used as the source data
that gets compiled into the GBA-native formats at build time — it is not itself part of the C/C++ port.

## Code style

- Always use braces `{ }` for `if`/`else`/`for`/`while`/etc. bodies, even single-statement ones.
- Prefer C++ casts (`static_cast`, `reinterpret_cast`, `const_cast`) over C-style casts, and RAII over manual acquire/release, in C++ code.

## Build & run

From `src/`:

```
./build.sh
```

This creates `src/build/`, runs `cmake ..` + `make -j2`, then launches the produced ROM
(`build/anodyne/first.gba`) in the mGBA emulator (path is hardcoded in `build.sh` to
`~/Downloads/mGBA-0.10.5-appimage-x64.appimage` — adjust if mGBA lives elsewhere locally).

If `build/` already exists, just do the cmake/make steps manually instead of rerunning `build.sh` (it does
`mkdir build` unconditionally):

```
cd src/build
cmake ..
make -j2
```

Build artifacts of interest: `first.elf`, `first.gba`, `first.map`, `first.dump` (objdump, via the `dump` target)
under `build/anodyne/`.

Requires the `arm-none-eabi` GCC toolchain, CMake, and (recursively cloned) git submodules
`src/3rdParty/libtonc` and `src/3rdParty/lodepng`. CI (`.github/workflows/c-cpp.yml`) builds both Linux and
Windows and is the reference for toolchain setup if the local build environment is broken.

There are no lint or test targets in this repo currently.

## Architecture

### Two-stage build: content compiler → GBA ROM

`src/CMakeLists.txt` builds two independent things:

1. **`tools/`** — built with the *host* compiler. `CompileContent` (from `tools/content_compiler/`) reads raw
   assets out of `old/Content/` (tilemap PNGs, `BG.csv` map layouts, dialogue text) via `ContentPaths`
   (`tools/content_compiler/content_paths.hpp`), and for every map listed in `tools/content_compiler/main.cpp`
   (`APARTMENT`, `OVERWORLD`, `NEXUS`, etc.) emits generated C++ headers into the build tree:
   - `<MAP>_pal.h` — palette, packed to GBA RGB555 (`GBA555()` in `main.cpp`)
   - `<MAP>_tiles.h` — 8bpp tile data
   - `<MAP>_map.h` — tilemap, packed into GBA screen-entry format (10-bit tile index + H/V flip bits)
   - `maps.h` / `map_registry.h` — aggregate include file and a `kMaps[]` registry array (`MapEntry` structs)
     indexed by map order, consumed by the game code

   This compiler only regenerates a map's headers when its source CSV/PNG is newer than the existing headers
   (`IsFileNewer()`), but it always runs (`RunContentCompiler` is an `ALL` target with no output tracked by
   CMake, so it reruns — and does its own freshness check — every build). `first_lib` depends on it, so content
   is always compiled before the ROM.

2. **`anodyne/`** — cross-compiled with `arm-none-eabi-gcc`/`g++` for the ARM7TDMI (`-mthumb`, `-mcpu=arm7tdmi`).
   `first_lib` is linked manually (not via CMake's normal executable/link step) against `tonc` (libtonc) with a
   custom linker script (`anodyne/platform/gba/sys/gba_cart.ld`), then `objcopy`'d to a raw binary and patched
   into a valid GBA ROM header by the in-tree `gbafix` tool. `anodyne/platform/gba/sys/` holds the GBA-specific
   startup code (`gba_crt0.s`) and syscalls; this is where any future PS1 port would add a sibling
   `anodyne/platform/ps1/` with its own startup/syscalls and CMake toolchain block.

### Runtime map handling (`anodyne/anodyne.cpp`)

- `#include "../build/tools/map_registry.h"` pulls in the content-compiler-generated `kMaps[]` array — this file
  does not exist until after a build has run.
- `SetCurrentMap()` copies the active map's palette/tileset into GBA palette/tile VRAM and calls
  `CopyMapRegionToBG0()`.
- `CopyMapRegionToBG0()` streams a 32x32 viewport window out of the (potentially much larger) map buffer into
  screen-block 31 (`se_mem[31]`), clamping at map edges — this is how scrolling works, rather than the whole map
  living in VRAM at once.
- The current `main()` is a minimal map viewer/smoke test: Select/Start cycle through `kMaps[]`, D-pad scrolls
  the camera. It is not yet real game logic.
- `ObjVramAllocator` (`anodyne/obj_vram_allocator.hpp`) is a simple first-fit block allocator over OBJ (sprite)
  VRAM, allocating in 32-byte (one 4bpp tile) blocks; `alloc()`/`free()` return/take a block ID that doubles as
  an OBJ tile index, and `ptr()` resolves a block ID to a `tile_mem[4]` pointer.
- `dialog_manager.c/.h` is a stubbed-out API (npc/area/scene dialogue lookup, progress tracking, random line
  selection) — declared but not yet implemented against real compiled dialogue data.

### Content compiler internals (`tools/content_compiler/`)

- `map_compiler.hpp/.cpp` (`MapCompiler`) parses a map's `BG.csv` + tileset PNG (via `lodepng`) into a
  `TileSet` of 8x8 `RGBATile`s plus an `Array2D<uint32_t> map8`, where each map cell packs a tile index with a
  `TileVariant` (H/V flip) in the high bits — this packed format is what `ConvertMapToGBA()` in `main.cpp`
  unpacks into GBA screen entries.
- `dialog_compiler.hpp/.cpp` (`DialogCompiler`) parses `old/Content/Dialogue/dialogue_<LANG>.txt`; currently
  invoked from `main.cpp` but disabled (`CompileDialog()` call is commented out).
- `string.hpp` has shared string-parsing helpers used by both compilers.

### Not yet wired up

- Dialogue compilation is disabled in `main.cpp`; `dialog_manager` has no real backing data yet.
- No PS1 platform target exists yet despite it being a project goal — only GBA.
