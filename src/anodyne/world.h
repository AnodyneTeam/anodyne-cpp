#pragma once

#include "ecs.h"
#include "map_data.h"
#include "obj_vram_allocator.hpp"

// The playable map, ported from AnodyneSharp: a 160x160 playfield (10x10 tiles of 16x16) shown in the middle of
// the 240x160 GBA screen, a player with the C# hitbox/walk speed colliding with the map's tiles (TileData.col),
// and screen-to-screen scrolling when the player crosses an edge.

// Creates the player sprite and its overlays. The world uses OAM slots 0-9 and OBJ palette banks 0-3.
bool World_Init(ObjVramAllocator& vram);

// Loads the map's palette/tileset into VRAM and its collision into RAM, then puts the player on a free spot.
// mapName (e.g. "FIELDS") selects the map's foot-overlay sprites; `audio` is its music/ambience settings and `scroll` its scrolling background (if any).
// loadEntities false: a World_PlaceAt follows (a warp or a new game), so that free spot's screen isn't loaded first.
void World_Load(const MapEntry& map, const char* mapName, const MapAudioData& audio, const MapScrollData& scroll,
                const MapDoorList& doors, const MapDarkData& dark, bool loadEntities = true);

// Per-frame: input, movement, collision, screen transitions, background scrolling and the player sprite.
void World_Update();

// Shows/hides the world (BG0, the 160x160 window and the player sprite). Call when switching modes.
void World_Enter();
void World_SetPlayerVisible(bool visible);

// The current BG scroll offsets per hardware BG (the registers are write-only).
void World_GetBgOffsets(int16_t x[4], int16_t y[4]);

// Warps (doors, teleporters, dying) run PlayState's S_MAP_EXIT / S_MAP_ENTER inside World_Update, the world updating
// on as in the C#. When the fade out ends the world calls the loader with the destination map (an index into kMaps)
// and then places the player (World_PlaceAt).
void World_SetMapLoader(void (*load)(uint8_t map));
// The fade of the warp under way, for the screen: black_overlay's alpha (16.16, 0..1) and the pixelation (8.8, 1 =
// none). swapped: the map was swapped this frame. False when no warp is under way.
bool World_GetWarpFade(int& dark, int& pixelation, bool& swapped);
// warp: a PlayState.Warp, which leaves the player's lastPosition where it was.
void World_PlaceAt(int x, int y, uint8_t facing, bool warp = false);
// The current map's door trigger of the Door preset at (presetX, presetY), nullptr if it has none.
const MapDoor* World_FindDoor(int presetX, int presetY);
// DoorMapPair.Warp: GlobalState.WARP to the door's linked door.
void World_TakeDoor(const MapDoor& d);
// A warp that isn't a door (teleporting entities, the bot link's test warp): GlobalState.WARP, taken like a door's.
void World_RequestWarp(int map, int x, int y, uint8_t facing);
// CutsceneState.Warp(map, grid) / Return: a cutscene shows screen (gx, gy) of another map (an index into the table
// World_SetMapTable gave), graphics only, with the play state frozen and hidden; Return puts the current map back.
void World_SetMapTable(const MapEntry* maps, uint32_t count);
void World_CutsceneWarp(int map, int gx, int gy);
void World_CutsceneReturn();
// GlobalState.WARP: a warp is under way (asked for, or fading out and in).
bool World_WarpPending();
// GlobalState.ScreenTransition: the camera is scrolling to the next screen.
bool World_InTransition();

// The player, for tools (game_probe.cpp): hitbox top-left in map pixels, facing (0 down, 1 right, 2 up, 3 left),
// the current screen, and whether the camera is scrolling to it.
struct WorldPlayerInfo final
{
    int mX;
    int mY;
    int mFacing;
    int mGridX;
    int mGridY;
    bool mTransition;
    bool mLadder;
    bool mFallen;
    int mHealth;
    bool mDeathMenu; // DeathState is up (the "Continue?" menu once it takes keys)
    int mDeaths;     // DeathStates finished (each one warps to the checkpoint)
};
void World_GetPlayerInfo(WorldPlayerInfo& info);

// The current map's collision (16x16 cells), for tools: size in cells, and each cell's blocked sides (Touching
// flags) and tile event (0 none, 1 conveyor, 2 thin, 3 hole, 4 slow, 5 spike, 6 ladder, 7 puddle, 8 reflection,
// 9 grass).
void World_GetCellsSize(int& w, int& h);
void World_GetCell(int cx, int cy, uint8_t& allow, uint8_t& event);
// Map.ChangeTile: tile (cx, cy) of a layer (0 BG, 1 BG2, 2 FG) becomes `tile` until the map loads again (collision
// and drawing follow).
void World_ChangeTile(int layer, int cx, int cy, int tile);

// Darkness (GlobalState.darkness): the screen darkens by alpha (0-1) except inside lights. Map settings set it as
// the player moves; entities (the Seer) drive it too.
void Darkness_TargetAlpha(fx alpha, fx vel = FxF(0.8f));
void Darkness_ForceAlpha(fx alpha);
fx Darkness_Alpha();
bool Darkness_ReachedAlpha();
// GlobalState.PlayerLight: the player's light, or the light standing in for it (BedRoomBounceLight). Null once that
// light is gone.
Entity* Darkness_PlayerLight();
void Darkness_SetPlayerLight(Entity& light);
// Light's constructor: draws e as a light shape from `sheet`, centred, at scale 3.
void Darkness_MakeLight(Entity& e, const char* sheet);
// A full-screen darken (0-16) for warps that also covers the lights; 0 leaves just the darkness.
void World_ScreenDarken(int level);
// GlobalState.gameScreenFade's alpha (0-1, black).
void Screen_SetFade(fx alpha);
// GlobalState.gameScreenFade.fadeColor: white (true) or black.
void Screen_SetFadeWhite(bool white);
// GlobalState.wave.active: the screen's rows ripple sideways (a GasGuy's gas, the Red Boss).
void Screen_SetWave(bool on);
bool Screen_Wave();
// The VBlank interrupt: arms the wave's HBlank DMA for the frame about to show (or stops it).
void Screen_WaveOnVBlank();

// GlobalState.screenShake / flash: shake the playfield by up to intensity (a fraction of the screen) for a time,
// or flash it white (or black) and fade back over a time.
// ScreenShake.Directions: the axes that shake (each draws its own number).
enum ShakeDirs : uint8_t
{
    SHAKE_NONE = 0,
    SHAKE_HORIZONTAL = 1,
    SHAKE_VERTICAL = 2,
    SHAKE_BOTH = 3,
};
void Screen_Shake(fx intensity, fx seconds, uint8_t dirs = SHAKE_BOTH);
// A new game's ScreenShake (parity replays start play from the C#'s state).
void Screen_ResetShake();
bool Screen_ShakeActive();
enum FlashColour : uint8_t
{
    FLASH_WHITE,
    FLASH_BLACK,
    FLASH_RED, // (drawn white for now: BLDY only fades to white or black)
};
void Screen_Flash(fx seconds, FlashColour colour = FLASH_WHITE);
bool Screen_FlashActive();
