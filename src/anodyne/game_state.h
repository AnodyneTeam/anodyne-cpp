#pragma once

#include <stdint.h>

#include "map_data.h"

// GlobalState / EntityManager.State / InventoryManager / EventRegister: the game's progress, ported from
// AnodyneSharp.

// GameEvents (Events.cs) fired at entities with Ecs_FireEvent.
enum GameEvent : uint8_t
{
    GEV_START_SCREEN_TRANSITION,
    GEV_END_SCREEN_TRANSITION,
    GEV_START_WARP,
    GEV_END_WARP,
    GEV_BROOM_USED,
    GEV_CHANGE_CARD_COUNT,
};

// Broom types (InventoryManager.BroomType).
enum BroomType : uint8_t
{
    BROOM_NONE,
    BROOM_NORMAL,
    BROOM_WIDE,
    BROOM_LONG,
    BROOM_TRANSFORMER,
};

struct GameState final
{
    // Per screen (PlayState.LoadGridEntities resets them).
    int mPuzzlesSolved; // PUZZLES_SOLVED
    int mEnemiesKilled; // ENEMIES_KILLED, kept up to date from the entities each frame

    int mCurHealth; // CUR_HEALTH, clamped to 0..mMaxHealth
    int mMaxHealth; // MAX_HEALTH, at most 16

    // InventoryManager
    uint8_t mEquippedBroom; // BroomType
    bool mHasBroom[5];      // by BroomType (NONE unused)
    bool mBroomActive;
    uint8_t mCardStatus[49];
    bool mBigKeyStatus[3];  // green, red, blue

    const char* mModeName; // what anodyne.cpp's main loop is showing (for the probe)
    const char* mMapName;  // CURRENT_MAP_NAME
    int mCurrentMap; // kMaps index
    int mGridX;      // CURRENT_GRID_X/Y
    int mGridY;
};

extern GameState gGame;

// New game (GlobalState.ResetValues + EntityManager.State cleared).
void GameState_Reset();

void GameState_SetHealth(int health);
void GameState_SetMaxHealth(int maxHealth);

// Room for the content compiler's kEntityPresetCount (anodyne.cpp checks it fits).
constexpr unsigned kMaxPresets = 4096;

// EntityPreset.Alive / Activated, kept per preset (MapEntity.mGlobalId). Alive defaults to true.
bool Preset_Alive(const MapEntity& preset);
void Preset_SetAlive(const MapEntity& preset, bool alive);
bool Preset_Activated(const MapEntity& preset);
void Preset_SetActivated(const MapEntity& preset, bool activated);
// PlayState.Warp: every preset of the map that isn't GLOBAL comes back.
void Preset_ReviveMap(const MapEntityList& list);
// OnGridExit / LoadGridEntities: the GRID_LOCAL presets of screen (gx, gy) come back.
void Preset_ReviveGrid(const MapEntityList& list, int gx, int gy);
// The preset sits on screen (gx, gy) (MapUtilities.GetRoomCoordinate of its position).
bool Preset_OnGrid(const MapEntity& preset, int gx, int gy);

// EventRegister: named counters ("ReceivedKey", "SweptDust", ...). Unknown names read 0.
int Event_Get(const char* name);
void Event_Set(const char* name, int value);
void Event_Inc(const char* name);

// GlobalState.Dialogue = DialogueManager.GetDialogue(npc, scene, id): opens the dialogue box.
void Game_ShowDialogue(const char* npc, const char* scene, int id = -1);
// GlobalState.LastDialogueFinished.
bool Game_DialogueFinished();

// SoundManager.PlaySoundEffect.
void Game_PlaySfx(const char* name);
