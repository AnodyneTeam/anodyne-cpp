// The GBA BIOS calls (tonc_bios.s / tonc_bios_ex.s) in C. IntrWait/VBlankIntrWait run the simulated hardware until
// the interrupt arrives, which is where the frame is drawn and shown.

#include "sim_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
#include "tonc_memmap.h"
#include "tonc_types.h"
#include "tonc_bios.h"
}

namespace
{
// BIOS sine table resolution: 256 steps per turn, 2.14 fixed point.
int16_t BiosSin(uint16_t alpha)
{
    return static_cast<int16_t>(lround(sin((alpha >> 8) * (2.0 * M_PI / 256.0)) * 16384.0));
}

int16_t BiosCos(uint16_t alpha)
{
    return static_cast<int16_t>(lround(cos((alpha >> 8) * (2.0 * M_PI / 256.0)) * 16384.0));
}

uint32_t ReadU32(const uint8_t* p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

void Lz77(const void* src, void* dst)
{
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint32_t header = ReadU32(s);
    if ((header & 0xF0) != 0x10)
    {
        sim::Fatal("LZ77UnComp: bad header 0x%08X at %p", header, src);
    }
    const uint32_t size = header >> 8;
    s += 4;
    uint32_t out = 0;
    while (out < size)
    {
        uint8_t flags = *s++;
        for (int bit = 0; bit < 8 && out < size; bit++, flags <<= 1)
        {
            if (flags & 0x80)
            {
                const uint32_t len = static_cast<uint32_t>(s[0] >> 4) + 3;
                const uint32_t disp = ((static_cast<uint32_t>(s[0] & 15) << 8) | s[1]) + 1;
                s += 2;
                if (disp > out)
                {
                    sim::Fatal("LZ77UnComp: back reference before the start of the output (%p)", src);
                }
                for (uint32_t i = 0; i < len && out < size; i++, out++)
                {
                    d[out] = d[out - disp];
                }
            }
            else
            {
                d[out++] = *s++;
            }
        }
    }
}

void RunLength(const void* src, void* dst)
{
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint32_t size = ReadU32(s) >> 8;
    s += 4;
    uint32_t out = 0;
    while (out < size)
    {
        const uint8_t flag = *s++;
        if (flag & 0x80)
        {
            const uint32_t len = (flag & 0x7Fu) + 3;
            const uint8_t value = *s++;
            for (uint32_t i = 0; i < len && out < size; i++)
            {
                d[out++] = value;
            }
        }
        else
        {
            const uint32_t len = (flag & 0x7Fu) + 1;
            for (uint32_t i = 0; i < len && out < size; i++)
            {
                d[out++] = *s++;
            }
        }
    }
}
} // namespace

extern "C"
{
void SoftReset(void)
{
    sim::Log("SoftReset");
    exit(0);
}

void RegisterRamReset(u32 flags)
{
    if (flags & 0x01)
    {
        memset(reinterpret_cast<void*>(static_cast<uintptr_t>(SIM_EWRAM_ORIGIN)), 0, SIM_EWRAM_LENGTH);
    }
    if (flags & 0x02)
    {
        memset(reinterpret_cast<void*>(static_cast<uintptr_t>(SIM_IWRAM_ORIGIN)), 0, SIM_IWRAM_LENGTH - 0x200);
    }
    if (flags & 0x04)
    {
        memset(sim::Pal(), 0, sim::kPalSize);
    }
    if (flags & 0x08)
    {
        memset(sim::Vram(), 0, sim::kVramSize);
    }
    if (flags & 0x10)
    {
        memset(sim::Oam(), 0, sim::kOamSize);
    }
}

void IntrWait(u32 flagClear, u32 irq)
{
    vu16& biosFlags = REG_IFBIOS;
    if (flagClear)
    {
        biosFlags = static_cast<u16>(biosFlags & ~irq);
    }
    const uint64_t startFrame = sim::FrameCount();
    while (!(biosFlags & irq))
    {
        sim::StepLine();
        if (sim::FrameCount() - startFrame > 2 && (!(sim::Io16(0x208) & 1) || !(sim::Io16(0x200) & irq)))
        {
            sim::Fatal("IntrWait(0x%X) with that interrupt disabled (IME=%u IE=0x%04X): this hangs the GBA", irq,
                       sim::Io16(0x208), sim::Io16(0x200));
        }
    }
    biosFlags = static_cast<u16>(biosFlags & ~irq);
}

void VBlankIntrWait(void)
{
    IntrWait(1, 1);
}

void Halt(void)
{
    // Until any enabled interrupt is requested.
    const uint16_t ifBefore = sim::Io16(0x202);
    while (!((sim::Io16(0x202) & ~ifBefore) & sim::Io16(0x200)))
    {
        sim::StepLine();
    }
}

void Stop(void)
{
    Halt();
}

s32 Div(s32 num, s32 den)
{
    if (den == 0)
    {
        sim::Fatal("Div by zero (the BIOS would hang)");
    }
    return num / den;
}

s32 DivArm(s32 den, s32 num)
{
    return Div(num, den);
}

int DivSafe(int num, int den)
{
    if (den == 0)
    {
        return num < 0 ? INT32_MIN : INT32_MAX;
    }
    return num / den;
}

int Mod(int num, int den)
{
    if (den == 0)
    {
        sim::Fatal("Mod by zero (the BIOS would hang)");
    }
    return num % den;
}

u32 DivAbs(int num, int den)
{
    return static_cast<u32>(abs(Div(num, den)));
}

int DivArmMod(int den, int num)
{
    return Mod(num, den);
}

u32 DivArmAbs(int den, int num)
{
    return DivAbs(num, den);
}

u32 Sqrt(u32 num)
{
    u32 r = static_cast<u32>(sqrt(static_cast<double>(num)));
    while (static_cast<uint64_t>(r) * r > num)
    {
        r--;
    }
    while (static_cast<uint64_t>(r + 1) * (r + 1) <= num)
    {
        r++;
    }
    return r;
}

s16 ArcTan(s16 dydx)
{
    // 1.14 tangent in, -pi/2..pi/2 as -0x4000..0x4000 out.
    return static_cast<s16>(lround(atan(dydx / 16384.0) * (32768.0 / M_PI)));
}

s16 ArcTan2(s16 x, s16 y)
{
    double a = atan2(static_cast<double>(y), static_cast<double>(x));
    if (a < 0)
    {
        a += 2.0 * M_PI;
    }
    return static_cast<s16>(static_cast<u16>(lround(a * (65536.0 / (2.0 * M_PI))) & 0xFFFF));
}

void CpuSet(const void* src, void* dst, u32 mode)
{
    const u32 count = mode & 0x1FFFFF;
    const bool fill = (mode & (1u << 24)) != 0;
    if (mode & (1u << 26))
    {
        const u32* s = static_cast<const u32*>(src);
        u32* d = static_cast<u32*>(dst);
        const u32 first = *s;
        for (u32 i = 0; i < count; i++)
        {
            d[i] = fill ? first : s[i];
        }
    }
    else
    {
        const u16* s = static_cast<const u16*>(src);
        u16* d = static_cast<u16*>(dst);
        const u16 first = *s;
        for (u32 i = 0; i < count; i++)
        {
            d[i] = fill ? first : s[i];
        }
    }
}

void CpuFastSet(const void* src, void* dst, u32 mode)
{
    // Words, in blocks of 8.
    const u32 count = ((mode & 0x1FFFFF) + 7) & ~7u;
    CpuSet(src, dst, (mode & (1u << 24)) | (1u << 26) | count);
}

void CpuFastFill(u32 wd, void* dst, u32 mode)
{
    CpuFastSet(&wd, dst, (mode & 0x1FFFFF) | (1u << 24));
}

u32 BiosCheckSum(void)
{
    return 0xBAAE187F; // the GBA BIOS
}

void ObjAffineSet(const ObjAffineSource* src, void* dst, s32 num, s32 offset)
{
    uint8_t* out = static_cast<uint8_t*>(dst);
    for (s32 i = 0; i < num; i++)
    {
        const int32_t sn = BiosSin(src[i].alpha);
        const int32_t cs = BiosCos(src[i].alpha);
        const s16 m[4] = {
            static_cast<s16>((src[i].sx * cs) >> 14),
            static_cast<s16>((-src[i].sx * sn) >> 14),
            static_cast<s16>((src[i].sy * sn) >> 14),
            static_cast<s16>((src[i].sy * cs) >> 14),
        };
        for (int k = 0; k < 4; k++)
        {
            memcpy(out + k * offset, &m[k], 2);
        }
        out += 4 * offset;
    }
}

void BgAffineSet(const BgAffineSource* src, BgAffineDest* dst, s32 num)
{
    for (s32 i = 0; i < num; i++)
    {
        const int32_t sn = BiosSin(src[i].alpha);
        const int32_t cs = BiosCos(src[i].alpha);
        BgAffineDest& d = dst[i];
        d.pa = static_cast<s16>((src[i].sx * cs) >> 14);
        d.pb = static_cast<s16>((-src[i].sx * sn) >> 14);
        d.pc = static_cast<s16>((src[i].sy * sn) >> 14);
        d.pd = static_cast<s16>((src[i].sy * cs) >> 14);
        d.dx = src[i].tex_x - (d.pa * src[i].scr_x + d.pb * src[i].scr_y);
        d.dy = src[i].tex_y - (d.pc * src[i].scr_x + d.pd * src[i].scr_y);
    }
}

void BitUnPack(const void* src, void* dst, const BUP* bup)
{
    const uint8_t* s = static_cast<const uint8_t*>(src);
    u32* d = static_cast<u32*>(dst);
    const u32 srcBpp = bup->src_bpp;
    const u32 dstBpp = bup->dst_bpp;
    const u32 offset = bup->dst_ofs & 0x7FFFFFFF;
    const bool zeroToo = (bup->dst_ofs & 0x80000000u) != 0;
    u32 acc = 0;
    u32 accBits = 0;
    for (u32 i = 0; i < bup->src_len; i++)
    {
        for (u32 b = 0; b < 8; b += srcBpp)
        {
            u32 v = (s[i] >> b) & ((1u << srcBpp) - 1);
            if (v || zeroToo)
            {
                v += offset;
            }
            acc |= (v & (dstBpp == 32 ? 0xFFFFFFFFu : ((1u << dstBpp) - 1))) << accBits;
            accBits += dstBpp;
            if (accBits >= 32)
            {
                *d++ = acc;
                acc = 0;
                accBits = 0;
            }
        }
    }
}

void LZ77UnCompWram(const void* src, void* dst)
{
    Lz77(src, dst);
}

void LZ77UnCompVram(const void* src, void* dst)
{
    Lz77(src, dst);
}

void HuffUnComp(const void* src, void* dst)
{
    (void)src;
    (void)dst;
    sim::Fatal("HuffUnComp is not simulated");
}

void RLUnCompWram(const void* src, void* dst)
{
    RunLength(src, dst);
}

void RLUnCompVram(const void* src, void* dst)
{
    RunLength(src, dst);
}

void Diff8bitUnFilterWram(const void* src, void* dst)
{
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const u32 size = ReadU32(s) >> 8;
    s += 4;
    uint8_t acc = 0;
    for (u32 i = 0; i < size; i++)
    {
        acc = static_cast<uint8_t>(acc + s[i]);
        d[i] = acc;
    }
}

void Diff8bitUnFilterVram(const void* src, void* dst)
{
    Diff8bitUnFilterWram(src, dst);
}

void Diff16bitUnFilter(const void* src, void* dst)
{
    const uint8_t* s = static_cast<const uint8_t*>(src);
    u16* d = static_cast<u16*>(dst);
    const u32 size = ReadU32(s) >> 8;
    s += 4;
    u16 acc = 0;
    for (u32 i = 0; i < size / 2; i++)
    {
        u16 v;
        memcpy(&v, s + 2 * i, 2);
        acc = static_cast<u16>(acc + v);
        d[i] = acc;
    }
}

void SoundBias(u32 bias)
{
    sim::IoSet16(0x88, static_cast<uint16_t>((sim::Io16(0x88) & ~0x3FFu) | (bias ? 0x200u : 0u)));
}

void VBlankIntrDelay(u32 count)
{
    for (u32 i = 0; i < count; i++)
    {
        VBlankIntrWait();
    }
}
}
