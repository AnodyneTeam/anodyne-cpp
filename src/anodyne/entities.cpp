#include "entities.h"

#include "entity_classes.h"
#include "sprite.h"

#include "tonc.h"

namespace
{
constexpr int kFirstOam = 10;
constexpr int kOamEnd = 120; // 120-127: the dialogue box (dialog.cpp)
constexpr int kFirstPalBank = 4;
constexpr int kPalBankCount = 9; // 13-15: the HUD and the dialogue box

MapEntityList gList = { 0, nullptr };
const char* const* gSheetNames = nullptr;
uint32_t gSheetCount = 0;
const char* const* gTypeValues = nullptr;
uint32_t gTypeCount = 0;
bool gHaveMap = false;
int gGridX = -1;
int gGridY = -1;
bool gShown = false;

// The player, as a stand-in entity world.cpp keeps in sync: what the entities collide with and look at.
Entity* gPlayer = nullptr;

const EntityClass kPlayerClass = {
    "Player", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, GROUP_PLAYER, 0, ECF_MAP_COLLISION,
};

// An Entities.xml element the port doesn't implement yet: its sheet's frame, nothing else.
const EntityClass kPlaceholderClass = {
    "Placeholder", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

Facing ToFacing(uint8_t worldFacing)
{
    static const Facing kMap[4] = { FACE_DOWN, FACE_RIGHT, FACE_UP, FACE_LEFT };
    return kMap[worldFacing & 3];
}

uint8_t FromFacing(uint8_t facing)
{
    static const uint8_t kMap[4] = { 3, 1, 2, 0 }; // LEFT, RIGHT, UP, DOWN -> world order
    return kMap[facing & 3];
}

void SyncIn(const PlayerBridge& p)
{
    Transform& t = *gPlayer->mT;
    t.mX = p.mX;
    t.mY = p.mY;
    t.mLastX = p.mLastX;
    t.mLastY = p.mLastY;
    gPlayer->mB->mTouching = p.mTouching;
    gPlayer->mFacing = ToFacing(p.mFacing);
}

void SyncOut(PlayerBridge& p)
{
    const Transform& t = *gPlayer->mT;
    p.mX = t.mX;
    p.mY = t.mY;
    p.mLastX = t.mLastX;
    p.mLastY = t.mLastY;
    p.mTouching = gPlayer->mB->mTouching;
    p.mFacing = FromFacing(gPlayer->mFacing);
}

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
        return true;
    default:
        return false;
    }
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

Entity* World_Player()
{
    return gPlayer;
}

// The player's map collision is still world.cpp's; entities don't collide with the map yet.
void Map_Collide(Entity&)
{
}

uint8_t Map_CollisionAllowAt(fx, fx)
{
    return 0;
}

void Entities_Init(ObjVramAllocator& vram)
{
    Ecs_Init();
    Ecs_SetRenderer(vram, kFirstPalBank, kPalBankCount);
    gPlayer = Ecs_Create(kPlayerClass, 0, 0, 0);
    gPlayer->mPlayerOwned = true;
    gPlayer->mB->mW = 10;
    gPlayer->mB->mH = 12;
    gHaveMap = false;
}

void Entities_SetMap(const MapEntityList& list, const char* const* sheetNames, uint32_t sheetCount,
                     const char* const* typeValues, uint32_t typeCount)
{
    if (gHaveMap)
    {
        Preset_ReviveMap(gList);
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
        Ecs_HideAll(kFirstOam, kOamEnd);
        gShown = false;
    }
}

void Entities_LoadGrid(int gx, int gy, const PlayerBridge& player, bool initial)
{
    if (!gPlayer)
    {
        return;
    }
    SyncIn(player);
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

    // ENEMIES_KILLED starts at the enemies of this screen that are already dead; PUZZLES_SOLVED at 0.
    int killed = 0;
    for (uint32_t i = 0; i < gList.mCount; i++)
    {
        const MapEntity& p = gList.mEntities[i];
        if (!Preset_OnGrid(p, gx, gy))
        {
            continue;
        }
        if (!Preset_Alive(p))
        {
            killed += IsEnemyType(p.mType) ? 1 : 0;
            continue;
        }
        const SpawnFn spawn = Entities_SpawnFn(p.mType);
        if (spawn)
        {
            spawn(p, *gPlayer);
        }
        else
        {
            SpawnPlaceholder(p);
        }
    }
    Ecs_SetBaseKilledEnemies(killed);
    gGame.mGridX = gx;
    gGame.mGridY = gy;
}

void Entities_EndTransition()
{
    Ecs_FreeOld();
    Ecs_FireEvent(GEV_END_SCREEN_TRANSITION);
}

void Entities_Collide(PlayerBridge& player)
{
    if (!gPlayer)
    {
        return;
    }
    SyncIn(player);
    Ecs_DoCollisions(false, gGridX * 160, gGridY * 160);
    SyncOut(player);
}

bool Entities_Interact(const PlayerBridge& player)
{
    if (!gPlayer)
    {
        return false;
    }
    SyncIn(player);
    return Ecs_CheckInteraction(*gPlayer);
}

void Entities_Update(const PlayerBridge& player)
{
    if (!gPlayer)
    {
        return;
    }
    SyncIn(player);
    Ecs_UpdateAll(gPlayer);
    Ecs_FreeDead();
    gGame.mEnemiesKilled = Ecs_KilledEnemies();
}

void Entities_Draw(int camX, int camY, int screenX)
{
    Ecs_Draw(camX, camY, screenX, kFirstOam, kOamEnd, 0, 0);
    gShown = true;
}
