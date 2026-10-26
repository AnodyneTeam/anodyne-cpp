#include "entities.h"
#include "dialog.h"

#include "entity_classes.h"
#include "player.h"
#include "sprite.h"

#include "tonc.h"

namespace
{
constexpr int kFirstOam = 0;
// (the dialogue box, a menu's labels and panel take the entries from Dialog_FirstReservedOam on)
constexpr int kFirstPalBank = 0;
constexpr int kPalBankCount = 13; // 13-15: the HUD and the dialogue box

MapEntityList gList = { 0, nullptr };
const char* const* gSheetNames = nullptr;
uint32_t gSheetCount = 0;
const char* const* gTypeValues = nullptr;
uint32_t gTypeCount = 0;
bool gHaveMap = false;
int gGridX = -1;
int gGridY = -1;
bool gShown = false;

// An Entities.xml element the port doesn't implement yet: its sheet's frame, nothing else.
const EntityClass kPlaceholderClass = {
    "Placeholder", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

Entity* SpawnPlaceholder(const MapEntity& preset)
{
    if (preset.mSheet >= gSheetCount)
    {
        return nullptr;
    }
    const SpriteSheetEntry* sheet = Sprite_FindSheet(gSheetNames[preset.mSheet]);
    if (!sheet || !Sprite_IsDrawable(*sheet))
    {
        return nullptr;
    }
    Entity* e = Ecs_Create(kPlaceholderClass, Fx(preset.mX), Fx(preset.mY), PART_VISUAL, &preset);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 16;
    e->mB->mH = 16;
    e->mV->mSheet = sheet;
    Ecs_SetFrame(*e, sheet->mFrameCount ? preset.mLookFrame % sheet->mFrameCount : 0);
    return e;
}

// The classes with [Enemy].
bool IsEnemyType(uint8_t type)
{
    switch (type)
    {
    case EPT_Slime:
    case EPT_Shieldy:
    case EPT_Annoyer:
    case EPT_Seer:
    case EPT_Dog:
    case EPT_BurstPlant:
    case EPT_Frog:
    case EPT_Slasher:
    case EPT_Mover:
    case EPT_RedBoss:
    case EPT_WallBoss:
    case EPT_Rat:
    case EPT_Silverfish:
    case EPT_GasGuy:
    case EPT_TeleGuy:
    case EPT_SplitBoss:
    case EPT_Dustmaid:
    case EPT_Contort:
    case EPT_Lion:
    case EPT_CircusFolks:
    case EPT_SageBoss:
        return true;
    default:
        return false;
    }
}

// The classes with [AlwaysSpawn]: made even when their preset is dead.
static bool IsAlwaysSpawnType(uint8_t type)
{
    return type == EPT_SuburbWalker || type == EPT_EyeBossWater;
}

void FreeScreen()
{
    Ecs_FreeAllButPlayer();
    Ecs_SetBaseKilledEnemies(0);
}
} // namespace

const char* Preset_TypeValue(const MapEntity& preset)
{
    return (gTypeValues && preset.mTypeValue < gTypeCount) ? gTypeValues[preset.mTypeValue] : "";
}

Entity* Spawn_Basic(const EntityClass& cls, const MapEntity& preset, const char* sheet, int frameW, int frameH,
                    uint8_t parts)
{
    Entity* e = Ecs_Create(cls, Fx(preset.mX), Fx(preset.mY), parts, &preset);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 16;
    e->mB->mH = 16;
    if (sheet)
    {
        Ecs_SetSheet(*e, sheet, frameW, frameH);
    }
    return e;
}

void Entities_Init(ObjVramAllocator& vram)
{
    Ecs_Init();
    Ecs_SetRenderer(vram, kFirstPalBank, kPalBankCount);
    gHaveMap = false;
}

const MapEntityList& Entities_Presets()
{
    return gList;
}

void Entities_SetMap(const MapEntityList& list, const char* const* sheetNames, uint32_t sheetCount,
                     const char* const* typeValues, uint32_t typeCount)
{
    if (gHaveMap)
    {
        Preset_ReviveMap(gList);
        gGame.mPillarSwitchOn = 0;
    }
    FreeScreen();
    Ecs_FlushRenderCaches();
    gList = list;
    gSheetNames = sheetNames;
    gSheetCount = sheetCount;
    gTypeValues = typeValues;
    gTypeCount = typeCount;
    gHaveMap = true;
    gGridX = gGridY = -1;
}

void Entities_Clear()
{
    if (gShown)
    {
        Ecs_HideAll(kFirstOam, Dialog_FirstReservedOam());
        gShown = false;
    }
}

void Entities_LoadGrid(int gx, int gy, bool initial)
{
    Entity* player = World_Player();
    if (gGridX >= 0 && (gGridX != gx || gGridY != gy))
    {
        Preset_ReviveGrid(gList, gGridX, gGridY); // OnGridExit
    }
    if (initial)
    {
        FreeScreen();
    }
    else
    {
        Ecs_FreeOld(); // a transition that never finished
        Ecs_MarkScreenOld();
        Preset_ReviveGrid(gList, gx, gy);
    }
    gGridX = gx;
    gGridY = gy;
    gGame.mPuzzlesSolved = 0; // before the constructors: Console counts the activated ones

    // ENEMIES_KILLED starts at the enemies of this screen that are already dead; PUZZLES_SOLVED at 0. PlayState sets
    // it before the constructors (a Gate the player stands on checks it), so the new screen's gates never see the
    // last screen's kills.
    int killed = 0;
    for (uint32_t i = 0; i < gList.mCount; i++)
    {
        const MapEntity& p = gList.mEntities[i];
        if (Preset_OnGrid(p, gx, gy) && !Preset_Alive(p))
        {
            killed += IsEnemyType(p.mType) ? 1 : 0;
        }
    }
    Ecs_SetBaseKilledEnemies(killed);
    gGame.mEnemiesKilled = killed;
    for (uint32_t i = 0; i < gList.mCount; i++)
    {
        const MapEntity& p = gList.mEntities[i];
        if (!Preset_OnGrid(p, gx, gy) || (!Preset_Alive(p) && !IsAlwaysSpawnType(p.mType)))
        {
            continue;
        }
        const SpawnFn spawn = Entities_SpawnFn(p.mType);
        if (spawn)
        {
            spawn(p, *player);
        }
        else
        {
            SpawnPlaceholder(p);
        }
    }
    Ecs_MovePlayerOwnedLast();
    // Player.SubEntities ends with the raft, then the follower.
    if (Entity* raft = Player_Raft())
    {
        Ecs_MoveLast(*raft);
        Miao_MoveFollowerLast();
    }
    gGame.mGridX = gx;
    gGame.mGridY = gy;
}

void Entities_EndTransition()
{
    Ecs_FreeOld();
    Ecs_FireEvent(GEV_END_SCREEN_TRANSITION);
}

void Entities_Draw(int camX, int camY, int screenX, int shakeX, int shakeY)
{
    Ecs_Draw(camX, camY, screenX, kFirstOam, Dialog_FirstReservedOam(), shakeX, shakeY);
    gShown = true;
}
