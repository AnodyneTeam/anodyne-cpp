#pragma once

// Internals of the host-native GBA simulator (see README.md next to this file).
//
// The game is compiled for the host and runs at full speed; only the hardware around the CPU is simulated. Every
// GBA memory region the game can address is mapped at its real address (so tonc's MEM_* macros and `(u32)pointer`
// casts work unchanged), sized exactly like the hardware / gba_cart.ld, with the rest of its last page poisoned for
// AddressSanitizer. IO register writes are trapped (the IO page is read-only to the game; a write faults, is
// single-stepped and then handled), so DMA starts, timer reloads, FIFO resets and the like happen at the write,
// as on the hardware.

#include <stdint.h>

#include "sim_memory_layout.h" // generated from gba_cart.ld: SIM_EWRAM_*/SIM_IWRAM_*/SIM_ROM_*

namespace sim
{
constexpr uint32_t kPalBase = 0x05000000;
constexpr uint32_t kPalSize = 0x400;
constexpr uint32_t kVramBase = 0x06000000;
constexpr uint32_t kVramSize = 0x18000;
constexpr uint32_t kOamBase = 0x07000000;
constexpr uint32_t kOamSize = 0x400;
constexpr uint32_t kIoBase = 0x04000000;
constexpr uint32_t kIoSize = 0x400; // 0x04000000-0x040003FF (+ WAITCNT's neighbour 0x04000800 in the same page)

constexpr int kScreenW = 240;
constexpr int kScreenH = 160;
constexpr int kLines = 228;
constexpr int kCyclesPerLine = 1232;
constexpr int kCyclesPerFrame = kCyclesPerLine * kLines; // 280896
constexpr int kClockHz = 16777216;

// ---- memory (sim_memory.cpp) ----
void Memory_Init();
// The simulator's own (writable, untrapped) view of the IO registers. The game's view at 0x04000000 is read-only.
uint8_t* Io();
inline uint16_t Io16(uint32_t ofs)
{
    return *reinterpret_cast<const uint16_t*>(Io() + ofs);
}
inline uint32_t Io32(uint32_t ofs)
{
    return *reinterpret_cast<const uint32_t*>(Io() + ofs);
}
// Writes a register from the simulator side (no write hook runs).
void IoSet16(uint32_t ofs, uint16_t value);
// The register values as of the last handled write (what a game store is compared against).
uint8_t* IoShadow();
uint8_t* Vram();
uint16_t* Pal();
uint16_t* Oam();
// True if [addr, addr+size) is inside a mapped region (for DMA source/destination checks).
bool IsMapped(uint32_t addr, uint32_t size);

// ---- IO register write hooks (sim_io.cpp) ----
// Called (outside the signal handler's fault, after the store) for each halfword the game's store changed.
void Io_OnWrite16(uint32_t ofs, uint16_t oldValue, uint16_t newValue);
// Turns the IO write trap on (default) or off (then the polling fallback in Io_Poll catches DMA/timer starts).
void Io_InstallTrap(bool enabled);
bool Io_TrapEnabled();
// Fallback for --no-io-trap: compares the IO page with the last copy and runs the write hooks for changes.
void Io_Poll();

// ---- interrupts (sim_irq.cpp) ----
enum Irq : uint16_t
{
    IRQ_VBLANK_BIT = 1 << 0,
    IRQ_HBLANK_BIT = 1 << 1,
    IRQ_VCOUNT_BIT = 1 << 2,
    IRQ_TIMER0_BIT = 1 << 3,
    IRQ_DMA0_BIT = 1 << 8,
    IRQ_KEYPAD_BIT = 1 << 12,
};
// Sets the IF bit and, if IME and IE allow it, runs the master ISR (like the CPU taking the interrupt).
void Irq_Raise(uint16_t flag);
// The pending, enabled interrupts are serviced (call after IME/IE were written).
void Irq_Service();

// ---- DMA / timers (sim_dma_timer.cpp) ----
void Dma_OnControlWrite(int channel, uint16_t oldCnt, uint16_t newCnt);
void Dma_OnVBlank();
void Dma_OnHBlank();
// A FIFO wants 16 more bytes (Direct Sound): runs the DMA channel that feeds it, if any. fifo = 0 (A) or 1 (B).
void Dma_OnFifoRequest(int fifo);
void Timer_OnControlWrite(int timer, uint16_t oldCnt, uint16_t newCnt);
void Timer_OnReloadWrite(int timer, uint16_t value);
void Timer_Advance(int cycles);
// Counter value for a running timer (what reading TMxCNT_L returns).
uint16_t Timer_Counter(int timer);

// ---- sound (sim_apu.cpp) ----
void Apu_Init(bool enableOutput, const char* wavPath);
void Apu_Shutdown();
// One timer overflowed: the Direct Sound channels clocked by it take their next sample.
void Apu_OnTimerOverflow(int timer);
void Apu_PushFifo(int fifo, uint32_t word);
void Apu_ResetFifo(int fifo);
void Apu_Advance(int cycles);
void Apu_EndFrame();

// ---- video (sim_ppu.cpp) ----
void Ppu_BeginFrame();
// BGxX/BGxY were written: the internal reference point of that affine BG (2 or 3) reloads.
void Ppu_OnAffineRefWrite(int bg);
void Ppu_RenderLine(int line);
// The finished frame, 240x160 ARGB8888.
const uint32_t* Ppu_Frame();

// ---- scheduler (sim_main.cpp) ----
// Runs the hardware until the next line boundary (renders, IRQs, DMA, timers, sound).
void StepLine();
int CurrentLine();
uint64_t FrameCount();
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void Fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
} // namespace sim
