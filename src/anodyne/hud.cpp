#include "hud.h"

#include <string.h>

#include "tonc.h"

#include "ecs.h"
#include "game_state.h"
#include "hud_data.h"
#include "pause_draw.h"
#include "player.h"
#include "room.h"
#include "title_data.h"

namespace
{
constexpr int kPalBankLeft = 13;  // the broom icon and the mini minimap
constexpr int kPalBankOther = 14; // the frame, dust, key, label, health and the panels
constexpr int kTileBytes = 32;

// The canvases: OBJs drawn into from the CPU, then copied to VRAM when they change.
struct Canvas final
{
    int mTilesW;
    int mTilesH;
    int mBlock;
    uint32_t mPixels[32 * 8]; // (32 tiles: 32x64 or 64x32)
};

enum CanvasId : uint8_t
{
    CANVAS_LEFT,      // bank 14 over bank 13's
    CANVAS_LEFT_ICON, // bank 13
    CANVAS_HEALTH,    // bank 14
    CANVAS_COUNT,
};

// Screen positions. The left bar: the C#'s header items, in the same order down instead of across. Wide rooms: the
// C#'s header row (16px there), the items squeezed up and together to fit a 64px canvas each side.
constexpr int kLeftX = kWideRooms ? 40 : 4;
constexpr int kLeftY = 0;
constexpr int kIconX = kWideRooms ? 2 : 9; // (in the left canvases)
constexpr int kIconY = kWideRooms ? 0 : 4;
constexpr int kDustX = kIconX + 8; // (the C#'s 2,3 and 10,10)
constexpr int kDustY = kIconY + 7;
constexpr int kKeyX = kWideRooms ? 18 : 8;
constexpr int kKeyY = kWideRooms ? 1 : 24;
constexpr int kLabelX = kKeyX + 7; // (the C#'s 30,3 and 37,5)
constexpr int kLabelY = kKeyY + 2;
constexpr int kMinimapX = kWideRooms ? 39 : 3;
constexpr int kMinimapY = kWideRooms ? 0 : 42;
constexpr int kHealthX = kWideRooms ? 144 : 202;
constexpr int kHealthY = kWideRooms ? 2 : 4;
constexpr int kHealthPerRow = kWideRooms ? 8 : 4; // (the C#'s rows of 8 don't fit a side bar)
constexpr int kMaxHealth = 16;
constexpr int kGlyphTop = 2; // the font's glyphs sit 2px down their 8px line

// What the HUD shows: the canvases are redrawn when it changes.
struct HudState final
{
    int8_t mIcon; // HudIcon (-1: no broom)
    uint8_t mDustFrame;
    uint8_t mKeys;
    uint8_t mHealthHidden;
    uint8_t mMaxHealth;
    uint8_t mCurHealth;
    uint8_t mFlash;
    uint8_t mMinimapW; // 0: no minimap here
    int8_t mPlayerX;
    int8_t mPlayerY;
    uint8_t mWindowX;
    uint8_t mWindowY;
    uint8_t mCells[5 * 4]; // the window's seen counts
};

struct HudData final
{
    bool mReady;
    bool mValid;
    bool mShown;
    int mBlackBlock;
    // DustIcon: "default" (4), "full" (4 3 2 1 0), "empty" (0 1 2 3 4) at 13fps, not looping
    uint8_t mDustAnim;
    float mDustTime;
    float mFlashTime;
    HudState mState;
    Canvas mCanvas[CANVAS_COUNT];
};

enum DustAnim : uint8_t
{
    DUST_DEFAULT,
    DUST_FULL,
    DUST_EMPTY,
};

HudData gHud __attribute__((section(".sbss"))); // (EWRAM)

void Plot(Canvas& c, int x, int y, uint8_t colour)
{
    if (static_cast<unsigned>(x) >= static_cast<unsigned>(c.mTilesW * 8) ||
        static_cast<unsigned>(y) >= static_cast<unsigned>(c.mTilesH * 8))
    {
        return;
    }
    uint32_t& row = c.mPixels[((y >> 3) * c.mTilesW + (x >> 3)) * 8 + (y & 7)];
    const int shift = (x & 7) * 4;
    row = (row & ~(15u << shift)) | (static_cast<uint32_t>(colour) << shift);
}

void DrawImage(Canvas& c, const hud::Image& img, int frame, int x, int y)
{
    const int cols = img.mW / img.mFrameW;
    const int sx = (frame % cols) * img.mFrameW;
    const int sy = (frame / cols) * img.mFrameH;
    for (int r = 0; r < img.mFrameH; r++)
    {
        const uint8_t* row = img.mPixels + (sy + r) * img.mW + sx;
        for (int col = 0; col < img.mFrameW; col++)
        {
            if (row[col])
            {
                Plot(c, x + col, y + r, row[col]);
            }
        }
    }
}

// A UILabel with its shadow (the menu font).
void DrawText(Canvas& c, const char* text, int x, int y)
{
    for (int pass = 0; pass < 2; pass++)
    {
        int cx = x;
        for (const char* s = text; *s; s++, cx += 7)
        {
            const uint8_t ch = static_cast<uint8_t>(*s);
            if (ch <= ' ' || ch >= 128 || (title::kFontAscii[ch] == 0 && ch != 'a'))
            {
                continue;
            }
            const uint32_t* rows = reinterpret_cast<const uint32_t*>(title::kFontTiles) + title::kFontAscii[ch] * 8;
            for (int r = 0; r < 8; r++)
            {
                for (int col = 0; col < 8; col++)
                {
                    if ((rows[r] >> (col * 4)) & 15)
                    {
                        Plot(c, cx + col, y + kGlyphTop + r + (pass == 0 ? 1 : 0),
                             pass == 0 ? hud::kColourBlack : hud::kColourLabel);
                    }
                }
            }
        }
    }
}

void Clear(Canvas& c)
{
    memset(c.mPixels, 0, sizeof(c.mPixels));
}

void Upload(const Canvas& c)
{
    memcpy32(&tile_mem[4][c.mBlock], c.mPixels, c.mTilesW * c.mTilesH * kTileBytes / 4);
}

// PlayState.UpdateBroomIcon
int8_t BroomIcon()
{
    switch (Inventory_EquippedBroom())
    {
    case BROOM_NORMAL:
        if (Game_IsCell())
        {
            return 1;
        }
        return gGame.mMapName && strcmp(gGame.mMapName, "SUBURB") == 0 && Event_Get("Stabbed") != 0 ? 2 : 0;
    case BROOM_WIDE:
        return 3;
    case BROOM_LONG:
        return 4;
    case BROOM_TRANSFORMER:
        return 5;
    default:
        return -1;
    }
}

// DustIcon.Update: the dust in hand fills it, putting it down empties it.
uint8_t DustFrame()
{
    HudData& h = gHud;
    PlayerProbe probe;
    Player_GetProbe(probe);
    const bool holding = Player_BroomDust() != nullptr;
    if (h.mDustAnim == DUST_FULL && (probe.mJustReleasedDust || !holding))
    {
        h.mDustAnim = DUST_EMPTY;
        h.mDustTime = 0;
    }
    else if (h.mDustAnim != DUST_FULL && !probe.mJustReleasedDust && holding)
    {
        h.mDustAnim = DUST_FULL;
        h.mDustTime = 0;
    }
    h.mDustTime += kUiDeltaTime;
    if (h.mDustAnim == DUST_DEFAULT)
    {
        return 4;
    }
    int i = static_cast<int>(h.mDustTime * 13);
    i = i > 4 ? 4 : i;
    return static_cast<uint8_t>(h.mDustAnim == DUST_FULL ? 4 - i : i);
}

HudState CurrentState(bool healthHidden)
{
    HudData& h = gHud;
    HudState s;
    memset(&s, 0, sizeof(s));
    s.mIcon = BroomIcon();
    s.mDustFrame = DustFrame();
    s.mKeys = static_cast<uint8_t>(Inventory_MapKeys());
    s.mHealthHidden = healthHidden;
    s.mMaxHealth = static_cast<uint8_t>(gGame.mMaxHealth > kMaxHealth ? kMaxHealth : gGame.mMaxHealth);
    s.mCurHealth = static_cast<uint8_t>(gGame.mCurHealth);
    // HealthBarPiece "flash" (0 2 at 7fps) on the last heart
    h.mFlashTime = gGame.mCurHealth == 1 ? h.mFlashTime + kUiDeltaTime : 0;
    s.mFlash = static_cast<uint8_t>(static_cast<int>(h.mFlashTime * 7) & 1);
    int w = 0;
    int ht = 0;
    if (Minimap_Size(w, ht))
    {
        // PlayState.DrawUI: a 5x4 window of the map around the player
        s.mMinimapW = static_cast<uint8_t>(w);
        const int x = gGame.mGridX - 2 < 0 ? 0 : (gGame.mGridX - 2 > w - 5 ? w - 5 : gGame.mGridX - 2);
        const int y = gGame.mGridY - 1 < 0 ? 0 : (gGame.mGridY - 1 > ht - 4 ? ht - 4 : gGame.mGridY - 1);
        s.mWindowX = static_cast<uint8_t>(x < 0 ? 0 : x);
        s.mWindowY = static_cast<uint8_t>(y < 0 ? 0 : y);
        s.mPlayerX = static_cast<int8_t>(gGame.mGridX);
        s.mPlayerY = static_cast<int8_t>(gGame.mGridY);
        for (int row = 0; row < 4; row++)
        {
            for (int col = 0; col < 5; col++)
            {
                s.mCells[row * 5 + col] = static_cast<uint8_t>(Minimap_Seen(s.mWindowX + col, s.mWindowY + row));
            }
        }
    }
    return s;
}

void Redraw(const HudState& s)
{
    HudData& h = gHud;
    Canvas& left = h.mCanvas[CANVAS_LEFT];
    Canvas& icon = h.mCanvas[CANVAS_LEFT_ICON];
    Canvas& health = h.mCanvas[CANVAS_HEALTH];
    Clear(left);
    Clear(icon);
    Clear(health);
    if (s.mIcon >= 0)
    {
        memcpy16(&pal_obj_bank[kPalBankLeft][5], hud::kIconPalettes[s.mIcon], 11);
        DrawImage(icon, *hud::kIcons[s.mIcon], 0, kIconX, kIconY);
    }
    DrawImage(left, hud::kImgFrame, 0, kIconX, kIconY);
    DrawImage(left, hud::kImgDust, s.mDustFrame, kDustX, kDustY);
    DrawImage(left, hud::kImgKey, 0, kKeyX, kKeyY);
    char label[8] = {'x'};
    int n = 1;
    if (s.mKeys >= 10)
    {
        label[n++] = static_cast<char>('0' + s.mKeys / 10 % 10);
    }
    label[n++] = static_cast<char>('0' + s.mKeys % 10);
    label[n] = '\0';
    DrawText(left, label, kLabelX, kLabelY);
    if (s.mMinimapW)
    {
        // Minimap.Draw with bounds: the cells seen, the chests on them, the player
        for (int i = 0; i < 20; i++)
        {
            const int cx = s.mWindowX + i % 5;
            const int cy = s.mWindowY + i / 5;
            const int px = kMinimapX + (i % 5) * 5;
            const int py = kMinimapY + (i / 5) * 5;
            if (s.mCells[i] > 0)
            {
                const int tile = Minimap_Tile(cx, cy);
                if (tile >= 0)
                {
                    DrawImage(icon, hud::kImgMiniMinimap, tile, px, py);
                }
            }
            if (s.mCells[i] > 1)
            {
                DrawImage(icon, hud::kImgMiniMinimap, 18, px, py);
            }
            if (cx == s.mPlayerX && cy == s.mPlayerY)
            {
                DrawImage(icon, hud::kImgMiniMinimap, 27, px, py);
            }
        }
    }
    if (!s.mHealthHidden)
    {
        for (int i = 0; i < s.mMaxHealth; i++)
        {
            int frame = i < s.mCurHealth ? 0 : 1;
            if (i == 0 && s.mCurHealth == 1)
            {
                frame = s.mFlash ? 2 : 0;
            }
            DrawImage(health, hud::kImgHealth, frame, 8 * (i % kHealthPerRow), 7 * (i / kHealthPerRow));
        }
    }
    Upload(left);
    Upload(icon);
    Upload(health);
}

void SetObj(int oam, int block, int bank, bool wide, int x, int y)
{
    obj_set_attr(&oam_mem[oam], (wide ? ATTR0_WIDE : ATTR0_TALL) | ATTR0_4BPP | ATTR0_Y(y & 0xFF),
                 ATTR1_SIZE_64 | ATTR1_X(x & 0x1FF), ATTR2_ID(block) | ATTR2_PRIO(0) | ATTR2_PALBANK(bank));
}
} // namespace

void Hud_Init(ObjVramAllocator& vram)
{
    HudData& h = gHud;
    memset(&h, 0, sizeof(h));
    const int sizes[CANVAS_COUNT][2] = {{kWideRooms ? 8 : 4, kWideRooms ? 4 : 8}, {kWideRooms ? 8 : 4, kWideRooms ? 4 : 8}, {8, 4}};
    for (int i = 0; i < CANVAS_COUNT; i++)
    {
        h.mCanvas[i].mTilesW = sizes[i][0];
        h.mCanvas[i].mTilesH = sizes[i][1];
        h.mCanvas[i].mBlock = vram.alloc(32 * kTileBytes);
    }
    h.mBlackBlock = vram.alloc(32 * kTileBytes);
    h.mReady = h.mBlackBlock >= 0 && h.mCanvas[CANVAS_COUNT - 1].mBlock >= 0;
    if (!h.mReady)
    {
        return;
    }
    // The panels: one 32x64 block of black, shown six times a bar
    const uint32_t black = hud::kColourBlack * 0x11111111u;
    memset32(&tile_mem[4][h.mBlackBlock], black, 32 * kTileBytes / 4);
}

void Hud_Draw(bool healthHidden)
{
    HudData& h = gHud;
    if (!h.mReady)
    {
        return;
    }
    // (the banks are written every frame: other screens use them while the play state isn't shown)
    memcpy16(&pal_obj_bank[kPalBankOther][1], hud::kPalette14, sizeof(hud::kPalette14) / 2);
    memcpy16(&pal_obj_bank[kPalBankLeft][1], hud::kPaletteMinimap, sizeof(hud::kPaletteMinimap) / 2);
    if (h.mState.mIcon >= 0 && h.mValid)
    {
        memcpy16(&pal_obj_bank[kPalBankLeft][5], hud::kIconPalettes[h.mState.mIcon], 11);
    }
    const HudState s = CurrentState(healthHidden);
    if (!h.mValid || memcmp(&s, &h.mState, sizeof(s)) != 0)
    {
        Redraw(s);
        h.mState = s;
        h.mValid = true;
    }
    // (the frame, dust and key over the broom icon and the minimap; the panels under them all)
    SetObj(0, h.mCanvas[CANVAS_LEFT].mBlock, kPalBankOther, kWideRooms, kLeftX, kLeftY);
    SetObj(1, h.mCanvas[CANVAS_LEFT_ICON].mBlock, kPalBankLeft, kWideRooms, kLeftX, kLeftY);
    SetObj(2, h.mCanvas[CANVAS_HEALTH].mBlock, kPalBankOther, true, kHealthX, kHealthY);
    int oam = 3;
    if (kWideRooms)
    {
        // The header row (world.cpp's window 0): the panel as 64x32, its bottom half across the top
        for (int col = 0; col < 4; col++)
        {
            SetObj(oam++, h.mBlackBlock, kPalBankOther, true, col * 64, kRoomViewY - 32);
        }
        while (oam < kHudOamCount)
        {
            obj_hide(&oam_mem[oam++]);
        }
        h.mShown = true;
        return;
    }
    if (gSettings.mWideView)
    {
        // Just the boxes (world.cpp's windows 0/1): a 32x64 panel and one 8px on, ending at the box's bottom (the
        // right box's start above the screen and wrap round off it).
        const int bottoms[2] = {kHudWideLeftH, kHudWideRightH};
        for (int bar = 0; bar < 2; bar++)
        {
            for (int col = 0; col < 2; col++)
            {
                SetObj(oam++, h.mBlackBlock, kPalBankOther, false, bar * (240 - kHudBarW) + col * 8, bottoms[bar] - 64);
            }
        }
        while (oam < kHudOamCount)
        {
            obj_hide(&oam_mem[oam++]);
        }
        h.mShown = true;
        return;
    }
    for (int bar = 0; bar < 2; bar++)
    {
        for (int col = 0; col < 2; col++)
        {
            for (int row = 0; row < 3; row++)
            {
                SetObj(oam++, h.mBlackBlock, kPalBankOther, false, bar * 200 + col * 8, row * 64);
            }
        }
    }
    h.mShown = true;
}

void Hud_Hide()
{
    HudData& h = gHud;
    if (!h.mShown)
    {
        return;
    }
    for (int i = 0; i < kHudOamCount; i++)
    {
        obj_hide(&oam_mem[i]);
    }
    h.mShown = false;
}
