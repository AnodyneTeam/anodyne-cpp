#include "credits.h"

#include <string.h>

#include "tonc.h"

#include "dialog.h"
#include "ending_data.h"
#include "ecs.h"
#include "game_state.h"
#include "save.h"
#include "soft_mosaic.hpp"
#include "sprite.h"
#include "title_data.h"
#include "world.h"

// The C#'s logic runs as it does there: its labels in float, the backdrop, overlay and screenies (UIEntities) in Fix
// (the bot's held Accept runs it at 8x, so its timing decides the frame the title comes back on). Drawn here: the labels (BG0, the font's 7px glyphs, centred like the C#'s), the
// "go" backdrop under its dim overlay (BG1), the screenies (BG2/BG3, one per frame cross-fading into the next), the
// cast's sprites and the save popup.
namespace
{
constexpr int kMaxLabels = 28;
constexpr int kLineSep = 10;   // English font: 8 + 2
constexpr int kCharW = 7;      // its spaceWidth
constexpr int kViewTop = 10;   // the 180px C# screen shows rows 10..170, as on the title
constexpr int kSbText = 31;
constexpr int kSbGo = 28;  // 32x64: 28 and 29
constexpr int kCbbGo = 2;
// The screenies: 8bpp tiles from CBB1 on, after the text's (which end at tile 65 of CBB1). Slot 0 (BG2, the even
// frames) fills the pool from the bottom, slot 1 (BG3, the odd ones) from the top, so neighbours never overlap.
// Slot 1's map takes go's screenblock: go is hidden for good before frame 1.
constexpr int kCbbShots = 1;
constexpr int kShotBlank = 65;
constexpr int kShotPoolFirst = 66;
constexpr int kShotPoolEnd = (kSbGo * 0x800 - kCbbShots * 0x4000) / 64;
constexpr int kSbShot[2] = { 30, kSbGo };
constexpr int kCols = 20;      // 160px
constexpr int kRingRows = 32;  // the BG's 256px wrap

// CreateEntities: the cast, each at a label's position plus an offset, playing its own frames.
struct CastDef final
{
    uint8_t mLabel;
    int16_t mX;
    int16_t mY;
    const char* mSheet;
    uint8_t mW;
    uint8_t mH;
    uint8_t mFps;
    bool mFlip;
    bool mFront; // SUBMENU_SLIDER: over the text
    uint8_t mCount;
    uint8_t mFrames[16];
};

const CastDef kCast[] = {
    { 16, 8, 36, "slime", 16, 16, 5, false, false, 2, { 0, 1 } },
    { 16, 134, 66, "annoyer", 16, 16, 8, false, false, 6, { 0, 1, 2, 3, 4, 5 } },
    { 16, 9, 96, "pew_laser", 16, 16, 0, false, false, 1, { 0 } },
    { 16, 134, 122, "shieldy", 16, 16, 5, false, false, 11, { 1, 2, 1, 0, 1, 2, 1, 0, 16, 17, 18 } },
    { 16, 9, 114, "pew_laser_bullet", 16, 8, 8, false, false, 2, { 0, 1 } },
    { 16, 15, 140, "sun_guy", 16, 24, 3, false, false, 5, { 0, 1, 2, 3, 4 } },
    { 16, 129, 150, "light_orb", 16, 16, 6, false, false, 8, { 0, 1, 2, 3, 4, 3, 2, 1 } },
    { 16, 12, 174, "sun_guy_wave", 128, 8, 8, false, false, 3, { 3, 4, 5 } },
    { 17, 39, 6, "f_mover", 16, 16, 4, false, false, 2, { 0, 1 } },
    { 17, 118, 30, "on_off_shooter", 16, 16, 2, false, false, 6, { 0, 1, 2, 2, 1, 0 } },
    { 17, 14, 60, "f_four_shooter", 16, 16, 3, false, false, 6, { 0, 1, 2, 2, 1, 0 } },
    { 17, 119, 80, "f_slasher", 24, 24, 3, false, false, 6, { 0, 1, 0, 1, 0, 1 } },
    { 17, 16, 112, "red_boss", 32, 32, 3, false, false, 6, { 0, 0, 1, 0, 0, 2 } },
    { 17, 8, 140, "red_boss_ripple", 48, 8, 12, false, false, 2, { 0, 1 } },
    { 18, 26, 1, "dog", 16, 16, 4, false, false, 14, { 2, 3, 2, 3, 4, 5, 4, 5, 6, 7, 6, 7, 2, 3 } },
    { 18, 122, 30, "frog", 16, 16, 2, false, false, 6, { 0, 1, 0, 1, 3, 3 } },
    { 18, 24, 54, "f_rotator", 16, 16, 10, false, false, 2, { 0, 1 } },
    { 18, 124, 84, "person", 16, 16, 5, false, false, 16, { 0, 1, 0, 1, 2, 3, 2, 3, 4, 5, 4, 5, 2, 3, 2, 3 } },
    { 18, 0, 130, "wallboss_wall", 160, 32, 4, false, false, 6, { 0, 1, 0, 1, 0, 1 } },
    { 18, 48, 130, "f_wallboss_face", 64, 32, 3, false, true, 6, { 0, 0, 1, 0, 0, 2 } },
    { 18, 12, 160, "f_wallboss_l_hand", 32, 32, 1, false, true, 4, { 0, 1, 2, 3 } },
    { 18, 122, 160, "f_wallboss_l_hand", 32, 32, 1, true, true, 4, { 0, 1, 2, 3 } },
    { 19, 32, -2, "rat", 16, 16, 5, false, false, 2, { 0, 1 } },
    { 19, 126, 30, "gas_guy", 16, 24, 4, false, false, 2, { 0, 1 } },
    { 19, 141, 44, "gas_guy_cloud", 24, 24, 3, false, false, 2, { 0, 1 } },
    { 19, 9, 56, "silverfish", 16, 16, 5, false, false, 2, { 4, 5 } },
    { 19, 141, 92, "dash_trap", 16, 16, 12, false, false, 2, { 4, 5 } },
    { 19, 9, 102, "spike_roller_horizontal", 128, 16, 5, false, false, 2, { 0, 1 } },
    { 19, 66, 150, "splitboss", 24, 32, 5, false, false, 4, { 0, 1, 2, 1 } },
    { 20, 14, -2, "dustmaid", 16, 24, 7, false, false, 11, { 0, 0, 0, 1, 2, 1, 2, 3, 4, 3, 4 } },
    { 20, 124, 30, "burst_plant", 16, 16, 8, false, false, 10, { 0, 0, 1, 0, 1, 3, 3, 3, 3, 0 } },
    { 20, 9, 80, "eye_boss_water", 24, 24, 6, false, false, 6, { 0, 1, 2, 3, 2, 1 } },
    { 20, 124, 80, "eye_boss_water", 24, 24, 6, false, false, 7, { 4, 5, 4, 5, 6, 7, 6 } },
    { 21, 14, -15, "lion", 32, 32, 4, false, false, 8, { 10, 11, 10, 11, 10, 11, 12, 12 } },
    { 21, 140, -10, "contort_big", 16, 32, 9, false, false, 4, { 0, 1, 2, 1 } },
    { 21, 140, 20, "contort_small", 16, 16, 9, false, false, 2, { 0, 1 } },
    { 21, 118, 20, "contort_small", 16, 16, 9, false, false, 2, { 2, 3 } },
    { 21, 126, 35, "contort_small", 16, 16, 9, false, false, 2, { 4, 5 } },
    { 21, 9, 80, "fire_pillar_base", 16, 16, 8, false, false, 2, { 0, 1 } },
    { 21, 9, 64, "fire_pillar", 16, 32, 9, false, true, 13, { 0, 0, 0, 0, 1, 2, 3, 4, 3, 4, 5, 6, 0 } },
    { 21, 73, 122, "arthur_javiera", 16, 32, 8, false, false, 2, { 0, 1 } },
    { 22, 18, 5, "follower_bro", 16, 24, 4, false, false, 4, { 1, 2, 1, 0 } },
    { 22, 124, 30, "sadman", 16, 16, 2, false, false, 2, { 0, 1 } },
    { 22, 10, 62, "beach_npcs", 16, 16, 3, false, false, 2, { 10, 11 } },
    { 22, 124, 60, "redwalker", 32, 48, 6, false, false, 5, { 0, 1, 2, 3, 4 } },
    { 22, 10, 108, "beach_npcs", 16, 16, 2, false, false, 1, { 0 } },
    { 23, 133, 0, "forest_npcs", 16, 16, 4, false, false, 2, { 30, 31 } },
    { 23, 6, 25, "fields_npcs", 16, 16, 4, false, false, 2, { 10, 11 } },
    { 23, 6, 55, "fields_npcs", 16, 16, 4, false, false, 2, { 50, 51 } },
    { 23, 144, 77, "fields_npcs", 16, 16, 4, false, false, 2, { 0, 1 } },
    { 23, 6, 82, "fields_npcs", 32, 32, 18, false, false, 13, { 15, 15, 15, 15, 15, 15, 15, 15, 16, 17, 17, 18, 18 } },
    { 23, 128, 117, "fields_npcs", 16, 16, 4, false, false, 2, { 20, 21 } },
    { 24, 8, -4, "forest_npcs", 16, 16, 4, false, false, 2, { 0, 1 } },
    { 24, 134, 18, "forest_npcs", 16, 16, 4, false, false, 6, { 10, 10, 11, 10, 10, 12 } },
    { 24, 8, 36, "forest_npcs", 16, 16, 4, false, false, 4, { 20, 21, 20, 22 } },
    { 24, 134, 58, "forest_npcs", 16, 16, 4, false, false, 2, { 30, 31 } },
    { 24, 8, 76, "cliffs_npcs", 16, 16, 4, false, false, 15, { 1, 3, 1, 5, 1, 1, 1, 0, 2, 0, 4, 0, 0, 1, 1 } },
    { 24, 7, 103, "suburb_walkers", 16, 16, 4, false, false, 2, { 0, 1 } },
    { 24, 140, 100, "suburb_walkers", 16, 16, 4, false, false, 2, { 9, 10 } },
    { 24, 7, 123, "suburb_walkers", 16, 16, 4, false, false, 2, { 18, 19 } },
    { 24, 140, 123, "suburb_walkers", 16, 16, 4, false, false, 2, { 27, 28 } },
    { 24, 7, 143, "suburb_walkers", 16, 16, 4, false, false, 2, { 36, 37 } },
    { 24, 140, 143, "suburb_walkers", 16, 16, 4, false, false, 2, { 45, 46 } },
    { 25, 16, -8, "chaser", 16, 32, 4, false, false, 2, { 8, 9 } },
    { 25, 7, 116, "space_npcs", 32, 32, 4, true, false, 2, { 10, 11 } },
    { 25, 120, 116, "space_npcs", 32, 32, 4, true, false, 2, { 12, 13 } },
    { 25, 24, 56, "space_npcs", 16, 16, 4, false, false, 2, { 0, 1 } },
    { 25, 124, 56, "space_npcs", 16, 16, 4, false, false, 2, { 10, 11 } },
    { 25, 24, 86, "space_npcs", 16, 16, 4, false, false, 2, { 20, 21 } },
    { 25, 124, 86, "space_npcs", 16, 16, 4, false, false, 2, { 22, 23 } },
    { 26, 43, -3, "young_player", 16, 16, 6, false, false, 2, { 0, 1 } },
    { 26, 114, 29, "mitra", 16, 16, 6, false, false, 2, { 0, 1 } },
    { 26, 24, 56, "sage", 16, 16, 6, false, false, 2, { 0, 1 } },
    { 26, 129, 89, "briar", 16, 16, 6, false, false, 2, { 0, 1 } },
    { 27, 10, 38, "dev_npcs", 16, 16, 0, false, false, 1, { 0 } },
    { 27, 140, 38, "dev_npcs", 16, 16, 0, false, false, 1, { 10 } },
};
constexpr int kCastCount = static_cast<int>(sizeof(kCast) / sizeof(kCast[0]));

// The cast on screen: one OBJ palette bank and 8 OAM entries each (the box keeps bank 15 and OAM 120-127).
constexpr int kLiveMax = 14;
struct CastSprite final
{
    Sprite mSprite;
    int mDef = -1;
    int mIndex = 0;
    float mTimer = 0;
};

struct Credits final
{
    bool mRequested = false;
    float mLabelY[kMaxLabels];
    int mBase[kMaxLabels]; // each label's y relative to the first's
    const char* mText[kMaxLabels];
    const char* mEndText = "";
    fx mBgY = kFxZero; // (UIEntity positions and opacities are Fix in the C#, so these move as Fix; the labels stay float)
    fx mDim = kFxZero;
    bool mStopScroll = false;
    int mStep = 0;
    int mIndex = 0;
    bool mPopup = false;
    bool mInputDelay = true;
    bool mYes = true;
    bool mEndShown = false;
    int mBand[kRingRows]; // which 8px band of the content each ring row holds (INT32_MIN: none)
    CastSprite mLive[kLiveMax];
    bool mCastShown[kCastCount];
    // Screenie
    int mShotFrame = -1;
    bool mShotDone = false;
    fx mShotOverlay = kFxZero; // its own opaque dim overlay
    fx mShot1 = kFxZero;       // the frame shown
    fx mShot2 = kFxZero;       // the frame before, fading out over it
} gC __attribute__((section(".sbss")));
ObjVramAllocator* gVram = nullptr;

void HideCast(CastSprite& c)
{
    Sprite_Hide(c.mSprite);
    Sprite_Destroy(c.mSprite, *gVram);
    gC.mCastShown[c.mDef] = false;
    c.mDef = -1;
}

// Shows the cast members on screen (UIEntity: position + velocity, so with their label), drops the others.
void DrawCast(float dt)
{
    for (CastSprite& c : gC.mLive)
    {
        if (c.mDef < 0)
        {
            continue;
        }
        const CastDef& d = kCast[c.mDef];
        const float y = gC.mLabelY[d.mLabel] + d.mY;
        if (gC.mPopup || y + d.mH <= kViewTop || y >= kViewTop + 160)
        {
            HideCast(c);
            continue;
        }
        if (d.mFps > 0)
        {
            c.mTimer += dt * d.mFps;
            while (c.mTimer >= 1)
            {
                c.mTimer -= 1;
                c.mIndex = (c.mIndex + 1) % d.mCount;
            }
        }
        Sprite_SetFrame(c.mSprite, d.mFrames[c.mIndex]);
        Sprite_Update(c.mSprite);
        Sprite_Draw(c.mSprite, 40 + d.mX, static_cast<int>(y) - kViewTop, d.mFlip);
    }
    if (gC.mPopup)
    {
        return;
    }
    for (int i = 0; i < kCastCount; i++)
    {
        const CastDef& d = kCast[i];
        const float y = gC.mLabelY[d.mLabel] + d.mY;
        if (gC.mCastShown[i] || y + d.mH <= kViewTop || y >= kViewTop + 160)
        {
            continue;
        }
        for (int s = 0; s < kLiveMax; s++)
        {
            CastSprite& c = gC.mLive[s];
            if (c.mDef >= 0)
            {
                continue;
            }
            if (Sprite_Create(c.mSprite, *gVram, d.mSheet, s, s * 8, d.mW, d.mH))
            {
                c.mSprite.mPriority = d.mFront ? 0 : 1;
                c.mDef = i;
                c.mIndex = 0;
                c.mTimer = 0;
                gC.mCastShown[i] = true;
                Sprite_SetFrame(c.mSprite, d.mFrames[0]);
                Sprite_Update(c.mSprite);
                Sprite_Draw(c.mSprite, 40 + d.mX, static_cast<int>(y) - kViewTop, d.mFlip);
            }
            break;
        }
    }
}

// Screenie.ProgressFrame: the next frame into its slot, the last one fading out over it.
void ProgressScreenie()
{
    gC.mShotFrame++;
    gC.mShotDone = false;
    const int f = gC.mShotFrame;
    if (f >= ending::kScreenieCount)
    {
        return;
    }
    const ending::Screenie& shot = ending::kScreenies[f];
    const int slot = f & 1;
    const int firstTile = slot == 0 ? kShotPoolFirst : kShotPoolEnd - shot.mTileCount;
    const int firstColour = ending::kGoFirstColour + ending::kGoColours + slot * ending::kScreenieColours;
    // (pixels are 0-based: add the slot's first colour, four at a time)
    uint32_t* tiles = reinterpret_cast<uint32_t*>(&tile8_mem[kCbbShots][firstTile]);
    LZ77UnCompVram(shot.mTiles, tiles);
    const uint32_t add = static_cast<uint32_t>(firstColour) * 0x01010101u;
    for (int i = 0; i < shot.mTileCount * 16; i++)
    {
        tiles[i] += add;
    }
    memcpy16(&pal_bg_mem[firstColour], shot.mPalette, shot.mColours);
    SCR_ENTRY* map = se_mem[kSbShot[slot]];
    for (int i = 0; i < 32 * 32; i++)
    {
        map[i] = kShotBlank;
    }
    for (int r = 0; r < 20; r++)
    {
        for (int c = 0; c < 20; c++)
        {
            const uint16_t e = shot.mMap[r * 20 + c];
            map[r * 32 + c] = static_cast<SCR_ENTRY>((e & 0xC00) | ((e & 0x3FF) + firstTile));
        }
    }
    if (f == 0)
    {
        gC.mShot1 = kFxZero;
    }
    else
    {
        gC.mShot1 = kFxOne;
        gC.mShot2 = kFxOne;
    }
}

// MathUtilities.MoveTo (its Fix overload) with GameTimes.DeltaTime scaled by TimeScale.
bool MoveToScaled(fx& v, fx target, fx speed, fx dt)
{
    const fx step = FxMul(speed, dt);
    if (v > target)
    {
        v = v - step < target ? target : v - step;
    }
    else
    {
        v = v + step > target ? target : v + step;
    }
    return v == target;
}

int LineCount(const char* text)
{
    int n = 1;
    for (const char* c = text; *c; c++)
    {
        if (*c == '\n' || *c == '\r')
        {
            n++;
        }
    }
    return n;
}

// The content shown: the labels (y relative to the first's), or the end label alone at the top.
int ContentTop()
{
    return gC.mEndShown ? 0 : static_cast<int>(gC.mLabelY[0]);
}

// Draws one glyph's rows that fall in band `band` (8px of content) into the ring row's tiles.
void BlitGlyph(char c, int x, int y, int band, uint32_t* rowTiles, uint32_t ink, int dy)
{
    const uint32_t* rows = reinterpret_cast<const uint32_t*>(title::kFontTiles) + title::kFontAscii[static_cast<uint8_t>(c)] * 8;
    for (int r = 0; r < 8; r++)
    {
        const int cy = y + r + dy - band * 8;
        if (cy < 0 || cy >= 8 || !rows[r])
        {
            continue;
        }
        const int tx = x >> 3;
        const int sh = (x & 7) * 4;
        const uint32_t low = rows[r] << sh;
        if (tx >= 0 && tx < kCols)
        {
            uint32_t& w = rowTiles[tx * 8 + cy];
            w = (w & ~(low * 15u)) | (low * ink);
        }
        if (sh && tx + 1 < kCols)
        {
            const uint32_t high = rows[r] >> (32 - sh);
            uint32_t& w = rowTiles[(tx + 1) * 8 + cy];
            w = (w & ~(high * 15u)) | (high * ink);
        }
    }
}

// TextWriter.Draw with CenterText: each line at 80 - width / 2, a shadow 1px down.
void DrawText(const char* text, int top, int band, uint32_t* rowTiles)
{
    int line = 0;
    const char* start = text;
    for (const char* c = text;; c++)
    {
        if (*c == '\n' || *c == '\r' || *c == 0)
        {
            const int len = static_cast<int>(c - start);
            const int y = top + line * kLineSep;
            if (y + 9 > band * 8 && y < band * 8 + 8)
            {
                const int x0 = 80 - len * kCharW / 2;
                for (int pass = 0; pass < 2; pass++)
                {
                    for (int i = 0; i < len; i++)
                    {
                        const char ch = start[i];
                        const uint8_t u = static_cast<uint8_t>(ch);
                        // (index 0 is 'a': anything else without a glyph maps there)
                        if (u < 128 && ch != ' ' && (title::kFontAscii[u] != 0 || ch == 'a'))
                        {
                            BlitGlyph(ch, x0 + i * kCharW, y, band, rowTiles, pass == 0 ? 2 : 1, pass == 0 ? 1 : 0);
                        }
                    }
                }
            }
            if (*c == 0)
            {
                break;
            }
            line++;
            start = c + 1;
        }
    }
}

void RenderBand(int band)
{
    const int ring = band & (kRingRows - 1);
    uint32_t* rowTiles = reinterpret_cast<uint32_t*>(&tile_mem[0][1 + ring * kCols]);
    memset(rowTiles, 0, kCols * 32);
    if (gC.mEndShown)
    {
        DrawText(gC.mEndText, 0, band, rowTiles);
    }
    else
    {
        for (int i = 0; i < kMaxLabels; i++)
        {
            // (the last label is hidden with the popup)
            if (i == kMaxLabels - 1 && gC.mPopup)
            {
                continue;
            }
            const int top = gC.mBase[i];
            const int bottom = top + LineCount(gC.mText[i]) * kLineSep;
            if (bottom > band * 8 && top < band * 8 + 8)
            {
                DrawText(gC.mText[i], top, band, rowTiles);
            }
        }
    }
    gC.mBand[ring] = band;
}

void Draw()
{
    // Content y v shows at C# y top + v; the GBA's row g is C# y g + kViewTop.
    const int top = ContentTop();
    const int first = kViewTop - top;
    for (int y = first; y < first + 160 + 8; y += 8)
    {
        const int band = y >= 0 ? y >> 3 : -((-y + 7) >> 3);
        if (gC.mBand[band & (kRingRows - 1)] != band)
        {
            RenderBand(band);
        }
    }
    REG_BG0HOFS = static_cast<uint16_t>(-40);
    REG_BG0VOFS = static_cast<uint16_t>(first);
    // the backdrop, its top at C# y bgY, darkened by the overlay over it
    if (gC.mPopup)
    {
        return;
    }
    REG_BG1HOFS = static_cast<uint16_t>(-40);
    REG_BG1VOFS = static_cast<uint16_t>(kViewTop - FxTrunc(gC.mBgY));
    REG_BG2HOFS = REG_BG3HOFS = static_cast<uint16_t>(-40);
    REG_BG2VOFS = REG_BG3VOFS = 0; // (the screenies sit at C# y 10)
    auto weight = [](float v) { return static_cast<uint16_t>(static_cast<int>(v * 16 + 0.5f)); };
    const int f = gC.mShotFrame;
    if (f < 0)
    {
        REG_BLDCNT = BLD_BG1 | BLD_BLACK;
        REG_BLDY = weight(FxToFloat(gC.mDim));
        return;
    }
    if (f == 0)
    {
        // The first frame and its overlay fade in together over the dimmed backdrop, which is gone once they're done.
        REG_BG2CNT = BG_CBB(kCbbShots) | BG_SBB(kSbShot[0]) | BG_8BPP | BG_REG_32x32 | BG_PRIO(2);
        if (gC.mShotOverlay >= kFxOne)
        {
            REG_DISPCNT = (REG_DISPCNT & ~DCNT_BG1) | DCNT_BG2;
            REG_BLDCNT = 0;
            return;
        }
        REG_DISPCNT = REG_DISPCNT | DCNT_BG1 | DCNT_BG2;
        REG_BLDCNT = BLD_BG2 | BLD_STD | (BLD_BG1 << 8);
        REG_BLDALPHA = BLDA_BUILD(weight(FxToFloat(gC.mShot1)), weight((1 - FxToFloat(gC.mDim)) * (1 - FxToFloat(gC.mShotOverlay)) * (1 - FxToFloat(gC.mShot1))));
        return;
    }
    // The last frame (on top) fades out over this one.
    const int upper = (f - 1) & 1;
    const int lower = f & 1;
    REG_BGCNT[2 + upper] = BG_CBB(kCbbShots) | BG_SBB(kSbShot[upper]) | BG_8BPP | BG_REG_32x32 | BG_PRIO(2);
    REG_BGCNT[2 + lower] = BG_CBB(kCbbShots) | BG_SBB(kSbShot[lower]) | BG_8BPP | BG_REG_32x32 | BG_PRIO(3);
    const uint16_t bgTop = static_cast<uint16_t>(DCNT_BG2 << upper);
    const uint16_t bgBottom = static_cast<uint16_t>(DCNT_BG2 << lower);
    REG_DISPCNT = (REG_DISPCNT & ~DCNT_BG1) | bgBottom;
    if (gC.mShot2 > kFxZero)
    {
        REG_DISPCNT = REG_DISPCNT | bgTop;
        REG_BLDCNT = (BLD_BG2 << upper) | BLD_STD | ((BLD_BG2 << lower) << 8);
        REG_BLDALPHA = BLDA_BUILD(weight(FxToFloat(gC.mShot2)), weight(1 - FxToFloat(gC.mShot2)));
    }
    else
    {
        REG_DISPCNT = REG_DISPCNT & ~bgTop;
        REG_BLDCNT = 0;
    }
}

void InvalidateRing()
{
    for (int& b : gC.mBand)
    {
        b = INT32_MIN;
    }
}

// SavePopup.Draw (its box is the menus' panel here).
void ShowPopup()
{
    const char* lines[3] = { Dialog_GetFor("misc", "any", "checkpoint", 0), Dialog_GetFor("misc", "any", "checkpoint", 1),
                             Dialog_GetFor("misc", "any", "checkpoint", 2) };
    // topLeft (40, 20 + (160 - 29) / 2) of the 180px screen
    const int left = 40;
    const int top = 20 + (160 - 29) / 2 - kViewTop;
    const int xs[3] = { left + 5, left + 5 + 14, left + 5 + 14 };
    const int ys[3] = { top, top + 8, top + 16 };
    Dialog_ShowPanel(left, top, 80, 29);
    Dialog_ShowLabels(lines, xs, ys, 3, top, left + 8, top + (gC.mYes ? 10 : 18), true);
}
} // namespace

void Credits_Request()
{
    gC.mRequested = true;
}

bool Credits_TakeRequest()
{
    const bool r = gC.mRequested;
    gC.mRequested = false;
    return r;
}

void Credits_Enter(ObjVramAllocator& vram)
{
    gC = Credits();
    gVram = &vram;
    Ecs_FlushRenderCaches(); // (the play state's sprite frames give their VRAM back)
    Mosaic_End();
    // SetState: every effect is deactivated.
    Screen_StopWave();
    Screen_EndEnding();
    Screen_SetFadeWhite(false);
    Screen_SetFade(0);
    REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ | DCNT_OBJ_1D;
    REG_WININ = REG_WINOUT = 0;
    REG_BLDCNT = 0;
    REG_BLDY = 0;
    oam_init(oam_mem, 128);
    pal_bg_mem[0] = CLR_BLACK;
    pal_bg_mem[1] = CLR_WHITE;
    pal_bg_mem[2] = RGB15(4, 4, 4);
    // The labels: 180px apart at least, the first just under the screen.
    int y = 0;
    for (int i = 0; i < kMaxLabels; i++)
    {
        gC.mText[i] = Dialog_GetFor("misc", "any", "ending", i);
        gC.mBase[i] = y;
        gC.mLabelY[i] = static_cast<float>(180 + y);
        const int h = LineCount(gC.mText[i]) * kLineSep + 5;
        y += h > 180 ? h : 180;
    }
    gC.mEndText = Dialog_GetFor("misc", "any", "ending", 25 + 4);
    gC.mBgY = Fx(-480 + 180);
    gC.mDim = FxF(0.2f);
    InvalidateRing();
    for (int i = 0; i < 32 * 32; i++)
    {
        se_mem[kSbText][i] = 0;
    }
    memset(&tile_mem[0][0], 0, sizeof(TILE));
    for (int r = 0; r < kRingRows; r++)
    {
        for (int c = 0; c < kCols; c++)
        {
            se_mem[kSbText][r * 32 + c] = static_cast<uint16_t>(1 + r * kCols + c);
        }
    }
    REG_BG0CNT = BG_CBB(0) | BG_SBB(kSbText) | BG_4BPP | BG_REG_32x32 | BG_PRIO(1);
    // The backdrop: its own tiles and colours, the whole 160x480 in a 256x512 map.
    memcpy(&pal_bg_mem[ending::kGoFirstColour], ending::kGoPalette, ending::kGoColours * 2);
    memcpy(&tile8_mem[kCbbGo][0], ending::kGoTiles, sizeof(ending::kGoTiles));
    SCR_ENTRY* goMap = se_mem[kSbGo]; // (32x64: the next screenblock holds the lower half)
    for (int i = 0; i < 32 * 64; i++)
    {
        goMap[i] = 0;
    }
    for (int r = 0; r < ending::kGoTilesH; r++)
    {
        for (int c = 0; c < ending::kGoTilesW; c++)
        {
            goMap[r * 32 + c] = ending::kGoMap[r * ending::kGoTilesW + c];
        }
    }
    REG_BG1CNT = BG_CBB(kCbbGo) | BG_SBB(kSbGo) | BG_8BPP | BG_REG_32x64 | BG_PRIO(3);
    memset(&tile8_mem[kCbbShots][kShotBlank], 0, sizeof(TILE8));
    Draw();
    REG_DISPCNT = REG_DISPCNT | DCNT_BG0 | DCNT_BG1;
}

bool Credits_Update()
{
    float timeScale = 1;
    if (!gC.mStopScroll)
    {
        timeScale = key_is_down(KEY_A) ? 8.0f : 1.0f;
        const float speed = 15 * (kUiDeltaTime * timeScale);
        for (int i = 0; i < kMaxLabels; i++)
        {
            gC.mLabelY[i] = gC.mLabelY[i] + -speed;
            if (i == kMaxLabels - 1 && gC.mLabelY[i] <= 0)
            {
                gC.mLabelY[i] = 0;
                gC.mStopScroll = true;
            }
        }
    }
    const float dt = kUiDeltaTime * timeScale;
    const fx dtFx = FxF(dt); // GameTimes.DeltaTime: the float frame time times TimeScale, made Fix
    DrawCast(dt);

    // StateLogic
    switch (gC.mStep)
    {
    case 0:
        Game_PlaySong("ending");
        gC.mStep = 1;
        [[fallthrough]];
    case 1:
        if (!MoveToScaled(gC.mBgY, kFxZero, Fx(15), dtFx))
        {
            break;
        }
        gC.mIndex = 6;
        gC.mStep = 2;
        [[fallthrough]];
    case 2:
        if (gC.mIndex < kMaxLabels)
        {
            // ProgressScreenie
            if (!(gC.mLabelY[gC.mIndex] > 180))
            {
                gC.mIndex++;
                ProgressScreenie();
            }
            break;
        }
        gC.mStep = 3;
        [[fallthrough]];
    case 3:
        if (!MoveToScaled(gC.mDim, kFxZero, FxF(0.06f), dtFx))
        {
            break;
        }
        gC.mStep = 4;
        [[fallthrough]];
    case 4:
        if (gC.mStopScroll)
        {
            break;
        }
        gC.mStep = 5;
        [[fallthrough]];
    case 5:
        if (!key_hit(KEY_A))
        {
            break;
        }
        gC.mPopup = true;
        gC.mEndShown = true;
        InvalidateRing();
        REG_DISPCNT = REG_DISPCNT & ~(DCNT_BG1 | DCNT_BG2 | DCNT_BG3);
        REG_BLDCNT = 0;
        ShowPopup();
        gC.mStep = 6;
        break;
    default:
        break;
    }

    // Screenie.Update
    if (!gC.mShotDone)
    {
        if (gC.mShotFrame == 0)
        {
            if (MoveToScaled(gC.mShot1, kFxOne, FxF(0.54f), dtFx) || MoveToScaled(gC.mShotOverlay, kFxOne, FxF(0.54f), dtFx))
            {
                gC.mShot1 = kFxOne;
                gC.mShotOverlay = kFxOne;
                gC.mShotDone = true;
            }
        }
        else if (MoveToScaled(gC.mShot2, kFxZero, FxF(0.54f), dtFx))
        {
            gC.mShotDone = true;
        }
    }

    // SavePopup.Update
    bool done = false;
    if (gC.mPopup)
    {
        if (gC.mInputDelay)
        {
            gC.mInputDelay = false;
        }
        else if (key_hit(KEY_UP) && !gC.mYes)
        {
            gC.mYes = true;
            ShowPopup();
        }
        else if (key_hit(KEY_DOWN) && gC.mYes)
        {
            gC.mYes = false;
            ShowPopup();
        }
        else if (key_hit(KEY_A))
        {
            if (gC.mYes)
            {
                Event_Set("SeenCredits", 1);
                Save_Write(Save_Slot());
            }
            Game_PlaySfx("menu_select");
            done = true;
        }
    }
    Draw();
    return done;
}

void Credits_Exit()
{
    for (CastSprite& c : gC.mLive)
    {
        if (c.mDef >= 0)
        {
            HideCast(c);
        }
    }
    Dialog_HideLabels();
    Dialog_HidePanel();
    REG_DISPCNT = REG_DISPCNT & ~(DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3);
    REG_BG0HOFS = REG_BG0VOFS = REG_BG1HOFS = REG_BG1VOFS = 0;
    REG_BG2HOFS = REG_BG2VOFS = REG_BG3HOFS = REG_BG3VOFS = 0;
    REG_BLDCNT = 0;
}
