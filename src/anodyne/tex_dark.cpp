#include "tex_dark.h"

#include <string.h>

#include "ecs.h"
#include "tonc.h"

namespace
{
// The colours blended: the BG palette (but the backdrop, which also fills the margins: copied as it is) and the
// entities' OBJ banks 0-12 (the HUD's and the dialog box's aren't on the playfield). They follow each other in
// palette RAM, so one copy swaps a band in.
constexpr int kEntries = 256 + 13 * 16;
constexpr int kBandRows = 160 / kDarkBands;

alignas(4) uint16_t gBands[kDarkBands][kEntries] __attribute__((section(".sbss"))); // (EWRAM)
uint16_t gBase[kEntries] __attribute__((section(".sbss")));
uint8_t gLut[kDarkBands][3][32] __attribute__((section(".sbss")));

// The BG colours in use (the map's palette, rounded up to a word): the rest of the 256 are left alone.
int gBgCount = 256;

// The end of the OBJ colours blended: the banks the entities have drawn with since the texture went on (they take
// the lowest free bank, so usually only a few of the 13).
volatile int gEnd = kEntries;

// The next of the colours blended after i: the BG ones in use, then the OBJ banks.
int NextEntry(int i)
{
    return i + 1 == gBgCount ? 256 : i + 1;
}

volatile bool gActive = false;
volatile int gShown = 0; // the band in palette RAM
const DarkTexture* gTexture = nullptr;
int gAlpha = -1;
int gTint = -1;
uint16_t gTintColour = 0;

// The VCOUNT line that swaps band `band` in: the line before its first, band 0 in the last line of VBlank.
int SwapLine(int band)
{
    return band == 0 ? 227 : band * kBandRows - 1;
}

void Show(int band)
{
    memcpy32(pal_bg_mem, gBands[band], gBgCount / 2);
    if (gEnd > 256)
    {
        memcpy32(pal_obj_mem, gBands[band] + 256, (gEnd - 256) / 2);
    }
    gShown = band;
    REG_DISPSTAT = static_cast<uint16_t>((REG_DISPSTAT & 0xFF) | (SwapLine((band + 1) % kDarkBands) << 8));
}

void OnVCount()
{
    if (!gActive)
    {
        return;
    }
    const int line = REG_VCOUNT;
    Show(line >= 160 ? 0 : (line + 1) / kBandRows);
}

// x / 255 for 0 <= x < 65535, without a division call (Thumb has none: the LUTs are rebuilt as the darkness fades).
int Div255(int x)
{
    return (x + (x >> 8) + 1) >> 8;
}

// 2 * p / 255 for 0 <= p <= 255 * 255.
int TwiceDiv255(int p)
{
    const int q = Div255(p);
    return 2 * q + (2 * (p - 255 * q) >= 255 ? 1 : 0);
}

// blendOverlay (blend.fx), 0-255.
int Overlay(int base, int blend)
{
    return base < 128 ? TwiceDiv255(base * blend) : 255 - TwiceDiv255((255 - base) * (255 - blend));
}

// The LUTs in three steps, each redone only when what it depends on changes (a fade changes the alpha every frame,
// a flash the tint): the texture blended over each colour, then the alpha mix, then the flash.
uint8_t gTexMix[kDarkBands][3][32] __attribute__((section(".sbss")));
uint8_t gUntinted[kDarkBands][3][32] __attribute__((section(".sbss")));
const DarkTexture* gMixTexture = nullptr;
int gUntintedAlpha = -1;

int Colour8(int c5)
{
    return (c5 * 255 * 0x2109) >> 18; // c5 * 255 / 31
}

void BuildLuts(const DarkTexture& texture, int alpha, int tint, uint16_t tintColour)
{
    if (&texture != gMixTexture)
    {
        for (int band = 0; band < kDarkBands; band++)
        {
            const uint8_t* t = texture.mBands[band];
            for (int ch = 0; ch < 3; ch++)
            {
                for (int c5 = 0; c5 < 32; c5++)
                {
                    const int c = Colour8(c5);
                    const int f = texture.mHardLight ? Overlay(t[ch], c) : Overlay(c, t[ch]);
                    gTexMix[band][ch][c5] = static_cast<uint8_t>(Div255(f * t[3] + c * (255 - t[3])));
                }
            }
        }
        gMixTexture = &texture;
        gUntintedAlpha = -1;
    }
    if (alpha != gUntintedAlpha)
    {
        for (int band = 0; band < kDarkBands; band++)
        {
            for (int ch = 0; ch < 3; ch++)
            {
                for (int c5 = 0; c5 < 32; c5++)
                {
                    const int c = Colour8(c5);
                    const int final8 = (gTexMix[band][ch][c5] * alpha + c * (256 - alpha)) >> 8;
                    gUntinted[band][ch][c5] = static_cast<uint8_t>(Div255(final8 * 31 + 127));
                }
            }
        }
        gUntintedAlpha = alpha;
    }
    if (tint == 0)
    {
        memcpy(gLut, gUntinted, sizeof(gLut));
        return;
    }
    for (int band = 0; band < kDarkBands; band++)
    {
        for (int ch = 0; ch < 3; ch++)
        {
            const int target = (tintColour >> (5 * ch)) & 31;
            for (int c5 = 0; c5 < 32; c5++)
            {
                int v = gUntinted[band][ch][c5];
                // the flash after it (FadeEffect, as TintPalettes)
                v += (target - v) * tint / 32;
                gLut[band][ch][c5] = static_cast<uint8_t>(v);
            }
        }
    }
}

uint16_t Blend(int band, uint16_t colour)
{
    const uint8_t(&lut)[3][32] = gLut[band];
    return static_cast<uint16_t>(lut[0][colour & 31] | (lut[1][(colour >> 5) & 31] << 5) |
                                 (lut[2][(colour >> 10) & 31] << 10));
}

void BlendEntry(int i)
{
    for (int band = 0; band < kDarkBands; band++)
    {
        gBands[band][i] = i == 0 ? gBase[0] : Blend(band, gBase[i]);
    }
}
} // namespace

void TexDark_Init()
{
    irq_add(II_VCOUNT, OnVCount);
    irq_disable(II_VCOUNT);
}

void TexDark_SetBgColours(int count)
{
    TexDark_Off(); // (the bands were built for the old count)
    gBgCount = count < 2 ? 2 : (count > 256 ? 256 : (count + 1) & ~1);
}

bool TexDark_Active()
{
    return gActive;
}

void TexDark_Off()
{
    if (!gActive)
    {
        return;
    }

    const u16 ime = REG_IME;
    REG_IME = 0;
    irq_disable(II_VCOUNT);
    gActive = false;
    // (only the colours still showing the band: the others were rewritten since)
    for (int i = 0; i < gEnd; i = NextEntry(i))
    {
        if (pal_bg_mem[i] == gBands[gShown][i])
        {
            pal_bg_mem[i] = gBase[i];
        }
    }
    REG_IME = ime;
    gTexture = nullptr;
}

void TexDark_Frame(const DarkTexture* texture, int alpha, int tint, uint16_t tintColour)
{
    if (!texture || alpha <= 0)
    {
        TexDark_Off();
        return;
    }
    const bool starting = !gActive;
    const bool lutChanged =
        starting || texture != gTexture || alpha != gAlpha || tint != gTint || (tint && tintColour != gTintColour);
    if (lutChanged)
    {
        BuildLuts(*texture, alpha, tint, tintColour);
        gTexture = texture;
        gAlpha = alpha;
        gTint = tint;
        gTintColour = tintColour;
    }
    // The OBJ banks in use only grow while it's on (those it stops covering would keep a band's colours).
    const int oldEnd = starting ? 256 : gEnd;
    const int want = 256 + 16 * Ecs_ObjBanksInUse();
    const int end = want > oldEnd ? want : oldEnd;
    // New base colours: all of them when starting (or newly covered), else those the game rewrote since the band
    // went in. Compared a word (two colours) at a time: both ranges are whole words.
    static uint16_t sDirty[kEntries] __attribute__((section(".sbss")));
    int dirtyCount = 0;
    const int ranges[2][2] = { { 0, gBgCount }, { 256, end } };
    {
        const u16 ime = REG_IME;
        REG_IME = 0;
        const uint32_t* const pal = reinterpret_cast<const uint32_t*>(pal_bg_mem);
        const uint32_t* const shown = reinterpret_cast<const uint32_t*>(gBands[gShown]);
        for (const auto& range : ranges)
        {
            for (int i = range[0]; i < range[1]; i += 2)
            {
                const uint32_t now = pal[i >> 1];
                const bool all = starting || i >= oldEnd;
                if (!all && now == shown[i >> 1])
                {
                    continue;
                }
                for (int half = 0; half < 2; half++)
                {
                    const uint16_t colour = static_cast<uint16_t>(now >> (16 * half));
                    if (all || colour != static_cast<uint16_t>(shown[i >> 1] >> (16 * half)))
                    {
                        gBase[i + half] = colour;
                        sDirty[dirtyCount++] = static_cast<uint16_t>(i + half);
                    }
                }
            }
        }
        REG_IME = ime;
    }
    if (lutChanged)
    {
        for (const auto& range : ranges)
        {
            for (int i = range[0]; i < range[1]; i++)
            {
                BlendEntry(i);
            }
        }
    }
    else
    {
        for (int k = 0; k < dirtyCount; k++)
        {
            BlendEntry(sDirty[k]);
        }
    }
    // The band on screen now, then the interrupt takes over from the next one.
    const u16 ime = REG_IME;
    REG_IME = 0;
    gEnd = end;
    const int line = REG_VCOUNT;
    Show(line >= 160 ? 0 : line / kBandRows);
    if (starting)
    {
        gActive = true;
        irq_enable(II_VCOUNT);
    }
    REG_IME = ime;
}
