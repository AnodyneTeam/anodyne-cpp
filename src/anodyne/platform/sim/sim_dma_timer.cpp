// DMA channels 0-3 and timers 0-3.

#include "sim_internal.h"

#include <string.h>

namespace sim
{
namespace
{
// ---- DMA ----
enum : uint16_t
{
    DMA_DST_SHIFT = 5,
    DMA_SRC_SHIFT = 7,
    DMA_REPEAT_BIT = 1 << 9,
    DMA_WORD_BIT = 1 << 10,
    DMA_TIMING_SHIFT = 12,
    DMA_IRQ_BIT = 1 << 14,
    DMA_ENABLE_BIT = 1 << 15,
};
enum Timing
{
    TIMING_NOW = 0,
    TIMING_VBLANK = 1,
    TIMING_HBLANK = 2,
    TIMING_SPECIAL = 3, // sound FIFO (channels 1/2), video capture (3)
};

struct Channel final
{
    uint32_t mSrc = 0; // internal registers, latched when the channel is enabled
    uint32_t mDst = 0;
    uint32_t mCount = 0;
    bool mRunning = false;
};
Channel gDma[4];

uint32_t RegBase(int ch)
{
    return 0xB0 + 12 * static_cast<uint32_t>(ch);
}

uint32_t MaxCount(int ch)
{
    return ch == 3 ? 0x10000 : 0x4000;
}

// Reads/writes one unit for the DMA. IO goes through the simulator's view so register side effects happen.
uint32_t BusRead(uint32_t addr, bool word)
{
    if (addr >= kIoBase && addr < kIoBase + kIoSize)
    {
        return word ? Io32(addr - kIoBase) : Io16(addr - kIoBase);
    }
    const void* p = reinterpret_cast<const void*>(static_cast<uintptr_t>(addr));
    uint32_t v = 0;
    memcpy(&v, p, word ? 4 : 2);
    return v;
}

void BusWrite(uint32_t addr, uint32_t value, bool word)
{
    if (addr >= kIoBase && addr < kIoBase + kIoSize)
    {
        const uint32_t ofs = addr - kIoBase;
        if (ofs == 0xA0 || ofs == 0xA4)
        {
            Apu_PushFifo(static_cast<int>((ofs - 0xA0) / 4), value);
            return;
        }
        for (uint32_t i = 0; i < (word ? 4u : 2u); i += 2)
        {
            const uint16_t old = Io16(ofs + i);
            const uint16_t now = static_cast<uint16_t>(value >> (8 * i));
            IoSet16(ofs + i, now);
            Io_OnWrite16(ofs + i, old, now);
        }
        return;
    }
    void* p = reinterpret_cast<void*>(static_cast<uintptr_t>(addr));
    memcpy(p, &value, word ? 4 : 2);
}

int Step(int control)
{
    switch (control)
    {
    case 0:
    case 3:
        return 1;
    case 1:
        return -1;
    default:
        return 0;
    }
}

void Finish(int ch)
{
    const uint32_t base = RegBase(ch);
    const uint16_t cnt = Io16(base + 10);
    const int timing = (cnt >> DMA_TIMING_SHIFT) & 3;
    if ((cnt & DMA_REPEAT_BIT) && timing != TIMING_NOW)
    {
        gDma[ch].mCount = Io16(base + 8) ? Io16(base + 8) : MaxCount(ch);
        if (((cnt >> DMA_DST_SHIFT) & 3) == 3)
        {
            gDma[ch].mDst = Io32(base + 4);
        }
    }
    else
    {
        gDma[ch].mRunning = false;
        IoSet16(base + 10, static_cast<uint16_t>(cnt & ~DMA_ENABLE_BIT));
    }
    if (cnt & DMA_IRQ_BIT)
    {
        Irq_Raise(static_cast<uint16_t>(IRQ_DMA0_BIT << ch));
    }
}

void Transfer(int ch, uint32_t units, bool forceWord, bool fixedDst)
{
    Channel& c = gDma[ch];
    const uint16_t cnt = Io16(RegBase(ch) + 10);
    const bool word = forceWord || (cnt & DMA_WORD_BIT);
    const int size = word ? 4 : 2;
    const int dstStep = fixedDst ? 0 : Step((cnt >> DMA_DST_SHIFT) & 3) * size;
    const int srcStep = Step((cnt >> DMA_SRC_SHIFT) & 3) * size;
    for (uint32_t i = 0; i < units; i++)
    {
        BusWrite(c.mDst, BusRead(c.mSrc, word), word);
        c.mSrc += static_cast<uint32_t>(srcStep);
        c.mDst += static_cast<uint32_t>(dstStep);
    }
}

void Run(int ch)
{
    Transfer(ch, gDma[ch].mCount, false, false);
    Finish(ch);
}

void RunTiming(int timing)
{
    for (int ch = 0; ch < 4; ch++)
    {
        if (gDma[ch].mRunning && ((Io16(RegBase(ch) + 10) >> DMA_TIMING_SHIFT) & 3) == timing)
        {
            Run(ch);
        }
    }
}

// ---- Timers ----
struct Timer final
{
    uint16_t mReload = 0;
    uint32_t mCounter = 0;
    uint32_t mSubCycles = 0; // cycles towards the next tick at the prescaler rate
    bool mRunning = false;
};
Timer gTimers[4];

const uint32_t kPrescale[4] = { 1, 64, 256, 1024 };

uint16_t TimerCnt(int t)
{
    return Io16(0x102 + 4 * static_cast<uint32_t>(t));
}

void Tick(int t, uint32_t ticks);

void Overflow(int t)
{
    Apu_OnTimerOverflow(t);
    if (TimerCnt(t) & 0x40)
    {
        Irq_Raise(static_cast<uint16_t>(IRQ_TIMER0_BIT << t));
    }
    if (t < 3 && gTimers[t + 1].mRunning && (TimerCnt(t + 1) & 0x04))
    {
        Tick(t + 1, 1); // count-up (cascade) timer
    }
}

void Tick(int t, uint32_t ticks)
{
    Timer& tm = gTimers[t];
    while (ticks > 0)
    {
        const uint32_t toOverflow = 0x10000 - tm.mCounter;
        if (ticks < toOverflow)
        {
            tm.mCounter += ticks;
            break;
        }
        ticks -= toOverflow;
        tm.mCounter = tm.mReload;
        Overflow(t);
    }
    IoSet16(0x100 + 4 * static_cast<uint32_t>(t), static_cast<uint16_t>(tm.mCounter));
}
} // namespace

void Dma_OnControlWrite(int ch, uint16_t oldCnt, uint16_t newCnt)
{
    const uint32_t base = RegBase(ch);
    Channel& c = gDma[ch];
    if (!(newCnt & DMA_ENABLE_BIT))
    {
        c.mRunning = false;
        return;
    }
    if (oldCnt & DMA_ENABLE_BIT)
    {
        return; // already running: the hardware only latches on the 0 -> 1 edge
    }
    c.mSrc = Io32(base);
    c.mDst = Io32(base + 4);
    c.mCount = Io16(base + 8) ? Io16(base + 8) : MaxCount(ch);
    c.mRunning = true;
    // Unaligned addresses are forced to the unit size, like the hardware.
    const uint32_t align = (newCnt & DMA_WORD_BIT) ? ~3u : ~1u;
    c.mSrc &= align;
    c.mDst &= align;
    if (((newCnt >> DMA_TIMING_SHIFT) & 3) == TIMING_NOW)
    {
        Run(ch);
    }
}

void Dma_OnVBlank()
{
    RunTiming(TIMING_VBLANK);
}

void Dma_OnHBlank()
{
    RunTiming(TIMING_HBLANK);
}

void Dma_OnFifoRequest(int fifo)
{
    const uint32_t fifoAddr = kIoBase + 0xA0 + 4 * static_cast<uint32_t>(fifo);
    for (int ch = 1; ch <= 2; ch++)
    {
        const uint16_t cnt = Io16(RegBase(ch) + 10);
        if (gDma[ch].mRunning && ((cnt >> DMA_TIMING_SHIFT) & 3) == TIMING_SPECIAL && gDma[ch].mDst == fifoAddr)
        {
            // Sound DMA: always 4 words to the fixed FIFO address, whatever the count/size bits say.
            Transfer(ch, 4, true, true);
            if (cnt & DMA_IRQ_BIT)
            {
                Irq_Raise(static_cast<uint16_t>(IRQ_DMA0_BIT << ch));
            }
            if (!(cnt & DMA_REPEAT_BIT))
            {
                gDma[ch].mRunning = false;
                IoSet16(RegBase(ch) + 10, static_cast<uint16_t>(cnt & ~DMA_ENABLE_BIT));
            }
            return;
        }
    }
}

void Timer_OnReloadWrite(int t, uint16_t value)
{
    gTimers[t].mReload = value;
}

void Timer_OnControlWrite(int t, uint16_t oldCnt, uint16_t newCnt)
{
    Timer& tm = gTimers[t];
    const bool wasOn = (oldCnt & 0x80) != 0;
    const bool on = (newCnt & 0x80) != 0;
    if (on && !wasOn)
    {
        tm.mCounter = tm.mReload; // starting a timer loads the reload value
        tm.mSubCycles = 0;
    }
    tm.mRunning = on;
    IoSet16(0x100 + 4 * static_cast<uint32_t>(t), static_cast<uint16_t>(tm.mCounter));
}

uint16_t Timer_Counter(int t)
{
    return static_cast<uint16_t>(gTimers[t].mCounter);
}

void Timer_Advance(int cycles)
{
    for (int t = 0; t < 4; t++)
    {
        Timer& tm = gTimers[t];
        const uint16_t cnt = TimerCnt(t);
        if (!tm.mRunning || (t > 0 && (cnt & 0x04)))
        {
            continue; // stopped, or a cascade timer (ticked by the one below)
        }
        const uint32_t pre = kPrescale[cnt & 3];
        tm.mSubCycles += static_cast<uint32_t>(cycles);
        const uint32_t ticks = tm.mSubCycles / pre;
        tm.mSubCycles -= ticks * pre;
        if (ticks)
        {
            Tick(t, ticks);
        }
    }
}
} // namespace sim
