// IO register writes. The game's view of the IO page is read-only: a store faults (SIGSEGV), the handler makes
// the page writable and single-steps the store (trap flag), and the SIGTRAP that follows re-protects the page and
// hands every halfword the store changed to Io_OnWrite16. So a register write has its hardware side effect at
// the write: a DMA starts, a timer gets its reload value (while reads keep returning the counter), read-only
// bits stay as they were, IF bits are acknowledged, FIFOs reset.

#include "sim_internal.h"

#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

namespace sim
{
namespace
{
bool gTrapEnabled = false;
bool gStepping = false;
uint32_t gFaultOfs = 0xFFFFFFFF; // IO offset the store being single-stepped faulted at
struct sigaction gOldSegv;
struct sigaction gOldTrap;

void* GamePage()
{
    return reinterpret_cast<void*>(static_cast<uintptr_t>(kIoBase));
}

size_t PageSize()
{
    return static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

void Chain(const struct sigaction& old, int sig, siginfo_t* info, void* ctx)
{
    if (old.sa_flags & SA_SIGINFO)
    {
        if (old.sa_sigaction)
        {
            old.sa_sigaction(sig, info, ctx);
            return;
        }
    }
    else if (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN)
    {
        old.sa_handler(sig);
        return;
    }
    // Default action: re-raise with the default handler so the process dies with the right signal.
    signal(sig, SIG_DFL);
    raise(sig);
}

void OnSegv(int sig, siginfo_t* info, void* ctx)
{
    const uintptr_t addr = reinterpret_cast<uintptr_t>(info->si_addr);
    ucontext_t* uc = static_cast<ucontext_t*>(ctx);
    const bool write = (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0;
    if (gTrapEnabled && !gStepping && write && addr >= kIoBase && addr < kIoBase + PageSize())
    {
        mprotect(GamePage(), PageSize(), PROT_READ | PROT_WRITE);
        uc->uc_mcontext.gregs[REG_EFL] |= 0x100; // TF: trap after the store
        gStepping = true;
        gFaultOfs = static_cast<uint32_t>(addr - kIoBase);
        return;
    }
    Chain(gOldSegv, sig, info, ctx);
}

void OnTrap(int sig, siginfo_t* info, void* ctx)
{
    if (!gStepping)
    {
        Chain(gOldTrap, sig, info, ctx);
        return;
    }
    ucontext_t* uc = static_cast<ucontext_t*>(ctx);
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
    gStepping = false;
    mprotect(GamePage(), PageSize(), PROT_READ);
    // A store of the value a register already holds changes no memory but still counts for IF (acknowledge every
    // bit written). Only a store that starts right at IF is known to have covered it.
    if (gFaultOfs == 0x202 && memcmp(Io() + 0x202, IoShadow() + 0x202, 2) == 0)
    {
        IoSet16(0x202, 0);
    }
    Io_Poll();
    gFaultOfs = 0xFFFFFFFF;
}
} // namespace

void Io_InstallTrap(bool enabled)
{
    gTrapEnabled = enabled;
    if (!enabled)
    {
        return;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = OnSegv;
    sigaction(SIGSEGV, &sa, &gOldSegv);
    sa.sa_sigaction = OnTrap;
    sigaction(SIGTRAP, &sa, &gOldTrap);
    mprotect(GamePage(), PageSize(), PROT_READ);
}

bool Io_TrapEnabled()
{
    return gTrapEnabled;
}

void Io_Poll()
{
    const uint32_t faultOfs = gFaultOfs;
    uint8_t* io = Io();
    uint8_t* shadow = IoShadow();
    bool fifoTouched[2] = { false, false };
    for (uint32_t ofs = 0; ofs < kIoSize; ofs += 2)
    {
        uint16_t now;
        uint16_t was;
        memcpy(&now, io + ofs, 2);
        memcpy(&was, shadow + ofs, 2);
        if (now == was)
        {
            if ((ofs == 0xA0 || ofs == 0xA4) && ofs == faultOfs)
            {
                fifoTouched[(ofs - 0xA0) / 4] = true; // same word again: still a push
            }
            continue;
        }
        memcpy(shadow + ofs, &now, 2);
        if (ofs >= 0xA0 && ofs < 0xA8)
        {
            fifoTouched[(ofs - 0xA0) / 4] = true; // a FIFO takes whole words: push once per store
            continue;
        }
        Io_OnWrite16(ofs, was, now);
    }
    for (int f = 0; f < 2; f++)
    {
        if (fifoTouched[f])
        {
            Apu_PushFifo(f, Io32(0xA0 + 4 * f));
        }
    }
}

void Io_OnWrite16(uint32_t ofs, uint16_t oldValue, uint16_t newValue)
{
    switch (ofs)
    {
    case 0x004: // DISPSTAT: the three status flags are read-only
        IoSet16(ofs, static_cast<uint16_t>((newValue & ~7u) | (oldValue & 7u)));
        return;
    case 0x006: // VCOUNT: read-only
    case 0x130: // KEYINPUT: read-only
        IoSet16(ofs, oldValue);
        return;
    case 0x082: // SOUNDCNT_H: bits 11/15 reset FIFO A/B and always read back as 0
        if (newValue & 0x0800)
        {
            Apu_ResetFifo(0);
        }
        if (newValue & 0x8000)
        {
            Apu_ResetFifo(1);
        }
        IoSet16(ofs, static_cast<uint16_t>(newValue & ~0x8800u));
        return;
    case 0x084: // SOUNDCNT_X: the PSG status bits are read-only
        IoSet16(ofs, static_cast<uint16_t>((newValue & 0x80u) | (oldValue & 0x0Fu)));
        return;
    case 0x0BA:
    case 0x0C6:
    case 0x0D2:
    case 0x0DE:
        Dma_OnControlWrite(static_cast<int>((ofs - 0xBA) / 12), oldValue, newValue);
        return;
    case 0x100:
    case 0x104:
    case 0x108:
    case 0x10C:
    {
        // A write sets the reload value; reads keep returning the counter.
        const int t = static_cast<int>((ofs - 0x100) / 4);
        Timer_OnReloadWrite(t, newValue);
        IoSet16(ofs, Timer_Counter(t));
        return;
    }
    case 0x102:
    case 0x106:
    case 0x10A:
    case 0x10E:
        Timer_OnControlWrite(static_cast<int>((ofs - 0x102) / 4), oldValue, newValue);
        return;
    case 0x028: // BG2X/BG2Y, BG3X/BG3Y: a write reloads the internal reference point
    case 0x02A:
    case 0x02C:
    case 0x02E:
        Ppu_OnAffineRefWrite(2);
        return;
    case 0x038:
    case 0x03A:
    case 0x03C:
    case 0x03E:
        Ppu_OnAffineRefWrite(3);
        return;
    case 0x202: // IF: writing 1 acknowledges
        IoSet16(ofs, static_cast<uint16_t>(oldValue & ~newValue));
        return;
    default:
        return; // plain registers (display, BG, windows, blending...): the PPU reads them as they are
    }
}
} // namespace sim
