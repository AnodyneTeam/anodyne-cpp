#pragma once

#include <stdint.h>

#include "map_data.h"
#include "obj_vram_allocator.hpp"

// The map's entities (Entities.xml presets) on the entity system (ecs.h), loaded a screen at a time like
// PlayState.LoadGridEntities. The classes themselves are in ent_*.cpp (the player's in player.cpp).
//
// Everything in the world draws into OAM 0-119 (120-127: the dialogue box) with OBJ palette banks 0-12.

void Entities_Init(ObjVramAllocator& vram);

// The map changed (PlayState.Warp): the previous map's non-GLOBAL presets come back, then `list` becomes the map's.
// Removes every entity of the previous map.
// sheetNames = kEntitySheets (MapEntity.mSheet), typeValues = kEntityTypeValues (MapEntity.mTypeValue).
void Entities_SetMap(const MapEntityList& list, const char* const* sheetNames, uint32_t sheetCount,
                     const char* const* typeValues, uint32_t typeCount);

// Hides every entity sprite (the world isn't being shown).
void Entities_Clear();

// Screen (gx, gy) becomes current: the GRID_LOCAL presets of the screen being left come back (OnGridExit), its
// entities become "old" (drawn, not updated) and the new screen's presets are created. `initial`: on a map load /
// warp (the old screen's entities are dropped right away).
void Entities_LoadGrid(int gx, int gy, bool initial);
// The camera arrived (PlayState.EndScreenTransition): the old screen's entities go; EndScreenTransition fires.
void Entities_EndTransition();

// Draws the entities: camX/camY = camera top-left in map pixels, screenX = where the playfield starts on screen.
void Entities_Draw(int camX, int camY, int screenX);
