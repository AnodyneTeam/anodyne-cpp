#include "pixelate_test.hpp"

#include <stdint.h>
#include <string.h>

#include "tonc.h"

#include "soft_mosaic.hpp"

namespace
{
constexpr int kViewX = 40;      // the 160px playfield is centred on the 240px screen
constexpr int kSize = 160;
constexpr int kTiles = kSize / 8;
constexpr int kSbPattern = 28;
constexpr int kMaxStride = 48;

// The same pseudo-random 4x4 cells (255 colours) as PixelateTest.PatternColour in the C#.
int PatternIndex(int x, int y)
{
    const uint32_t h = static_cast<uint32_t>(x / 4) * 73856093u ^ static_cast<uint32_t>(y / 4) * 19349663u;
    return 1 + static_cast<int>((h >> 8) % 255u);
}
} // namespace

// Block size (C# Pixelation), 1 = off. Not in the anonymous namespace so a script can find it by name.
volatile int gPixelateTestStride = 1;

void PixelateTest_Enter()
{
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG3 | DCNT_WIN0;
    REG_BG3CNT = BG_CBB(0) | BG_SBB(kSbPattern) | BG_8BPP | BG_REG_32x32 | BG_PRIO(0);
    REG_BG3HOFS = static_cast<uint16_t>(-kViewX);
    REG_BG3VOFS = 0;
    REG_WIN0H = (kViewX << 8) | (kViewX + kSize);
    REG_WIN0V = (0 << 8) | kSize;
    REG_WININ = 0x3F;
    REG_WINOUT = 0;
    REG_BLDCNT = 0;
    REG_BLDY = 0;

    memset16(pal_bg_mem, 0, 256);
    for (int idx = 1; idx < 256; idx++)
    {
        pal_bg_mem[idx] = RGB15((idx * 7) & 31, (idx * 13) & 31, (idx * 29) & 31);
    }

    // Tile 0 stays blank; the pattern's tile (tx, ty) is tile 1 + ty * 20 + tx.
    alignas(4) uint8_t tile[64];
    memset32(&tile8_mem[0][0], 0, 16);
    for (int ty = 0; ty < kTiles; ty++)
    {
        for (int tx = 0; tx < kTiles; tx++)
        {
            for (int py = 0; py < 8; py++)
            {
                for (int px = 0; px < 8; px++)
                {
                    tile[py * 8 + px] = static_cast<uint8_t>(PatternIndex(tx * 8 + px, ty * 8 + py));
                }
            }
            memcpy16(&tile8_mem[0][1 + ty * kTiles + tx], tile, 32);
        }
    }
    for (int i = 0; i < 32 * 32; i++)
    {
        se_mem[kSbPattern][i] = 0;
    }
    for (int ty = 0; ty < kTiles; ty++)
    {
        for (int tx = 0; tx < kTiles; tx++)
        {
            se_mem[kSbPattern][ty * 32 + tx] = static_cast<uint16_t>(1 + ty * kTiles + tx);
        }
    }
    oam_init(oam_mem, 128);
    gPixelateTestStride = 1;
}

void PixelateTest_Update()
{
    if (key_hit(KEY_R) && gPixelateTestStride < kMaxStride)
    {
        gPixelateTestStride = gPixelateTestStride + 1;
    }
    if (key_hit(KEY_L) && gPixelateTestStride > 1)
    {
        gPixelateTestStride = gPixelateTestStride - 1;
    }
    const int stride = gPixelateTestStride;
    if (stride <= 1)
    {
        Mosaic_End();
        return;
    }
    MosaicBgOffsets offsets;
    offsets.x[3] = static_cast<int16_t>(-kViewX);
    Mosaic_Apply(stride << 8, offsets);
}
