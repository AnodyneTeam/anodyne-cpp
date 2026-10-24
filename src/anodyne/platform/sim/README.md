# GBA simulator (SDL3)

The game compiled for the host (Linux x86-64), running against a simulated GBA: the CPU is the host's, everything
around it is simulated from the registers the game writes. Used for fast iteration, debugging with host tools,
AddressSanitizer/UBSan, and bots.

## Build and run

Needs SDL3 (`cmake --install` of SDL 3.2 if the distro has none), Python 3 and a host C/C++ compiler. Use a build
directory directly under `src/` (the content compiler finds `old/Content` relative to it):

```
cd src
cmake -S . -B build-sim -DANODYNE_SIM=ON
make -C build-sim -j8 anodyne_sim
./build-sim/anodyne/platform/sim/anodyne_sim
```

Keys: arrows/WASD d-pad, X/K = A, Z/J = B, Enter = Start, Backspace = Select, Q/E = L/R, Tab (held) fast-forward,
F12 screenshot, Esc quit. A gamepad works too. `--help` lists the options: `--headless`, `--frames N`,
`--input file.gbainput` (scripted keys, the bot's run-length format), `--record`, `--shot N:file.png`,
`--shot-every N:dir`, `--wav`, `--trace`, `--ipc PORT` (the bot link, see `sim_ipc.h`).

`-DANODYNE_SIM_SANITIZE=OFF` builds without ASan/UBSan.

## How it works

- **Memory at the real addresses.** EWRAM, IWRAM, palette RAM, VRAM, OAM and the IO registers are mapped at
  0x02000000-0x07000000 with exactly the hardware's sizes; EWRAM/IWRAM sizes are read from the `MEMORY` block of
  `gba_cart.ld` at configure time. The rest of each region's last page is poisoned for ASan, so an access past
  the end of VRAM/OAM/palette (or into a mirror, which isn't simulated) is reported where it happens. Only the
  top page of IWRAM is also mapped at 0x03FFF000, for the BIOS interrupt flags at 0x03FFFFF8. Because the GBA
  addresses are real, tonc's headers and `(u32)pointer` casts for DMA work unchanged; for that the binary is
  linked without PIE above the GBA's address range (`-Ttext-segment=0x10000000`).
- **IO writes are trapped.** The game's view of the IO page is read-only; a store faults, is single-stepped and
  handed to `Io_OnWrite16` (`sim_io.cpp`), so writes have their hardware effect at once: DMA starts (immediate,
  VBlank, HBlank and sound FIFO timing), timers take reload values while reads return the counter, IF is
  acknowledged by writing 1, read-only bits stay put, BGxX/Y reload the affine reference points. `--no-io-trap`
  falls back to comparing the page once per scanline. In gdb: `handle SIGSEGV SIGTRAP nostop noprint pass`.
- **Scanline PPU** (`sim_ppu.cpp`): modes 0-5, text and affine BGs, OBJs (affine, double size, 1D/2D, 4/8bpp),
  priorities, windows 0/1/OBJ, alpha/brighten/darken and semi-transparent OBJs, BG/OBJ mosaic, forced blank.
- **Time**: `VBlankIntrWait`/`IntrWait` (`sim_bios.cpp`) step the hardware a scanline at a time (HBlank/VBlank/
  VCOUNT interrupts, HBlank/VBlank DMA, timers, Direct Sound) until the interrupt comes. The game's interrupt
  handlers run through libtonc's own `irq_*` code with a C `isr_master`.
- **Sound**: Direct Sound A/B with their 32-byte FIFOs, refilled by sound DMA, clocked by timer 0/1, at 32768 Hz.
- **BIOS**: LZ77/RL/diff decompression, CpuSet/CpuFastSet, division, Sqrt, ArcTan2, the affine set-up calls.
- libtonc's C sources compile as they are; its assembly is replaced (data tables and fonts converted by
  `tools/s2c.py`, routines in `tonc_host.c`).

## Checking it against a real emulator

`tools/compare_frames.py` plays the same `.gbainput` on the GBA ROM in headless mGBA (`src/tools/mgba`, built with
its Python bindings) and on the simulator, then compares the pictures:

```
CMP_WINDOW=25 LD_LIBRARY_PATH=tools/mgba/build python3 anodyne/platform/sim/tools/compare_frames.py \
    build-sim/anodyne/platform/sim/anodyne_sim build/anodyne/first.gba keys.gbainput /tmp/cmp 300,1400,2000
```

The title, the intro text box (blended OBJs) and BLANK (four BGs, window, OBJs) match pixel for pixel. Frame
numbers drift apart over time (~20 frames by frame 3000) because the simulator has no CPU timing: on the GBA some
loop iterations overrun a frame (lag), in the simulator none do, hence `CMP_WINDOW`.

## Not simulated

CPU timing (lag frames, cycles counted by timers within a frame), the PSG sound channels, the hardware ignoring
8-bit writes to VRAM/OAM/palette, memory mirrors (other than the BIOS IRQ slot), serial/link, the cartridge
save chip. Busy-waiting on `REG_VCOUNT` would hang (nothing advances while the game spins). On a 64-bit host
tonc's `DMA_REC`/`REG_DMA[]` struct doesn't match the hardware layout (8-byte pointers): use `REG_DMAxSAD` etc.
