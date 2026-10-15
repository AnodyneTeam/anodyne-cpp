#pragma once

#include "map_data.h"
#include "obj_vram_allocator.hpp"

// The playable map, ported from AnodyneSharp: a 160x160 playfield (10x10 tiles of 16x16) shown in the middle of
// the 240x160 GBA screen, a player with the C# hitbox/walk speed colliding with the map's tiles (TileData.col),
// and screen-to-screen scrolling when the player crosses an edge.

// Creates the player sprite and its overlays. The world uses OAM slots 0-9 and OBJ palette banks 0-3.
bool World_Init(ObjVramAllocator& vram);

// Loads the map's palette/tileset into VRAM and its collision into RAM, then puts the player on a free spot.
// mapName (e.g. "FIELDS") selects the map's foot-overlay sprites; `audio` is its music/ambience settings and `scroll` its scrolling background (if any).
void World_Load(const MapEntry& map, const char* mapName, const MapAudioData& audio, const MapScrollData& scroll,
                const MapDoorList& doors);

// Per-frame: input, movement, collision, screen transitions, background scrolling and the player sprite.
void World_Update();

// Shows/hides the world (BG0, the 160x160 window and the player sprite). Call when switching modes.
void World_Enter();
void World_SetPlayerVisible(bool visible);

// Doors: when the player steps on a door, World_TakeWarp returns true once with the destination (map index,
// player hitbox top-left in map pixels, facing 0-3 or 255). The caller loads that map (if it differs) and
// calls World_PlaceAt.
bool World_TakeWarp(uint8_t& destMap, int& destX, int& destY, uint8_t& facing);
void World_PlaceAt(int x, int y, uint8_t facing);
