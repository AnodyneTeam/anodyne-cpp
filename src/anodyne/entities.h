#pragma once

#include "map_data.h"
#include "obj_vram_allocator.hpp"

// Placeholder map entities: draws a static sprite for every Entities.xml entity on the current screen (no
// behaviour yet). Uses OAM slots 10-127 and OBJ palette banks 4-15; the sprites are rebuilt when the screen
// changes.
void Entities_Init(ObjVramAllocator& vram);

// Sets the map's entities and the names of its sheets (kEntitySheets); removes the previous sprites.
void Entities_SetMap(const MapEntityList& list, const char* const* sheetNames, uint32_t sheetCount);

// Removes all sprites (they are rebuilt on the next Entities_Update).
void Entities_Clear();

// Per frame: shows the entities of screen (gridX, gridY) and draws them. camX/camY = camera top-left in map
// pixels, screenX = where the playfield starts on the GBA screen.
void Entities_Update(int gridX, int gridY, int camX, int camY, int screenX);
