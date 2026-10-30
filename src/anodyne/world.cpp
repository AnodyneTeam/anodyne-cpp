#include "world.h"

#include "dialog.h"
#include "entities.h"
#include "hud.h"
#include "pause.h"
#include "pause_draw.h"
#include "player.h"
#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_manager.h"
#include "ecs.h"
#include "game_state.h"
#include "screen_fx.hpp"
#include "soft_mosaic.hpp"
#include "sprite.h"
#include "tex_dark.h"
#include "tonc.h"

namespace
{
// ---- Constants from AnodyneSharp (GameConstants.cs / PlayState.cs) ---------------------------------------------
constexpr int kTileSize = 16;
constexpr int kScreenPx = 160;               // SCREEN_WIDTH/HEIGHT_IN_PIXELS
constexpr int kScreenCells = kScreenPx / kTileSize; // 10x10 tiles per screen
constexpr int kViewX = (240 - kScreenPx) / 2; // playfield is centred on the 240x160 GBA screen
constexpr fx kScrollSpeed = Fx(250);         // PlayState.Scroll_Speed, px/s

// CollisionEventType, same values as the C# enum.
enum : uint8_t
{
    EVENT_NONE = 0,
    EVENT_CONVEYOR = 1,
    EVENT_THIN = 2,
    EVENT_HOLE = 3,
    EVENT_SLOW = 4,
    EVENT_SPIKE = 5,
    EVENT_LADDER = 6,
    EVENT_PUDDLE = 7,
    EVENT_REFLECTION = 8,
    EVENT_GRASS = 9,
};

// Largest map: 120x120 16x16 tiles.
constexpr uint32_t kMaxCollisionCells = 120 * 120;
constexpr uint32_t kMaxLayerBytes = kMaxCollisionCells * 2;

// The map is three stacked layers (BG, BG2, FG) plus an optional scrolling background; each is one hardware BG
// (all 8bpp, sharing the map's tileset in CBB0). Lower priority number = in front. The C# draw order is
// BACKGROUND < MAP_BG < reflection < foot overlay < MAP_BG2 < entities < grass overlay < MAP_FG < DEC_OVER, so by
// default: scrolling background = 3 (behind BG, which wins ties by being a lower BG index), BG = 3, BG2 = 2, FG = 0;
// the player (and anything "entity") is an OBJ at priority 1; things between BG and BG2 are OBJ at priority 3.
// Only the windmill has a DEC_OVER layer (the rain): it takes hardware BG0 so it ties above FG, which moves to BG3.
constexpr int kLayerCount = 3;                 // BG, BG2, FG: the layers made from the map's own CSVs
constexpr int kRoleCount = 4;                  // + the scrolling background / overlay
constexpr int kRoleScroll = 3;
constexpr int kRoleScreenblock[kRoleCount] = { 31, 30, 29, 28 }; // the tileset must end below 0xE000
constexpr uint32_t kMaxTilesetBytes = 0xE000;

// Uninitialised EWRAM (".sbss" in gba_cart.ld): the current map's unpacked layers and collision cells.
alignas(4) uint8_t gLayerBuffer[kLayerCount][kMaxLayerBytes] __attribute__((section(".sbss")));
alignas(4) uint8_t gCollisionGrid[kMaxCollisionCells] __attribute__((section(".sbss")));

ObjVramAllocator* gVram = nullptr;
const MapEntry* gMap = nullptr;
const uint16_t* gTile16Entries = nullptr; // per 16x16 tile: 4 screen entries (TL, TR, BL, BR)
uint32_t gTile16Count = 0;
const uint16_t* gLayerTiles[kLayerCount] = {}; // unpacked 16x16 tile indices, row-major
int gLayerW[kLayerCount] = {};
int gLayerH[kLayerCount] = {};
int gCellsW = 0; // collision cells (16x16)
int gCellsH = 0;
const uint8_t* gClasses = nullptr;
// The map's collision classes, copied so ChangeTile can add the ones its new tiles need.
uint8_t gClassRam[256 * 3];
int gClassCount = 0;

const char* gMapName = "";
const MapAudioData* gAudio = nullptr;
const char* gMusicPlaying = nullptr;
bool gIgnoreMusic = false; // Map.ignore_music
fx gSongVolume = kFxOne;

fx gCamX = 0; // top-left of the current screen being shown, Fx
fx gCamY = 0;
int gGridX = 0; // current 10x10-tile screen
int gGridY = 0;
bool gTransition = false;

int gMapOffCells = 0; // Map.offset.X in 16x16 cells: world cell x shows (and collides as) map cell x + this

int gBgTX = 0; // tile coordinate of the BG ring's top-left (each layer holds a 32x32 window of the map)
int gBgTY = 0;

// Which hardware BG / priority each role (BG, BG2, FG, scroll) uses on the current map.
int gHw[kRoleCount] = { 2, 1, 0, 3 };
int gPrio[kRoleCount] = { 3, 2, 0, 3 };

const MapScrollData* gScroll = nullptr; // null: no scrolling background on this map
fx gScrollPosX = 0;                     // ScrollingTex.Position (0 down to -160, wrapping)
fx gScrollPosY = 0;
int gScrollTX = 0;                      // ring origin (tile coordinates) of the scroll layer
int gScrollTY = 0;
// What the position's wraps took off, in px: the ring follows the unwrapped position, so a wrap (a 160px jump the
// 160px pattern hides) is a one-column step for it, not a redraw of 20 columns.
int gScrollUnwrapX = 0;
int gScrollUnwrapY = 0;

// ---- Background ---------------------------------------------------------------------------------------------
// Screen entry for 8x8 tile (mx, my) of a layer: look up its 16x16 tile and pick the matching quarter. Outside the
// layer (or index 0) is empty, like TileMap.GetTile.
// Map.replacements (ReplaceTiles): BG tiles drawn as others (Map.GetTile; collisions read the real ones).
constexpr int kMaxReplacements = 8;
uint16_t gReplaceFrom[kMaxReplacements];
uint16_t gReplaceTo[kMaxReplacements];
int gReplaceCount = 0;

// Each 16x16 tile's animated tile (an index into the map's MapTileAnimData), if it has one.
constexpr uint8_t kNoTileAnim = 0xFF;
constexpr int kMaxTile16 = 1024;
uint8_t gTileAnimOf[kMaxTile16] __attribute__((section(".sbss")));
const MapTileAnimData* gTileAnimData = nullptr;

// The 16x16 tile at map cell (cx, cy) of a layer, after the tile replacements: 0 for none.
uint32_t CellTile(int layer, int cx, int cy)
{
    cx += gMapOffCells;
    if (cx < 0 || cy < 0 || cx >= gLayerW[layer] || cy >= gLayerH[layer])
    {
        return 0;
    }
    uint32_t tile = gLayerTiles[layer][cy * gLayerW[layer] + cx];
    for (int i = 0; layer == 0 && i < gReplaceCount; i++)
    {
        if (tile == gReplaceFrom[i])
        {
            tile = gReplaceTo[i];
            break;
        }
    }
    return tile < gTile16Count ? tile : 0;
}

// One of a 16x16 tile's four screen entries (sub: x + 2 * y within it).
uint16_t TileEntry(int layer, uint32_t tile, int sub)
{
    if (tile == 0)
    {
        return 0;
    }
    if (layer == 0 && gTileAnimOf[tile] != kNoTileAnim)
    {
        return gTileAnimData->mAnims[gTileAnimOf[tile] * 8 + 1 + sub]; // (its own VRAM tiles)
    }
    return gTile16Entries[tile * 4 + sub];
}

uint16_t LayerEntry(int layer, int mx, int my)
{
    return TileEntry(layer, CellTile(layer, mx >> 1, my >> 1), (mx & 1) + 2 * (my & 1));
}

void WriteBgTile(int mx, int my)
{
    const int slot = (my & 31) * 32 + (mx & 31);
    for (int layer = 0; layer < kLayerCount; layer++)
    {
        se_mem[kRoleScreenblock[layer]][slot] = LayerEntry(layer, mx, my);
    }
}

void BgFull(int tx, int ty)
{
    gBgTX = tx;
    gBgTY = ty;
    // (a row of 16x16 cells at a time: each cell's tile is looked up once for its four entries)
    const int c0 = tx >> 1;
    const int cells = ((tx + 31) >> 1) - c0 + 1;
    uint32_t tiles[17];
    for (int layer = 0; layer < kLayerCount; layer++)
    {
        uint16_t* const sb = se_mem[kRoleScreenblock[layer]];
        for (int cy = ty >> 1; cy <= (ty + 31) >> 1; cy++)
        {
            for (int k = 0; k < cells; k++)
            {
                tiles[k] = CellTile(layer, c0 + k, cy);
            }
            for (int sy = 0; sy < 2; sy++)
            {
                const int my = 2 * cy + sy;
                if (my < ty || my >= ty + 32)
                {
                    continue;
                }
                uint16_t* const row = sb + (my & 31) * 32;
                for (int mx = tx; mx < tx + 32; mx++)
                {
                    row[mx & 31] = TileEntry(layer, tiles[(mx >> 1) - c0], (mx & 1) + 2 * sy);
                }
            }
        }
    }
}

// Each layer is a 32x32 ring: map tile (mx, my) always lives at entry (my & 31, mx & 31), so scrolling by a tile only
// rewrites the row/column that just came into view.
void BgScrollTo(int tx, int ty)
{
    if (abs(tx - gBgTX) >= 32 || abs(ty - gBgTY) >= 32)
    {
        BgFull(tx, ty);
        return;
    }

    while (gBgTX < tx)
    {
        gBgTX++;
        for (int j = 0; j < 32; j++)
        {
            WriteBgTile(gBgTX + 31, gBgTY + j);
        }
    }
    while (gBgTX > tx)
    {
        gBgTX--;
        for (int j = 0; j < 32; j++)
        {
            WriteBgTile(gBgTX, gBgTY + j);
        }
    }
    while (gBgTY < ty)
    {
        gBgTY++;
        for (int i = 0; i < 32; i++)
        {
            WriteBgTile(gBgTX + i, gBgTY + 31);
        }
    }
    while (gBgTY > ty)
    {
        gBgTY--;
        for (int i = 0; i < 32; i++)
        {
            WriteBgTile(gBgTX + i, gBgTY);
        }
    }
}

// The scroll registers are write-only, so keep a copy for the pixelate effect to sample the screen with.
int16_t gBgOfsX[4];
int16_t gBgOfsY[4];

void SetBgOffset(int hw, int x, int y)
{
    REG_BG_OFS[hw].x = static_cast<uint16_t>(x);
    REG_BG_OFS[hw].y = static_cast<uint16_t>(y);
    gBgOfsX[hw] = static_cast<int16_t>(x);
    gBgOfsY[hw] = static_cast<int16_t>(y);
}

// ---- ScreenShake / FlashEffect ----------------------------------------------------------------------------------
struct Shake final
{
    fx mTime;      // seconds left
    fx mIntensity; // fraction of the screen
    fx mUpdate;    // _shakeUpdate: runs on from the last shake
    uint8_t mDirs; // ShakeDirs
    int mX;
    int mY;
};
Shake gShake = {};

struct Flash final
{
    fx mAlpha;
    fx mStep;     // per frame
    fx mEaseStep; // per frame, while easing in
    uint16_t mColour;
    bool mEasing;
    bool mFull;
    bool mTint; // (not full white or black: BLDY can't)
};
Flash gFlash = {};

float gQuickSaveTimer = 5; // PlayState.QuickSaveTimer: no quick save for 5s after it starts
bool gQuickLoad = false;   // PlayState.DoQuickLoad
float gTexRandomTimer = 0; // PlayState._texRandomTimer
bool gForceTextureReload = false;
// ResourceManager._randomizeabletextureHandles.Count: the textures the C# had loaded by the cheats line, one RNG draw each
// when chaos picks them new ones (the port's own sprites stay as they are).
constexpr int kRandomizableTextures = 232;

// The red flash tints the palettes towards red (all but the backdrop, which also fills the margins round the
// playfield). An entry something else rewrites meanwhile becomes the new base, and at the end only the entries
// still holding the tint go back.
constexpr int kPalEntries = 512; // BG then OBJ
uint16_t gTintBase[kPalEntries] __attribute__((section(".sbss")));
uint16_t gTintLast[kPalEntries] __attribute__((section(".sbss")));
bool gTinted = false;

// FadeEffect: each colour moves `level`/32 of the way to `colour`.
void TintPalettes(int level, uint16_t colour)
{
    const int tr = colour & 31;
    const int tg = (colour >> 5) & 31;
    const int tb = (colour >> 10) & 31;
    for (int i = 1; i < kPalEntries; i++)
    {
        const uint16_t now = pal_bg_mem[i];
        if (!gTinted || now != gTintLast[i])
        {
            gTintBase[i] = now;
        }
        const int base = gTintBase[i];
        const int r = base & 31;
        const int g = (base >> 5) & 31;
        const int b = (base >> 10) & 31;
        const uint16_t out = RGB15(r + (tr - r) * level / 32, g + (tg - g) * level / 32, b + (tb - b) * level / 32);
        pal_bg_mem[i] = out;
        gTintLast[i] = out;
    }
    gTinted = true;
}

void UntintPalettes()
{
    if (!gTinted)
    {
        return;
    }
    for (int i = 1; i < kPalEntries; i++)
    {
        if (pal_bg_mem[i] == gTintLast[i])
        {
            pal_bg_mem[i] = gTintBase[i];
        }
    }
    gTinted = false;
}

void UpdateScreenEffects()
{
    // ScreenShake.Update (only while Active)
    if (gShake.mTime > 0)
    {
        gShake.mTime -= kFxTick;
        gShake.mUpdate += kFxTick;
        if (gShake.mUpdate >= FxSec(1 / 30.0f))
        {
            gShake.mUpdate = 0;
            // _pos = (float)RNG.NextDouble() * 2 * _intensity - _intensity, times the screen size when drawn
            if (gShake.mDirs & SHAKE_HORIZONTAL)
            {
                const fx x = FxMul(Rng_Unit() * 2, gShake.mIntensity) - gShake.mIntensity;
                gShake.mX = FxTrunc(FxMul(x, Fx(kScreenPx)));
            }
            if (gShake.mDirs & SHAKE_VERTICAL)
            {
                const fx y = FxMul(Rng_Unit() * 2, gShake.mIntensity) - gShake.mIntensity;
                gShake.mY = FxTrunc(FxMul(y, Fx(kScreenPx)));
            }
        }
    }
    if (gShake.mTime <= 0)
    {
        gShake.mX = gShake.mY = 0;
    }
    if (gFlash.mEasing)
    {
        gFlash.mAlpha += gFlash.mEaseStep;
        if (gFlash.mAlpha >= kFxOne)
        {
            gFlash.mAlpha = kFxOne;
            gFlash.mEasing = false;
            gFlash.mFull = true;
        }
    }
    else if (gFlash.mAlpha > 0)
    {
        gFlash.mAlpha -= gFlash.mStep;
        gFlash.mAlpha = gFlash.mAlpha < 0 ? 0 : gFlash.mAlpha;
    }
}

// Screen x of world x 0 is kViewX - camera, so the BG origin is the camera minus the playfield's left margin.
void UpdateBackground(bool force)
{
    const int vx = FxFloor(gCamX) - kViewX;
    const int vy = FxFloor(gCamY);
    if (force)
    {
        BgFull(vx >> 3, vy >> 3);
    }
    else
    {
        BgScrollTo(vx >> 3, vy >> 3);
    }
    for (int layer = 0; layer < kLayerCount; layer++)
    {
        SetBgOffset(gHw[layer], (vx - gShake.mX) & 255, (vy - gShake.mY) & 255);
    }
}

// TileReplacement: "a-b;c-d" (BG tile a draws as b, ...). A change redraws the BG.
void SetTileReplacements(const char* rules)
{
    uint16_t from[kMaxReplacements];
    uint16_t to[kMaxReplacements];
    int count = 0;
    for (const char* c = rules; *c && count < kMaxReplacements;)
    {
        from[count] = static_cast<uint16_t>(strtol(c, const_cast<char**>(&c), 10));
        if (*c != '-')
        {
            break;
        }
        to[count++] = static_cast<uint16_t>(strtol(c + 1, const_cast<char**>(&c), 10));
        if (*c == ';')
        {
            c++;
        }
    }
    bool same = count == gReplaceCount;
    for (int i = 0; same && i < count; i++)
    {
        same = from[i] == gReplaceFrom[i] && to[i] == gReplaceTo[i];
    }
    if (same)
    {
        return;
    }
    gReplaceCount = count;
    for (int i = 0; i < count; i++)
    {
        gReplaceFrom[i] = from[i];
        gReplaceTo[i] = to[i];
    }
    if (gMap)
    {
        UpdateBackground(true);
    }
}

// ---- Scrolling background (ScrollingTex.cs) ------------------------------------------------------------------
int FloorMod(int value, int divisor)
{
    const int m = value % divisor;
    return m < 0 ? m + divisor : m;
}

// The 20x20-tile pattern repeats every 160px, so tile (mx, my) is pattern[my mod 20][mx mod 20].
void WriteScrollTile(int mx, int my)
{
    se_mem[kRoleScreenblock[kRoleScroll]][(my & 31) * 32 + (mx & 31)] =
        gScroll->mPattern[FloorMod(my, 20) * 20 + FloorMod(mx, 20)];
}

void ScrollFull(int tx, int ty)
{
    gScrollTX = tx;
    gScrollTY = ty;
    for (int j = 0; j < 32; j++)
    {
        for (int i = 0; i < 32; i++)
        {
            WriteScrollTile(tx + i, ty + j);
        }
    }
}

void ScrollRingTo(int tx, int ty)
{
    if (abs(tx - gScrollTX) >= 32 || abs(ty - gScrollTY) >= 32)
    {
        ScrollFull(tx, ty);
        return;
    }
    while (gScrollTX < tx)
    {
        gScrollTX++;
        for (int j = 0; j < 32; j++)
        {
            WriteScrollTile(gScrollTX + 31, gScrollTY + j);
        }
    }
    while (gScrollTX > tx)
    {
        gScrollTX--;
        for (int j = 0; j < 32; j++)
        {
            WriteScrollTile(gScrollTX, gScrollTY + j);
        }
    }
    while (gScrollTY < ty)
    {
        gScrollTY++;
        for (int i = 0; i < 32; i++)
        {
            WriteScrollTile(gScrollTX + i, gScrollTY + 31);
        }
    }
    while (gScrollTY > ty)
    {
        gScrollTY--;
        for (int i = 0; i < 32; i++)
        {
            WriteScrollTile(gScrollTX + i, gScrollTY);
        }
    }
}

// The texture is screen-locked (drawn at camera + Position) and moves by Position: playfield pixel x shows texture
// pixel x - Position.X, so the hardware BG origin is -(Position + the playfield's left margin).
void UpdateScroll(bool advance, bool force)
{
    if (!gScroll)
    {
        return;
    }

    if (advance)
    {
        gScrollPosX += FxStep(Fx(gScroll->mVelocityX));
        gScrollPosY += FxStep(Fx(gScroll->mVelocityY));
        if (gScrollPosX < -Fx(160)) // Position.X < -Texture.Width / 2
        {
            gScrollUnwrapX += FxTrunc(gScrollPosX);
            gScrollPosX = 0;
        }
        if (gScrollPosY > 0)
        {
            gScrollUnwrapY += FxTrunc(gScrollPosY) + 160;
            gScrollPosY = -Fx(160);
        }
        else if (gScrollPosY < -Fx(160))
        {
            gScrollUnwrapY += FxTrunc(gScrollPosY);
            gScrollPosY = 0;
        }
        // Every 1280px (160 tiles: whole patterns, and whole turns of the 32-tile ring) the ring's tiles are the same
        // again: fold it back there so it never overflows.
        constexpr int kFold = 1280;
        if (gScrollUnwrapX <= -kFold || gScrollUnwrapX >= kFold)
        {
            const int fold = gScrollUnwrapX < 0 ? -kFold : kFold;
            gScrollUnwrapX -= fold;
            gScrollTX += fold / 8;
        }
        if (gScrollUnwrapY <= -kFold || gScrollUnwrapY >= kFold)
        {
            const int fold = gScrollUnwrapY < 0 ? -kFold : kFold;
            gScrollUnwrapY -= fold;
            gScrollTY += fold / 8;
        }
    }

    // (int) cast truncates towards zero
    const int posX = FxTrunc(gScrollPosX) + gScrollUnwrapX;
    const int posY = FxTrunc(gScrollPosY) + gScrollUnwrapY;
    const int vx = -kViewX - posX;
    const int vy = -posY;
    if (force)
    {
        ScrollFull(vx >> 3, vy >> 3);
    }
    else
    {
        ScrollRingTo(vx >> 3, vy >> 3);
    }
    SetBgOffset(gHw[kRoleScroll], vx & 255, vy & 255);
}

bool gLightWindows = false; // the darkness's lights are on (see Darkness below)
uint16_t gWinLayers = WIN_BG0 | WIN_BG1 | WIN_BG2 | WIN_OBJ;
void ApplyWindows();

// Hardware BG roles for the current map, and the registers that depend on them.
void ApplyDisplayRegs(bool windowOn)
{
    const bool overlay = gScroll && gScroll->mOverlay;
    // roles: BG, BG2, FG, scroll
    gHw[0] = 2;
    gHw[1] = 1;
    gPrio[0] = 3;
    gPrio[1] = 2;
    if (overlay)
    {
        gHw[kRoleScroll] = 0; // rain: in front of everything, FG included
        gHw[2] = 3;
        gPrio[kRoleScroll] = 0;
        gPrio[2] = 0;
    }
    else
    {
        gHw[2] = 0;
        gHw[kRoleScroll] = 3;
        gPrio[2] = 0;
        gPrio[kRoleScroll] = 3;
    }

    for (int role = 0; role < kRoleCount; role++)
    {
        REG_BGCNT[gHw[role]] = BG_CBB(0) | BG_SBB(kRoleScreenblock[role]) | BG_PRIO(gPrio[role]) | BG_8BPP | BG_REG_32x32;
    }

    uint16_t dispcnt = DCNT_MODE0 | DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_OBJ | DCNT_OBJ_1D;
    uint16_t winin = WIN_BG0 | WIN_BG1 | WIN_BG2 | WIN_OBJ;
    if (gScroll)
    {
        dispcnt |= DCNT_BG3;
        winin |= WIN_BG3;
    }
    if (windowOn)
    {
        dispcnt |= DCNT_WIN0;
    }
    REG_DISPCNT = dispcnt;
    gWinLayers = winin;
    ApplyWindows();
}

// ---- Audio ---------------------------------------------------------------------------------------------------
void PlaySfx(const char* name)
{
    Game_PlaySfx(name);
}

// Map.ReloadSettings: the music for where the player is - the first Settings.json area containing the player's
// position, else the map's default. Only restarts the music when the track actually changes. A screen transition
// after IgnoreMusicNextUpdate leaves the music be.
void UpdateMusic(bool screenTransition = false)
{
    if (!gAudio)
    {
        return;
    }
    const bool ignore = screenTransition && gIgnoreMusic;
    gIgnoreMusic = false;
    if (ignore)
    {
        return;
    }

    const char* music = gAudio->mMusic;
    const int px = FxTrunc(Player_Entity()->mT->mX);
    const int py = FxTrunc(Player_Entity()->mT->mY);
    for (uint32_t i = 0; i < gAudio->mAreaCount; i++)
    {
        const MapAudioArea& a = gAudio->mAreas[i];
        if (px >= a.mX && px < a.mX + a.mWidth && py >= a.mY && py < a.mY + a.mHeight)
        {
            music = a.mMusic;
            break;
        }
    }

    // (PlaySong sets the area's MusicVolume even when the song carries on: not compiled yet, so 1)
    Game_SetSongVolume(kFxOne);
    if (gMusicPlaying != nullptr && strcmp(music, gMusicPlaying) == 0)
    {
        return;
    }
    gMusicPlaying = music;

    const int32_t id = music[0] ? AudioManager_FindBgm(music) : -1;
    if (id >= 0)
    {
        AudioManager_PlayBGM(static_cast<uint32_t>(id));
    }
    else
    {
        AudioManager_StopBGM();
    }
}

// ---- Darkness (Drawing/Effects/Darkness.cs, Entities/Lights) ----------------------------------------------------
// The C# multiplies the screen by a light map and mixes that back in by alpha. Here: the whole screen darkens by
// alpha (BLDY), except inside the lights, which are OBJ-window shapes. The OBJ window loses to window 0, so while
// lights are on the margins move to windows 0/1 and the playfield becomes the outside.
struct Darkness final
{
    fx mAlpha;
    fx mTarget;
    fx mVel;
};
Darkness gDark = { 0, 0, FxF(0.8f) };
const MapDarkData* gDarkData = nullptr;
const DarkTexture* gDarkTextures = nullptr;
const DarkTexture* gDarkTex = nullptr; // the darkness's texture (tex_dark.h); nullptr: it darkens plainly
int gTintLevel = 0;                    // this frame's tinting flash, when TexDark tints
bool gDarkWritten = false; // the last frame's BLD came from the darkness
Entity* gPlayerLight = nullptr;
Entity* gMainLight = nullptr; // GlobalState.PlayerLight
const EntityClass* gMainLightClass = nullptr;

void ApplyWindows()
{
    if ((REG_DISPCNT & DCNT_WIN0) == 0)
    {
        return; // windows are off (the sprite gallery)
    }
    if (gLightWindows)
    {
        REG_DISPCNT = REG_DISPCNT | DCNT_WIN1 | DCNT_WINOBJ;
        REG_WIN0H = (0 << 8) | kViewX;
        REG_WIN1H = static_cast<uint16_t>(((kViewX + kScreenPx) << 8) | 240);
        REG_WIN0V = REG_WIN1V = (0 << 8) | kScreenPx;
        REG_WININ = WIN_OBJ | (WIN_OBJ << 8); // the side bars: the HUD, never darkened (hud.h)
        REG_WINOUT = static_cast<uint16_t>((gWinLayers | WIN_BLD) | (gWinLayers << 8));
    }
    else
    {
        REG_DISPCNT = REG_DISPCNT & ~(DCNT_WIN1 | DCNT_WINOBJ);
        REG_WIN0H = (kViewX << 8) | (kViewX + kScreenPx);
        REG_WIN0V = (0 << 8) | kScreenPx;
        REG_WININ = gWinLayers | WIN_BLD; // anodyne.cpp hides map loads behind a full darken
        REG_WINOUT = WIN_OBJ;             // the side bars: the HUD over its black panels, never darkened (hud.h)
    }
}

// The map's ExtraBlend texture (GlobalState.extraBlend), nullptr if it has none.
const DarkTexture* ExtraBlend()
{
    return gDarkData && gDarkData->mExtraBlend >= 0 && gDarkTextures ? &gDarkTextures[gDarkData->mExtraBlend] : nullptr;
}

// TexDark draws this frame: a darkness texture, or the ExtraBlend.
bool TexDarkOn()
{
    return (gDarkTex && gDark.mAlpha > 0) || ExtraBlend();
}

// Plain darkness under an ExtraBlend goes into TexDark's colours too: the overlay brightens the darkened screen (the
// C# blends after the darkness), which a BLD over the blended colours can't give. Not with the player's light on: its
// hole in the darkness needs the BLD and windows.
bool PlainDarkInTexDark()
{
    return !gDarkTex && ExtraBlend() && !(gPlayerLight && gPlayerLight->mExists);
}

int DarkLevel()
{
    if (gDarkTex || PlainDarkInTexDark())
    {
        return 0; // (TexDark draws it)
    }
    const int y = FxFloor(gDark.mAlpha * 16 + kFxOne / 2);
    return y < 0 ? 0 : (y > 16 ? 16 : y);
}

// Map.ReloadSettings: the darkness (DarknessAlpha, its texture), PlayerLight and ReplaceTiles for where the player is,
// the darkness forced on a map load and faded to on a screen change.
void UpdateDarkness(bool transition)
{
    int alpha = -1;
    int light = -1;
    int texture = -1;
    const char* replace = nullptr;
    auto take = [&](int a, int l, int t, const char* r) {
        alpha = alpha < 0 ? a : alpha;
        light = light < 0 ? l : light;
        texture = texture < 0 ? t : texture;
        replace = replace ? replace : r;
    };
    if (gDarkData)
    {
        const int px = FxTrunc(Player_Entity()->mT->mX);
        const int py = FxTrunc(Player_Entity()->mT->mY);
        for (uint32_t i = 0; i < gDarkData->mAreaCount; i++)
        {
            const MapDarkArea& a = gDarkData->mAreas[i];
            if (px >= a.mX && px < a.mX + a.mWidth && py >= a.mY && py < a.mY + a.mHeight)
            {
                take(a.mAlpha, a.mPlayerLight, a.mTexture, a.mReplace);
            }
        }
        for (uint32_t i = 0; i < gDarkData->mEventCount; i++)
        {
            const MapDarkEvent& e = gDarkData->mEvents[i];
            if (Event_Get(e.mEvent) != 0)
            {
                take(e.mAlpha, -1, e.mTexture, e.mReplace);
            }
        }
        take(gDarkData->mAlpha, gDarkData->mPlayerLight ? 1 : 0, gDarkData->mTexture, "");
    }
    gDarkTex = texture >= 0 && gDarkTextures ? &gDarkTextures[texture] : nullptr;
    // (no DarknessAlpha: fully dark with a texture, none without)
    const fx target = alpha >= 0 ? alpha * (kFxOne / 256) : (gDarkTex ? kFxOne : 0);
    if (transition)
    {
        Darkness_TargetAlpha(target);
    }
    else
    {
        Darkness_ForceAlpha(target);
    }
    if (gPlayerLight)
    {
        gPlayerLight->mExists = light > 0;
        gPlayerLight->mV->mVisible = true; // the death state hid it
    }
    SetTileReplacements(replace ? replace : "");
}

// PlayerLight: follows the player's centre, its scale flickering a little.
struct PlayerLightData final
{
    fx mFlickerTimer;
};

void PlayerLightUpdate(Entity& self)
{
    PlayerLightData& d = self.Data<PlayerLightData>();
    const Entity& p = *Player_Entity();
    self.mT->mX = Ecs_CenterX(p);
    self.mT->mY = Ecs_CenterY(p);
    d.mFlickerTimer -= kFxTick;
    if (d.mFlickerTimer < 0)
    {
        if (self.mV->mScale != Fx(2))
        {
            self.mV->mScale = Fx(2);
            d.mFlickerTimer = FxSec(6 / 30.0f);
        }
        else
        {
            self.mV->mScale += FxMul(Rng_Unit(), FxF(0.2f));
            d.mFlickerTimer = FxSec(0.05f);
        }
    }
    Ecs_BaseUpdate(self);
}

const EntityClass kPlayerLightClass = {
    "PlayerLight", PlayerLightUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void UpdateAmbience()
{
    const int32_t id = (gAudio && gAudio->mAmbience[0]) ? AudioManager_FindAmbience(gAudio->mAmbience) : -1;
    if (id >= 0)
    {
        AudioManager_PlayAmbience(static_cast<uint32_t>(id));
    }
    else
    {
        AudioManager_StopAmbience();
    }
}

// ---- Collision ----------------------------------------------------------------------------------------------
const uint8_t* ClassAtCell(int cx, int cy)
{
    cx += gMapOffCells; // (Map.ToMapLoc)
    // TileMap.GetTile: outside the map is tile 0 (nothing).
    if (cx < 0 || cy < 0 || cx >= gCellsW || cy >= gCellsH)
    {
        return gClasses; // class 0
    }
    return gClasses + gCollisionGrid[cy * gCellsW + cx] * 3;
}

// Map.GetCollisionData: the sides the tile under a world point blocks.
uint8_t AllowAt(fx x, fx y)
{
    const int px = FxFloor(x);
    const int py = FxFloor(y);
    return ClassAtCell(px >> 4, py >> 4)[0];
}

// Picks the screen that looks most like a real room/field: the most balanced mix of open and solid cells (an
// empty screen of void is all "open"), then the open cell nearest its centre.
void PickSpawn()
{
    int bestScreenX = 0;
    int bestScreenY = 0;
    int bestScore = -1;
    for (int gy = 0; gy + kScreenCells <= gCellsH; gy += kScreenCells)
    {
        for (int gx = 0; gx + kScreenCells <= gCellsW; gx += kScreenCells)
        {
            int open = 0;
            int solid = 0;
            for (int y = 0; y < kScreenCells; y++)
            {
                for (int x = 0; x < kScreenCells; x++)
                {
                    const uint8_t* c = ClassAtCell(gx + x, gy + y);
                    open += (c[0] == 0 && c[1] == 0) ? 1 : 0;
                    solid += c[0] == 15 ? 1 : 0;
                }
            }
            const int score = open < solid ? open : solid;
            if (score > bestScore)
            {
                bestScore = score;
                bestScreenX = gx / kScreenCells;
                bestScreenY = gy / kScreenCells;
            }
        }
    }

    gGridX = bestScreenX;
    gGridY = bestScreenY;

    int bestCellX = gGridX * kScreenCells + kScreenCells / 2;
    int bestCellY = gGridY * kScreenCells + kScreenCells / 2;
    int bestDist = 1 << 30;
    for (int y = 0; y < kScreenCells; y++)
    {
        for (int x = 0; x < kScreenCells; x++)
        {
            const int cx = gGridX * kScreenCells + x;
            const int cy = gGridY * kScreenCells + y;
            const uint8_t* c = ClassAtCell(cx, cy);
            const int dx = x - kScreenCells / 2;
            const int dy = y - kScreenCells / 2;
            if (c[0] == 0 && c[1] == 0 && dx * dx + dy * dy < bestDist)
            {
                bestDist = dx * dx + dy * dy;
                bestCellX = cx;
                bestCellY = cy;
            }
        }
    }

    // The 10x12 hitbox sits inside the one 16x16 cell.
    Player_PlaceAt(Fx(bestCellX * kTileSize + 3), Fx(bestCellY * kTileSize + 2), -1);
}

// ---- Map collision for entities (Map.Collide / CollideTile) ------------------------------------------------------
int FloorDiv16(int v)
{
    return v >> 4; // arithmetic shift: floor, also for the negative positions just off the map's top/left edge
}

bool CenterIn(const Entity& e, fx tx, fx ty)
{
    const Rect tile = { FxTrunc(tx), FxTrunc(ty), kTileSize, kTileSize };
    return Ecs_Contains(tile, Ecs_CenterX(e), Ecs_CenterY(e));
}

void SendMapEvent(Entity& e, MapEvent ev, uint8_t dir, fx tx, fx ty)
{
    if (e.mClass->mMapEvent)
    {
        e.mClass->mMapEvent(e, ev, dir, tx, ty);
    }
    else if (ev == MAPEV_FALL)
    {
        e.mExists = false; // Entity.Fall
    }
}

void CollideTile(int cx, int cy, Entity& e)
{
    const uint8_t* c = ClassAtCell(cx, cy);
    const uint8_t allow = c[0];
    const uint8_t event = c[1];
    const uint8_t dir = c[2];
    const fx tx = Fx(cx * kTileSize);
    const fx ty = Fx(cy * kTileSize);
    const fx size = Fx(kTileSize);

    if (e.mB->mHoleAsWall && event == EVENT_HOLE)
    {
        Ecs_SeparateRect(e, tx, ty, size, size, TOUCH_ANY);
        return;
    }
    if (allow != TOUCH_NONE && !Ecs_SeparateRect(e, tx, ty, size, size, allow))
    {
        return;
    }
    if (event == EVENT_NONE || !e.mB->mMapInteraction)
    {
        return;
    }
    switch (event)
    {
    case EVENT_CONVEYOR:
        if (CenterIn(e, tx, ty))
        {
            SendMapEvent(e, MAPEV_CONVEYOR, dir, tx, ty);
        }
        break;
    case EVENT_THIN:
        // A 3px strip along one edge of the tile that blocks on every side.
        switch (dir)
        {
        case TOUCH_DOWN:
            Ecs_SeparateRect(e, tx, ty + Fx(13), size, Fx(3), TOUCH_ANY);
            break;
        case TOUCH_UP:
            Ecs_SeparateRect(e, tx, ty, size, Fx(3), TOUCH_ANY);
            break;
        case TOUCH_LEFT:
            Ecs_SeparateRect(e, tx, ty, Fx(3), size, TOUCH_ANY);
            break;
        case TOUCH_RIGHT:
            Ecs_SeparateRect(e, tx + Fx(13), ty, Fx(3), size, TOUCH_ANY);
            break;
        default:
            break;
        }
        break;
    case EVENT_HOLE:
    {
        const Rect hole = { cx * kTileSize + 5, cy * kTileSize + 5, 6, 4 };
        if (Ecs_Intersects(hole, Ecs_Hitbox(e)))
        {
            SendMapEvent(e, MAPEV_FALL, dir, tx, ty);
        }
        break;
    }
    case EVENT_SLOW:
        SendMapEvent(e, MAPEV_SLOW, dir, tx, ty);
        break;
    case EVENT_SPIKE:
    {
        const Rect spike = { cx * kTileSize + 6, cy * kTileSize + 6, 5, 6 };
        if (e.mClass == Player_Entity()->mClass && Ecs_Intersects(spike, Ecs_Hitbox(e)))
        {
            SendMapEvent(e, MAPEV_SPIKE, dir, tx, ty);
        }
        break;
    }
    case EVENT_LADDER:
        SendMapEvent(e, MAPEV_LADDER, dir, tx, ty);
        break;
    case EVENT_PUDDLE:
        if (CenterIn(e, tx, ty))
        {
            SendMapEvent(e, MAPEV_PUDDLE, dir, tx, ty);
        }
        break;
    case EVENT_REFLECTION:
        if (CenterIn(e, tx, ty))
        {
            SendMapEvent(e, MAPEV_PUDDLE, dir, tx, ty);
            SendMapEvent(e, MAPEV_REFLECTION, dir, tx, ty);
        }
        break;
    case EVENT_GRASS:
        if (CenterIn(e, tx, ty))
        {
            SendMapEvent(e, MAPEV_GRASS, dir, tx, ty);
        }
        break;
    default:
        break;
    }
}

// ---- Doors ----------------------------------------------------------------------------------------------------
const MapDoorList* gDoors = nullptr;
const MapSwapperList* gSwapper = nullptr;

// GlobalState.WARP and PlayState's states around it (S_MAP_EXIT, S_MAP_ENTER), with its fade as the C# floats.
enum WarpState : uint8_t
{
    WARP_NONE,
    WARP_EXIT,
    WARP_ENTER,
};

struct PendingWarp final
{
    bool mPending = false; // GlobalState.WARP: set by the door, cleared when S_MAP_ENTER ends
    uint8_t mMap = 0;
    int mX = 0;
    int mY = 0;
    uint8_t mFacing = 0;
    uint8_t mState = WARP_NONE;
    bool mSwapped = false;    // the map was swapped this frame
    fx mAlpha = 0;            // black_overlay.alpha (a FadeEffect: Fix)
    float mPixelation = 1.0f; // GlobalState.pixelation
} gWarp;
void (*gMapLoader)(uint8_t map) = nullptr;

// PlayState.Warp: the new map (if it is another one), the player at the destination, its screen's entities.
void WarpNow()
{
    const PendingWarp w = gWarp;
    if (gMapLoader)
    {
        gMapLoader(w.mMap);
    }
    World_PlaceAt(w.mX, w.mY, w.mFacing, true);
    gWarp = w; // (loading a map resets the doors' state)
    gWarp.mSwapped = true;
}

// PlayState S_MAP_EXIT: pixelate and fade to black, then warp.
void WarpExit()
{
    Player_SetInvincible(true);
    Player_SetDontMove(true);
    Player_SetActionsDisabled(true);
    gWarp.mPixelation = gWarp.mPixelation + 30.0f * kUiDeltaTime;
    gWarp.mPixelation = gWarp.mPixelation < 1.0f ? 1.0f : gWarp.mPixelation;
    const float transitionOut = gMapName && strcmp(gMapName, "BLANK") == 0 ? 1.568f : 0.785f;
    // (ChangeAlpha(1 / transition_out): a float speed, made Fix, times DeltaTime)
    const fx alpha = gWarp.mAlpha + FxMul(FxF(1.0f / transitionOut), kFxTick);
    gWarp.mAlpha = alpha > kFxOne ? kFxOne : alpha;
    if (gWarp.mAlpha == kFxOne || gFuckItMode)
    {
        WarpNow();
        Player_SetInvincible(true);
        Player_SetDontMove(true);
        Player_SetActionsDisabled(true);
        gWarp.mState = WARP_ENTER;
        gWarp.mPixelation = 10.0f;
    }
}

// PlayState S_MAP_ENTER: back from black; the player gets control when it is clear.
void WarpEnter()
{
    if (gFuckItMode)
    {
        gWarp.mPixelation = 1.0f;
    }
    gWarp.mPixelation = gWarp.mPixelation - 30.0f * kUiDeltaTime;
    gWarp.mPixelation = gWarp.mPixelation < 1.0f ? 1.0f : gWarp.mPixelation;
    const fx alpha = gWarp.mAlpha + FxMul(FxF(-1.0f / 0.8f), kFxTick);
    gWarp.mAlpha = alpha < 0 ? 0 : alpha;
    if (gWarp.mAlpha == 0)
    {
        gWarp.mState = WARP_NONE;
        Player_SetDontMove(false);
        Player_SetInvincible(false);
        Player_SetActionsDisabled(false);
        gWarp.mPending = false;
    }
}

// Door.Collided: stepping onto a door (or pressing B on an interactive one) warps the player.
// ---- PlayState: screens ----------------------------------------------------------------------------------------
void MoveCameraToGrid()
{
    gCamX = Fx(gGridX * kScreenPx);
    gCamY = Fx(gGridY * kScreenPx);
}

bool GridExists(int gx, int gy)
{
    return gx >= 0 && gy >= 0 && (gx + 1) * kScreenCells <= gCellsW && (gy + 1) * kScreenCells <= gCellsH;
}

// PlayState.CheckForTransition. The C# lets the player walk off the map (a glitch effect); here a missing
// neighbouring screen just stops the player at the edge.
void CheckForTransition()
{
    Entity& p = *Player_Entity();
    Transform& t = *p.mT;
    const fx w = Fx(p.mB->mW);
    const fx h = Fx(p.mB->mH);
    const fx bx = Fx(gGridX * kScreenPx);
    const fx by = Fx(gGridY * kScreenPx);
    int gx = gGridX;
    int gy = gGridY;
    fx newX = t.mX;
    fx newY = t.mY;

    if (t.mX < bx)
    {
        gx--;
        newX = bx - w;
    }
    else if (t.mY < by)
    {
        gy--;
        newY = by - h;
    }
    else if (t.mY > by + Fx(kScreenPx) - h)
    {
        gy++;
        newY = by + Fx(kScreenPx);
    }
    else if (t.mX > bx + Fx(kScreenPx) - w)
    {
        gx++;
        newX = bx + Fx(kScreenPx);
    }
    else
    {
        return;
    }

    if (!GridExists(gx, gy))
    {
        // Keep the hitbox inside the current screen.
        t.mX = t.mX < bx ? bx : (t.mX > bx + Fx(kScreenPx) - w ? bx + Fx(kScreenPx) - w : t.mX);
        t.mY = t.mY < by ? by : (t.mY > by + Fx(kScreenPx) - h ? by + Fx(kScreenPx) - h : t.mY);
        t.mLastX = t.mX;
        t.mLastY = t.mY;
        return;
    }

    gTransition = true;
    gGridX = gx;
    gGridY = gy;
    t.mX = newX;
    t.mY = newY;
    Ecs_FireEvent(GEV_START_SCREEN_TRANSITION);
    World_TileChangesStart();
    UpdateMusic(true); // ReloadSettings(player position) as the screen changes
    UpdateDarkness(true);
    Player_SetGridEntrance();
    Player_SetDontMove(true);
    Player_LoseBroomDust();
    t.mVelX = t.mVelY = 0;
    Entities_LoadGrid(gGridX, gGridY, false);
}

// PlayState.StateTransition: the camera scrolls to the new screen.
void StateTransition()
{
    Player_SetInvincible(true);
    // (UpdateCamera: fuck-it mode jumps straight there)
    if (gFuckItMode)
    {
        MoveCameraToGrid();
    }
    const bool doneX = MoveTo(gCamX, Fx(gGridX * kScreenPx), kScrollSpeed);
    const bool doneY = MoveTo(gCamY, Fx(gGridY * kScreenPx), kScrollSpeed);
    if (doneX && doneY)
    {
        Player_SetInvincible(false);
        Player_SetDontMove(false);
        gTransition = false;
        World_TileChangesEnd(); // (FinalizeTransition)
        Entities_EndTransition();
    }
}

bool gDialogOpen = false; // a DialogueState child state is running
bool gShown = false;

// CutsceneState: the entity's step function runs once a frame (while no dialogue is open) until it returns true.
struct Cutscene final
{
    CutsceneFn mFn = nullptr;
    Entity* mOwner = nullptr;
    const EntityClass* mClass = nullptr;
} gCutscene;

// DeathState: the player's "die" dummy over a white fade, then Accept continues from the checkpoint.
struct Death final
{
    bool mActive = false;
    bool mGotControl = false;
    bool mFadingOut = false;
    bool mNo = false; // "No.." selected
    Entity* mDummy = nullptr;
    fx mFade = 0; // DeathFadeIn opacity, then the black overlay
} gDeath;
int gDeaths = 0;
fx gDeathFallbackX = 0; // where the map was entered (dying before any checkpoint)
fx gDeathFallbackY = 0;

const EntityClass kDieDummyClass = {
    "PlayerDieDummy", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// The DeathFadeIn (white, or the CELL's black) leaves the sprites (the dummy) alone; the black overlay after
// covers everything.
void SetDeathBlend(bool fadeIn, fx level)
{
    int y = FxFloor(level * 16 + kFxOne / 2);
    y = y < 0 ? 0 : (y > 16 ? 16 : y);
    const bool white = fadeIn && !Game_IsCell();
    REG_BLDCNT = BLD_BG0 | BLD_BG1 | BLD_BG2 | BLD_BG3 | BLD_BACKDROP | (white ? BLD_WHITE : BLD_BLACK) |
                 (fadeIn ? 0 : BLD_OBJ);
    REG_BLDY = static_cast<uint16_t>(y);
}

void DeathStart()
{
    gDeath = Death();
    gDeath.mActive = true;
    Screen_SetWave(false); // DeathState: wave.Deactivate()
    UntintPalettes();
    gGame.mDisableMenu = true;
    AudioManager_StopBGM();
    Entity& p = *Player_Entity();
    const fx px = p.mT->mX;
    const fx py = p.mT->mY;
    Player_Reset(gMapName);
    p.mT->mX = px;
    p.mT->mY = py;
    p.mExists = false;
    // UpdateEntities = false and only the dummy is drawn over the fade.
    for (Entity* e = Ecs_First(); e; e = e->mNext)
    {
        if (e->mV)
        {
            e->mV->mVisible = false;
        }
    }
    gDeath.mDummy = Ecs_Create(kDieDummyClass, px, py, PART_VISUAL);
    if (gDeath.mDummy)
    {
        Entity& d = *gDeath.mDummy;
        d.mPlayerOwned = true; // kept until the death state ends
        Ecs_SetSheet(d, Game_IsCell() ? "young_player_cell" : "young_player", 16, 16);
        d.mV->mOffX = Fx(3);
        d.mV->mOffY = Fx(4);
        d.mV->mLayer = LAYER_UI;
        Ecs_Play(d, "die");
    }
}

void DeathUpdate()
{
    Death& d = gDeath;
    if (d.mDummy)
    {
        Entity& dummy = *d.mDummy;
        // PlayerDieDummy: towards (80, 100) of the screen (80, 80 of the playfield).
        MoveTo(dummy.mT->mX, Fx(gGridX * kScreenPx + 80), Fx(30));
        MoveTo(dummy.mT->mY, Fx(gGridY * kScreenPx + 80), Fx(30));
        Ecs_BasePostUpdate(dummy);
        if (!d.mGotControl && Ecs_AnimFinished(dummy))
        {
            PlaySfx("player_hit_1");
            const int32_t song = AudioManager_FindBgm("gameover");
            if (song >= 0)
            {
                AudioManager_PlayBGM(static_cast<uint32_t>(song));
            }
            d.mGotControl = true;
        }
    }
    if (d.mFadingOut)
    {
        // black_overlay.ChangeAlpha(0.6f)
        if (MoveTo(d.mFade, kFxOne, FxF(0.6f)))
        {
            d.mActive = false;
            gDeaths++;
            if (d.mDummy)
            {
                Ecs_Free(*d.mDummy);
                d.mDummy = nullptr;
            }
            Player_Entity()->mExists = true;
            int map = gGame.mCheckpointMap;
            int x = gGame.mCheckpointX;
            int y = gGame.mCheckpointY;
            if (d.mNo)
            {
                // "No..": the death room, DRAWER.
                map = Game_MapIndex("DRAWER");
                x = 368;
                y = 224;
            }
            else if (map < 0)
            {
                // No checkpoint yet: where the map was entered.
                map = gGame.mCurrentMap;
                x = FxTrunc(gDeathFallbackX);
                y = FxTrunc(gDeathFallbackY);
            }
            // (PlayState: the death state's exit warps at once and goes on in S_MAP_ENTER, the screen black already)
            World_RequestWarp(map, x, y, 255);
            WarpNow();
            gWarp.mState = WARP_ENTER;
            gWarp.mAlpha = kFxOne;
        }
        SetDeathBlend(false, d.mFade);
        return;
    }
    MoveTo(d.mFade, kFxOne, FxF(0.9f));
    SetDeathBlend(true, d.mFade);
    if (!d.mGotControl)
    {
        return;
    }
    // (either way, even onto what's already picked)
    if (key_hit(KEY_UP))
    {
        d.mNo = false;
        PlaySfx("menu_move");
    }
    else if (key_hit(KEY_DOWN))
    {
        d.mNo = true;
        PlaySfx("menu_move");
    }
    else if (key_hit(KEY_A))
    {
        PlaySfx("menu_select");
        GameState_SetHealth(gGame.mMaxHealth);
        d.mFadingOut = true;
        d.mFade = 0;
        Dialog_HideLabels();
        if (d.mNo)
        {
            // (EquippedBroom = NONE is refused by the setter: the death room's own rule leaves only the plain broom)
            gGame.mInDeathRoom = true;
        }
        else
        {
            gGame.mDisableMenu = false;
        }
        return;
    }
    // "Continue?" / "Yes" / "No.." at (50, 60) of the C# screen (its playfield starts 20 px down), 8 px apart.
    static const char* const kLines[3] = { "Continue?", "Yes", "No.." };
    static const int kX[3] = { 50, 69, 67 };
    static const int kY[3] = { 40, 48, 56 };
    Dialog_ShowLabels(kLines, kX, kY, 3, 38, 53, (d.mNo ? kY[2] : kY[1]) + 2, Game_IsCell()); // (white on the CELL's black)
}

void DrawWorld()
{
    UpdateBackground(false);
    Hud_Draw(gDeath.mActive);
    Entities_Draw(FxFloor(gCamX), FxFloor(gCamY), kViewX, gShake.mX, gShake.mY);
    gShown = true;
}
// TileMap.changedTiles / oldChangedTiles: each tile's value from before its first change on this screen. As a screen
// transition ends, the old screen's are put back (so swaps, and anything else that changes tiles, last only while
// you stay on the screen).
struct TileChange final
{
    uint8_t mLayer;
    uint16_t mX;
    uint16_t mY;
    uint16_t mOld;
};
constexpr int kMaxTileChanges = 256;
TileChange gTileChanges[2][kMaxTileChanges] __attribute__((section(".sbss"))); // (EWRAM)
int gTileChangeCount[2] = {};
int gTileChangesCur = 0;

void ResetTileChanges()
{
    gTileChangeCount[0] = 0;
    gTileChangeCount[1] = 0;
}

// The map's animated tiles (AnimatedTile: an Anim each, looped): their current frames go into their own VRAM tiles.
constexpr int kMaxTileAnims = 32;
struct TileAnimState final
{
    float mTimer;
    uint8_t mIndex;
    bool mDirty;
};
TileAnimState gTileAnimState[kMaxTileAnims] __attribute__((section(".sbss")));
bool gTileAnimsHeld = false; // the mosaic borrowed the tiles: rewrite them all once it's done

void WriteTileAnim(int i)
{
    const uint16_t* a = &gTileAnimData->mAnims[i * 8];
    const uint8_t* frame = gTileAnimData->mFrames + (a[7] + gTileAnimState[i].mIndex) * 4 * 64;
    for (int q = 0; q < 4; q++)
    {
        memcpy32(&tile8_mem[0][a[1 + q]], frame + q * 64, 64 / 4);
    }
    gTileAnimState[i].mDirty = false;
}

void LoadTileAnims(const MapTileAnimData& anims)
{
    gTileAnimData = &anims;
    memset(gTileAnimOf, kNoTileAnim, sizeof(gTileAnimOf));
    const int count = anims.mCount < kMaxTileAnims ? static_cast<int>(anims.mCount) : kMaxTileAnims;
    for (int i = 0; i < count; i++)
    {
        const uint16_t tile = anims.mAnims[i * 8];
        if (tile < kMaxTile16)
        {
            gTileAnimOf[tile] = static_cast<uint8_t>(i);
        }
        gTileAnimState[i] = TileAnimState();
        WriteTileAnim(i);
    }
}

// Map.Update: each Anim.Update.
void UpdateTileAnims()
{
    if (!gTileAnimData)
    {
        return;
    }
    const int count = gTileAnimData->mCount < kMaxTileAnims ? static_cast<int>(gTileAnimData->mCount) : kMaxTileAnims;
    const bool held = Mosaic_Active();
    for (int i = 0; i < count; i++)
    {
        TileAnimState& s = gTileAnimState[i];
        const uint16_t* a = &gTileAnimData->mAnims[i * 8];
        const float delay = 1.0f / a[6];
        s.mTimer += kUiDeltaTime;
        while (s.mTimer > delay)
        {
            s.mTimer -= delay;
            s.mIndex = static_cast<uint8_t>(s.mIndex + 1 == a[5] ? 0 : s.mIndex + 1);
            s.mDirty = true;
        }
        if (!held && (s.mDirty || gTileAnimsHeld))
        {
            WriteTileAnim(i);
        }
    }
    gTileAnimsHeld = held;
}

} // namespace

void Game_PlaySong(const char* name, fx volume)
{
    Game_SetSongVolume(volume);
    gMusicPlaying = name;
    const int32_t id = name[0] ? AudioManager_FindBgm(name) : -1;
    if (id >= 0)
    {
        AudioManager_PlayBGM(static_cast<uint32_t>(id));
    }
    else
    {
        AudioManager_StopBGM();
    }
}

const char* Game_CurrentSong()
{
    return gMusicPlaying ? gMusicPlaying : "";
}

void Game_IgnoreMusicNextUpdate()
{
    gIgnoreMusic = true;
}

fx Game_SongVolume()
{
    return gSongVolume;
}

void Game_SetSongVolume(fx volume)
{
    gSongVolume = volume < 0 ? 0 : (volume > kFxOne ? kFxOne : volume);
}

void Screen_Shake(fx intensity, fx seconds, uint8_t dirs)
{
    // (ScreenShake.Shake: _shakeUpdate carries on; no shaking with the setting off)
    gShake.mTime = gSettings.mScreenshake ? seconds : 0;
    gShake.mIntensity = intensity;
    gShake.mDirs = dirs;
    gShake.mX = gShake.mY = 0;
}

void Screen_ResetShake()
{
    gShake = Shake();
}

bool Screen_ShakeActive()
{
    return gShake.mTime > 0;
}

void Screen_Flash(fx seconds, FlashColour colour)
{
    static const uint8_t kColours[][3] = {
        { 255, 255, 255 }, { 0, 0, 0 }, { 255, 0, 0 }, { 255, 17, 17 }, { 17, 17, 255 },
    };
    const float brightness = gSettings.mFlashBrightness;
    int c[3];
    for (int i = 0; i < 3; i++)
    {
        c[i] = static_cast<int>(kColours[colour][i] * brightness * 31 / 255 + 0.5f);
    }
    gFlash.mColour = static_cast<uint16_t>(RGB15(c[0], c[1], c[2]));
    gFlash.mTint = gFlash.mColour != 0 && gFlash.mColour != 0x7FFF;
    gFlash.mStep = FxMul(FxDiv(kFxOne, seconds), kFxTick); // (ChangeAlpha(-1 / time))
    if (gSettings.mFlashEasing == 0.0f)
    {
        gFlash.mAlpha = kFxOne;
        gFlash.mEasing = false;
        gFlash.mFull = true;
    }
    else
    {
        gFlash.mAlpha = FxF(0.1f);
        gFlash.mEaseStep = FxMul(FxF(1.0f / gSettings.mFlashEasing), kFxTick); // (ChangeAlpha(1f / flash_easing))
        gFlash.mEasing = true;
        gFlash.mFull = false;
    }
}

bool Screen_FlashActive()
{
    return gFlash.mAlpha > 0;
}

bool Screen_FlashFull()
{
    return gFlash.mFull;
}

void Darkness_TargetAlpha(fx alpha, fx vel)
{
    gDark.mVel = vel;
    gDark.mTarget = alpha;
    if (gDark.mAlpha == 0 && alpha != 0)
    {
        gDark.mAlpha = FxF(0.1f);
    }
}

void Darkness_ForceAlpha(fx alpha)
{
    gDark.mTarget = gDark.mAlpha = alpha;
}

fx Darkness_Alpha()
{
    return gDark.mAlpha;
}

bool Darkness_ReachedAlpha()
{
    return gDark.mAlpha == gDark.mTarget;
}

Entity* Darkness_PlayerLight()
{
    if (gMainLight && (gMainLight->mFreed || gMainLight->mClass != gMainLightClass))
    {
        gMainLight = nullptr;
    }
    return gMainLight;
}

void Darkness_SetPlayerLight(Entity& light)
{
    gMainLight = &light;
    gMainLightClass = light.mClass;
}

void Darkness_MakeLight(Entity& e, const char* sheet)
{
    Ecs_SetSheet(e, sheet);
    e.mV->mWindow = true;
    e.mV->mOffX = Fx(Ecs_SpriteWidth(e) / 2);
    e.mV->mOffY = Fx(Ecs_SpriteHeight(e) / 2);
    e.mV->mScale = Fx(3);
    Ecs_SetSolid(e, false);
}

// GlobalState.gameScreenFade (black): over everything but the UI.
static fx gGameFade = 0;
static bool gGameFadeWhite = false;
static bool gGameFadeSparesUi = false;
static fx gEndingBlack = 0;

int World_Deaths()
{
    return gDeaths;
}

void World_SetDeaths(int deaths)
{
    gDeaths = deaths;
}

void Screen_SetFadeSparesUi(bool on)
{
    gGameFadeSparesUi = on;
}

void Screen_EndEnding()
{
    gGameFadeSparesUi = false;
    gEndingBlack = 0;
}

// The screen is all white under it by then: the BGs go and the backdrop stands in for them, darkened with the sprites.
void Screen_SetEndingBlack(fx alpha)
{
    gEndingBlack = alpha;
    pal_bg_mem[0] = CLR_WHITE;
    REG_DISPCNT = REG_DISPCNT & ~(DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3);
    REG_BLDCNT = BLD_OBJ | BLD_BACKDROP | BLD_BLACK;
    const int y = FxFloor(alpha * 16 + kFxOne / 2);
    REG_BLDY = static_cast<uint16_t>(y > 16 ? 16 : y);
}

void Screen_SetFadeWhite(bool white)
{
    gGameFadeWhite = white;
}

void Screen_SetFade(fx alpha)
{
    gGameFade = alpha;
    World_ScreenDarken(0);
}

// ---- Wave (Drawing/Effects/Wave.cs) --------------------------------------------------------------------------
// The shader shifts each row of the 160x180 screen sideways by 3 - |phase| px: a triangle wave of 15-row steps that
// loops every two seconds. Here an HBlank DMA rewrites the BG scroll registers on every line (the sprites don't
// wave). The main loop fills a table for the next frame; the VBlank interrupt arms it.
namespace
{
struct WaveOfs final
{
    uint16_t mX;
    uint16_t mY;
};
constexpr int kWaveRows = kScreenPx + 1; // line 0 by hand, then one HBlank transfer before each further line
constexpr int kWaveNone = -1;            // nothing new for the interrupt
constexpr int kWaveOff = -2;             // the interrupt stops the DMA
alignas(4) WaveOfs gWaveTable[2][kWaveRows][4] __attribute__((section(".sbss")));
volatile int gWaveReady = kWaveNone;
int gWaveArmed = -1; // the table the DMA reads (interrupt side)
int gWaveWrite = 0;
bool gWave = false;
float gWaveTimer = 0;

// Wave.Update (every frame, on or not), then the table for what was just drawn.
void UpdateWave()
{
    gWaveTimer += kUiDeltaTime * 0.5f;
    if (gWaveTimer > 1)
    {
        gWaveTimer -= 1;
    }
    if (!gWave)
    {
        if (gWaveArmed >= 0)
        {
            gWaveReady = kWaveOff;
        }
        return;
    }
    const int phaseOffset = static_cast<int>(gWaveTimer * 180);
    WaveOfs(*table)[4] = gWaveTable[gWaveWrite];
    for (int line = 0; line < kWaveRows; line++)
    {
        // (the C#'s screen has its 20-row HUD strip above the playfield)
        const int yloc = (line + 20 - phaseOffset + 180) % 180;
        const int phase = (yloc / 15) % 12 - 6;
        const int shift = 3 - (phase < 0 ? -phase : phase);
        for (int bg = 0; bg < 4; bg++)
        {
            table[line][bg].mX = static_cast<uint16_t>(gBgOfsX[bg] + shift);
            table[line][bg].mY = static_cast<uint16_t>(gBgOfsY[bg]);
        }
    }
    gWaveReady = gWaveWrite;
    gWaveWrite ^= 1;
}
} // namespace

void Screen_WaveOnVBlank()
{
    const int ready = gWaveReady;
    if (ready == kWaveOff)
    {
        gWaveReady = kWaveNone;
        REG_DMA0CNT = 0;
        gWaveArmed = -1;
        for (int bg = 0; bg < 4; bg++)
        {
            REG_BG_OFS[bg].x = static_cast<uint16_t>(gBgOfsX[bg]);
            REG_BG_OFS[bg].y = static_cast<uint16_t>(gBgOfsY[bg]);
        }
        return;
    }
    if (ready >= 0)
    {
        gWaveArmed = ready;
        gWaveReady = kWaveNone;
    }
    if (gWaveArmed < 0)
    {
        return;
    }
    const WaveOfs(*table)[4] = gWaveTable[gWaveArmed];
    REG_DMA0CNT = 0;
    for (int bg = 0; bg < 4; bg++)
    {
        REG_BG_OFS[bg].x = table[0][bg].mX;
        REG_BG_OFS[bg].y = table[0][bg].mY;
    }
    REG_DMA0SAD = static_cast<u32>(reinterpret_cast<uintptr_t>(&table[1][0]));
    REG_DMA0DAD = static_cast<u32>(reinterpret_cast<uintptr_t>(&REG_BG_OFS[0]));
    REG_DMA0CNT = (sizeof(table[0]) / 4) | DMA_DST_RELOAD | DMA_REPEAT | DMA_32 | DMA_AT_HBLANK | DMA_ENABLE;
}

void Screen_SetWave(bool on)
{
    gWave = on;
}

void Screen_StopWave()
{
    gWave = false;
    gWaveReady = kWaveNone;
    gWaveArmed = -1;
    REG_DMA0CNT = 0;
}

bool Screen_Wave()
{
    return gWave;
}

void World_ScreenDarken(int level)
{
    const int fade = FxFloor(gGameFade * 16 + kFxOne / 2);
    if (gGameFadeWhite && fade > 0 && fade >= level)
    {
        // (fading to white: brighten instead, over the lights too)
        if (gLightWindows)
        {
            gLightWindows = false;
            ApplyWindows();
        }
        if (gEndingBlack > 0)
        {
            Screen_SetEndingBlack(gEndingBlack);
            return;
        }
        REG_BLDCNT = BLD_BG0 | BLD_BG1 | BLD_BG2 | BLD_BG3 | (gGameFadeSparesUi ? 0 : BLD_OBJ) | BLD_BACKDROP | BLD_WHITE;
        REG_BLDY = static_cast<uint16_t>(fade > 16 ? 16 : fade);
        return;
    }
    level = fade > level ? fade : level;
    const int dark = DarkLevel();
    const bool lights = level == 0 && dark > 0;
    if (lights != gLightWindows)
    {
        gLightWindows = lights;
        ApplyWindows();
    }
    ScreenDarken(level > dark ? level : dark);
}

// ---- Map callbacks for the entity system ---------------------------------------------------------------------------
void Map_Collide(Entity& e)
{
    const Rect h = Ecs_Hitbox(e);
    const int tlx = FloorDiv16(h.mX);
    const int tly = FloorDiv16(h.mY);
    const int brx = FloorDiv16(h.mX + h.mW);
    const int bry = FloorDiv16(h.mY + h.mH);
    for (int cy = tly; cy <= bry; cy++)
    {
        for (int cx = tlx; cx <= brx; cx++)
        {
            CollideTile(cx, cy, e);
        }
    }
}

uint8_t Map_CollisionAllowAt(fx x, fx y)
{
    return AllowAt(x, y);
}

Entity* World_Player()
{
    return Player_Entity();
}

fx World_CamX()
{
    return gCamX;
}

fx World_CamY()
{
    return gCamY;
}

// ---- World API ---------------------------------------------------------------------------------------------------
bool World_Init(ObjVramAllocator& vram)
{
    gVram = &vram;
    Entities_Init(vram);
    Player_Create();
    gPlayerLight = Ecs_Create(kPlayerLightClass, 0, 0, PART_VISUAL | PART_BRAIN);
    gPlayerLight->mPlayerOwned = true;
    Darkness_MakeLight(*gPlayerLight, "player-light");
    gPlayerLight->mExists = false;
    World_NewPlayState();
    return true;
}

void World_NewPlayState()
{
    // (the title starts a new PlayState, with a new Player and so a new PlayerLight)
    gPlayerLight->mV->mScale = Fx(2);
    gPlayerLight->Data<PlayerLightData>().mFlickerTimer = FxSec(6 / 30.0f);
    gQuickSaveTimer = 5;
    gQuickLoad = false;
    gTexRandomTimer = 0;
}

void World_QuickSave()
{
    if (gQuickSaveTimer > 0)
    {
        Game_PlaySfx("noise_step_1");
        return;
    }
    Game_PlaySfx("menu_select");
    const Entity& p = *World_Player();
    Save_QuickWrite(gGame.mCurrentMap, FxTrunc(p.mT->mX), FxTrunc(p.mT->mY));
}

void World_ReloadTileset()
{
    if (!gMap)
    {
        return;
    }
    uint32_t tilesetBytes = gMap->mTileSetData.mTileSetSize;
    if (tilesetBytes > kMaxTilesetBytes)
    {
        tilesetBytes = kMaxTilesetBytes;
    }
    memcpy16(tile_mem, gMap->mTileSetData.mTileSetData, tilesetBytes / 2);
}

void World_ForceTextureReload()
{
    gForceTextureReload = true;
}

void World_RequestQuickLoad()
{
    gQuickLoad = true;
}

bool World_TakeQuickLoad()
{
    // (PlayState.QuickLoad: nothing happens without a quick save)
    const bool load = gQuickLoad && Save_QuickExists();
    gQuickLoad = false;
    if (load)
    {
        Game_PlaySfx("menu_select");
    }
    return load;
}

// The map's palette, tileset and layers (into VRAM / EWRAM): what the BG layers draw.
static void LoadMapGraphics(const MapEntry& map)
{
    // Palette (halfwords) and 8bpp tileset into BG VRAM. The tileset must stay clear of screenblocks 28-31
    // (0x0600E000 and up), which the layers use for their maps.
    TexDark_SetBgColours(static_cast<int>(map.mPalData.mPalSize)); // (first: it puts the old colours back)
    memcpy16(pal_bg_mem, map.mPalData.mPalData, map.mPalData.mPalSize);
    uint32_t tilesetBytes = map.mTileSetData.mTileSetSize;
    if (tilesetBytes > kMaxTilesetBytes)
    {
        tilesetBytes = kMaxTilesetBytes;
    }
    memcpy16(tile_mem, map.mTileSetData.mTileSetData, tilesetBytes / 2);
    LoadTileAnims(map.mTileAnims);

    // Layers, unpacked into EWRAM.
    ResetTileChanges();
    gTile16Entries = map.mScreenBlockData.mTile16Entries;
    gTile16Count = map.mScreenBlockData.mTile16Count;
    for (int layer = 0; layer < kLayerCount; layer++)
    {
        const MapLayerData& data = map.mScreenBlockData.mLayers[layer];
        gLayerW[layer] = data.mWidth;
        gLayerH[layer] = data.mHeight;
        gLayerTiles[layer] = reinterpret_cast<const uint16_t*>(gLayerBuffer[layer]);
        if (data.mWidth == 0 || data.mUnpackedSize == 0 || data.mUnpackedSize > kMaxLayerBytes)
        {
            gLayerW[layer] = 0; // empty layer
            gLayerH[layer] = 0;
        }
        else
        {
            LZ77UnCompWram(data.mPacked, gLayerBuffer[layer]);
        }
    }
}

void World_Load(const MapEntry& map, const char* mapName, const MapAudioData& audio, const MapScrollData& scroll,
                const MapDoorList& doors, const MapDarkData& dark, bool loadEntities)
{
    gDarkData = &dark;
    Darkness_SetPlayerLight(*gPlayerLight);
    gMapOffCells = 0;
    gMap = &map;
    gMapName = mapName;
    gDoors = &doors;
    gWarp.mPending = false;
    gAudio = &audio;

    LoadMapGraphics(map);

    gCellsW = map.mCollision.mCellsW;
    gCellsH = map.mCollision.mCellsH;
    gClassCount = map.mCollision.mClassCount;
    memcpy(gClassRam, map.mCollision.mClasses, static_cast<size_t>(gClassCount) * 3);
    gClasses = gClassRam;
    if (map.mCollision.mUnpackedSize <= kMaxCollisionCells)
    {
        LZ77UnCompWram(map.mCollision.mPacked, gCollisionGrid);
    }

    gTransition = false;
    Player_Reset(mapName);

    gScroll = scroll.mPattern ? &scroll : nullptr;
    gScrollPosX = 0;
    gScrollPosY = 0;
    gScrollUnwrapX = 0;
    gScrollUnwrapY = 0;
    ApplyDisplayRegs((REG_DISPCNT & DCNT_WIN0) != 0);

    if (!loadEntities)
    {
        // (World_PlaceAt follows: it places the player and draws that screen, so don't pick one and draw it too)
        UpdateAmbience();
        return;
    }
    PickSpawn();
    MoveCameraToGrid();
    UpdateBackground(true);
    UpdateScroll(false, true);

    UpdateMusic();
    UpdateAmbience();
    UpdateDarkness(false);
    gDeathFallbackX = Player_Entity()->mT->mX;
    gDeathFallbackY = Player_Entity()->mT->mY;
    Entities_LoadGrid(gGridX, gGridY, true);
}

void World_GetBgOffsets(int16_t x[4], int16_t y[4])
{
    for (int i = 0; i < 4; i++)
    {
        x[i] = gBgOfsX[i];
        y[i] = gBgOfsY[i];
    }
}

void World_Enter()
{
    // BG layers + OBJ, with window 0 cutting everything to the 160x160 playfield.
    gLightWindows = false;
    ApplyDisplayRegs(true);
}

void World_SetPlayerVisible(bool visible)
{
    if (!visible && gShown)
    {
        Entities_Clear();
        gShown = false;
    }
}

// CutsceneState.Warp / Return: a cutscene showing a screen of another map (graphics only; the play state neither
// updates nor draws meanwhile).
struct CutsceneView final
{
    bool mActive;
    const MapScrollData* mScroll;
    fx mDark;
} gView = {};
const MapEntry* gMapTable = nullptr;
uint32_t gMapTableCount = 0;

void World_SetDarkTextures(const DarkTexture* textures)
{
    gDarkTextures = textures;
}

int World_CellsW()
{
    return gCellsW;
}

int World_CellsH()
{
    return gCellsH;
}

void World_ReloadSettings()
{
    UpdateDarkness(false);
}

void World_SetMapTable(const MapEntry* maps, uint32_t count)
{
    gMapTable = maps;
    gMapTableCount = count;
}

void World_CutsceneWarp(int map, int gx, int gy)
{
    if (!gMapTable || map < 0 || static_cast<uint32_t>(map) >= gMapTableCount)
    {
        return;
    }
    if (!gView.mActive)
    {
        gView.mActive = true;
        gView.mScroll = gScroll;
        gView.mDark = Darkness_Alpha();
    }
    LoadMapGraphics(gMapTable[map]);
    gScroll = nullptr;
    ApplyDisplayRegs((REG_DISPCNT & DCNT_WIN0) != 0);
    gCamX = Fx(gx * kScreenPx);
    gCamY = Fx(gy * kScreenPx);
    UpdateBackground(true);
    Ecs_SetCutsceneView(true);
}

void World_CutsceneReturn()
{
    if (!gView.mActive)
    {
        return;
    }
    gView.mActive = false;
    LoadMapGraphics(*gMap);
    gScroll = gView.mScroll;
    ApplyDisplayRegs((REG_DISPCNT & DCNT_WIN0) != 0);
    MoveCameraToGrid();
    UpdateBackground(true);
    UpdateScroll(false, true);
    Darkness_ForceAlpha(gView.mDark);
    Ecs_SetCutsceneView(false);
}

// PlayState.SetBroom: menu_move when the broom changed.
void SetBroom(uint8_t broom)
{
    Inventory_SetEquippedBroom(broom);
    if (gGame.mEquippedBroomChanged)
    {
        Game_PlaySfx("menu_move");
    }
}

// PlayState.SwitchBroom: the next (R) or previous (L) broom held, round BroomType's order.
void SwitchBroom(bool next)
{
    if (!Inventory_HasAnyBroom())
    {
        return;
    }
    int current = Inventory_EquippedBroom();
    if (!Game_CanChangeBroom())
    {
        // (only the plain broom and the transformer: step forward from just before the other one)
        current = (current == BROOM_TRANSFORMER ? BROOM_NORMAL : BROOM_TRANSFORMER) - 1;
        next = true;
    }
    do
    {
        current += next ? 1 : -1;
        current = (current + BROOM_TYPE_COUNT) % BROOM_TYPE_COUNT;
    } while (!Inventory_HasBroomType(static_cast<uint8_t>(current)));
    SetBroom(static_cast<uint8_t>(current));
}

// PlayState.Update.
void World_Update()
{
    gGame.mPlayFrames++;
    gQuickSaveTimer -= kUiDeltaTime;
    Ecs_ReleaseSpawned();
    const bool child = gDialogOpen || gCutscene.mFn || gDeath.mActive || Pause_IsOpen();
    bool updateEntities = !gDeath.mActive && !Pause_IsOpen() && !gView.mActive;
    if (child)
    {
        // A child state (dialogue, cutscene, death): the player stands still; entities keep updating (except death).
        Player_SetInvincible(true);
        Player_SetDontMove(true);
        Player_SetActionsDisabled(true);
        // Child states stack: a box the cutscene itself opened pauses it (CutsceneState's DialogueEvent
        // substate, also for the frame it closes), any other box runs alongside it.
        const bool cutsceneBox = gDialogOpen && Game_DialogueIsCutscene();
        bool cutsceneEnded = false;
        if (gDialogOpen)
        {
            if (!Dialog_Update())
            {
                gDialogOpen = false;
            }
        }
        if (gCutscene.mFn && !cutsceneBox)
        {
            Entity* owner = gCutscene.mOwner;
            const CutsceneFn fn = gCutscene.mFn;
            if (owner->mFreed || owner->mClass != gCutscene.mClass || fn(*owner))
            {
                // (one that ends by starting the next: the C# drops its state now and adds the new one after)
                cutsceneEnded = true;
                if (gCutscene.mFn == fn)
                {
                    gCutscene.mFn = nullptr;
                }
            }
        }
        if (gDeath.mActive)
        {
            DeathUpdate();
        }
        if (Pause_IsOpen())
        {
            Pause_Update();
        }
        if (!gDialogOpen && (!gCutscene.mFn || cutsceneEnded) && !gDeath.mActive && !Pause_IsOpen())
        {
            Player_SetDontMove(false);
            Player_SetActionsDisabled(false);
            Player_SetInvincible(false);
            Player_SetSkipBroom(); // the press that closed the box doesn't also swing the broom
        }
        if (updateEntities)
        {
            Ecs_DoCollisions(false, gGridX * kScreenPx, gGridY * kScreenPx);
        }
    }
    else
    {
        const bool wasTransition = gTransition;
        if (gTransition)
        {
            StateTransition();
        }
        else if (gWarp.mState == WARP_EXIT)
        {
            WarpExit();
        }
        else if (gWarp.mState == WARP_ENTER)
        {
            WarpEnter();
        }
        else if (key_hit(KEY_START) && !gGame.mDisableMenu)
        {
            // StateNormal: Pause opens the menu, and nothing else this frame
            Pause_Open();
            Game_PlaySfx("pause_sound");
        }
        else if (gWarp.mPending)
        {
            gWarp.mState = WARP_EXIT; // StateNormal: GlobalState.WARP -> S_MAP_EXIT, and nothing else this frame
            Ecs_FireEvent(GEV_START_WARP);
            World_TileChangesStart();
        }
        else
        {
            CheckForTransition();
            if (!Player_BroomOut())
            {
                if (key_hit(KEY_R))
                {
                    SwitchBroom(true);
                }
                else if (key_hit(KEY_L))
                {
                    SwitchBroom(false);
                }
            }
        }
        // No player-map collision during a screen transition.
        Ecs_DoCollisions(wasTransition || gTransition, gGridX * kScreenPx, gGridY * kScreenPx);
    }

    // PlayState.EntityUpdate
    if (updateEntities)
    {
        Entity& player = *Player_Entity();
        if (key_hit(KEY_A) && !child && !Pause_IsOpen() && Player_State() == PS_GROUND && !Player_SkipBroom() &&
            Ecs_CheckInteraction(player))
        {
            Player_SetSkipBroom();
        }
        Ecs_UpdateAll(&player);
        Player_Update();
        Ecs_FreeDead();
    }

    // GlobalState.SetDialogueMode: something opened the dialogue box this frame.
    if (!Pause_IsOpen() && Dialog_IsOpen())
    {
        // (a second box over an open one is a new DialogueState too)
        const bool opened = Dialog_TakeOpened();
        if (!gDialogOpen || opened)
        {
            gDialogOpen = true;
            Player_BeIdle();
        }
    }
    gGame.mEnemiesKilled = Ecs_KilledEnemies();

    UpdateTileAnims();

    // PlayState.Refreshes
    if (gGame.mEquippedBroomChanged)
    {
        gGame.mEquippedBroomChanged = false;
        Player_UpdateBroomType();
    }
    if (gGame.mGameMode == GAME_MODE_NORMAL)
    {
        Sprite_ChaosOff(); // (a new game, or a load)
    }
    if (gGame.mGameMode == GAME_MODE_EXTREME_CHAOS)
    {
        gTexRandomTimer += kUiDeltaTime;
        if (gTexRandomTimer >= 0.8f)
        {
            gForceTextureReload = true;
            gTexRandomTimer = 0;
        }
    }
    if (gForceTextureReload)
    {
        // ResourceManager.ReloadRandomizableTextures
        gForceTextureReload = false;
        if (gGame.mGameMode != GAME_MODE_NORMAL)
        {
            static float sDraws[kRandomizableTextures] __attribute__((section(".sbss")));
            for (int i = 0; i < kRandomizableTextures; i++)
            {
                sDraws[i] = static_cast<float>(Rng_NextDouble());
            }
            Sprite_ChaosShuffle(sDraws, kRandomizableTextures);
        }
        else
        {
            Sprite_ChaosOff();
        }
    }
    // PlayState.UpdateHealth
    if (!gDialogOpen && !gCutscene.mFn && !gDeath.mActive && !Pause_IsOpen() && !gWarp.mPending && gGame.mCurHealth == 0)
    {
        DeathStart();
    }

    // The darkness fades on (Map.Update), and its BLD stands until something else (death, a warp) takes over.
    if (!gDeath.mActive)
    {
        MoveTo(gDark.mAlpha, gDark.mTarget, gDark.mVel);
    }
    // PlayState._map_specific_update
    if (gMapName && strcmp(gMapName, "BLANK") == 0)
    {
        if (Rng_NextDouble() < 0.05)
        {
            Screen_Shake(FxF(0.007f), FxF(0.1f));
        }
        if (Rng_NextDouble() < 0.005)
        {
            Darkness_ForceAlpha(FxF(0.1f));
            Darkness_TargetAlpha(kFxZero);
        }
    }
    // The full screen effects update after the game state (AnodyneGame.Update). A flash whitens everything.
    UpdateScreenEffects();
    if (!gDeath.mActive)
    {
        if (gFlash.mAlpha > 0)
        {
            if (gLightWindows)
            {
                gLightWindows = false;
                ApplyWindows();
            }
            if (gFlash.mTint)
            {
                int level = FxFloor(gFlash.mAlpha * 32 + kFxOne / 2);
                level = level > 32 ? 32 : level;
                if (TexDarkOn())
                {
                    gTintLevel = level; // (TexDark tints its own colours)
                }
                else
                {
                    TintPalettes(level, gFlash.mColour);
                }
                if (gDark.mAlpha > 0 || gDarkWritten)
                {
                    World_ScreenDarken(0);
                }
                gDarkWritten = gDark.mAlpha > 0;
            }
            else
            {
                UntintPalettes();
                int y = FxFloor(gFlash.mAlpha * 16 + kFxOne / 2);
                y = y > 16 ? 16 : y;
                ScreenDarken(0);
                REG_BLDCNT = BLD_BG0 | BLD_BG1 | BLD_BG2 | BLD_BG3 | BLD_OBJ | BLD_BACKDROP |
                             (gFlash.mColour ? BLD_WHITE : BLD_BLACK);
                REG_BLDY = static_cast<uint16_t>(y);
                gDarkWritten = true;
            }
        }
        else
        {
            UntintPalettes();
            if (gDark.mAlpha > 0 || gDarkWritten)
            {
                World_ScreenDarken(0);
            }
            gDarkWritten = gDark.mAlpha > 0;
        }
    }

    UpdateScroll(true, false);
    DrawWorld();
    if (MenuDraw_IsOpen() || Mosaic_Active())
    {
        TexDark_Off();
    }
    else
    {
        if (TexDarkOn())
        {
            UntintPalettes(); // (from before the texture: TexDark takes the plain colours)
        }
        TexDark_Frame(PlainDarkInTexDark() ? TexDark_Plain() : gDarkTex, FxFloor(gDark.mAlpha * 256 + kFxOne / 2),
                      gTintLevel, gFlash.mColour, ExtraBlend());
    }
    gTintLevel = 0;
    if (MenuDraw_IsOpen())
    {
        // (the pause menu covers the playfield)
        MenuDraw_Present();
    }
    else
    {
        UpdateWave();
    }
}

void Game_StartCutscene(Entity& owner, CutsceneFn fn)
{
    gCutscene.mFn = fn;
    gCutscene.mOwner = &owner;
    gCutscene.mClass = owner.mClass;
}

bool Game_InCutscene()
{
    return gCutscene.mFn != nullptr;
}

void World_SetMapLoader(void (*load)(uint8_t map))
{
    gMapLoader = load;
}

bool World_GetWarpFade(int& dark, int& pixelation, bool& swapped)
{
    swapped = gWarp.mSwapped;
    gWarp.mSwapped = false;
    dark = gWarp.mAlpha.mRaw;
    pixelation = static_cast<int>(gWarp.mPixelation * 256.0f);
    return gWarp.mState != WARP_NONE;
}

bool World_InTransition()
{
    return gTransition;
}

bool World_WarpPending()
{
    return gWarp.mPending;
}

void World_DropChildStates()
{
    if (gDialogOpen)
    {
        Dialog_Close();
        gDialogOpen = false;
    }
    gCutscene.mFn = nullptr;
}

void World_RequestWarp(int map, int x, int y, uint8_t facing)
{
    gWarp.mPending = true;
    gWarp.mMap = static_cast<uint8_t>(map);
    gWarp.mX = x;
    gWarp.mY = y;
    gWarp.mFacing = facing;
}

const MapDoor* World_FindDoor(int presetX, int presetY)
{
    for (uint32_t i = 0; gDoors && i < gDoors->mCount; i++)
    {
        const MapDoor& d = gDoors->mDoors[i];
        if (d.mSrcX == presetX && d.mSrcY == presetY)
        {
            return &d;
        }
    }
    return nullptr;
}

void World_TakeDoor(const MapDoor& d)
{
    World_RequestWarp(d.mDestMap, d.mDestX, d.mDestY, d.mFacing);
}

void World_PlaceAt(int x, int y, uint8_t facing, bool warp)
{
    // World facing order (0 down, 1 right, 2 up, 3 left) -> Facing.
    static const int kFacing[4] = { FACE_DOWN, FACE_RIGHT, FACE_UP, FACE_LEFT };
    gTransition = false;
    Player_Reset(gMapName);
    Player_PlaceAt(Fx(x), Fx(y), facing <= 3 ? kFacing[facing] : -1, warp);
    Player_BeIdle();
    gGridX = x / kScreenPx;
    gGridY = y / kScreenPx;
    while (gGridX > 0 && !GridExists(gGridX, gGridY))
    {
        gGridX--;
    }
    while (gGridY > 0 && !GridExists(gGridX, gGridY))
    {
        gGridY--;
    }
    MoveCameraToGrid();
    UpdateBackground(true);
    UpdateScroll(false, true);
    UpdateMusic();
    UpdateDarkness(false);
    gDeathFallbackX = Player_Entity()->mT->mX;
    gDeathFallbackY = Player_Entity()->mT->mY;
    Entities_LoadGrid(gGridX, gGridY, true);
}

void World_GetPlayerInfo(WorldPlayerInfo& info)
{
    static const int kWorldFacing[4] = { 3, 1, 2, 0 }; // Facing (LEFT, RIGHT, UP, DOWN) -> world order
    const Entity& p = *Player_Entity();
    info.mX = FxTrunc(p.mT->mX);
    info.mY = FxTrunc(p.mT->mY);
    info.mFacing = kWorldFacing[p.mFacing & 3];
    info.mGridX = gGridX;
    info.mGridY = gGridY;
    info.mTransition = gTransition;
    info.mLadder = false;
    info.mFallen = Player_HasFallen();
    info.mHealth = gGame.mCurHealth;
    info.mDeathMenu = gDeath.mActive;
    info.mDeaths = gDeaths;
}

void World_GetCellsSize(int& w, int& h)
{
    w = gCellsW;
    h = gCellsH;
}

namespace
{
void SetTile(int layer, int cx, int cy, int tile)
{
    const_cast<uint16_t*>(gLayerTiles[layer])[cy * gLayerW[layer] + cx] = static_cast<uint16_t>(tile);

    // The cell's class again, merged from BG and BG2 like the content compiler's
    if (layer < 2 && cx < gCellsW && cy < gCellsH)
    {
        const MapCollisionData& col = gMap->mCollision;
        auto tileClass = [&](uint32_t t, uint8_t out[3]) {
            if (t < col.mTileCount)
            {
                memcpy(out, col.mTiles + t * 3, 3);
            }
            else
            {
                out[0] = t == 0 ? TOUCH_NONE : TOUCH_ANY;
                out[1] = 0;
                out[2] = TOUCH_ANY;
            }
        };
        auto layerTile = [&](int l) -> uint32_t {
            return (cx < gLayerW[l] && cy < gLayerH[l]) ? gLayerTiles[l][cy * gLayerW[l] + cx] : 0;
        };
        uint8_t merged[3];
        tileClass(layerTile(0), merged);
        const uint32_t t2 = layerTile(1);
        if (t2 != 0)
        {
            uint8_t c2[3];
            tileClass(t2, c2);
            merged[0] |= c2[0];
            if (c2[1] != 0)
            {
                merged[1] = c2[1];
                merged[2] = c2[2];
            }
        }
        if (merged[1] == 0)
        {
            merged[2] = TOUCH_ANY;
        }
        int index = 0;
        while (index < gClassCount && memcmp(gClassRam + index * 3, merged, 3) != 0)
        {
            index++;
        }
        if (index == gClassCount && gClassCount < 256)
        {
            memcpy(gClassRam + index * 3, merged, 3);
            gClassCount++;
        }
        if (index < gClassCount)
        {
            gCollisionGrid[cy * gCellsW + cx] = static_cast<uint8_t>(index);
        }
    }

    // Redraw it if it's on the current screen (the streamed BG window holds it)
    cx -= gMapOffCells;
    if (cx >= 0 && cx / kScreenCells == gGridX && cy / kScreenCells == gGridY)
    {
        for (int i = 0; i < 4; i++)
        {
            WriteBgTile(cx * 2 + (i & 1), cy * 2 + (i >> 1));
        }
    }
}
} // namespace

void World_ChangeTile(int layer, int cx, int cy, int tile)
{
    // TileMap.ChangeTile: outside the layer does nothing
    if (layer < 0 || layer >= kLayerCount || cx < 0 || cy < 0 || cx >= gLayerW[layer] || cy >= gLayerH[layer])
    {
        return;
    }
    TileChange* changes = gTileChanges[gTileChangesCur];
    int& count = gTileChangeCount[gTileChangesCur];
    int i = 0;
    while (i < count && !(changes[i].mLayer == layer && changes[i].mX == cx && changes[i].mY == cy))
    {
        i++;
    }
    if (i == count && count < kMaxTileChanges)
    {
        changes[count++] = { static_cast<uint8_t>(layer), static_cast<uint16_t>(cx), static_cast<uint16_t>(cy),
                             static_cast<uint16_t>(World_GetTile(layer, cx, cy)) };
    }
    SetTile(layer, cx, cy, tile);
}

void World_TileChangesStart()
{
    gTileChangesCur ^= 1;
    gTileChangeCount[gTileChangesCur] = 0;
}

void World_TileChangesEnd()
{
    const int old = gTileChangesCur ^ 1;
    for (int i = 0; i < gTileChangeCount[old]; i++)
    {
        const TileChange& c = gTileChanges[old][i];
        SetTile(c.mLayer, c.mX, c.mY, c.mOld);
    }
    gTileChangeCount[old] = 0;
}

void World_SetMapOffsetX(int px)
{
    gMapOffCells = px / 16;
    BgFull(gBgTX, gBgTY);
}

void World_GetCell(int cx, int cy, uint8_t& allow, uint8_t& event)
{
    const uint8_t* c = ClassAtCell(cx, cy);
    allow = c[0];
    event = c[1];
}

int World_GetTile(int layer, int cx, int cy)
{
    if (layer < 0 || layer >= kLayerCount || cx < 0 || cy < 0 || cx >= gLayerW[layer] || cy >= gLayerH[layer])
    {
        return 0;
    }
    return gLayerTiles[layer][cy * gLayerW[layer] + cx];
}

void World_SetSwapper(const MapSwapperList& swapper)
{
    gSwapper = &swapper;
}

uint8_t World_CheckSwapper(int cx, int cy)
{
    // SwapperControl.CheckCoord(tile * 16): the first region holding the point (Rectangle.Contains)
    const int x = cx * 16;
    const int y = cy * 16;
    for (uint32_t i = 0; gSwapper && i < gSwapper->mCount; i++)
    {
        const MapSwapRegion& r = gSwapper->mRegions[i];
        if (x >= r.mX && x < r.mX + r.mW && y >= r.mY && y < r.mY + r.mH)
        {
            return r.mState;
        }
    }
    return SWAP_DEFAULT;
}
