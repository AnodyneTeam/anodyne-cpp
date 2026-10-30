// C versions of libtonc's ARM assembly routines (tonc_memcpy.s, tonc_memset.s, clr_*_fast.s, tonc_nocash.s and
// the tte *_fast.s glyph renderers), with the same semantics: counts are in halfwords/words, not bytes.
#include <stdio.h>
#include <string.h>

#include "tonc.h"

void memcpy16(void* dst, const void* src, uint hwcount)
{
    u16* d = (u16*)dst;
    const u16* s = (const u16*)src;
    for (uint i = 0; i < hwcount; i++)
    {
        d[i] = s[i];
    }
}

void memset16(void* dst, u16 hw, uint hwcount)
{
    u16* d = (u16*)dst;
    for (uint i = 0; i < hwcount; i++)
    {
        d[i] = hw;
    }
}

void memcpy32(void* dst, const void* src, uint wcount)
{
    u32* d = (u32*)dst;
    const u32* s = (const u32*)src;
    for (uint i = 0; i < wcount; i++)
    {
        d[i] = s[i];
    }
}

void memset32(void* dst, u32 wd, uint wcount)
{
    u32* d = (u32*)dst;
    for (uint i = 0; i < wcount; i++)
    {
        d[i] = wd;
    }
}

void clr_blend_fast(COLOR* srca, COLOR* srcb, COLOR* dst, uint nclrs, u32 alpha)
{
    clr_blend(srca, srcb, dst, nclrs, alpha);
}

void clr_fade_fast(COLOR* src, COLOR clr, COLOR* dst, uint nclrs, u32 alpha)
{
    clr_fade(src, clr, dst, nclrs, alpha);
}

int nocash_puts(const char* str)
{
    fprintf(stderr, "[nocash] %s\n", str);
    return 0;
}

void nocash_message(void)
{
    nocash_puts(nocash_buffer);
}

void bmp8_drawg_b1cts_fast(uint gid)
{
    bmp8_drawg_b1cts(gid);
}

void chr4c_drawg_b1cts_fast(uint gid)
{
    chr4c_drawg_b1cts(gid);
}

void chr4c_drawg_b4cts_fast(uint gid)
{
    chr4c_drawg_b4cts(gid);
}

void chr4r_drawg_b1cts_fast(uint gid)
{
    chr4r_drawg_b1cts(gid);
}
