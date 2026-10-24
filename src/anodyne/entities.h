#pragma once

#include <stdint.h>

#include "map_data.h"
#include "obj_vram_allocator.hpp"

// The map's entities (Entities.xml presets) on the entity system (ecs.h), loaded a screen at a time like
// PlayState.LoadGridEntities. world.cpp drives them through this plain interface; the classes themselves are in
// ent_*.cpp.
//
// They draw into OAM 10-119 (120-127: the dialogue box) with OBJ palette banks 4-12.

void Entities_Init(ObjVramAllocator& vram);

// The map changed (PlayState.Warp): the previous map's non-GLOBAL presets come back, then `list` becomes the map's.
// Removes every entity of the previous map.
// sheetNames = kEntitySheets (MapEntity.mSheet), typeValues = kEntityTypeValues (MapEntity.mTypeValue).
void Entities_SetMap(const MapEntityList& list, const char* const* sheetNames, uint32_t sheetCount,
                     const char* const* typeValues, uint32_t typeCount);

// Hides every entity sprite (the world isn't being shown).
void Entities_Clear();

// The player as the entities see it: hitbox top-left, last position (16.16 map pixels), the sides it touches and the
// way it faces (0 down, 1 right, 2 up, 3 left: world.cpp's order).
struct PlayerBridge final
{
    int32_t mX;
    int32_t mY;
    int32_t mLastX;
    int32_t mLastY;
    uint8_t mTouching;
    uint8_t mFacing;
};

// Screen (gx, gy) becomes current: the GRID_LOCAL presets of the screen being left come back (OnGridExit), its
// entities become "old" (drawn, not updated) and the new screen's presets are created. `initial`: on a map load /
// warp (the old screen's entities are dropped right away).
void Entities_LoadGrid(int gx, int gy, const PlayerBridge& player, bool initial);
// The camera arrived (PlayState.EndScreenTransition): the old screen's entities go; EndScreenTransition fires.
void Entities_EndTransition();

// CollisionGroups.DoCollision for the player: the solid entities push the player out (player updated in place).
void Entities_Collide(PlayerBridge& player);
// Accept pressed (PlayState.CheckInteraction). True if an entity took it.
bool Entities_Interact(const PlayerBridge& player);
// Update + PostUpdate of every entity (not during a screen transition).
void Entities_Update(const PlayerBridge& player);
// Draws the entities: camX/camY = camera top-left in map pixels, screenX = where the playfield starts on screen.
void Entities_Draw(int camX, int camY, int screenX);
