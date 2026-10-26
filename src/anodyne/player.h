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

// What the bot link reports of the player (Player.cs fields the C# bot reads).
struct PlayerProbe final
{
    int mState;     // PlayerState
    int mAnimState; // PlayerAnimState order (ANIM_DEFAULT, ANIM_ATK, ANIM_FALL, ANIM_DEAD, idle, walk, slumped)
    bool mDontMove;
    bool mOnConveyor;
    bool mSlow;
    fx mAddVelX;
    fx mAddVelY;
    bool mBroomOut; // the broom's hitbox, while it exists (a swing)
    int mBroomX;
    int mBroomY;
    int mBroomW;
    int mBroomH;
    bool mJustReleasedDust;
};
void Player_GetProbe(PlayerProbe& out);

// Player.AutoJump(period, target, height): a jump that carries the player to (targetX, targetY).
void Player_AutoJump(fx period, fx targetX, fx targetY, int height = 24);
bool Player_JustLanded();
void Player_SetFacing(Facing f);

// The broom's dust (Broom.dust): the dust entity carried or just put down (nullptr if none or freed).
Entity* Player_BroomDust();
// PlayState screen transition: broom.dust = null (a carried dust is gone).
void Player_LoseBroomDust();
// PlayState.Refreshes after the equipped broom changed: Broom.UpdateBroomType.
void Player_UpdateBroomType();
// Player.raft: the dust ridden over water, or nullptr.
Entity* Player_Raft();
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
