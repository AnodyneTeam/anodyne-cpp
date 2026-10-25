#pragma once

#include "ecs.h"

// The player (Player.cs) and the entities that belong to it: the broom (Broom.cs), its shadow, the foot overlay
// (Foot_Overlay.cs) and the reflection (PlayerReflection.cs). All of them are player-owned entities: they stay in
// the entity list across screens and maps.

enum PlayerState : uint8_t
{
    PS_GROUND,
    PS_AIR,
    PS_AUTO_JUMP,
    PS_ENTER_FALL,
    PS_LADDER,
};

// Creates the player and its sub-entities (once, after Ecs_Init).
Entity* Player_Create();
Entity* Player_Entity();

// Player.Reset: back to standing idle on the ground, for a new map (`mapName` picks the foot overlay's sheets).
void Player_Reset(const char* mapName);
// Puts the hitbox's top-left at (x, y) map pixels; facing 0-3 is Facing (LEFT, RIGHT, UP, DOWN), other = keep.
// The grid entrance (where falling or drowning respawns) becomes this spot.
void Player_PlaceAt(fx x, fx y, int facing);
void Player_SetGridEntrance();

// The flags PlayState sets on the player around transitions, warps and dialogue.
void Player_SetDontMove(bool on);
void Player_SetActionsDisabled(bool on);
void Player_SetInvincible(bool on);
void Player_SetSkipBroom();
void Player_BeIdle();
PlayerState Player_State();
bool Player_SkipBroom();
bool Player_BroomOut();
bool Player_HasFallen();

// Player.AutoJump(period, target, height): a jump that carries the player to (targetX, targetY).
void Player_AutoJump(fx periodFrames, fx targetX, fx targetY, int height = 24);
bool Player_JustLanded();
void Player_SetFacing(Facing f);

// The broom's dust (Broom.dust): the dust entity carried or just put down (nullptr if none or freed).
Entity* Player_BroomDust();
// broom.dust = null: whatever the broom was carrying is gone.
void Player_DropBroomDust();
// Dust.Collided on another dust: the one just put down merged back (just_released_dust = false).
void Player_DustMerged();
// A dust entity is being freed: the broom forgets the pointer (keeping what it carries).
void Player_DustFreed(Entity& dust);

// Player.Fall(fallPoint): a hole (entity) under the player, its top-left at (x, y).
void Player_Fall(fx x, fx y);
// The player's facing (Facing).
Facing Player_Facing();

// Player.ReceiveDamage.
void Player_ReceiveDamage(int amount, bool knockback = true, bool playSound = true);

// The player's Update + PostUpdate (PlayState runs it after every other entity).
void Player_Update();
