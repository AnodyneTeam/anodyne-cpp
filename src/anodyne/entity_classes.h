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
// Dust.ON_CONVEYOR (the only class in GROUP_DUST)
bool Dust_OnConveyor(Entity& dust);
// Dust.IS_RAFT: the player rides it over water.
void Dust_SetRaft(Entity& dust, bool raft);
bool Dust_IsRaft(Entity& dust);
// Moves Player.follower (and its dust) to the end of the update order.
void Miao_MoveFollowerLast();
// A CardTreasure for `card` spawned at (x, y) and given (GetTreasure).
void Treasure_GiveCard(fx x, fx y, int card);
// WhirlPool.DoTransition: the Fisherman fell in.
void WhirlPool_DoTransition(Entity& pool);
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
Entity* Spawn_BurstPlant(const MapEntity& preset, Entity& player);
Entity* Spawn_Frog(const MapEntity& preset, Entity& player);
Entity* Spawn_DashTrap(const MapEntity& preset, Entity& player);
Entity* Spawn_FourShooter(const MapEntity& preset, Entity& player);
Entity* Spawn_Slasher(const MapEntity& preset, Entity& player);
Entity* Spawn_Fisherman(const MapEntity& preset, Entity& player);
Entity* Spawn_RedWalker(const MapEntity& preset, Entity& player);
Entity* Spawn_RedseaNpc(const MapEntity& preset, Entity& player);
Entity* Spawn_Sign(const MapEntity& preset, Entity& player);
Entity* Spawn_RedCaveEntrance(const MapEntity& preset, Entity& player);
Entity* Spawn_RedPillar(const MapEntity& preset, Entity& player);
Entity* Spawn_Mover(const MapEntity& preset, Entity& player);
Entity* Spawn_OnOffLaser(const MapEntity& preset, Entity& player);
Entity* Spawn_Rotator(const MapEntity& preset, Entity& player);
Entity* Spawn_Door(const MapEntity& preset, Entity& player);
// Shieldy.ReceiveDamage (a laser hit).
void Shieldy_ReceiveDamage(Entity& shieldy, int damage);

// NPCs and decorations (ent_npcs.cpp).
Entity* Spawn_SageNexus(const MapEntity& preset, Entity& player);
Entity* Spawn_SageOverworld(const MapEntity& preset, Entity& player);
Entity* Spawn_SageBedroom(const MapEntity& preset, Entity& player);
Entity* Spawn_SageRedCave(const MapEntity& preset, Entity& player);
Entity* Spawn_SageCrowd(const MapEntity& preset, Entity& player);
Entity* Spawn_BioFilm(const MapEntity& preset, Entity& player);
Entity* Spawn_WindmillConsole(const MapEntity& preset, Entity& player);
Entity* Spawn_SuburbWalker(const MapEntity& preset, Entity& player);
Entity* Spawn_Rat(const MapEntity& preset, Entity& player);
Entity* Spawn_Silverfish(const MapEntity& preset, Entity& player);
Entity* Spawn_GasGuy(const MapEntity& preset, Entity& player);
Entity* Spawn_TeleGuy(const MapEntity& preset, Entity& player);
Entity* Spawn_Rail(const MapEntity& preset, Entity& player);
Entity* Spawn_EasterEgg(const MapEntity& preset, Entity& player);
Entity* Spawn_SplitBoss(const MapEntity& preset, Entity& player);
Entity* Spawn_Dustmaid(const MapEntity& preset, Entity& player);
Entity* Spawn_SteamPipe(const MapEntity& preset, Entity& player);
Entity* Spawn_EyebossPreview(const MapEntity& preset, Entity& player);
Entity* Spawn_HotelGuy(const MapEntity& preset, Entity& player);
Entity* Spawn_Elevator(const MapEntity& preset, Entity& player);
Entity* Spawn_EyeBossWater(const MapEntity& preset, Entity& player);
Entity* Spawn_EyeBossLand(const MapEntity& preset, Entity& player);
Entity* Spawn_SpaceFace(const MapEntity& preset, Entity& player);
Entity* Spawn_SpaceNpc(const MapEntity& preset, Entity& player);
Entity* Spawn_Chaser(const MapEntity& preset, Entity& player);
Entity* Spawn_CliffDog(const MapEntity& preset, Entity& player);
Entity* Spawn_CellBody(const MapEntity& preset, Entity& player);
Entity* Spawn_FirePillar(const MapEntity& preset, Entity& player);
Entity* Spawn_Contort(const MapEntity& preset, Entity& player);
Entity* Spawn_Lion(const MapEntity& preset, Entity& player);
Entity* Spawn_ArthurDanger(const MapEntity& preset, Entity& player);
Entity* Spawn_JavieraDanger(const MapEntity& preset, Entity& player);
Entity* Spawn_ArthurDead(const MapEntity& preset, Entity& player);
Entity* Spawn_JavieraDead(const MapEntity& preset, Entity& player);
Entity* Spawn_CircusFolks(const MapEntity& preset, Entity& player);
Entity* Spawn_DashPad(const MapEntity& preset, Entity& player);
Entity* Spawn_SuburbKiller(const MapEntity& preset, Entity& player);
Entity* Spawn_SuburbBlocker(const MapEntity& preset, Entity& player);
Entity* Spawn_SuburbIndoors(const MapEntity& preset, Entity& player);
Entity* Spawn_SuburbQuest(const MapEntity& preset, Entity& player);
// GlobalState.SpawnEntity(new DoorToggle(position, w, h)).
void Spawn_DoorToggle(fx x, fx y, int w, int h);
Entity* Spawn_Bunny(const MapEntity& preset, Entity& player);
Entity* Spawn_BunnyRun(const MapEntity& preset, Entity& player);
Entity* Spawn_Rock(const MapEntity& preset, Entity& player);
Entity* Spawn_Statue(const MapEntity& preset, Entity& player);
Entity* Spawn_Sadbro(const MapEntity& preset, Entity& player);
Entity* Spawn_DungeonStatue(const MapEntity& preset, Entity& player);
Entity* Spawn_EyeLight(const MapEntity& preset, Entity& player);
Entity* Spawn_BedroomBounceLight(const MapEntity& preset, Entity& player);
Entity* Spawn_Seer(const MapEntity& preset, Entity& player);
Entity* Spawn_RedBoss(const MapEntity& preset, Entity& player);
Entity* Spawn_WallBoss(const MapEntity& preset, Entity& player);
Entity* Spawn_Person(const MapEntity& preset, Entity& player);
Entity* Spawn_SpikeRoller(const MapEntity& preset, Entity& player);
Entity* Spawn_Propelled(const MapEntity& preset, Entity& player);
Entity* Spawn_StopMarker(const MapEntity& preset, Entity& player);
Entity* Spawn_FollowerBro(const MapEntity& preset, Entity& player);
Entity* Spawn_HealthCicada(const MapEntity& preset, Entity& player);
Entity* Spawn_BigKey(const MapEntity& preset, Entity& player);
Entity* Spawn_MitraOverworld(const MapEntity& preset, Entity& player);
Entity* Spawn_MitraFields(const MapEntity& preset, Entity& player);
Entity* Spawn_MiaoXiao(const MapEntity& preset, Entity& player);
Entity* Spawn_OutsideMonster(const MapEntity& preset, Entity& player);
Entity* Spawn_SwitchPillar(const MapEntity& preset, Entity& player);
Entity* Spawn_PillarSwitch(const MapEntity& preset, Entity& player);
Entity* Spawn_InsideMonster(const MapEntity& preset, Entity& player);
Entity* Spawn_ShopKeep(const MapEntity& preset, Entity& player);

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
