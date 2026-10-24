#include "world.h"
#include "dialog.h"
#include "entities.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_manager.h"
#include "sprite.h"
#include "tonc.h"

namespace
{
// ---- Constants from AnodyneSharp (GameConstants.cs / Player.cs / PlayState.cs) ----------------------------------
constexpr int kTileSize = 16;
constexpr int kScreenPx = 160;               // SCREEN_WIDTH/HEIGHT_IN_PIXELS
constexpr int kScreenCells = kScreenPx / kTileSize; // 10x10 tiles per screen
constexpr int kViewX = (240 - kScreenPx) / 2; // playfield is centred on the 240x160 GBA screen
constexpr int kHitW = 10;                    // Player HITBOX_WIDTH/HEIGHT
constexpr int kHitH = 12;
constexpr int kSpriteOffX = 3;               // Player.offset (sprite top-left = Position - offset)
constexpr int kSpriteOffY = 4;
constexpr int kWalkSpeed = 70;               // px/s, diagonal x0.7
constexpr int kConveyorSpeed = 35;           // Player.Conveyor CONVEYOR_VEL
constexpr int kCornerAssistSpeed = 30;       // Set_init_vel sideways nudge around tile corners
constexpr int kBumpSpeed = 50;               // Player.BUMP_VELOCITY
constexpr int kScrollSpeed = 250;            // PlayState.Scroll_Speed, px/s
constexpr int kOverlapBias = 4;              // GameObject.OVERLAP_BIAS
constexpr int kFps = 60;                     // GameTimes.FIXED_DELTA_TIME

// Timers, in frames at 60 FPS.
constexpr int kInvincibleFrames = 48;        // INVINCIBLE_MAX 0.8s
constexpr int kBumpFrames = 12;              // BUMP_TIMER_MAX 0.2s
constexpr int kFallFrames = 8;               // FALL_TIMER_DEFAULT 0.129s
constexpr int kFlickerFrames = 3;            // Entity.FlickerLength 0.05s
constexpr int kRespawnFlickerFrames = 60;    // ResetAfterFalling: Flicker(1)
constexpr int kMaxHealth = 3;                // no HUD/death yet: health wraps back to full

// All world positions are 16.16 fixed point pixels.
constexpr int kFrac = 16;
constexpr int Fx(int px)
{
    return px << kFrac;
}
// px/s -> Fx px per frame
constexpr int FxPerFrame(int pxPerSecond)
{
    return (pxPerSecond * 65536 + kFps / 2) / kFps;
}

// Touching flags / CollisionEventType, same values as the C# enums.
enum : uint8_t
{
    TOUCH_NONE = 0,
    TOUCH_LEFT = 1,
    TOUCH_RIGHT = 2,
    TOUCH_UP = 4,
    TOUCH_DOWN = 8,
    TOUCH_ANY = 15,
};
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

// OAM slots / OBJ palette banks used by the world (the sprite gallery test uses the rest). A sprite takes one OAM
// slot per hardware OBJ in its frame - the 24x24 overlays take 4 each - so these are spaced for that. Among sprites
// of the same priority a lower OAM slot is in front, so the grass overlay goes first, in front of the player.
constexpr int kOamGrass = 0;      // 0-3
constexpr int kOamPlayer = 4;     // 4
constexpr int kOamWater = 5;      // 5-8
constexpr int kOamReflection = 9; // 9

// Uninitialised EWRAM (".sbss" in gba_cart.ld): the current map's unpacked layers and collision cells.
alignas(4) uint8_t gLayerBuffer[kLayerCount][kMaxLayerBytes] __attribute__((section(".sbss")));
alignas(4) uint8_t gCollisionGrid[kMaxCollisionCells] __attribute__((section(".sbss")));

enum class Facing
{
    Down,
    Right,
    Up,
    Left
};

enum class PlayerState
{
    Ground,
    Ladder,
};

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

Sprite gPlayer;
Sprite gWaterOverlay;   // overlay_<MAP>_water / overlay_water: ripples around the feet in puddles and sludge
Sprite gGrassOverlay;   // overlay_<MAP>_grass
Sprite gReflection;     // young_player_reflection
bool gPlayerOk = false;
bool gHaveWater = false;
bool gHaveGrass = false;
bool gHaveReflection = false;

// ---- Player state (Player.cs) -------------------------------------------------------------------------------
Facing gFacing = Facing::Down;
PlayerState gState = PlayerState::Ground;

int gPosX = 0; // hitbox top-left in the world, Fx
int gPosY = 0;
int gLastX = 0;
int gLastY = 0;
int gVelX = 0; // Fx px per frame
int gVelY = 0;
int gAddVelX = 0; // conveyor push / corner assist for the next movement
int gAddVelY = 0;
bool gSlow = false;       // slowMul = 0.5
uint8_t gTouching = 0;    // sides the map touched last frame (GameObject.touching)
bool gOnConveyor = false;
bool gSinking = false;
bool gOnHole = false;
bool gIsSlipping = false;
bool gHasFallen = false;
int gFallTimer = 0;
int gFallPointX = 0;
int gFallPointY = 0;
int gYPush = 0;           // how far the player has sunk (Fx px)
int gBumpTimer = 0;
int gInvincible = 0;
int gFlicker = 0;
int gHealth = kMaxHealth;
int gGridEntranceX = 0;   // where the player respawns after falling / drowning
int gGridEntranceY = 0;
bool gFootWater = false;  // set by the map this frame
bool gFootGrass = false;
bool gReflecting = false;
int gTick = 0;
int gStepTimer = 0;        // Player.step_noise_timer, in frames

const MapAudioData* gAudio = nullptr;
const char* gMusicPlaying = nullptr;

int gCamX = 0; // top-left of the current screen being shown, Fx
int gCamY = 0;
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

int Abs(int v)
{
    return v < 0 ? -v : v;
}

// The player as the entities see it (entities.h).
PlayerBridge Bridge()
{
    PlayerBridge b;
    b.mX = gPosX;
    b.mY = gPosY;
    b.mLastX = gLastX;
    b.mLastY = gLastY;
    b.mTouching = gTouching;
    b.mFacing = static_cast<uint8_t>(gFacing);
    return b;
}

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
        SetBgOffset(gHw[layer], vx & 255, vy & 255);
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
    REG_WININ = winin | WIN_BLD; // anodyne.cpp hides map loads behind a full darken
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
    const int px = gPosX >> kFrac;
    const int py = gPosY >> kFrac;
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

// GameObject.SeparateX/SeparateY against an immovable rectangle (the player allows collisions on every side).
// `allow` = the rectangle's sides that block (Touching flags); everything is Fx. Returns true if it moved the player.
bool SeparateX(int rx, int ry, int rw, int rh, uint8_t allow)
{
    const int delta = gPosX - gLastX;
    const int deltaAbs = Abs(delta);
    const int p1x = gPosX - (delta > 0 ? delta : 0);
    const int p1y = gLastY;
    const int s1w = Fx(kHitW) + deltaAbs;
    const int s1h = Fx(kHitH);

    int overlap = 0;
    if (p1x + s1w > rx && p1x < rx + rw && p1y + s1h > ry && p1y < ry + rh)
    {
        const int maxOverlap = deltaAbs + Fx(kOverlapBias);
        const int overlapRight = gPosX + Fx(kHitW) - rx;
        const int overlapLeft = gPosX - rw - rx;
        if (Abs(overlapRight) < Abs(overlapLeft))
        {
            overlap = overlapRight;
            if (overlap > maxOverlap || (allow & TOUCH_LEFT) == 0)
            {
                overlap = 0;
            }
            else
            {
                gTouching |= TOUCH_RIGHT;
            }
        }
        else
        {
            overlap = overlapLeft;
            if (-overlap > maxOverlap || (allow & TOUCH_RIGHT) == 0)
            {
                overlap = 0;
            }
            else
            {
                gTouching |= TOUCH_LEFT;
            }
        }
    }

    if (overlap != 0)
    {
        gPosX -= overlap;
        gLastX = gPosX;
        return true;
    }
    return false;
}

bool SeparateY(int rx, int ry, int rw, int rh, uint8_t allow)
{
    const int delta = gPosY - gLastY;
    const int deltaAbs = Abs(delta);
    const int p1x = gLastX;
    const int p1y = gPosY - (delta > 0 ? delta : 0);
    const int s1w = Fx(kHitW);
    const int s1h = Fx(kHitH) + deltaAbs;

    int overlap = 0;
    if (p1x + s1w > rx && p1x < rx + rw && p1y + s1h > ry && p1y < ry + rh)
    {
        const int maxOverlap = deltaAbs + Fx(kOverlapBias);
        const int overlapDown = gPosY + Fx(kHitH) - ry;
        const int overlapUp = gPosY - rh - ry;
        if (Abs(overlapDown) < Abs(overlapUp))
        {
            overlap = overlapDown;
            if (overlap > maxOverlap || (allow & TOUCH_UP) == 0)
            {
                overlap = 0;
            }
            else
            {
                gTouching |= TOUCH_DOWN;
            }
        }
        else
        {
            overlap = overlapUp;
            if (-overlap > maxOverlap || (allow & TOUCH_DOWN) == 0)
            {
                overlap = 0;
            }
            else
            {
                gTouching |= TOUCH_UP;
            }
        }
    }

    if (overlap != 0)
    {
        gPosY -= overlap;
        gLastY = gPosY;
        return true;
    }
    return false;
}

bool Separate(int rx, int ry, int rw, int rh, uint8_t allow)
{
    const bool x = SeparateX(rx, ry, rw, rh, allow);
    const bool y = SeparateY(rx, ry, rw, rh, allow);
    return x || y;
}

// Rectangle.Intersects / Contains on the hitbox, with world pixel rects.
bool HitboxIntersects(int px, int py, int pw, int ph)
{
    const int hx = gPosX >> kFrac;
    const int hy = gPosY >> kFrac;
    return hx < px + pw && hx + kHitW > px && hy < py + ph && hy + kHitH > py;
}

bool TileContainsCenter(int tileX, int tileY)
{
    const int cx = (gPosX >> kFrac) + kHitW / 2;
    const int cy = (gPosY >> kFrac) + kHitH / 2;
    return cx >= tileX && cx < tileX + kTileSize && cy >= tileY && cy < tileY + kTileSize;
}

// Player.ReceiveDamage: health, a short invincibility with flicker, and a knockback bump. There's no HUD or death
// state yet, so health just wraps back to full instead of killing the player.
void ReceiveDamage(bool knockback)
{
    if (gInvincible > 0 || gHasFallen)
    {
        return;
    }

    PlaySfx("player_hit_1");
    gHealth--;
    if (gHealth <= 0)
    {
        gHealth = kMaxHealth;
    }
    gInvincible = kInvincibleFrames;
    gFlicker = kInvincibleFrames;
    if (knockback)
    {
        gBumpTimer = kBumpFrames;
    }
}

// The tile-event half of Map.CollideTile (the player's Conveyor/Fall/SlowTile/Ladder/Puddle/Reflection/Grass).
void TileEvent(uint8_t event, uint8_t dir, int tileXpx, int tileYpx)
{
    const int tx = Fx(tileXpx);
    const int ty = Fx(tileYpx);
    switch (event)
    {
    case EVENT_THIN:
        // A 3px strip along one edge of the tile that blocks on every side.
        switch (dir)
        {
        case TOUCH_DOWN:
            Separate(tx, ty + Fx(13), Fx(kTileSize), Fx(3), TOUCH_ANY);
            break;
        case TOUCH_UP:
            Separate(tx, ty, Fx(kTileSize), Fx(3), TOUCH_ANY);
            break;
        case TOUCH_LEFT:
            Separate(tx, ty, Fx(3), Fx(kTileSize), TOUCH_ANY);
            break;
        case TOUCH_RIGHT:
            Separate(tx + Fx(13), ty, Fx(3), Fx(kTileSize), TOUCH_ANY);
            break;
        default:
            break;
        }
        break;

    case EVENT_CONVEYOR:
        if (TileContainsCenter(tileXpx, tileYpx))
        {
            // Player.Conveyor: pushed along, half speed, and sinking (this is the deep water / sludge tile).
            gOnConveyor = true;
            gFootWater = true;
            switch (dir)
            {
            case TOUCH_DOWN:
                gAddVelY = FxPerFrame(kConveyorSpeed);
                break;
            case TOUCH_UP:
                gAddVelY = -FxPerFrame(kConveyorSpeed);
                break;
            case TOUCH_RIGHT:
                gAddVelX = FxPerFrame(kConveyorSpeed);
                break;
            case TOUCH_LEFT:
                gAddVelX = -FxPerFrame(kConveyorSpeed);
                break;
            default:
                break;
            }
            gSlow = true;
            gSinking = true;
        }
        break;

    case EVENT_HOLE:
        // Only the middle of the hole counts (tile + (5,5), 6x4).
        if (HitboxIntersects(tileXpx + 5, tileYpx + 5, 6, 4))
        {
            // Player.Fall
            if (!gIsSlipping && !gHasFallen)
            {
                gIsSlipping = true;
                gFallTimer = kFallFrames;
            }
            gFallPointX = tx;
            gFallPointY = ty;
            gOnHole = true;
        }
        break;

    case EVENT_SLOW:
        gSlow = true;
        break;

    case EVENT_SPIKE:
        if (HitboxIntersects(tileXpx + 6, tileYpx + 6, 5, 6))
        {
            ReceiveDamage(true);
        }
        break;

    case EVENT_LADDER:
        if (gState == PlayerState::Ground)
        {
            gState = PlayerState::Ladder;
        }
        break;

    case EVENT_PUDDLE:
        if (TileContainsCenter(tileXpx, tileYpx))
        {
            gOnConveyor = true;
            gFootWater = true;
        }
        break;

    case EVENT_REFLECTION:
        if (TileContainsCenter(tileXpx, tileYpx))
        {
            gOnConveyor = true;
            gFootWater = true;
            gReflecting = true;
        }
        break;

    case EVENT_GRASS:
        if (TileContainsCenter(tileXpx, tileYpx))
        {
            gFootGrass = true;
        }
        break;

    default:
        break;
    }
}

// Map.Collide: every cell the hitbox overlaps (the C# includes the cell at hitbox.Right/Bottom too).
void CollideWithMap()
{
    const int px = gPosX >> kFrac;
    const int py = gPosY >> kFrac;
    const int tlx = px >> 4;
    const int tly = py >> 4;
    const int brx = (px + kHitW) >> 4;
    const int bry = (py + kHitH) >> 4;

    for (int cy = tly; cy <= bry; cy++)
    {
        for (int cx = tlx; cx <= brx; cx++)
        {
            const uint8_t* c = ClassAtCell(cx, cy);
            const uint8_t allow = c[0];
            const uint8_t event = c[1];
            const uint8_t dir = c[2];

            // Map.CollideTile: events run when the tile doesn't block, or did separate the player.
            if (allow == 0 || Separate(Fx(cx * kTileSize), Fx(cy * kTileSize), Fx(kTileSize), Fx(kTileSize), allow))
            {
                if (event != EVENT_NONE)
                {
                    TileEvent(event, dir, cx * kTileSize, cy * kTileSize);
                }
            }
        }
    }
}

// ---- Doors ----------------------------------------------------------------------------------------------------
const MapDoorList* gDoors = nullptr;
bool gDoorLatch = false; // Door.player_on_door: no re-trigger until the player has left every door

struct PendingWarp final
{
    bool mPending = false;
    const MapDoor* mDoor = nullptr;
} gWarp;

// Door.Collided: stepping onto a door (or pressing B on an interactive one) warps the player.
void CheckDoors()
{
    if (!gDoors || gWarp.mPending)
    {
        return;
    }
    bool touching = false;
    for (uint32_t i = 0; i < gDoors->mCount; i++)
    {
        const MapDoor& d = gDoors->mDoors[i];
        if (!HitboxIntersects(d.mX, d.mY, d.mW, d.mH))
        {
            continue;
        }
        touching = true;
        if (gDoorLatch || (d.mInteract && !key_hit(KEY_B)))
        {
            continue;
        }
        PlaySfx(d.mPortal || d.mInteract ? "teleport_up" : "enter_door");
        gWarp.mPending = true;
        gWarp.mDoor = &d;
        gDoorLatch = true;
        return;
    }
    if (!touching)
    {
        gDoorLatch = false;
    }
}

// ---- Player / screens ---------------------------------------------------------------------------------------
void MoveCameraToGrid()
{
    gCamX = Fx(gGridX * kScreenPx);
    gCamY = Fx(gGridY * kScreenPx);
}

bool MoveTo(int& value, int target, int step)
{
    if (value > target)
    {
        value = value - step < target ? target : value - step;
    }
    else
    {
        value = value + step > target ? target : value + step;
    }
    return value == target;
}

bool GridExists(int gx, int gy)
{
    return gx >= 0 && gy >= 0 && (gx + 1) * kScreenCells <= gCellsW && (gy + 1) * kScreenCells <= gCellsH;
}

// PlayState.CheckForTransition. The C# lets the player walk off the map (a glitch effect); here a missing
// neighbouring screen just stops the player at the edge.
void CheckForTransition()
{
    const int bx = Fx(gGridX * kScreenPx);
    const int by = Fx(gGridY * kScreenPx);
    int gx = gGridX;
    int gy = gGridY;
    int newX = gPosX;
    int newY = gPosY;

    if (gPosX < bx)
    {
        gx--;
        newX = bx - Fx(kHitW);
    }
    else if (gPosY < by)
    {
        gy--;
        newY = by - Fx(kHitH);
    }
    else if (gPosY > by + Fx(kScreenPx) - Fx(kHitH))
    {
        gy++;
        newY = by + Fx(kScreenPx);
    }
    else if (gPosX > bx + Fx(kScreenPx) - Fx(kHitW))
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
        gPosX = gPosX < bx ? bx : (gPosX > bx + Fx(kScreenPx) - Fx(kHitW) ? bx + Fx(kScreenPx) - Fx(kHitW) : gPosX);
        gPosY = gPosY < by ? by : (gPosY > by + Fx(kScreenPx) - Fx(kHitH) ? by + Fx(kScreenPx) - Fx(kHitH) : gPosY);
        gLastX = gPosX;
        gLastY = gPosY;
        return;
    }

    gGridX = gx;
    gGridY = gy;
    gPosX = newX;
    gPosY = newY;
    gLastX = gPosX;
    gLastY = gPosY;
    gGridEntranceX = gPosX; // PlayState: _player.grid_entrance = _player.Position
    gGridEntranceY = gPosY;
    gVelX = gVelY = 0;
    gTransition = true;
    Entities_LoadGrid(gGridX, gGridY, Bridge(), false);
    UpdateMusic(); // ReloadSettings(player position) as the screen changes
}

bool KeyLeft()
{
    return key_is_down(KEY_LEFT);
}
bool KeyRight()
{
    return key_is_down(KEY_RIGHT);
}
bool KeyUp()
{
    return key_is_down(KEY_UP);
}
bool KeyDown()
{
    return key_is_down(KEY_DOWN);
}

// Player.Set_init_vel: the D-pad to a velocity (Left beats Right, Up beats Down, like the C#), plus the sideways
// nudge that slides the player around a tile corner they're pushing against.
void SetInitialVelocity()
{
    const int px = gPosX >> kFrac;
    const int py = gPosY >> kFrac;
    gVelX = 0;
    gVelY = 0;

    if (KeyUp())
    {
        gVelY = -1;
        if (gTouching & TOUCH_UP)
        {
            const uint8_t tl = AllowAt(gPosX, gPosY - Fx(8));
            const uint8_t tr = AllowAt(gPosX + Fx(kHitW), gPosY - Fx(8));
            if ((tl & TOUCH_DOWN) == 0 && (tr & TOUCH_DOWN) == 0)
            {
                // nothing
            }
            else if (((px + kHitW) & 15) < 6 && (tl & TOUCH_DOWN) == 0)
            {
                gAddVelX -= FxPerFrame(kCornerAssistSpeed);
            }
            else if ((px & 15) > 9 && (tr & TOUCH_DOWN) == 0)
            {
                gAddVelX += FxPerFrame(kCornerAssistSpeed);
            }
        }
    }
    else if (KeyDown())
    {
        gVelY = 1;
        if (gTouching & TOUCH_DOWN)
        {
            const uint8_t bl = AllowAt(gPosX, gPosY + Fx(kHitH + 8));
            const uint8_t br = AllowAt(gPosX + Fx(kHitW), gPosY + Fx(kHitH + 8));
            if ((bl & TOUCH_UP) == 0 && (br & TOUCH_UP) == 0)
            {
                // nothing
            }
            else if (((px + kHitW) & 15) < 6 && (bl & TOUCH_UP) == 0)
            {
                gAddVelX -= FxPerFrame(kCornerAssistSpeed);
            }
            else if ((px & 15) > 9 && (br & TOUCH_UP) == 0)
            {
                gAddVelX += FxPerFrame(kCornerAssistSpeed);
            }
        }
    }

    if (KeyLeft())
    {
        gVelX = -1;
        if (gTouching & TOUCH_LEFT)
        {
            const uint8_t tl = AllowAt(gPosX - Fx(8), gPosY);
            const uint8_t bl = AllowAt(gPosX - Fx(8), gPosY + Fx(kHitH));
            if ((tl & TOUCH_RIGHT) == 0 && (bl & TOUCH_RIGHT) == 0)
            {
                // nothing
            }
            else if (((py + kHitH) & 15) < 6 && (tl & TOUCH_RIGHT) == 0)
            {
                gAddVelY -= FxPerFrame(kCornerAssistSpeed);
            }
            else if ((py & 15) > 9 && (bl & TOUCH_RIGHT) == 0)
            {
                gAddVelY += FxPerFrame(kCornerAssistSpeed);
            }
        }
    }
    else if (KeyRight())
    {
        gVelX = 1;
        if (gTouching & TOUCH_RIGHT)
        {
            const uint8_t tr = AllowAt(gPosX + Fx(kHitW + 8), gPosY);
            const uint8_t br = AllowAt(gPosX + Fx(kHitW + 8), gPosY + Fx(kHitH));
            if ((tr & TOUCH_LEFT) == 0 && (br & TOUCH_LEFT) == 0)
            {
                // nothing
            }
            else if (((py + kHitH) & 15) < 6 && (tr & TOUCH_LEFT) == 0)
            {
                gAddVelY -= FxPerFrame(kCornerAssistSpeed);
            }
            else if ((py & 15) > 9 && (br & TOUCH_LEFT) == 0)
            {
                gAddVelY += FxPerFrame(kCornerAssistSpeed);
            }
        }
    }

    // velocity *= walkSpeed; diagonals x0.7
    const int step = FxPerFrame(kWalkSpeed);
    gVelX *= step;
    gVelY *= step;
    if (gVelX != 0 && gVelY != 0)
    {
        gVelX = (gVelX * 7) / 10;
        gVelY = (gVelY * 7) / 10;
    }
}

void FacingFromVelocity()
{
    if (gVelX == 0 && gVelY == 0)
    {
        return;
    }
    if (gVelY < 0)
    {
        gFacing = Facing::Up;
    }
    else if (gVelY > 0)
    {
        gFacing = Facing::Down;
    }
    else if (gVelX < 0)
    {
        gFacing = Facing::Left;
    }
    else
    {
        gFacing = Facing::Right;
    }
}

void FacingDirection(int& dx, int& dy)
{
    dx = 0;
    dy = 0;
    switch (gFacing)
    {
    case Facing::Up:
        dy = -1;
        break;
    case Facing::Down:
        dy = 1;
        break;
    case Facing::Left:
        dx = -1;
        break;
    case Facing::Right:
        dx = 1;
        break;
    }
}

// Player.GroundMovement + SlippingLogic/ResetAfterFalling/SinkingLogic. playFallAnim is set while the fall
// animation should be showing.
void GroundMovement(bool& playFallAnim)
{
    SetInitialVelocity();

    if (gBumpTimer > 0)
    {
        gBumpTimer--;
        if (gBumpTimer > 0)
        {
            int fx = 0;
            int fy = 0;
            FacingDirection(fx, fy);
            gVelX = -fx * FxPerFrame(kBumpSpeed);
            gVelY = -fy * FxPerFrame(kBumpSpeed);
        }
    }

    gVelX += gAddVelX;
    gVelY += gAddVelY;
    if (gSlow)
    {
        gVelX /= 2;
        gVelY /= 2;
    }

    if (gHasFallen)
    {
        gVelX = gVelY = 0; // dontMove
    }

    if (gIsSlipping)
    {
        // SlippingLogic: still over the hole? count down, then fall.
        if (!gOnHole)
        {
            gIsSlipping = false;
        }
        else
        {
            gFallTimer--;
            if (gFallTimer <= 0)
            {
                PlaySfx("fall_in_hole");
                gHasFallen = true;
                gIsSlipping = false;
                gPosX = gFallPointX + Fx(3);
                gPosY = gFallPointY + Fx(5);
                gLastX = gPosX;
                gLastY = gPosY;
                gVelX = gVelY = 0;
            }
        }
    }
    else if (gHasFallen)
    {
        // ResetAfterFalling: once the fall animation reaches its last frame, back to where the screen was entered.
        if (Sprite_CurrentFrame(gPlayer) == 31)
        {
            gPosX = gGridEntranceX;
            gPosY = gGridEntranceY;
            gLastX = gPosX;
            gLastY = gPosY;
            gHasFallen = false;
            gFlicker = kRespawnFlickerFrames;
        }
    }

    if (gSinking && !gHasFallen)
    {
        // SinkingLogic: sink 16/3 px/s; after 3 seconds drown and respawn (minus one health).
        gYPush += (16 * 65536 / 3) / kFps;
        if (gYPush > Fx(16))
        {
            gYPush = 0;
            gPosX = gGridEntranceX;
            gPosY = gGridEntranceY;
            gLastX = gPosX;
            gLastY = gPosY;
            ReceiveDamage(false);
        }
    }
    else
    {
        gYPush = 0;
    }

    // Player.Movement: footsteps while walking on puddles / in sludge (half as often when sinking).
    gStepTimer--;
    if (gStepTimer <= 0 && (gVelX != 0 || gVelY != 0) && gOnConveyor)
    {
        if (gSinking)
        {
            PlaySfx("water_step");
            gStepTimer = kFps / 2; // step_timer_max 0.5s
        }
        else
        {
            PlaySfx("puddle_step");
            gStepTimer = kFps / 4;
        }
    }

    playFallAnim = gHasFallen;
}

// Player.LadderLogic: climb with the D-pad (vertical at 0.7 speed), then fall back to Ground - touching the ladder
// tile again in the map collision puts the player back on it.
void LadderMovement(const char*& anim)
{
    gVelX = 0;
    gVelY = 0;
    anim = "idle_climb";
    if (KeyUp())
    {
        gVelY = -(FxPerFrame(kWalkSpeed) * 7) / 10;
        anim = "climb";
    }
    else if (KeyDown())
    {
        gVelY = (FxPerFrame(kWalkSpeed) * 7) / 10;
        anim = "climb";
    }

    if (KeyLeft())
    {
        gVelX = -FxPerFrame(kWalkSpeed);
        anim = "climb";
    }
    else if (KeyRight())
    {
        gVelX = FxPerFrame(kWalkSpeed);
        anim = "climb";
    }

    gStepTimer--;
    if (gStepTimer <= 0 && gVelY != 0)
    {
        gStepTimer = kFps / 5; // 0.2s
        PlaySfx((gTick & 1) ? "ladder_step_1" : "ladder_step_2");
    }
    gState = PlayerState::Ground;
}

// ---- Drawing ------------------------------------------------------------------------------------------------
int ScreenX(int worldFx)
{
    return (worldFx >> kFrac) - (gCamX >> kFrac) + kViewX;
}

int ScreenY(int worldFx)
{
    return (worldFx >> kFrac) - (gCamY >> kFrac);
}

void DrawPlayerAndOverlays(const char* anim, bool moving)
{
    gTick++;

    // Player sprite
    if (anim == nullptr)
    {
        static const char* const kWalk[] = { "walk_d", "walk_r", "walk_u", "walk_l" };
        static const char* const kIdle[] = { "idle_d", "idle_r", "idle_u", "idle_l" };
        const int f = static_cast<int>(gFacing);
        anim = moving ? kWalk[f] : kIdle[f];
    }
    Sprite_Play(gPlayer, anim);
    Sprite_Update(gPlayer);

    // Entity.DoFlicker: toggle visibility every 3 frames while flickering.
    bool playerVisible = true;
    if (gFlicker > 0)
    {
        gFlicker--;
        playerVisible = gFlicker == 0 ? true : ((gFlicker / kFlickerFrames) % 2 == 0);
    }
    if (gInvincible > 0)
    {
        gInvincible--;
    }

    const int sinkPx = gYPush >> kFrac; // no way to crop an OBJ, so a sinking player is just pushed down
    const int sx = ScreenX(gPosX) - kSpriteOffX;
    const int sy = ScreenY(gPosY) - kSpriteOffY + sinkPx;
    if (playerVisible)
    {
        Sprite_Draw(gPlayer, sx, sy);
    }
    else
    {
        Sprite_Hide(gPlayer);
    }

    // Foot overlay, Foot_Overlay.cs: Position = player.Position - (7, 3), one pixel less to the left when facing
    // right. Puddles flicker it (0.1s), grass keeps it on and animates it by whether the player is moving.
    const int fx = ScreenX(gPosX) - 7 - (gFacing == Facing::Right ? 1 : 0);
    const int fy = ScreenY(gPosY) - 3;
    if (gHaveWater && gFootWater && ((gTick / kFlickerFrames) % 2 == 0))
    {
        Sprite_SetFrame(gWaterOverlay, (gTick * 5 / kFps) & 1); // "water": frames 0,1 at 5fps
        Sprite_Update(gWaterOverlay);
        Sprite_Draw(gWaterOverlay, fx, fy);
    }
    else if (gHaveWater)
    {
        Sprite_Hide(gWaterOverlay);
    }

    if (gHaveGrass && gFootGrass)
    {
        const bool grassMoving = gVelX != 0 || gVelY != 0;
        // "grass_go": frames 4,5 at 8fps; "grass_stop": frame 5
        Sprite_SetFrame(gGrassOverlay, grassMoving ? 4 + ((gTick * 8 / kFps) & 1) : 5);
        Sprite_Update(gGrassOverlay);
        Sprite_Draw(gGrassOverlay, fx, fy + 1);
    }
    else if (gHaveGrass)
    {
        Sprite_Hide(gGrassOverlay);
    }

    // PlayerReflection: shown only while on a reflection tile, 15px below the player, same frame.
    if (gHaveReflection && gReflecting)
    {
        Sprite_SetFrame(gReflection, Sprite_CurrentFrame(gPlayer));
        Sprite_Update(gReflection);
        Sprite_Draw(gReflection, sx, ScreenY(gPosY) + 7 + kSpriteOffY);
    }
    else if (gHaveReflection)
    {
        Sprite_Hide(gReflection);
    }
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
    gPosX = Fx(bestCellX * kTileSize + 3);
    gPosY = Fx(bestCellY * kTileSize + 2);
    gLastX = gPosX;
    gLastY = gPosY;
    gGridEntranceX = gPosX;
    gGridEntranceY = gPosY;
}

// The overlay sheets for a map: overlay_<MAP>_water or the generic overlay_water, and overlay_<MAP>_grass if the
// map has one.
void CreateMapSprites(const char* mapName)
{
    Sprite_Destroy(gWaterOverlay, *gVram);
    Sprite_Destroy(gGrassOverlay, *gVram);
    gHaveWater = false;
    gHaveGrass = false;

    char name[48];
    snprintf(name, sizeof(name), "overlay_%s_water", mapName);
    if (!Sprite_FindSheet(name))
    {
        strcpy(name, "overlay_water");
    }
    gHaveWater = Sprite_Create(gWaterOverlay, *gVram, name, 1, kOamWater);
    if (gHaveWater)
    {
        gWaterOverlay.mPriority = 3; // between BG and BG2
    }

    snprintf(name, sizeof(name), "overlay_%s_grass", mapName);
    if (Sprite_FindSheet(name))
    {
        gHaveGrass = Sprite_Create(gGrassOverlay, *gVram, name, 3, kOamGrass);
        if (gHaveGrass)
        {
            gGrassOverlay.mPriority = 1; // in front of the player (lower OAM slot), behind FG
        }
    }
}
} // namespace

bool World_Init(ObjVramAllocator& vram)
{
    gVram = &vram;
    Entities_Init(vram);
    gPlayerOk = Sprite_Create(gPlayer, vram, "young_player", 0, kOamPlayer);
    if (gPlayerOk)
    {
        Sprite_Play(gPlayer, "idle_d");
    }

    gHaveReflection = Sprite_Create(gReflection, vram, "young_player_reflection", 2, kOamReflection);
    if (gHaveReflection)
    {
        gReflection.mPriority = 3;
    }
    return gPlayerOk;
}

void World_Load(const MapEntry& map, const char* mapName, const MapAudioData& audio, const MapScrollData& scroll,
                const MapDoorList& doors)
{
    gMap = &map;
    gDoors = &doors;
    gDoorLatch = false;
    gWarp.mPending = false;
    gAudio = &audio;

    // Palette (halfwords) and 8bpp tileset into BG VRAM. The tileset must stay clear of screenblocks 29-31
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

    // Fresh player state for the new map.
    gTransition = false;
    gState = PlayerState::Ground;
    gVelX = gVelY = gAddVelX = gAddVelY = 0;
    gSlow = false;
    gTouching = 0;
    gOnConveyor = gSinking = gOnHole = gIsSlipping = gHasFallen = false;
    gYPush = 0;
    gBumpTimer = 0;
    gInvincible = 0;
    gFlicker = 0;
    gFootWater = gFootGrass = gReflecting = false;
    gHealth = kMaxHealth;

    if (gVram)
    {
        CreateMapSprites(mapName);
    }
    gScroll = scroll.mPattern ? &scroll : nullptr;
    gScrollPosX = 0;
    gScrollPosY = 0;
    ApplyDisplayRegs((REG_DISPCNT & DCNT_WIN0) != 0);

    PickSpawn();
    MoveCameraToGrid();
    UpdateBackground(true);
    UpdateScroll(false, true);

    gStepTimer = 0;
    UpdateMusic();
    UpdateAmbience();
    Entities_LoadGrid(gGridX, gGridY, Bridge(), true);
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
    ApplyDisplayRegs(true);
    REG_WIN0H = (kViewX << 8) | (kViewX + kScreenPx);
    REG_WIN0V = (0 << 8) | kScreenPx;
    REG_WINOUT = WIN_BLD; // the side margins show the backdrop, which the darken has to cover too
}

void World_SetPlayerVisible(bool visible)
{
    if (!visible)
    {
        Entities_Clear();
        if (gPlayerOk)
        {
            Sprite_Hide(gPlayer);
        }
        if (gHaveWater)
        {
            Sprite_Hide(gWaterOverlay);
        }
        if (gHaveGrass)
        {
            Sprite_Hide(gGrassOverlay);
        }
        if (gHaveReflection)
        {
            Sprite_Hide(gReflection);
        }
    }
}

void World_Update()
{
    const char* anim = nullptr; // nullptr: walk/idle from the velocity
    bool moving = false;

    // While a dialogue box is open the world stands still (it still draws), and the frame that closes it takes
    // no input either, so the closing press can't also interact with something.
    const bool dialogueFrame = Dialog_IsOpen();
    if (dialogueFrame)
    {
        Dialog_Update();
    }

    if (dialogueFrame)
    {
    }
    else if (gTransition)
    {
        // PlayState.StateTransition: the player stands still and the camera scrolls to the new screen.
        const int step = kScrollSpeed * 65536 / kFps;
        const int tx = Fx(gGridX * kScreenPx);
        const int ty = Fx(gGridY * kScreenPx);
        const bool doneX = MoveTo(gCamX, tx, step);
        const bool doneY = MoveTo(gCamY, ty, step);
        if (doneX && doneY)
        {
            gTransition = false;
            Entities_EndTransition();
        }
    }
    else
    {
        // ---- PlayState.EntityUpdate: Accept interacts with the entity the player faces (before they move) ----
        if (key_hit(KEY_A) && gState == PlayerState::Ground && !gWarp.mPending)
        {
            Entities_Interact(Bridge());
        }

        // ---- Player.Update -> Movement() ----
        bool playFall = false;
        if (gState == PlayerState::Ladder)
        {
            LadderMovement(anim);
        }
        else
        {
            GroundMovement(playFall);
            if (playFall)
            {
                anim = "fall";
            }
        }
        moving = gVelX != 0 || gVelY != 0;
        if (gBumpTimer == 0 || anim != nullptr)
        {
            FacingFromVelocity();
        }

        // slowMul and additionalVel only last for one movement step; the map collision below sets them for the next.
        gSlow = false;
        gAddVelX = 0;
        gAddVelY = 0;

        // ---- PostUpdate: integrate, then clear the per-frame map flags ----
        gLastX = gPosX;
        gLastY = gPosY;
        gPosX += gVelX;
        gPosY += gVelY;
        gTouching = 0;
        gOnConveyor = false;
        gSinking = false;
        gOnHole = false;
        gFootWater = false;
        gFootGrass = false;
        gReflecting = false;

        // ---- Map.Collide + PlayState.CheckForTransition ----
        CollideWithMap();
        PlayerBridge bridge = Bridge();
        Entities_Collide(bridge);
        gPosX = bridge.mX;
        gPosY = bridge.mY;
        gLastX = bridge.mLastX;
        gLastY = bridge.mLastY;
        gTouching = bridge.mTouching;
        CheckDoors();
        CheckForTransition();
    }

    UpdateBackground(false);
    UpdateScroll(true, false);
    if (!gTransition)
    {
        Entities_Update(Bridge());
    }
    Entities_Draw(gCamX >> kFrac, gCamY >> kFrac, kViewX);
    if (gPlayerOk)
    {
        DrawPlayerAndOverlays(anim, moving);
    }
}

bool World_TakeWarp(uint8_t& destMap, int& destX, int& destY, uint8_t& facing)
{
    if (!gWarp.mPending)
    {
        return false;
    }
    gWarp.mPending = false;
    destMap = gWarp.mDoor->mDestMap;
    destX = gWarp.mDoor->mDestX;
    destY = gWarp.mDoor->mDestY;
    facing = gWarp.mDoor->mFacing;
    return true;
}

void World_PlaceAt(int x, int y, uint8_t facing)
{
    gDoorLatch = true; // Door's constructor: starting on a door doesn't trigger it
    gPosX = gLastX = Fx(x);
    gPosY = gLastY = Fx(y);
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
    gGridEntranceX = gPosX;
    gGridEntranceY = gPosY;
    gVelX = gVelY = 0;
    if (facing <= 3)
    {
        gFacing = static_cast<Facing>(facing);
    }
    MoveCameraToGrid();
    UpdateBackground(true);
    UpdateScroll(false, true);
    UpdateMusic();
    Entities_LoadGrid(gGridX, gGridY, Bridge(), true);
}

void World_GetPlayerInfo(WorldPlayerInfo& info)
{
    info.mX = gPosX >> kFrac;
    info.mY = gPosY >> kFrac;
    info.mFacing = static_cast<int>(gFacing);
    info.mGridX = gGridX;
    info.mGridY = gGridY;
    info.mTransition = gTransition;
    info.mLadder = gState == PlayerState::Ladder;
    info.mFallen = gHasFallen;
    info.mHealth = gHealth;
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
