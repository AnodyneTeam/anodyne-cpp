#include "tex_dark.h"

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
// The BG colours from here on are the FG layer's own (FG_Blend leaves them alone).
int gFgFrom = 256;

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
const DarkTexture* gExtra = nullptr;
const DarkTexture* gFg = nullptr;
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

// The LUTs in two steps: the FG_Blend and the texture over it blended over each colour (redone only for new
// textures), then the alpha mix, the ExtraBlend over that and the flash (a fade changes the alpha every frame, a
// flash the tint).
// With colours the FG_Blend leaves alone, their LUT (gLut without it) takes the cache's room instead (the link-cable
// build's EWRAM has none to spare) and the cache isn't kept; with the grayscale, its second step's LUT does.
union
{
    uint8_t mTexMix[kDarkBands][3][32];
    uint8_t mLutNoFg[kDarkBands][3][32];
    uint8_t mLutGray[kDarkBands][3][32]; // per grey level (0-25): the ExtraBlend and the flash over it, 0-31
} gShared __attribute__((section(".sbss")));
uint8_t (&gTexMix)[kDarkBands][3][32] = gShared.mTexMix;
uint8_t (&gLutNoFg)[kDarkBands][3][32] = gShared.mLutNoFg;
uint8_t (&gLutGray)[kDarkBands][3][32] = gShared.mLutGray;
constexpr int kGrayLevels = 25; // blackwhite.fx: int(average * 25) / 25
// The static (on with the grayscale): static.fx's average, c * 0.845 + 0.024, goes in before the grayscale; the
// world's noise layer adds its own average back on top (World_ApplyBlendLayers: 2/16 of greys 0-30/31).
constexpr int kStaticLift = 2; // (0-31)
const DarkTexture* gMixTexture = nullptr;
const DarkTexture* gMixFg = nullptr;
bool gMixValid = false;

int Colour8(int c5)
{
    return (c5 * 255 * 0x2109) >> 18; // c5 * 255 / 31
}

const DarkTexture kPlain = {};

// blend.fx: the texture's band colour over `c` (0-255), mixed in by the band's alpha.
int TextureOver(const DarkTexture& texture, int band, int ch, int c)
{
    const uint8_t* t = texture.mBands[band];
    const int f = texture.mHardLight == kTexMultiply ? Div255(c * t[ch])
                  : texture.mHardLight                ? Overlay(t[ch], c)
                                                      : Overlay(c, t[ch]);
    return Div255(f * t[3] + c * (255 - t[3]));
}

// The darkness texture over `c` (0-255).
int DarkOver(const DarkTexture* texture, int band, int ch, int c)
{
    return texture == &kPlain ? 0 : (texture ? TextureOver(*texture, band, ch, c) : c);
}

// The alpha mix of `tex` (the darkness texture over `c`) into `c`, 0-255.
int Mix(int tex, int c, int alpha)
{
    return (tex * alpha + c * (256 - alpha)) >> 8;
}

// The ExtraBlend and the flash over `final8`, in 0-31.
int Over(int final8, int band, int ch, const DarkTexture* extra, int target, int tint)
{
    if (extra)
    {
        final8 = TextureOver(*extra, band, ch, final8); // (a full screen effect: after the darkness)
    }
    int v = Div255(final8 * 31 + 127);
    // the flash after it (FadeEffect, as TintPalettes)
    v += (target - v) * tint / 32;
    return v;
}

int Finish(int tex, int c, int alpha, int band, int ch, const DarkTexture* extra, int target, int tint)
{
    return Over(Mix(tex, c, alpha), band, ch, extra, target, tint);
}

bool gGray = false; // the LUTs were built for the grayscale

// With the grayscale (GrayScale, on with the static: a full screen effect after the darkness, before the
// ExtraBlend) gLut holds the colours up to the darkness, 0-255, and gLutGray the rest per grey level.
void BuildLuts(const DarkTexture* texture, int alpha, int tint, uint16_t tintColour, const DarkTexture* extra,
               const DarkTexture* fg, bool gray)
{
    // (FG_Blend: a game effect, before the darkness)
    auto fgOver = [fg](int band, int ch, int c) { return fg ? TextureOver(*fg, band, ch, c) : c; };
    const bool noFg = fg && gFgFrom < gBgCount && !gray;
    gGray = gray;
    if (noFg || gray)
    {
        gMixValid = false;
    }
    else if (!gMixValid || texture != gMixTexture || fg != gMixFg)
    {
        for (int band = 0; band < kDarkBands; band++)
        {
            for (int ch = 0; ch < 3; ch++)
            {
                for (int c5 = 0; c5 < 32; c5++)
                {
                    gTexMix[band][ch][c5] =
                        static_cast<uint8_t>(DarkOver(texture, band, ch, fgOver(band, ch, Colour8(c5))));
                }
            }
        }
        gMixTexture = texture;
        gMixFg = fg;
        gMixValid = true;
    }
    for (int band = 0; band < kDarkBands; band++)
    {
        for (int ch = 0; ch < 3; ch++)
        {
            const int target = (tintColour >> (5 * ch)) & 31;
            for (int c5 = 0; c5 < 32; c5++)
            {
                const int c = Colour8(c5);
                const int fgc = fgOver(band, ch, c);
                const int tex = noFg || gray ? DarkOver(texture, band, ch, fgc) : gTexMix[band][ch][c5];
                gLut[band][ch][c5] = static_cast<uint8_t>(
                    gray ? (Mix(tex, fgc, alpha) * 216 + 6 * 256) >> 8 // (the static's average)
                         : Finish(tex, fgc, alpha, band, ch, extra, target, tint));
                if (gray && c5 <= kGrayLevels)
                {
                    int v = Over(c5 * 255 / kGrayLevels, band, ch, extra, 0, 0) - kStaticLift;
                    v = v < 0 ? 0 : v;
                    gLutGray[band][ch][c5] = static_cast<uint8_t>(v + (target - v) * tint / 32);
                }
                if (noFg)
                {
                    gLutNoFg[band][ch][c5] = static_cast<uint8_t>(
                        Finish(DarkOver(texture, band, ch, c), c, alpha, band, ch, extra, target, tint));
                }
            }
        }
    }
}

uint16_t Blend(int band, uint16_t colour, bool fg)
{
    if (gGray)
    {
        const uint8_t(&lut)[3][32] = gLut[band];
        const int sum = lut[0][colour & 31] + lut[1][(colour >> 5) & 31] + lut[2][(colour >> 10) & 31];
        const int level = sum * kGrayLevels / (3 * 255);
        const uint8_t(&gray)[3][32] = gLutGray[band];
        return static_cast<uint16_t>(gray[0][level] | (gray[1][level] << 5) | (gray[2][level] << 10));
    }
    const uint8_t(&lut)[3][32] = fg ? gLut[band] : gLutNoFg[band];
    return static_cast<uint16_t>(lut[0][colour & 31] | (lut[1][(colour >> 5) & 31] << 5) |
                                 (lut[2][(colour >> 10) & 31] << 10));
}

void BlendEntry(int i)
{
    const bool fg = !gFg || i < gFgFrom || i >= 256;
    for (int band = 0; band < kDarkBands; band++)
    {
        gBands[band][i] = i == 0 ? gBase[0] : Blend(band, gBase[i], fg);
    }
}
} // namespace

const DarkTexture* TexDark_Plain()
{
    return &kPlain;
}

void TexDark_Init()
{
    irq_add(II_VCOUNT, OnVCount);
    irq_disable(II_VCOUNT);
}

void TexDark_SetBgColours(int count, int fgFrom)
{
    TexDark_Off(); // (the bands were built for the old count)
    gBgCount = count < 2 ? 2 : (count > 256 ? 256 : (count + 1) & ~1);
    gFgFrom = fgFrom;
    gMixValid = false;
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
    gExtra = nullptr;
    gFg = nullptr;
}

void TexDark_Frame(const DarkTexture* texture, int alpha, int tint, uint16_t tintColour, const DarkTexture* extra,
                   const DarkTexture* fg, bool gray)
{
    if (!texture || alpha <= 0)
    {
        texture = nullptr;
        alpha = 0;
        if (!extra && !fg && !gray)
        {
            TexDark_Off();
            return;
        }
    }
    const bool starting = !gActive;
    const bool lutChanged = starting || texture != gTexture || extra != gExtra || fg != gFg || gray != gGray ||
                            alpha != gAlpha || tint != gTint || (tint && tintColour != gTintColour);
    if (lutChanged)
    {
        BuildLuts(texture, alpha, tint, tintColour, extra, fg, gray);
        gTexture = texture;
        gExtra = extra;
        gFg = fg;
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
