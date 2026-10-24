#pragma once

#include "ecs.h"
#include "game_state.h"

// Shared by entities.cpp and the entity class files (ent_*.cpp): how a preset is turned into an entity.

// Creates the entity for `preset` (its class's constructor). Returns nullptr if it couldn't (pools full).
using SpawnFn = Entity* (*)(const MapEntity& preset, Entity& player);

// The spawn function of each EntityPresetType (nullptr: drawn as a placeholder).
SpawnFn Entities_SpawnFn(uint8_t type);

// The preset's XML "type" string (EntityPreset.TypeValue).
const char* Preset_TypeValue(const MapEntity& preset);
// Entity(Vector2 pos, AnimatedSpriteRenderer, DrawOrder): an entity at the preset's position with a 16x16 hitbox,
// showing `sheet` (frameW x frameH; 0 = the sheet's only size).
Entity* Spawn_Basic(const EntityClass& cls, const MapEntity& preset, const char* sheet, int frameW = 16,
                    int frameH = 16, uint8_t parts = PART_VISUAL | PART_BRAIN);

// The entity classes (ent_gadgets.cpp, ...).
Entity* Spawn_Console(const MapEntity& preset, Entity& player);
Entity* Spawn_Gate(const MapEntity& preset, Entity& player);
Entity* Spawn_SageBlank(const MapEntity& preset, Entity& player);
