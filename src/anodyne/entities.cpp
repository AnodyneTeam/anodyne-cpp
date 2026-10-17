#include "entities.h"

#include "sprite.h"

#include "audio_manager.h"

namespace
{
constexpr int kFirstOam = 10;
constexpr int kOamEnd = 120; // 120-127: the dialogue box (dialog.cpp)
constexpr int kFirstPalBank = 4;
constexpr int kPalBankCount = 11; // bank 15: the dialogue box
constexpr int kMaxSprites = 64;
constexpr int kScreenPx = 160;

ObjVramAllocator* gVram = nullptr;
MapEntityList gList = { 0, nullptr };
const char* const* gSheetNames = nullptr;
uint32_t gSheetCount = 0;

Sprite gSprites[kMaxSprites];
int16_t gSpriteX[kMaxSprites];
int16_t gSpriteY[kMaxSprites];
int gSpriteCount = 0;
int gShownGrid = -1; // gridY * 256 + gridX of the sprites that exist, -1 = none

// Behaviour state. gDone is per entity of the current map (index into gList): a console that was activated, a
// gate that was opened for good. It survives leaving the screen (like EntityPreset.Activated / Alive) and is
// reset by Entities_SetMap.
constexpr int kMaxEntitiesPerMap = 1024;
constexpr int kGateStepTicks = 15; // "open" anim: 4 fps
uint8_t gDone[kMaxEntitiesPerMap];
int gSpriteEnt[kMaxSprites];       // entity index of each sprite
uint8_t gGateStep[kMaxSprites];    // 0 = closed, 1..4 = opening, 5 = open
uint8_t gGateTimer[kMaxSprites];
int gPuzzlesSolved = 0;            // GlobalState.PUZZLES_SOLVED: activated consoles on this screen

void PlaySfx(const char* name)
{
    const int32_t id = AudioManager_FindSfx(name);
    if (id >= 0)
    {
        AudioManager_PlaySFX(static_cast<uint32_t>(id));
    }
}

bool IsSolid(int sprite, const MapEntity& e)
{
    return e.mKind == ENT_CONSOLE || (e.mKind == ENT_GATE && gGateStep[sprite] == 0);
}

// Gate.ConditionSatisfied: frames 0-4/10-14 count enemies killed (not tracked yet), 5-7/15-17 puzzles solved.
bool GateCondition(const MapEntity& e)
{
    const int check = (e.mArg % 10 >= 5) ? gPuzzlesSolved : 0;
    return check > (e.mArg % 5);
}

void DestroyAll()
{
    for (int i = 0; i < gSpriteCount; i++)
    {
        Sprite_Hide(gSprites[i]);
        Sprite_Destroy(gSprites[i], *gVram);
    }
    gSpriteCount = 0;
    gShownGrid = -1;
}

void Rebuild(int gridX, int gridY)
{
    DestroyAll();
    gShownGrid = gridY * 256 + gridX;

    // One palette bank per distinct sheet on this screen.
    const SpriteSheetEntry* bankSheets[kPalBankCount] = {};
    int oamNext = kFirstOam;

    for (uint32_t i = 0; i < gList.mCount && gSpriteCount < kMaxSprites; i++)
    {
        const MapEntity& e = gList.mEntities[i];
        if (e.mX / kScreenPx != gridX || e.mY / kScreenPx != gridY || e.mSheet >= gSheetCount)
        {
            continue;
        }
        if (e.mKind == ENT_BLANK_SAGE)
        {
            continue; // invisible trigger, no sprite
        }
        if (e.mKind == ENT_GATE && i < kMaxEntitiesPerMap && gDone[i])
        {
            continue; // opened for good (Alive = false)
        }
        const SpriteSheetEntry* sheet = Sprite_FindSheet(gSheetNames[e.mSheet]);
        if (!sheet || !Sprite_IsDrawable(*sheet))
        {
            continue;
        }
        int bank = -1;
        for (int b = 0; b < kPalBankCount; b++)
        {
            if (bankSheets[b] == sheet)
            {
                bank = b;
                break;
            }
            if (!bankSheets[b] && bank < 0)
            {
                bank = b;
            }
        }
        if (bank < 0 || oamNext + static_cast<int>(sheet->mPartCount) > kOamEnd)
        {
            continue;
        }

        Sprite& sprite = gSprites[gSpriteCount];
        if (!Sprite_Create(sprite, *gVram, gSheetNames[e.mSheet], kFirstPalBank + bank, oamNext))
        {
            continue;
        }
        bankSheets[bank] = sheet;
        oamNext += Sprite_OamCount(sprite);
        Sprite_SetFrame(sprite, sheet->mFrameCount ? e.mFrame % sheet->mFrameCount : 0);
        Sprite_Update(sprite);
        gSpriteX[gSpriteCount] = e.mX;
        gSpriteY[gSpriteCount] = e.mY;
        gSpriteEnt[gSpriteCount] = static_cast<int>(i);
        gGateStep[gSpriteCount] = 0;
        gGateTimer[gSpriteCount] = 0;
        if (e.mKind == ENT_CONSOLE)
        {
            if (i < kMaxEntitiesPerMap && gDone[i])
            {
                Sprite_SetFrame(sprite, 2);
                gPuzzlesSolved++;
            }
            else
            {
                Sprite_Play(sprite, "active");
            }
        }
        gSpriteCount++;
    }
}

// Per frame: gates whose condition holds open (they are passable immediately; the anim just plays out).
void UpdateBehaviour()
{
    for (int i = 0; i < gSpriteCount; i++)
    {
        const MapEntity& e = gList.mEntities[gSpriteEnt[i]];
        if (e.mKind != ENT_GATE)
        {
            continue;
        }
        if (gGateStep[i] == 0 && GateCondition(e))
        {
            gGateStep[i] = 1;
            gGateTimer[i] = 0;
            PlaySfx("open");
            if (e.mArg < 10 && gSpriteEnt[i] < kMaxEntitiesPerMap)
            {
                gDone[gSpriteEnt[i]] = 1;
            }
        }
        if (gGateStep[i] >= 1 && gGateStep[i] < 5)
        {
            if (++gGateTimer[i] >= kGateStepTicks)
            {
                gGateTimer[i] = 0;
                gGateStep[i]++;
            }
            const int step = gGateStep[i] > 4 ? 3 : gGateStep[i] - 1;
            Sprite_SetFrame(gSprites[i], e.mFrame + step);
        }
    }
}
} // namespace

void Entities_Init(ObjVramAllocator& vram)
{
    gVram = &vram;
    gSpriteCount = 0;
    gShownGrid = -1;
}

void Entities_SetMap(const MapEntityList& list, const char* const* sheetNames, uint32_t sheetCount)
{
    if (gVram)
    {
        DestroyAll();
    }
    gList = list;
    gSheetNames = sheetNames;
    gSheetCount = sheetCount;
    for (int i = 0; i < kMaxEntitiesPerMap; i++)
    {
        gDone[i] = 0;
    }
    gPuzzlesSolved = 0;
}

void Entities_Clear()
{
    if (gVram)
    {
        DestroyAll();
    }
}

void Entities_Update(int gridX, int gridY, int camX, int camY, int screenX)
{
    if (!gVram)
    {
        return;
    }
    if (gShownGrid != gridY * 256 + gridX)
    {
        gPuzzlesSolved = 0;
        Rebuild(gridX, gridY);
    }
    UpdateBehaviour();
    for (int i = 0; i < gSpriteCount; i++)
    {
        Sprite_Update(gSprites[i]);
        Sprite_Draw(gSprites[i], gSpriteX[i] - camX + screenX, gSpriteY[i] - camY);
    }
}

int Entities_GetSolids(SolidRect* out, int max)
{
    int n = 0;
    for (int i = 0; i < gSpriteCount && n < max; i++)
    {
        if (IsSolid(i, gList.mEntities[gSpriteEnt[i]]))
        {
            out[n++] = { gSpriteX[i], gSpriteY[i], 16, 16 };
        }
    }
    return n;
}

// PlayState.CheckInteraction: the entity's box grows 1px on each side of the facing axis and shifts 1px towards
// the player, so it reaches 2px out on the near side.
bool Entities_Interact(int hitX, int hitY, int hitW, int hitH, int dirX, int dirY)
{
    const int fx = -dirX;
    const int fy = -dirY;
    const int ax = fx < 0 ? -fx : fx;
    const int ay = fy < 0 ? -fy : fy;
    for (int i = 0; i < gSpriteCount; i++)
    {
        const int idx = gSpriteEnt[i];
        const MapEntity& e = gList.mEntities[idx];
        if (e.mKind != ENT_CONSOLE || idx >= kMaxEntitiesPerMap || gDone[idx])
        {
            continue;
        }
        const int rx = gSpriteX[i] - ax + fx;
        const int ry = gSpriteY[i] - ay + fy;
        const int rw = 16 + 2 * ax;
        const int rh = 16 + 2 * ay;
        if (hitX + hitW > rx && hitX < rx + rw && hitY + hitH > ry && hitY < ry + rh)
        {
            gDone[idx] = 1;
            gPuzzlesSolved++;
            Sprite_SetFrame(gSprites[i], 2);
            Sprite_Play(gSprites[i], "green");
            PlaySfx("get_small_health");
            return true;
        }
    }
    return false;
}

bool Entities_TakeScreenTrigger(int gridX, int gridY, int* arg)
{
    for (uint32_t i = 0; i < gList.mCount && i < kMaxEntitiesPerMap; i++)
    {
        const MapEntity& e = gList.mEntities[i];
        if (e.mKind == ENT_BLANK_SAGE && !gDone[i] && e.mX / kScreenPx == gridX && e.mY / kScreenPx == gridY)
        {
            gDone[i] = 1;
            *arg = e.mArg;
            return true;
        }
    }
    return false;
}
