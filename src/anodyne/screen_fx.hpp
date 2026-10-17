#pragma once

#include "tonc.h"

// Full-screen effects shared by the title and the world transitions.

// Darkens every layer and the backdrop towards black by level/16 (0 = off). A forced blank would show white instead.
// Windows need WIN_BLD in WININ/WINOUT for it to reach inside/outside them (the world sets both).
inline void ScreenDarken(int level)
{
    REG_BLDCNT = level > 0 ? 0xFF : 0; // BG0-3 + OBJ + backdrop as target 1, mode 3 (darken)
    REG_BLDY = static_cast<uint16_t>(level);
}

// Hardware mosaic on all four BGs and every OBJ, standing in for the C# pixelation shader (block size `stride`
// px, capped at the GBA's 16). Sprite_Draw rewrites attr0 and the world rewrites BGxCNT, so call this every frame
// after drawing.
inline void ApplyMosaic(int stride)
{
    if (stride > 16)
    {
        stride = 16;
    }
    const int v = stride - 1;
    REG_MOSAIC = static_cast<uint16_t>(v | (v << 4) | (v << 8) | (v << 12));
    for (int bg = 0; bg < 4; bg++)
    {
        REG_BGCNT[bg] |= BG_MOSAIC;
    }
    for (int i = 0; i < 128; i++)
    {
        oam_mem[i].attr0 |= ATTR0_MOSAIC;
    }
}

inline void ClearMosaic()
{
    REG_MOSAIC = 0;
    for (int bg = 0; bg < 4; bg++)
    {
        REG_BGCNT[bg] &= ~BG_MOSAIC;
    }
    for (int i = 0; i < 128; i++)
    {
        oam_mem[i].attr0 &= ~ATTR0_MOSAIC;
    }
}
