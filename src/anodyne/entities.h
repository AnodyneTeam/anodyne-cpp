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

// ---- Behaviour (consoles and gates) ----
struct SolidRect final
{
    int mX;
    int mY;
    int mW;
    int mH;
};

// Fills `out` with the solid entities (closed gates, consoles) on the shown screen. Returns how many.
int Entities_GetSolids(SolidRect* out, int max);

// Player pressed Accept: if an interactable entity (a console) is within reach of the player's hitbox on the side
// they face (dirX/dirY = unit vector they face), activates it. Returns true if something was activated.
bool Entities_Interact(int hitX, int hitY, int hitW, int hitH, int dirX, int dirY);

// The camera has just arrived on screen (gridX, gridY) (PlayState EndScreenTransition): if one of its one-shot
// triggers (BLANK's Sage) is waiting, consumes it and returns true with the entity's "frame" in *arg.
bool Entities_TakeScreenTrigger(int gridX, int gridY, int* arg);
