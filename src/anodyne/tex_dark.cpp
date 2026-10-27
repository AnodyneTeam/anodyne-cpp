#include "tex_dark.h"

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
    memcpy32(pal_bg_mem, gBands[band], kEntries / 2);
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

// blendOverlay (blend.fx), 0-255.
int Overlay(int base, int blend)
{
    return base < 128 ? 2 * base * blend / 255 : 255 - 2 * (255 - base) * (255 - blend) / 255;
}

void BuildLuts(const DarkTexture& texture, int alpha, int tint, uint16_t tintColour)
{
    for (int band = 0; band < kDarkBands; band++)
    {
        const uint8_t* t = texture.mBands[band];
        for (int ch = 0; ch < 3; ch++)
        {
            for (int c5 = 0; c5 < 32; c5++)
            {
                const int c = c5 * 255 / 31;
                const int f = texture.mHardLight ? Overlay(t[ch], c) : Overlay(c, t[ch]);
                const int g = (f * t[3] + c * (255 - t[3])) / 255;
                const int final8 = (g * alpha + c * (256 - alpha)) >> 8;
                int v = (final8 * 31 + 127) / 255;
                // the flash after it (FadeEffect, as TintPalettes)
                v += (((tintColour >> (5 * ch)) & 31) - v) * tint / 32;
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
    for (int i = 0; i < kEntries; i++)
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
    // New base colours: all of them when starting, else those the game rewrote since the band went in.
    static bool sDirty[kEntries] __attribute__((section(".sbss")));
    {
        const u16 ime = REG_IME;
        REG_IME = 0;
        const int shown = gShown;
        for (int i = 0; i < kEntries; i++)
        {
            const uint16_t now = pal_bg_mem[i];
            sDirty[i] = starting || now != gBands[shown][i];
            if (sDirty[i])
            {
                gBase[i] = now;
            }
        }
        REG_IME = ime;
    }
    for (int i = 0; i < kEntries; i++)
    {
        if (lutChanged || sDirty[i])
        {
            BlendEntry(i);
        }
    }
    // The band on screen now, then the interrupt takes over from the next one.
    const u16 ime = REG_IME;
    REG_IME = 0;
    const int line = REG_VCOUNT;
    Show(line >= 160 ? 0 : line / kBandRows);
    if (starting)
    {
        gActive = true;
        irq_enable(II_VCOUNT);
    }
    REG_IME = ime;
}
