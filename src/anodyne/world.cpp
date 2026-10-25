#include "world.h"
#include "dialog.h"
#include "entities.h"
#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_manager.h"
#include "ecs.h"
#include "game_state.h"
#include "screen_fx.hpp"
#include "tonc.h"

namespace
{
// ---- Constants from AnodyneSharp (GameConstants.cs / PlayState.cs) ---------------------------------------------
constexpr int kTileSize = 16;
constexpr int kScreenPx = 160;               // SCREEN_WIDTH/HEIGHT_IN_PIXELS
constexpr int kScreenCells = kScreenPx / kTileSize; // 10x10 tiles per screen
constexpr int kViewX = (240 - kScreenPx) / 2; // playfield is centred on the 240x160 GBA screen
constexpr fx kScrollSpeed = Fx(250);         // PlayState.Scroll_Speed, px/s
constexpr int kFrac = kFxShift;

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

const char* gMapName = "";
const MapAudioData* gAudio = nullptr;
const char* gMusicPlaying = nullptr;

fx gCamX = 0; // top-left of the current screen being shown, Fx
fx gCamY = 0;
int gGridX = 0; // current 10x10-tile screen
int gGridY = 0;
bool gTransition = false;

int gBgTX = 0; // tile coordinate of the BG ring's top-left (each layer holds a 32x32 window of the map)
int gBgTY = 0;

// Which hardware BG / priority each role (BG, BG2, FG, scroll) uses on the current map.
int gHw[kRoleCount] = { 2, 1, 0, 3 };
int gPrio[kRoleCount] = { 3, 2, 0, 3 };

const MapScrollData* gScroll = nullptr; // null: no scrolling background on this map
int gScrollPosX = 0;                    // ScrollingTex.Position, Fx (0 down to -160, wrapping)
int gScrollPosY = 0;
int gScrollTX = 0;                      // ring origin (tile coordinates) of the scroll layer
int gScrollTY = 0;

// ---- Background ---------------------------------------------------------------------------------------------
// Screen entry for 8x8 tile (mx, my) of a layer: look up its 16x16 tile and pick the matching quarter. Outside the
// layer (or index 0) is empty, like TileMap.GetTile.
uint16_t LayerEntry(int layer, int mx, int my)
{
    const int cx = mx >> 1;
    const int cy = my >> 1;
    if (cx < 0 || cy < 0 || cx >= gLayerW[layer] || cy >= gLayerH[layer])
    {
        return 0;
    }
    const uint32_t tile = gLayerTiles[layer][cy * gLayerW[layer] + cx];
    if (tile == 0 || tile >= gTile16Count)
    {
        return 0;
    }
    return gTile16Entries[tile * 4 + (mx & 1) + 2 * (my & 1)];
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
    for (int j = 0; j < 32; j++)
    {
        for (int i = 0; i < 32; i++)
        {
            WriteBgTile(tx + i, ty + j);
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

// ---- HealthBar (UI/Health) ---------------------------------------------------------------------------------------
// One piece per point of max health, rows of 8 right-aligned at (155, 2) of the playfield (over it: the GBA has no
// room for the C#'s HUD strip).
constexpr int kMaxHealthPieces = 16;
Entity* gHealthPieces[kMaxHealthPieces] = {};

const EntityClass kHealthPieceClass = {
    "HealthBarPiece", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void CreateHealthBar()
{
    for (int i = 0; i < kMaxHealthPieces; i++)
    {
        Entity* e = Ecs_Create(kHealthPieceClass, 0, 0, PART_VISUAL);
        e->mPlayerOwned = true;
        Ecs_SetSheet(*e, "health_piece");
        Ecs_SetSolid(*e, false);
        e->mV->mLayer = LAYER_UI;
        e->mExists = false;
        gHealthPieces[i] = e;
    }
}

void UpdateHealthBar()
{
    for (int i = 0; i < kMaxHealthPieces; i++)
    {
        Entity& e = *gHealthPieces[i];
        e.mExists = i < gGame.mMaxHealth;
        if (!e.mExists)
        {
            continue;
        }
        const int x = 155 - 11 - 8 * (7 - i % 8) - 7 * (i / 8);
        const int y = 2 + i / 8 * 7;
        e.mT->mX = gCamX + Fx(x);
        e.mT->mY = gCamY + Fx(y);
        const char* anim = i < gGame.mCurHealth ? "full" : "empty";
        if (i == 0 && gGame.mCurHealth == 1)
        {
            anim = "flash";
        }
        Ecs_Play(e, anim);
    }
}

// ---- ScreenShake / FlashEffect ----------------------------------------------------------------------------------
struct Shake final
{
    fx mTime;      // frames left
    fx mIntensity; // fraction of the screen
    fx mUpdate;
    int mX;
    int mY;
};
Shake gShake = {};

struct Flash final
{
    fx mAlpha;
    fx mStep; // per frame
    bool mWhite;
};
Flash gFlash = {};

void UpdateScreenEffects()
{
    if (gShake.mTime > 0)
    {
        gShake.mTime -= kFxOne;
        gShake.mUpdate += kFxOne;
        if (gShake.mUpdate >= FxSec(1 / 30.0f))
        {
            gShake.mUpdate = 0;
            const fx px = FxMul(gShake.mIntensity, Fx(kScreenPx));
            gShake.mX = FxTrunc(FxMul(Rng_Unit() * 2 - kFxOne, px));
            gShake.mY = FxTrunc(FxMul(Rng_Unit() * 2 - kFxOne, px));
        }
    }
    if (gShake.mTime <= 0)
    {
        gShake.mX = gShake.mY = 0;
    }
    if (gFlash.mAlpha > 0)
    {
        gFlash.mAlpha -= gFlash.mStep;
        gFlash.mAlpha = gFlash.mAlpha < 0 ? 0 : gFlash.mAlpha;
    }
}

// Screen x of world x 0 is kViewX - camera, so the BG origin is the camera minus the playfield's left margin.
void UpdateBackground(bool force)
{
    const int vx = (gCamX >> kFrac) - kViewX;
    const int vy = gCamY >> kFrac;
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
        gScrollPosX += gScroll->mVelocityX * 65536 / kFps;
        gScrollPosY += gScroll->mVelocityY * 65536 / kFps;
        if (gScrollPosX < -Fx(160)) // Position.X < -Texture.Width / 2
        {
            gScrollPosX = 0;
        }
        if (gScrollPosY > 0)
        {
            gScrollPosY = -Fx(160);
        }
        else if (gScrollPosY < -Fx(160))
        {
            gScrollPosY = 0;
        }
    }

    // (int) cast truncates towards zero
    const int posX = gScrollPosX < 0 ? -((-gScrollPosX) >> kFrac) : (gScrollPosX >> kFrac);
    const int posY = gScrollPosY < 0 ? -((-gScrollPosY) >> kFrac) : (gScrollPosY >> kFrac);
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
    const int32_t id = AudioManager_FindSfx(name);
    if (id >= 0)
    {
        AudioManager_PlaySFX(static_cast<uint32_t>(id));
    }
}

// Map.ReloadSettings: the music for where the player is - the first Settings.json area containing the player's
// position, else the map's default. Only restarts the music when the track actually changes.
void UpdateMusic()
{
    if (!gAudio)
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
        REG_WININ = WIN_BLD | (WIN_BLD << 8);
        REG_WINOUT = static_cast<uint16_t>((gWinLayers | WIN_BLD) | (gWinLayers << 8));
    }
    else
    {
        REG_DISPCNT = REG_DISPCNT & ~(DCNT_WIN1 | DCNT_WINOBJ);
        REG_WIN0H = (kViewX << 8) | (kViewX + kScreenPx);
        REG_WIN0V = (0 << 8) | kScreenPx;
        REG_WININ = gWinLayers | WIN_BLD; // anodyne.cpp hides map loads behind a full darken
        REG_WINOUT = WIN_BLD;             // the side margins show the backdrop, which the darken has to cover too
    }
}

int DarkLevel()
{
    const int y = FxFloor(gDark.mAlpha * 16 + kFxOne / 2);
    return y < 0 ? 0 : (y > 16 ? 16 : y);
}

// Map.ReloadSettings: DarknessAlpha and PlayerLight for where the player is, forced on a map load and faded to on
// a screen change.
void UpdateDarkness(bool transition)
{
    fx target = 0;
    bool playerLight = false;
    if (gDarkData)
    {
        target = gDarkData->mAlpha * (kFxOne / 256);
        playerLight = gDarkData->mPlayerLight;
        const int px = FxTrunc(Player_Entity()->mT->mX);
        const int py = FxTrunc(Player_Entity()->mT->mY);
        bool alphaSet = false;
        bool lightSet = false;
        for (uint32_t i = 0; i < gDarkData->mAreaCount; i++)
        {
            const MapDarkArea& a = gDarkData->mAreas[i];
            if (px >= a.mX && px < a.mX + a.mWidth && py >= a.mY && py < a.mY + a.mHeight)
            {
                if (!alphaSet && a.mAlpha >= 0)
                {
                    target = a.mAlpha * (kFxOne / 256);
                    alphaSet = true;
                }
                if (!lightSet && a.mPlayerLight >= 0)
                {
                    playerLight = a.mPlayerLight != 0;
                    lightSet = true;
                }
            }
        }
    }
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
        gPlayerLight->mExists = playerLight;
    }
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
    d.mFlickerTimer -= kFxOne;
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
    // TileMap.GetTile: outside the map is tile 0 (nothing).
    if (cx < 0 || cy < 0 || cx >= gCellsW || cy >= gCellsH)
    {
        return gClasses; // class 0
    }
    return gClasses + gCollisionGrid[cy * gCellsW + cx] * 3;
}

// Map.GetCollisionData: the sides the tile under a world point blocks (point in Fx).
uint8_t AllowAt(int fxX, int fxY)
{
    const int px = fxX >> kFrac;
    const int py = fxY >> kFrac;
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
bool gDoorLatch = false; // Door.player_on_door: no re-trigger until the player has left every door

struct PendingWarp final
{
    bool mPending = false;
    uint8_t mMap = 0;
    int mX = 0;
    int mY = 0;
    uint8_t mFacing = 0;
    bool mBlack = false; // the screen is already black (death): no fade out
} gWarp;

// Door.Collided: stepping onto a door (or pressing B on an interactive one) warps the player.
void CheckDoors()
{
    if (!gDoors || gWarp.mPending)
    {
        return;
    }
    const Rect hit = Ecs_Hitbox(*Player_Entity());
    bool touching = false;
    for (uint32_t i = 0; i < gDoors->mCount; i++)
    {
        const MapDoor& d = gDoors->mDoors[i];
        const Rect door = { d.mX, d.mY, d.mW, d.mH };
        if (!Ecs_Intersects(hit, door))
        {
            continue;
        }
        touching = true;
        if (gDoorLatch || (d.mInteract && !key_hit(KEY_A)))
        {
            continue;
        }
        PlaySfx(d.mPortal || d.mInteract ? "teleport_up" : "enter_door");
        gWarp.mPending = true;
        gWarp.mMap = d.mDestMap;
        gWarp.mX = d.mDestX;
        gWarp.mY = d.mDestY;
        gWarp.mFacing = d.mFacing;
        gDoorLatch = true;
        Player_SetDontMove(true); // PlayState S_MAP_EXIT
        Player_SetActionsDisabled(true);
        Player_SetInvincible(true);
        return;
    }
    if (!touching)
    {
        gDoorLatch = false;
    }
}

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
    UpdateMusic(); // ReloadSettings(player position) as the screen changes
    UpdateDarkness(true);
    Player_SetGridEntrance();
    Player_SetDontMove(true);
    t.mVelX = t.mVelY = 0;
    Entities_LoadGrid(gGridX, gGridY, false);
}

// PlayState.StateTransition: the camera scrolls to the new screen.
void StateTransition()
{
    Player_SetInvincible(true);
    const bool doneX = MoveTo(gCamX, Fx(gGridX * kScreenPx), kScrollSpeed);
    const bool doneY = MoveTo(gCamY, Fx(gGridY * kScreenPx), kScrollSpeed);
    if (doneX && doneY)
    {
        Player_SetInvincible(false);
        Player_SetDontMove(false);
        gTransition = false;
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
    Entity* mDummy = nullptr;
    fx mFade = 0; // DeathFadeIn opacity, then the black overlay
} gDeath;
fx gDeathFallbackX = 0; // where the map was entered (dying before any checkpoint)
fx gDeathFallbackY = 0;

const EntityClass kDieDummyClass = {
    "PlayerDieDummy", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void SetDeathBlend(bool white, fx level)
{
    int y = FxFloor(level * 16 + kFxOne / 2);
    y = y < 0 ? 0 : (y > 16 ? 16 : y);
    REG_BLDCNT = BLD_BG0 | BLD_BG1 | BLD_BG2 | BLD_BG3 | BLD_BACKDROP | (white ? BLD_WHITE : BLD_BLACK) |
                 (white ? 0 : BLD_OBJ);
    REG_BLDY = static_cast<uint16_t>(y);
}

void DeathStart()
{
    gDeath = Death();
    gDeath.mActive = true;
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
        Ecs_SetSheet(d, "young_player", 16, 16);
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
            if (d.mDummy)
            {
                Ecs_Free(*d.mDummy);
                d.mDummy = nullptr;
            }
            Player_Entity()->mExists = true;
            int map = gGame.mCheckpointMap;
            int x = gGame.mCheckpointX;
            int y = gGame.mCheckpointY;
            if (map < 0)
            {
                // No checkpoint yet: where the map was entered.
                map = gGame.mCurrentMap;
                x = FxTrunc(gDeathFallbackX);
                y = FxTrunc(gDeathFallbackY);
            }
            World_RequestWarp(map, x, y, 255);
            gWarp.mBlack = true;
        }
        SetDeathBlend(false, d.mFade);
        return;
    }
    MoveTo(d.mFade, kFxOne, FxF(0.9f));
    SetDeathBlend(true, d.mFade);
    if (d.mGotControl && key_hit(KEY_A))
    {
        // "Continue?" Yes (the menu itself isn't drawn yet).
        PlaySfx("menu_select");
        GameState_SetHealth(gGame.mMaxHealth);
        d.mFadingOut = true;
        d.mFade = 0;
    }
}

void DrawWorld()
{
    UpdateBackground(false);
    UpdateHealthBar();
    Entities_Draw(gCamX >> kFrac, gCamY >> kFrac, kViewX, gShake.mX, gShake.mY);
    gShown = true;
}
} // namespace

void Game_PlaySong(const char* name)
{
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

void Screen_Shake(fx intensity, fx seconds)
{
    gShake = Shake();
    gShake.mTime = FxMul(seconds, Fx(kFps));
    gShake.mIntensity = intensity;
}

bool Screen_ShakeActive()
{
    return gShake.mTime > 0;
}

void Screen_Flash(fx seconds, bool white)
{
    gFlash.mAlpha = kFxOne;
    gFlash.mStep = FxDiv(kFxOne, FxMul(seconds, Fx(kFps)));
    gFlash.mWhite = white;
}

bool Screen_FlashActive()
{
    return gFlash.mAlpha > 0;
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

void World_ScreenDarken(int level)
{
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

// ---- World API ---------------------------------------------------------------------------------------------------
bool World_Init(ObjVramAllocator& vram)
{
    gVram = &vram;
    Entities_Init(vram);
    Player_Create();
    gPlayerLight = Ecs_Create(kPlayerLightClass, 0, 0, PART_VISUAL | PART_BRAIN);
    gPlayerLight->mPlayerOwned = true;
    Darkness_MakeLight(*gPlayerLight, "player-light");
    gPlayerLight->mV->mScale = Fx(2);
    gPlayerLight->mExists = false;
    CreateHealthBar();
    return true;
}

void World_Load(const MapEntry& map, const char* mapName, const MapAudioData& audio, const MapScrollData& scroll,
                const MapDoorList& doors, const MapDarkData& dark)
{
    gDarkData = &dark;
    Darkness_SetPlayerLight(*gPlayerLight);
    gMap = &map;
    gMapName = mapName;
    gDoors = &doors;
    gDoorLatch = false;
    gWarp.mPending = false;
    gAudio = &audio;

    // Palette (halfwords) and 8bpp tileset into BG VRAM. The tileset must stay clear of screenblocks 28-31
    // (0x0600E000 and up), which the layers use for their maps.
    memcpy16(pal_bg_mem, map.mPalData.mPalData, map.mPalData.mPalSize);
    uint32_t tilesetBytes = map.mTileSetData.mTileSetSize;
    if (tilesetBytes > kMaxTilesetBytes)
    {
        tilesetBytes = kMaxTilesetBytes;
    }
    memcpy16(tile_mem, map.mTileSetData.mTileSetData, tilesetBytes / 2);

    // Layers and collision cells, unpacked into EWRAM.
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

    gCellsW = map.mCollision.mCellsW;
    gCellsH = map.mCollision.mCellsH;
    gClasses = map.mCollision.mClasses;
    if (map.mCollision.mUnpackedSize <= kMaxCollisionCells)
    {
        LZ77UnCompWram(map.mCollision.mPacked, gCollisionGrid);
    }

    gTransition = false;
    Player_Reset(mapName);

    gScroll = scroll.mPattern ? &scroll : nullptr;
    gScrollPosX = 0;
    gScrollPosY = 0;
    ApplyDisplayRegs((REG_DISPCNT & DCNT_WIN0) != 0);

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

// PlayState.Update.
void World_Update()
{
    const bool child = gDialogOpen || gCutscene.mFn || gDeath.mActive;
    bool updateEntities = !gDeath.mActive;
    if (child)
    {
        // A child state (dialogue, cutscene, death): the player stands still; entities keep updating (except death).
        Player_SetInvincible(true);
        Player_SetDontMove(true);
        Player_SetActionsDisabled(true);
        if (gDialogOpen)
        {
            if (!Dialog_Update())
            {
                gDialogOpen = false;
            }
        }
        else if (gCutscene.mFn)
        {
            Entity* owner = gCutscene.mOwner;
            if (owner->mFreed || owner->mClass != gCutscene.mClass || gCutscene.mFn(*owner))
            {
                gCutscene.mFn = nullptr;
            }
        }
        if (gDeath.mActive)
        {
            DeathUpdate();
        }
        if (!gDialogOpen && !gCutscene.mFn && !gDeath.mActive)
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
        else if (!gWarp.mPending)
        {
            CheckForTransition();
        }
        // No player-map collision during a screen transition.
        Ecs_DoCollisions(wasTransition || gTransition, gGridX * kScreenPx, gGridY * kScreenPx);
        if (!gTransition)
        {
            CheckDoors();
        }
    }

    // PlayState.EntityUpdate
    if (updateEntities)
    {
        Entity& player = *Player_Entity();
        if (key_hit(KEY_A) && !child && Player_State() == PS_GROUND && !Player_SkipBroom() &&
            Ecs_CheckInteraction(player))
        {
            Player_SetSkipBroom();
        }
        Ecs_UpdateAll(&player);
        Player_Update();
        Ecs_FreeDead();
    }

    // GlobalState.SetDialogueMode: something opened the dialogue box this frame.
    if (!gDialogOpen && Dialog_IsOpen())
    {
        gDialogOpen = true;
        Player_BeIdle();
    }
    gGame.mEnemiesKilled = Ecs_KilledEnemies();

    // PlayState.UpdateHealth
    if (!gDialogOpen && !gCutscene.mFn && !gDeath.mActive && !gWarp.mPending && gGame.mCurHealth == 0)
    {
        DeathStart();
    }

    // The darkness fades on, and its BLD stands until something else (death, a warp) takes over.
    // A flash whitens everything over it.
    UpdateScreenEffects();
    if (!gDeath.mActive && !(gWarp.mPending && gWarp.mBlack))
    {
        MoveTo(gDark.mAlpha, gDark.mTarget, gDark.mVel);
        if (gFlash.mAlpha > 0)
        {
            if (gLightWindows)
            {
                gLightWindows = false;
                ApplyWindows();
            }
            int y = FxFloor(gFlash.mAlpha * 16 + kFxOne / 2);
            y = y > 16 ? 16 : y;
            ScreenDarken(0);
            REG_BLDCNT = BLD_BG0 | BLD_BG1 | BLD_BG2 | BLD_BG3 | BLD_OBJ | BLD_BACKDROP |
                         (gFlash.mWhite ? BLD_WHITE : BLD_BLACK);
            REG_BLDY = static_cast<uint16_t>(y);
            gDarkWritten = true;
        }
        else
        {
            if (gDark.mAlpha > 0 || gDarkWritten)
            {
                World_ScreenDarken(0);
            }
            gDarkWritten = gDark.mAlpha > 0;
        }
    }

    UpdateScroll(true, false);
    DrawWorld();
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

bool World_TakeWarp(uint8_t& destMap, int& destX, int& destY, uint8_t& facing, bool& alreadyBlack)
{
    if (!gWarp.mPending)
    {
        return false;
    }
    gWarp.mPending = false;
    destMap = gWarp.mMap;
    destX = gWarp.mX;
    destY = gWarp.mY;
    facing = gWarp.mFacing;
    alreadyBlack = gWarp.mBlack;
    gWarp.mBlack = false;
    return true;
}

bool World_InTransition()
{
    return gTransition;
}

void World_RequestWarp(int map, int x, int y, uint8_t facing)
{
    gWarp.mPending = true;
    gWarp.mMap = static_cast<uint8_t>(map);
    gWarp.mX = x;
    gWarp.mY = y;
    gWarp.mFacing = facing;
    Player_SetDontMove(true);
    Player_SetActionsDisabled(true);
    Player_SetInvincible(true);
}

void World_PlaceAt(int x, int y, uint8_t facing)
{
    // World facing order (0 down, 1 right, 2 up, 3 left) -> Facing.
    static const int kFacing[4] = { FACE_DOWN, FACE_RIGHT, FACE_UP, FACE_LEFT };
    gDoorLatch = true; // Door's constructor: starting on a door doesn't trigger it
    gTransition = false;
    Player_Reset(gMapName);
    Player_PlaceAt(Fx(x), Fx(y), facing <= 3 ? kFacing[facing] : -1);
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
}

void World_GetCellsSize(int& w, int& h)
{
    w = gCellsW;
    h = gCellsH;
}

void World_GetCell(int cx, int cy, uint8_t& allow, uint8_t& event)
{
    const uint8_t* c = ClassAtCell(cx, cy);
    allow = c[0];
    event = c[1];
}
