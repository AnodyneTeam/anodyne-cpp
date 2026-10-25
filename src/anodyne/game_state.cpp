#include "game_state.h"

#include <string.h>

#include "audio_manager.h"
#include "dialog.h"
#include "ecs.h"


GameState gGame;

namespace
{
constexpr unsigned kPresetWords = (kMaxPresets + 31) / 32;
uint32_t gDead[kPresetWords] __attribute__((section(".sbss")));      // !Alive
uint32_t gActivated[kPresetWords] __attribute__((section(".sbss"))); // Activated

bool GetBit(const uint32_t* bits, unsigned i)
{
    return i < kMaxPresets && (bits[i >> 5] >> (i & 31)) & 1;
}

void SetBit(uint32_t* bits, unsigned i, bool on)
{
    if (i >= kMaxPresets)
    {
        return;
    }
    if (on)
    {
        bits[i >> 5] |= 1u << (i & 31);
    }
    else
    {
        bits[i >> 5] &= ~(1u << (i & 31));
    }
}

// EventRegister: a small table of names (ROM strings or copied) and counters.
constexpr int kMaxEvents = 64;
constexpr int kEventNameLen = 24;
struct EventSlot final
{
    char mName[kEventNameLen];
    int mValue;
};
EventSlot gEvents[kMaxEvents];
int gEventCount = 0;

EventSlot* FindEvent(const char* name, bool create)
{
    for (int i = 0; i < gEventCount; i++)
    {
        if (strcmp(gEvents[i].mName, name) == 0)
        {
            return &gEvents[i];
        }
    }
    if (!create || gEventCount == kMaxEvents)
    {
        return nullptr;
    }
    EventSlot& e = gEvents[gEventCount++];
    strncpy(e.mName, name, kEventNameLen - 1);
    e.mName[kEventNameLen - 1] = '\0';
    e.mValue = 0;
    return &e;
}

bool gDialogueFinished = true;
bool gCutsceneDialogue = false;
} // namespace

void GameState_Reset()
{
    memset(&gGame, 0, sizeof(gGame));
    gGame.mMaxHealth = 6;
    gGame.mCurHealth = 6;
    gGame.mCheckpointMap = -1;
    gGame.mReturnMap = -1;
    memset(gDead, 0, sizeof(gDead));
    memset(gActivated, 0, sizeof(gActivated));
    gEventCount = 0;
    gDialogueFinished = true;
}

void GameState_SetHealth(int health)
{
    gGame.mCurHealth = health < 0 ? 0 : (health > gGame.mMaxHealth ? gGame.mMaxHealth : health);
}

void GameState_SetMaxHealth(int maxHealth)
{
    gGame.mMaxHealth = maxHealth > 16 ? 16 : maxHealth;
}

namespace
{
bool MapBit(uint32_t bits, const char* map)
{
    const int i = Game_MapIndex(map);
    return i >= 0 && ((bits >> i) & 1) != 0;
}
} // namespace

bool Game_Visited(const char* map)
{
    return MapBit(gGame.mVisitedMaps, map);
}

bool Game_BossDefeated(const char* map)
{
    return MapBit(gGame.mBossDefeated, map);
}

bool Game_LeftAfterBoss(const char* map)
{
    return MapBit(gGame.mLeftAfterBoss, map);
}

void Game_SetBossDefeated(const char* map)
{
    const int i = Game_MapIndex(map);
    if (i >= 0)
    {
        gGame.mBossDefeated |= 1u << i;
    }
}

bool Game_IsDungeon()
{
    static const char* const kDungeons[] = { "BEDROOM", "REDCAVE", "CROWD", "APARTMENT", "HOTEL", "CIRCUS" };
    for (const char* d : kDungeons)
    {
        if (gGame.mMapName && strcmp(gGame.mMapName, d) == 0)
        {
            return true;
        }
    }
    return false;
}

int Inventory_MapKeys()
{
    return (gGame.mCurrentMap >= 0 && gGame.mCurrentMap < kMaxMaps) ? gGame.mMapKeys[gGame.mCurrentMap] : 0;
}

void Inventory_AddMapKey()
{
    if (gGame.mCurrentMap >= 0 && gGame.mCurrentMap < kMaxMaps)
    {
        gGame.mMapKeys[gGame.mCurrentMap]++;
    }
}

void Inventory_RemoveMapKey()
{
    if (Inventory_MapKeys() > 0)
    {
        gGame.mMapKeys[gGame.mCurrentMap]--;
    }
}

int Inventory_CardCount()
{
    int n = 0;
    for (uint8_t c : gGame.mCardStatus)
    {
        n += c ? 1 : 0;
    }
    return n;
}

bool Preset_Alive(const MapEntity& preset)
{
    return !GetBit(gDead, preset.mGlobalId);
}

void Preset_SetAlive(const MapEntity& preset, bool alive)
{
    SetBit(gDead, preset.mGlobalId, !alive);
}

bool Preset_Activated(const MapEntity& preset)
{
    return GetBit(gActivated, preset.mGlobalId);
}

void Preset_SetActivated(const MapEntity& preset, bool activated)
{
    SetBit(gActivated, preset.mGlobalId, activated);
}

void Preset_ReviveMap(const MapEntityList& list)
{
    for (uint32_t i = 0; i < list.mCount; i++)
    {
        if (list.mEntities[i].mPermanence != PERM_GLOBAL)
        {
            Preset_SetAlive(list.mEntities[i], true);
        }
    }
}

bool Preset_OnGrid(const MapEntity& preset, int gx, int gy)
{
    // Positions are never negative in Entities.xml, so division is the floor.
    return preset.mX / 160 == gx && preset.mY / 160 == gy;
}

void Preset_ReviveGrid(const MapEntityList& list, int gx, int gy)
{
    for (uint32_t i = 0; i < list.mCount; i++)
    {
        const MapEntity& p = list.mEntities[i];
        if (p.mPermanence == PERM_GRID_LOCAL && Preset_OnGrid(p, gx, gy))
        {
            Preset_SetAlive(p, true);
        }
    }
}

int Event_Get(const char* name)
{
    const EventSlot* e = FindEvent(name, false);
    return e ? e->mValue : 0;
}

void Event_Set(const char* name, int value)
{
    EventSlot* e = FindEvent(name, true);
    if (e)
    {
        e->mValue = value;
    }
}

void Event_Inc(const char* name)
{
    Event_Set(name, Event_Get(name) + 1);
}

void Game_ShowDialogue(const char* npc, const char* scene, int id)
{
    bool top = false;
    const char* text = Dialog_Get(npc, scene, id, &top);
    if (text && text[0])
    {
        Dialog_Open(text, top, false);
        gDialogueFinished = false;
        gCutsceneDialogue = false;
    }
}

void Game_ShowCutsceneDialogue(const char* npc, const char* scene, int id)
{
    Game_ShowDialogue(npc, scene, id);
    gCutsceneDialogue = true;
}

bool Game_DialogueIsCutscene()
{
    return gCutsceneDialogue;
}

void Game_ShowDialogueFor(const char* npc, const char* area, const char* scene, int id)
{
    bool top = false;
    const char* text = Dialog_GetFor(npc, area, scene, id, &top);
    if (text && text[0])
    {
        Dialog_Open(text, top, false);
        gDialogueFinished = false;
        gCutsceneDialogue = false;
    }
}

void Game_ShowText(const char* text)
{
    // Dialog_Open keeps the pointer: copy, so callers can build the text on the stack.
    static char sText[512];
    strncpy(sText, text, sizeof(sText) - 1);
    sText[sizeof(sText) - 1] = '\0';
    Dialog_Open(sText, false, false);
    gCutsceneDialogue = false;
    gDialogueFinished = false;
}

bool Game_DialogueFinished()
{
    if (!gDialogueFinished && !Dialog_IsOpen())
    {
        gDialogueFinished = true;
    }
    return gDialogueFinished;
}

void Game_PlaySfxOneOf(const char* const* names, int count)
{
    // (every name is one of the game's sfx: the C# drops the missing ones before picking)
    const int32_t id = AudioManager_FindSfx(names[Rng_Range(0, count)]);
    if (id >= 0)
    {
        AudioManager_PlaySFX(static_cast<uint32_t>(id));
    }
}

void Game_PlaySfx(const char* name)
{
    if (AudioManager_FindSfx(name) >= 0)
    {
        Game_PlaySfxOneOf(&name, 1);
    }
}
