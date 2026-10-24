// Host entry point: sets up the simulated GBA, the SDL3 window/audio/input, then runs the game's own main()
// (compiled as GameMain). The game drives time by waiting for VBlank (VBlankIntrWait -> StepLine).

#include "sim_internal.h"
#include "sim_frontend.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__)
// The IO write trap needs its own SIGSEGV handler next to ASan's (it chains to ASan for every other fault).
extern "C" const char* __asan_default_options()
{
    return "allow_user_segv_handler=1:handle_segv=1:detect_leaks=0";
}
#endif

extern int GameMain();

namespace sim
{
namespace
{
int gLine = 0;
uint64_t gFrames = 0;
bool gFrameStarted = false;

void HBlank(int line)
{
    IoSet16(0x004, static_cast<uint16_t>(Io16(0x004) | 2));
    if (Io16(0x004) & 0x10)
    {
        Irq_Raise(IRQ_HBLANK_BIT);
    }
    if (line < kScreenH)
    {
        Dma_OnHBlank();
    }
    Irq_Service();
}

// Timers and sound advance in small steps so FIFO refills and sample timing interleave like on the hardware.
void AdvanceCycles(int cycles)
{
    constexpr int kChunk = 64;
    while (cycles > 0)
    {
        const int c = cycles < kChunk ? cycles : kChunk;
        Timer_Advance(c);
        Apu_Advance(c);
        cycles -= c;
    }
    Irq_Service();
}
} // namespace

int CurrentLine()
{
    return gLine;
}

uint64_t FrameCount()
{
    return gFrames;
}

void StepLine()
{
    if (!Io_TrapEnabled())
    {
        Io_Poll();
    }
    Irq_Service(); // anything the game enabled or requested since the last line

    const int line = gLine;
    if (line == 0 && !gFrameStarted)
    {
        Ppu_BeginFrame();
        gFrameStarted = true;
    }

    // VCOUNT and the DISPSTAT flags (bit 0 VBlank: lines 160-226, bit 2 VCOUNT match).
    IoSet16(0x006, static_cast<uint16_t>(line));
    uint16_t stat = Io16(0x004) & ~7u;
    if (line >= kScreenH && line < kLines - 1)
    {
        stat |= 1;
    }
    if ((Io16(0x004) >> 8) == line)
    {
        stat |= 4;
    }
    IoSet16(0x004, stat);
    if ((stat & 4) && (stat & 0x20))
    {
        Irq_Raise(IRQ_VCOUNT_BIT);
    }

    if (line == kScreenH)
    {
        // VBlank: the frame is complete. Show it, take input, then the interrupt (the game's VBlankIntrWait returns).
        gFrames++;
        gFrameStarted = false;
        Apu_EndFrame();
        Frontend_OnVBlank(Ppu_Frame());
        Ppu_BeginFrame();
        if (stat & 0x08)
        {
            Irq_Raise(IRQ_VBLANK_BIT);
        }
        Dma_OnVBlank();
    }
    Irq_Service();

    if (line < kScreenH)
    {
        Ppu_RenderLine(line);
    }
    AdvanceCycles(960);
    HBlank(line);
    AdvanceCycles(kCyclesPerLine - 960);
    IoSet16(0x004, static_cast<uint16_t>(Io16(0x004) & ~2u));

    gLine = (line + 1) % kLines;
}

void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[sim] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void Fatal(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[sim] FATAL (frame %llu, line %d): ", static_cast<unsigned long long>(gFrames), gLine);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
}
} // namespace sim

int main(int argc, char** argv)
{
    sim::FrontendOptions options;
    if (!sim::Frontend_ParseArgs(argc, argv, options))
    {
        return 2;
    }
    sim::Memory_Init();
    sim::Io_InstallTrap(options.mIoTrap);
    sim::Frontend_Init(options);
    atexit(sim::Frontend_Shutdown);
    GameMain();
    return 0;
}
