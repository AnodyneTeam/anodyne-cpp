#pragma once

#include "ecs.h"
#include "game_state.h"
#include "world.h"

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
Entity* Spawn_Treasure(const MapEntity& preset, Entity& player);
Entity* Spawn_Button(const MapEntity& preset, Entity& player);
Entity* Spawn_SolidTile(const MapEntity& preset, Entity& player);
Entity* Spawn_Blocker(const MapEntity& preset, Entity& player);
Entity* Spawn_VBlocker(const MapEntity& preset, Entity& player);
Entity* Spawn_Dust(const MapEntity& preset, Entity& player);
Entity* Spawn_CrackedTile(const MapEntity& preset, Entity& player);
Entity* Spawn_Hole(const MapEntity& preset, Entity& player);
Entity* Spawn_SmallKeyGate(const MapEntity& preset, Entity& player);
Entity* Spawn_CardGate(const MapEntity& preset, Entity& player);
Entity* Spawn_BigKeyGate(const MapEntity& preset, Entity& player);
Entity* Spawn_Key(const MapEntity& preset, Entity& player);
Entity* Spawn_HealthEntity(const MapEntity& preset, Entity& player);
Entity* Spawn_Checkpoint(const MapEntity& preset, Entity& player);
Entity* Spawn_SpringPad(const MapEntity& preset, Entity& player);
Entity* Spawn_JumpTrigger(const MapEntity& preset, Entity& player);
Entity* Spawn_DungeonEntrance(const MapEntity& preset, Entity& player);
Entity* Spawn_DungeonEntranceRef(const MapEntity& preset, Entity& player);
Entity* Spawn_BossKeyReset(const MapEntity& preset, Entity& player);

// Run-time entities other classes create.
// A dust put down by the broom after its own entity went with an old screen.
Entity* Spawn_DustAt(fx x, fx y);
// HealthDropper.Die: a health pickup (big: 3 hearts).
Entity* Spawn_HealthDrop(fx x, fx y, bool big);
// new Explosion(parent).
Entity* Spawn_Explosion(const Entity& parent);

// Enemies (ent_enemies.cpp).
Entity* Spawn_Slime(const MapEntity& preset, Entity& player);
Entity* Spawn_Annoyer(const MapEntity& preset, Entity& player);
Entity* Spawn_PewLaser(const MapEntity& preset, Entity& player);
Entity* Spawn_Shieldy(const MapEntity& preset, Entity& player);
Entity* Spawn_Dog(const MapEntity& preset, Entity& player);
Entity* Spawn_Rotator(const MapEntity& preset, Entity& player);
Entity* Spawn_Door(const MapEntity& preset, Entity& player);
// Shieldy.ReceiveDamage (a laser hit).
void Shieldy_ReceiveDamage(Entity& shieldy, int damage);

// NPCs and decorations (ent_npcs.cpp).
Entity* Spawn_SageNexus(const MapEntity& preset, Entity& player);
Entity* Spawn_SageOverworld(const MapEntity& preset, Entity& player);
Entity* Spawn_SageBedroom(const MapEntity& preset, Entity& player);
Entity* Spawn_Rock(const MapEntity& preset, Entity& player);
Entity* Spawn_Statue(const MapEntity& preset, Entity& player);
Entity* Spawn_Sadbro(const MapEntity& preset, Entity& player);
Entity* Spawn_DungeonStatue(const MapEntity& preset, Entity& player);
Entity* Spawn_EyeLight(const MapEntity& preset, Entity& player);
Entity* Spawn_BedroomBounceLight(const MapEntity& preset, Entity& player);
Entity* Spawn_Seer(const MapEntity& preset, Entity& player);
Entity* Spawn_FollowerBro(const MapEntity& preset, Entity& player);
Entity* Spawn_HealthCicada(const MapEntity& preset, Entity& player);
Entity* Spawn_BigKey(const MapEntity& preset, Entity& player);
Entity* Spawn_MitraOverworld(const MapEntity& preset, Entity& player);
Entity* Spawn_MitraFields(const MapEntity& preset, Entity& player);

// For the bot link (game_probe.cpp): the C# fields of an entity's class that the C# bot reads and the port keeps in its
// brain, written as JSON members ("name":value,...; C# names, float fields as 16.16). Returns the length, 0 for none.
int Enemies_ProbeFields(const Entity& e, char* buf, int cap);
// For the bot link: the entity's C# state machine as the C# bot reads it (the active states' type names from the top
// down, e.g. "ActiveState/TimerState"), or nullptr when the port doesn't report one for its class.
// The C# class of a Door entity (one port class plays them all), nullptr for other entities.
const char* Gadgets_ProbeName(const Entity& e);
// The C# class of a Sage (SageNexus, SageOverworld...), nullptr for other entities.
const char* Npcs_ProbeName(const Entity& e);
const char* Enemies_ProbeFsm(const Entity& e);
