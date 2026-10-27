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
    GEV_DUST_FALL,      // a dust fell down a hole
    GEV_EMPTY_TREASURE, // a card chest was opened for a card already held
};

// Broom types (BroomType, in the C#'s order: SwitchBroom steps through it).
enum BroomType : uint8_t
{
    BROOM_NONE,
    BROOM_NORMAL,
    BROOM_LONG,
    BROOM_WIDE,
    BROOM_TRANSFORMER,
    BROOM_TYPE_COUNT,
};

// InventoryManager.TradeState.
enum TradeState : uint8_t
{
    TRADE_NONE,
    TRADE_SHOES,
    TRADE_BOX,
};

// Room for the content compiler's map list (anodyne.cpp checks it fits).
constexpr int kMaxMaps = 32;

struct GameState final
{
    // Per screen (PlayState.LoadGridEntities resets them).
    int mPuzzlesSolved; // PUZZLES_SOLVED
    int mEnemiesKilled; // ENEMIES_KILLED, kept up to date from the entities each frame

    int mCurHealth; // CUR_HEALTH, clamped to 0..mMaxHealth
    int mMaxHealth; // MAX_HEALTH, at most 16

    // InventoryManager
    uint8_t mEquippedBroom; // BroomType (_equippedBroom: read it with Inventory_EquippedBroom)
    bool mEquippedBroomChanged; // InventoryManager.EquippedBroomChanged: PlayState.Refreshes takes it
    bool mHasBroom[5];      // by BroomType (NONE unused)
    bool mBroomActive;
    bool mCanJump; // InventoryManager.CanJump (the jump shoes)
    uint8_t mTradeState; // InventoryManager.TradeState: NONE, SHOES, BOX
    bool mSpookedMonster; // EventRegister.SpookedMonster
    int mPillarSwitchOn;  // GlobalState.PillarSwitchOn: reset entering another map
    bool mDisableMenu;    // GlobalState.disable_menu: no pause menu (from dying until Continue)
    bool mInDeathRoom;    // GlobalState.InDeathRoom
    uint8_t mCardStatus[49];
    bool mBigKeyStatus[3];  // green, red, blue
    bool mSecretStatus[14];
    uint8_t mMapKeys[kMaxMaps]; // small keys held, per map (InventoryManager's map key table)

    // GlobalState.checkpoint: where dying sends the player (map index, -1 = none yet; position in map pixels).
    int mCheckpointMap;
    int mCheckpointX;
    int mCheckpointY;
    // GlobalState.ReturnTarget: the dungeon entrance outside the current dungeon (map index, -1 = none).
    int mReturnMap;
    int mReturnX;
    int mReturnY;
    uint32_t mVisitedMaps;  // EventRegister.VisitedMaps, by map index
    uint32_t mBossDefeated; // EventRegister.BossDefeated, by map index
    uint32_t mLeftAfterBoss; // EventRegister.LeftAfterBoss, by map index
    uint32_t mNexusPortals;  // EventRegister.ActivatedNexusPortals, by map index

    const char* mModeName; // what anodyne.cpp's main loop is showing (for the probe)
    const char* mMapName;  // CURRENT_MAP_NAME
    int mCurrentMap; // kMaps index
    int mGridX;      // CURRENT_GRID_X/Y
    int mGridY;
};

extern GameState gGame;

// The kMaps index of a map name (-1 if unknown); anodyne.cpp has the map list.
int Game_MapIndex(const char* name);
// EventRegister.VisitedMaps / BossDefeated / LeftAfterBoss by name.
bool Game_Visited(const char* map);
bool Game_BossDefeated(const char* map);
bool Game_LeftAfterBoss(const char* map);
void Game_SetBossDefeated(const char* map);
// GlobalState.IsDungeon: the current map is one of the dungeons.
bool Game_IsDungeon();

// New game (GlobalState.ResetValues + EntityManager.State cleared).
void GameState_Reset();

// The progress a save keeps (GlobalState.Save): GameState, the presets' Alive/Activated, the events.
struct SaveWriter;
struct SaveReader;
void GameState_Save(SaveWriter& w);
void GameState_Load(SaveReader& r);

void GameState_SetHealth(int health);
void GameState_SetMaxHealth(int maxHealth);

// InventoryManager: the current map's small keys.
int Inventory_MapKeys();
void Inventory_AddMapKey();
void Inventory_RemoveMapKey();
int Inventory_CardCount();
// GlobalState.CanChangeBroom: not in the death room, CELL or SUBURB (there only the plain broom or the transformer).
bool Game_CanChangeBroom();
// GlobalState.IsCell: the CELL's own sprites (and some timings).
bool Game_IsCell();
// InventoryManager.HasBroomType / HasAnyBroom.
bool Inventory_HasBroomType(uint8_t type);
bool Inventory_HasAnyBroom();
// InventoryManager.EquippedBroom: the getter (the plain broom where it can't be changed) and the setter (only a broom
// that is held; sets mEquippedBroomChanged when it changes).
uint8_t Inventory_EquippedBroom();
void Inventory_SetEquippedBroom(uint8_t type);

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
// CutsceneState's DialogueEvent: the same box, but GlobalState.Dialogue stays empty (the C#'s traces show no dialogue).
void Game_ShowCutsceneDialogue(const char* npc, const char* scene, int id = -1);
void Game_ShowCutsceneDialogueFor(const char* npc, const char* area, const char* scene, int id = -1);
// Whether the open box came from Game_ShowCutsceneDialogue.
bool Game_DialogueIsCutscene();
// DialogueManager.RandomDialogue: one of the scene's lines at random (it draws a number).
void Game_ShowRandomDialogue(const char* npc, const char* scene);
// The same with the dialogue's area given (GetDialogue(npc, area, scene, id)).
void Game_ShowDialogueFor(const char* npc, const char* area, const char* scene, int id = -1);
// GlobalState.Dialogue = text (copied).
void Game_ShowText(const char* text);
// GlobalState.LastDialogueFinished.
bool Game_DialogueFinished();

// GlobalState.StartCutscene: a CutsceneState runs `fn(owner)` once a frame (the player frozen, entities updating)
// until it returns true. A dialogue opened by the step plays before the next step.
struct Entity;
using CutsceneFn = bool (*)(Entity& owner);
void Game_StartCutscene(Entity& owner, CutsceneFn fn);
bool Game_InCutscene();

// SoundManager.PlaySoundEffect.
void Game_PlaySfx(const char* name);
// SoundManager.PlaySoundEffect(names...): one of them at random. Each call draws a random number (GlobalState.RNG),
// even with one name, as the C#'s does.
void Game_PlaySfxOneOf(const char* const* names, int count);
// SoundManager.PlaySong / StopSong (""): until the next screen or map with other music.
// (also SetSongVolume(volume), even when the song is already playing)
void Game_PlaySong(const char* name, float volume = 1.0f);
// SoundManager.GetVolume / SetSongVolume: the song's volume, 0 to 1.
// SoundManager.CurrentSongName ("" when stopped).
const char* Game_CurrentSong();
// Map.IgnoreMusicNextUpdate: the next screen transition keeps the music playing.
void Game_IgnoreMusicNextUpdate();
float Game_SongVolume();
void Game_SetSongVolume(float volume);
