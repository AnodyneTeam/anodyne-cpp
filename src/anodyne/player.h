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
// The grid entrance (where falling or drowning respawns) becomes this spot. keepLast: PlayState.Warp only sets
// Position, so the first collisions there still see the old lastPosition.
void Player_PlaceAt(fx x, fx y, int facing, bool keepLast = false);
void Player_SetGridEntrance();
// Player.grid_entrance = (x, y): where falling or drowning on this screen respawns (bosses set their own).
void Player_SetGridEntranceAt(fx x, fx y);

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
// rotationSpeed: the player spins at that many radians a second until it lands.
void Player_AutoJump(fx period, fx targetX, fx targetY, int height = 24, fx rotationSpeed = 0);
bool Player_JustLanded();
void Player_SetFacing(Facing f);
// Stops the player and plays "slumped" (ANIM_STATE as_slumped).
void Player_Slump();
// The broom's hitbox (where it last was when it's not out).
Rect Player_BroomHitbox();

// The broom's dust (Broom.dust): the dust entity carried or just put down (nullptr if none or freed).
Entity* Player_BroomDust();
// PlayState screen transition: broom.dust = null (a carried dust is gone).
void Player_LoseBroomDust();
// PlayState.Refreshes after the equipped broom changed: Broom.UpdateBroomType.
void Player_UpdateBroomType();
// slowMul = mul for the next `ticks` updates and the one after (a Person in the way, an eye boss splash).
void Player_SetSlowFor(int ticks, float mul = 0.5f);
// Player.invincible (just hurt).
bool Player_Invincible();
// Player.reversed = true (a GasGuy's gas): the d-pad is backwards until 0.9 s have passed.
// Player.Dash (a DashPad).
void Player_Dash(Facing facing);
void Player_Reverse();
// Player.offset.Y (the height drawn above the shadow).
fx Player_OffsetY();
// FallDoor.Collided: the next map drops the player in from above (enter_fall); smack: it lands slumped (fall_smack).
void Player_EnterFall(bool smack);
// Player.DontFall: a platform under the player keeps it from slipping into a hole.
void Player_DontFall();
// Player.additionalVel += (x, y) (a moving platform carrying the player).
void Player_AddAdditionalVel(fx x, fx y);
// Player.additionalVel.X / .Y alone (a SteamPipe blowing).
void Player_SetAdditionalVelX(fx x);
void Player_SetAdditionalVelY(fx y);
// Broom.dust != null: the broom holds a dust (even one left on an old screen).
bool Player_CarryingDust();
// Player.additionalVel = (x, y) (a Mover pushing the player).
void Player_SetAdditionalVel(fx x, fx y);
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
