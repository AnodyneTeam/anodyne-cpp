// Player.cs, Broom.cs, Shadow.cs, Parabola_Thing.cs, Foot_Overlay.cs and PlayerReflection.cs on the entity system.
#include "player.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "entity_classes.h"
#include "game_state.h"
#include "sprite.h"

#include "tonc.h"

namespace
{
constexpr int kHitW = 10; // HITBOX_WIDTH / HEIGHT
constexpr int kHitH = 12;
constexpr int kDefaultYOffset = 4;
constexpr fx kWalkSpeed = Fx(70);
constexpr fx kBumpVelocity = Fx(50);
constexpr fx kConveyorVel = Fx(35);
constexpr fx kCornerAssist = Fx(30);
const fx kInvincibleMax = FxSec(0.8f);
const fx kBumpTimerMax = FxSec(0.2f);
const fx kFallTimerDefault = FxSec(0.016f * 8 + 0.001f);
const fx kActionLatencyMax = FxSec(0.24f);
const fx kJumpPeriod = FxSec(0.4f * 1.15f);
const fx kStepTimerMax = FxSec(0.5f);
constexpr int kJumpHeight = 24;

enum AnimState : uint8_t
{
    ANIM_DEFAULT,
    ANIM_ATK,
    ANIM_FALL,
    ANIM_DEAD,
    AS_IDLE,
    AS_WALK,
    AS_SLUMPED,
};

struct PlayerData final
{
    uint8_t mState; // PlayerState
    uint8_t mAnimState;
    int8_t mIdleTicks;
    bool mInvincible;
    bool mDontMove;
    bool mActionsDisabled;
    bool mSkipBroom;
    bool mJustLanded;
    bool mOnHole;
    bool mIsSlipping;
    bool mHasFallen;
    bool mJustFell;
    bool mOnConveyor;
    bool mSinking;
    bool mSlow; // slowMul = 0.5
    uint8_t mJumpStage; // 0: not started, 1: in the air, 2: landed (one more frame)
    fx mInvincibilityTime;
    fx mBumpTimer;
    fx mFallTimer;
    fx mFallPointX;
    fx mFallPointY;
    fx mGridEntranceX;
    fx mGridEntranceY;
    fx mAddVelX;
    fx mAddVelY;
    fx mActionLatency;
    fx mStepTimer;
    fx mJumpT; // Parabola_Thing.t
    fx mJumpStartOff;
    fx mJumpPeriod;
    int mJumpHeight;
    bool mJumpHasTarget; // AutoJump: the position moves from the start to the target over the jump
    fx mJumpBaseX;
    fx mJumpBaseY;
    fx mJumpTargetX;
    fx mJumpTargetY;
};

Entity* gPlayer = nullptr;
Entity* gBroom = nullptr;
Entity* gShadow = nullptr;
Entity* gFoot = nullptr;
Entity* gReflection = nullptr;
Entity* gBroomReflection = nullptr;
char gMapName[24];

PlayerData& P()
{
    return gPlayer->Data<PlayerData>();
}

// ---- Shadow (Shadow.cs, ShadowType.Normal) ---------------------------------------------------------------------
void ShadowUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    // Position = parent.Position - (offset - sprite size / 4): offset (3, -1), 16x16 player
    self.mT->mX = gPlayer->mT->mX + Fx(1);
    self.mT->mY = gPlayer->mT->mY + Fx(5);
}

const EntityClass kShadowClass = {
    "Shadow", ShadowUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// jumpHeight 0..1: how high up.
void UpdateShadow(fx jumpHeight)
{
    constexpr int kMaxFrame = 3;
    // (int)ceil(h * (maxFrame + 1)) - 1
    const fx scaled = jumpHeight * (kMaxFrame + 1);
    const int frame = static_cast<int>(ceilf(scaled.mV)) - 1;
    if (frame < 0)
    {
        gShadow->mV->mVisible = false;
    }
    else
    {
        gShadow->mV->mVisible = true;
        Ecs_SetFrame(*gShadow, frame);
    }
}

// ---- Player -----------------------------------------------------------------------------------------------------
void PlayerAnimationChanged(const char* name)
{
    if (!strcmp(name, "walk_l") || !strcmp(name, "attack_l"))
    {
        gPlayer->mV->mOffX = Fx(4);
    }
    else if (!strncmp(name, "walk_", 5) || !strncmp(name, "attack_", 7))
    {
        gPlayer->mV->mOffX = Fx(3);
    }
}

void PlayerPlay(const char* name, bool force = false)
{
    if (Ecs_Play(*gPlayer, name, force))
    {
        PlayerAnimationChanged(name);
    }
}

void PlayerPlayFacing(const char* prefix)
{
    static const char kSuffix[4] = { 'l', 'r', 'u', 'd' };
    char name[24];
    snprintf(name, sizeof(name), "%s_%c", prefix, kSuffix[gPlayer->mFacing & 3]);
    PlayerPlay(name);
}

void FacingFromVelocity()
{
    const Transform& t = *gPlayer->mT;
    if (t.mVelX == 0 && t.mVelY == 0)
    {
        return;
    }
    if (t.mVelY < 0)
    {
        gPlayer->mFacing = FACE_UP;
    }
    else if (t.mVelY > 0)
    {
        gPlayer->mFacing = FACE_DOWN;
    }
    else if (t.mVelX < 0)
    {
        gPlayer->mFacing = FACE_LEFT;
    }
    else
    {
        gPlayer->mFacing = FACE_RIGHT;
    }
}

bool Blocks(fx x, fx y, uint8_t side)
{
    return (Map_CollisionAllowAt(x, y) & side) != 0;
}

// Player.Set_init_vel: the d-pad to a velocity (Up beats Down, Left beats Right) plus the nudge around tile corners.
void SetInitVel(fx mul)
{
    Transform& t = *gPlayer->mT;
    PlayerData& d = P();
    if (gBroom->mExists)
    {
        t.mVelX = t.mVelY = 0;
        return;
    }
    const uint8_t touching = gPlayer->mB->mTouching;
    const fx x = t.mX;
    const fx y = t.mY;
    // (float %, as the C#'s Position.X % 16: 57.6 % 16 is 9.6, past the > 9 a whole pixel's 57 % 16 isn't)
    const float px = x.mV;
    const float py = y.mV;
    auto mod16 = [](float v) { return fmodf(v, 16.0f); };
    int vx = 0;
    int vy = 0;
    if (key_is_down(KEY_UP))
    {
        vy = -1;
        if (touching & TOUCH_UP)
        {
            const bool tl = Blocks(x, y - Fx(8), TOUCH_DOWN);
            const bool tr = Blocks(x + Fx(kHitW), y - Fx(8), TOUCH_DOWN);
            if (!tl && !tr)
            {
            }
            else if (mod16(px + kHitW) < 6 && !tl)
            {
                d.mAddVelX -= kCornerAssist;
            }
            else if (mod16(px) > 9 && !tr)
            {
                d.mAddVelX += kCornerAssist;
            }
        }
    }
    else if (key_is_down(KEY_DOWN))
    {
        vy = 1;
        if (touching & TOUCH_DOWN)
        {
            const bool bl = Blocks(x, y + Fx(kHitH + 8), TOUCH_UP);
            const bool br = Blocks(x + Fx(kHitW), y + Fx(kHitH + 8), TOUCH_UP);
            if (!bl && !br)
            {
            }
            else if (mod16(px + kHitW) < 6 && !bl)
            {
                d.mAddVelX -= kCornerAssist;
            }
            else if (mod16(px) > 9 && !br)
            {
                d.mAddVelX += kCornerAssist;
            }
        }
    }
    if (key_is_down(KEY_LEFT))
    {
        vx = -1;
        if (touching & TOUCH_LEFT)
        {
            const bool tl = Blocks(x - Fx(8), y, TOUCH_RIGHT);
            const bool bl = Blocks(x - Fx(8), y + Fx(kHitH), TOUCH_RIGHT);
            if (!tl && !bl)
            {
            }
            else if (mod16(py + kHitH) < 6 && !tl)
            {
                d.mAddVelY -= kCornerAssist;
            }
            else if (mod16(py) > 9 && !bl)
            {
                d.mAddVelY += kCornerAssist;
            }
        }
    }
    else if (key_is_down(KEY_RIGHT))
    {
        vx = 1;
        if (touching & TOUCH_RIGHT)
        {
            const bool tr = Blocks(x + Fx(kHitW + 8), y, TOUCH_LEFT);
            const bool br = Blocks(x + Fx(kHitW + 8), y + Fx(kHitH), TOUCH_LEFT);
            if (!tr && !br)
            {
            }
            else if (mod16(py + kHitH) < 6 && !tr)
            {
                d.mAddVelY -= kCornerAssist;
            }
            else if (mod16(py) > 9 && !br)
            {
                d.mAddVelY += kCornerAssist;
            }
        }
    }
    const fx speed = FxMul(mul, kWalkSpeed);
    t.mVelX = vx * speed;
    t.mVelY = vy * speed;
    if (vx != 0 && vy != 0)
    {
        t.mVelX = FxMul(t.mVelX, FxF(0.7f));
        t.mVelY = FxMul(t.mVelY, FxF(0.7f));
    }
}

void GroundMovement()
{
    PlayerData& d = P();
    Transform& t = *gPlayer->mT;
    SetInitVel(kFxOne);
    if (d.mBumpTimer > 0)
    {
        d.mBumpTimer -= kFxTick;
        if (d.mBumpTimer > 0)
        {
            const Facing f = static_cast<Facing>(gPlayer->mFacing);
            t.mVelX = -FacingX(f) * kBumpVelocity;
            t.mVelY = -FacingY(f) * kBumpVelocity;
        }
    }
    t.mVelX += d.mAddVelX;
    t.mVelY += d.mAddVelY;
    if (d.mSlow)
    {
        t.mVelX /= 2;
        t.mVelY /= 2;
    }
}

void SlippingLogic()
{
    PlayerData& d = P();
    if (!d.mOnHole)
    {
        d.mIsSlipping = false;
        return;
    }
    d.mFallTimer -= kFxTick;
    if (d.mFallTimer < 0)
    {
        Game_PlaySfx("fall_in_hole");
        d.mAnimState = ANIM_FALL;
        d.mHasFallen = true;
        d.mIsSlipping = false;
        d.mDontMove = true;
        gPlayer->mT->mX = d.mFallPointX + Fx(3);
        gPlayer->mT->mY = d.mFallPointY + Fx(5);
    }
}

void ResetAfterFalling()
{
    PlayerData& d = P();
    if (Ecs_Frame(*gPlayer) == 31)
    {
        gPlayer->mT->mX = d.mGridEntranceX;
        gPlayer->mT->mY = d.mGridEntranceY;
        d.mHasFallen = false;
        Ecs_Flicker(*gPlayer, FxSec(1));
        PlayerPlay("idle_d");
        Ecs_SetSolid(*gPlayer, false);
        d.mJustFell = true;
        d.mDontMove = false;
        d.mAnimState = AS_IDLE;
    }
}

void SinkingLogic()
{
    PlayerData& d = P();
    Visual& v = *gPlayer->mV;
    v.mYPush += FxStep(Fx(16) / 3);
    if (v.mYPush > Fx(16))
    {
        v.mYPush = 0;
        gPlayer->mT->mX = d.mGridEntranceX;
        gPlayer->mT->mY = d.mGridEntranceY;
        Player_ReceiveDamage(1, false);
    }
}

void BroomAttack();

void UpdateActions()
{
    PlayerData& d = P();
    if (d.mActionLatency > 0)
    {
        d.mActionLatency -= kFxTick;
    }
    if (d.mState != PS_AIR)
    {
        if (key_hit(KEY_A) && d.mActionLatency <= 0 && !d.mSkipBroom)
        {
            if (gGame.mEquippedBroom != BROOM_NONE && gGame.mEquippedBroom != BROOM_TRANSFORMER && !gBroom->mExists)
            {
                BroomAttack();
                d.mActionLatency = kActionLatencyMax;
                d.mAnimState = ANIM_ATK;
            }
        }
        d.mSkipBroom = false;

        if (key_hit(KEY_B) && !d.mSinking && gGame.mCanJump)
        {
            d.mState = PS_AIR;
            d.mJumpStage = 0;
            d.mJumpPeriod = kJumpPeriod;
            d.mJumpHeight = kJumpHeight;
            d.mJumpHasTarget = false;
            gShadow->mV->mVisible = true;
            gBroom->mExists = false;
            d.mIsSlipping = false;
        }
    }
}

void GroundAnimation()
{
    PlayerData& d = P();
    const Transform& t = *gPlayer->mT;
    const bool moving = t.mVelX != 0 || t.mVelY != 0;
    if (d.mBumpTimer > 0)
    {
        return; // no change of animation during a bump
    }
    if (d.mAnimState == ANIM_ATK && Ecs_AnimFinished(*gBroom))
    {
        d.mAnimState = AS_IDLE;
    }
    switch (d.mAnimState)
    {
    case ANIM_ATK:
        PlayerPlayFacing("attack");
        break;
    case ANIM_FALL:
        PlayerPlay("fall");
        break;
    case AS_IDLE:
        if (d.mIdleTicks > 0)
        {
            d.mIdleTicks--;
            break;
        }
        if (moving)
        {
            FacingFromVelocity();
            PlayerPlayFacing("walk");
            d.mAnimState = AS_WALK;
        }
        else
        {
            PlayerPlayFacing("idle");
        }
        break;
    case AS_WALK:
        if (d.mIdleTicks > 0)
        {
            d.mIdleTicks--;
        }
        if (!moving)
        {
            d.mAnimState = AS_IDLE;
            PlayerPlayFacing("idle");
        }
        else
        {
            FacingFromVelocity();
            PlayerPlayFacing("walk");
        }
        break;
    case AS_SLUMPED:
        PlayerPlay("slumped");
        if (moving)
        {
            d.mAnimState = AS_WALK;
            PlayerPlayFacing("walk");
        }
        break;
    default:
        break;
    }
}

// Player.JumpAnim + Parabola_Thing, one step per frame. Returns true once the jump is over.
bool JumpStep()
{
    PlayerData& d = P();
    Visual& v = *gPlayer->mV;
    if (d.mJumpStage == 0)
    {
        gBroom->mExists = false;
        Game_PlaySfx(d.mOnConveyor ? "puddle_up" : "player_jump_up");
        d.mJumpT = 0;
        d.mJumpStartOff = v.mOffY;
        d.mJumpBaseX = gPlayer->mT->mX;
        d.mJumpBaseY = gPlayer->mT->mY;
        d.mAnimState = AS_IDLE;
        PlayerPlayFacing("jump");
        d.mJumpStage = 1;
    }
    if (d.mJumpStage == 1)
    {
        d.mJumpT += kFxTick;
        if (d.mJumpT > d.mJumpPeriod)
        {
            if (d.mJumpHasTarget)
            {
                gPlayer->mT->mX = d.mJumpTargetX;
                gPlayer->mT->mY = d.mJumpTargetY;
            }
            v.mOffY = d.mJumpStartOff;
            UpdateShadow(0);
            Game_PlaySfx(d.mOnConveyor ? "puddle_down" : "player_jump_down");
            v.mOffY = Fx(kDefaultYOffset);
            d.mJumpStage = 2;
            return false;
        }
        v.mOffY = d.mJumpStartOff + ParabolaOffset(d.mJumpT, d.mJumpPeriod, d.mJumpHeight);
        // UpdateShadow(|0.5 - progress| * 2)
        fx progress = FxDiv(d.mJumpT, d.mJumpPeriod);
        progress = progress > kFxOne ? kFxOne : progress;
        if (d.mJumpHasTarget)
        {
            gPlayer->mT->mX = d.mJumpBaseX + FxMul(d.mJumpTargetX - d.mJumpBaseX, progress);
            gPlayer->mT->mY = d.mJumpBaseY + FxMul(d.mJumpTargetY - d.mJumpBaseY, progress);
        }
        const fx fromMiddle = kFxOne / 2 - progress;
        UpdateShadow((fromMiddle < 0 ? -fromMiddle : fromMiddle) * 2);
        return false;
    }
    d.mJustLanded = true;
    d.mState = PS_GROUND;
    Ecs_SetSolid(*gPlayer, true);
    gPlayer->mT->mRot = 0;
    gPlayer->mT->mAngVel = 0;
    d.mJumpStage = 0;
    return true;
}

void LadderLogic()
{
    PlayerData& d = P();
    Transform& t = *gPlayer->mT;
    t.mVelX = t.mVelY = 0;
    if (!d.mDontMove)
    {
        PlayerPlay("climb");
        if (key_is_down(KEY_UP))
        {
            t.mVelY = -FxMul(kWalkSpeed, FxF(0.7f));
        }
        else if (key_is_down(KEY_DOWN))
        {
            t.mVelY = FxMul(kWalkSpeed, FxF(0.7f));
        }
        else
        {
            PlayerPlay("idle_climb");
        }
        if (key_is_down(KEY_LEFT))
        {
            t.mVelX = -kWalkSpeed;
        }
        else if (key_is_down(KEY_RIGHT))
        {
            t.mVelX = kWalkSpeed;
        }
        d.mStepTimer -= kFxTick;
        if (d.mStepTimer < 0 && t.mVelY != 0)
        {
            d.mStepTimer = FxSec(0.2f);
            static const char* const kSteps[2] = { "ladder_step_1", "ladder_step_2" };
            Game_PlaySfxOneOf(kSteps, 2);
        }
    }
    else
    {
        PlayerPlay("idle_climb");
    }
    d.mState = PS_GROUND; // sticks again while still touching a ladder
    d.mAnimState = AS_IDLE;
}

void Movement()
{
    PlayerData& d = P();
    Transform& t = *gPlayer->mT;
    switch (d.mState)
    {
    case PS_GROUND:
        gShadow->mV->mVisible = false;
        GroundMovement();
        if (d.mDontMove)
        {
            t.mVelX = t.mVelY = 0;
        }
        if (d.mIsSlipping)
        {
            SlippingLogic();
        }
        else if (d.mHasFallen)
        {
            ResetAfterFalling();
        }
        if (!d.mDontMove)
        {
            if (d.mSinking)
            {
                SinkingLogic();
            }
            else
            {
                gPlayer->mV->mYPush = 0;
            }
        }
        d.mStepTimer -= kFxTick;
        if (d.mStepTimer < 0 && (t.mVelX != 0 || t.mVelY != 0) && d.mOnConveyor)
        {
            if (d.mSinking)
            {
                Game_PlaySfx("water_step");
                d.mStepTimer = kStepTimerMax;
            }
            else
            {
                Game_PlaySfx("puddle_step");
                d.mStepTimer = kStepTimerMax / 2;
            }
        }
        if (!d.mHasFallen && !d.mActionsDisabled)
        {
            UpdateActions();
        }
        GroundAnimation();
        break;
    case PS_AIR:
        if (d.mDontMove)
        {
            t.mVelX = t.mVelY = 0;
        }
        else
        {
            SetInitVel(FxF(0.83f));
            t.mVelX += d.mAddVelX;
            t.mVelY += d.mAddVelY;
            JumpStep();
        }
        break;
    case PS_AUTO_JUMP:
        JumpStep();
        break;
    case PS_LADDER:
        LadderLogic();
        break;
    default:
        break;
    }
}

void PlayerMapEvent(Entity& self, MapEvent ev, uint8_t dir, fx tileX, fx tileY)
{
    PlayerData& d = P();
    switch (ev)
    {
    case MAPEV_FALL:
        if (d.mState != PS_AIR && !d.mIsSlipping && !d.mHasFallen)
        {
            d.mIsSlipping = true;
            d.mFallTimer = d.mJustLanded ? -kFxOne : kFallTimerDefault;
        }
        d.mFallPointX = tileX;
        d.mFallPointY = tileY;
        d.mOnHole = true;
        break;
    case MAPEV_SLOW:
        d.mSlow = true;
        break;
    case MAPEV_PUDDLE:
    case MAPEV_REFLECTION:
        d.mOnConveyor = true;
        break;
    case MAPEV_LADDER:
        if (d.mState == PS_GROUND)
        {
            d.mState = PS_LADDER;
        }
        break;
    case MAPEV_CONVEYOR:
        d.mOnConveyor = true;
        if (d.mState != PS_AIR)
        {
            d.mAddVelX = dir == TOUCH_RIGHT ? kConveyorVel : (dir == TOUCH_LEFT ? -kConveyorVel : 0);
            d.mAddVelY = dir == TOUCH_DOWN ? kConveyorVel : (dir == TOUCH_UP ? -kConveyorVel : 0);
            d.mSlow = true;
            d.mSinking = true;
        }
        break;
    case MAPEV_SPIKE:
        if (d.mState != PS_AIR)
        {
            Player_ReceiveDamage(1, false);
        }
        break;
    default:
        break;
    }
    (void)self;
}

void PlayerUpdate(Entity& self)
{
    PlayerData& d = P();
    d.mJustLanded = false;
    if (d.mInvincibilityTime > 0)
    {
        d.mInvincibilityTime -= kFxTick;
        if (d.mInvincibilityTime <= 0)
        {
            d.mInvincible = false;
        }
    }
    if (!Ecs_Solid(self) && d.mJustFell)
    {
        Ecs_SetSolid(self, true);
        d.mJustFell = false;
    }
    Movement();
    d.mSlow = false; // slowTicks is never set before the Seer: slowMul goes back to 1 every frame
    d.mAddVelX = d.mAddVelY = 0;
    Ecs_BaseUpdate(self);
}

void PlayerPostUpdate(Entity&)
{
    PlayerData& d = P();
    d.mOnConveyor = false;
    d.mSinking = false;
    d.mOnHole = false;
}

const EntityClass kPlayerClass = {
    "Player", PlayerUpdate, PlayerPostUpdate, nullptr, nullptr, PlayerMapEvent, nullptr, nullptr,
    GROUP_PLAYER, 0, ECF_MAP_COLLISION,
};

// ---- Broom ------------------------------------------------------------------------------------------------------
struct BroomData final
{
    bool mBehindPlayer;
    bool mCarrying;     // Broom.dust != null (the entity itself may be gone: a screen change freed it)
    bool mJustReleased; // just_released_dust
    Entity* mDust;
    Entity* mCandidate; // pickup_candidate
};

BroomData& B()
{
    return gBroom->Data<BroomData>();
}

// Broom.DustDistance: furthest along the facing, then closest to that line.
fx DustDistance(const Entity& dust)
{
    const fx dx = Ecs_CenterX(dust) - Ecs_CenterX(*gPlayer);
    const fx dy = Ecs_CenterY(dust) - Ecs_CenterY(*gPlayer);
    const int fxDir = FacingX(static_cast<Facing>(gPlayer->mFacing));
    const int fyDir = FacingY(static_cast<Facing>(gPlayer->mFacing));
    const fx along = dx * fxDir + dy * fyDir;
    fx across = dx * fyDir + dy * fxDir;
    across = across < 0 ? -across : across;
    return 50 * along - across;
}

void BroomCollided(Entity&, Entity& other)
{
    BroomData& b = B();
    if (!b.mCarrying && !b.mJustReleased)
    {
        if (!b.mCandidate || DustDistance(*b.mCandidate) < DustDistance(other))
        {
            b.mCandidate = &other;
        }
    }
}

void BroomUpdatePos()
{
    Entity& b = *gBroom;
    const Transform& root = *gPlayer->mT;
    b.mFacing = gPlayer->mFacing;
    const int frame = Ecs_Frame(b);
    fx x = 0;
    fx y = 0;
    bool behind = false;
    switch (gPlayer->mFacing)
    {
    case FACE_LEFT:
        b.mT->mRot = 0;
        x = root.mX - Fx(14);
        y = root.mY;
        behind = true;
        x += frame == 0 ? Fx(10) : (frame == 1 ? Fx(6) : (frame == 2 ? -Fx(1) : 0));
        break;
    case FACE_RIGHT:
        b.mT->mRot = kFxPi;
        x = root.mX + Fx(gPlayer->mB->mW);
        y = root.mY - Fx(2);
        x -= frame == 0 ? Fx(12) : (frame == 1 ? Fx(8) : (frame == 2 ? Fx(1) : 0));
        break;
    case FACE_UP:
        b.mT->mRot = kFxPi / 2;
        x = root.mX - Fx(2);
        y = root.mY - Fx(16);
        behind = true;
        y += frame == 0 ? Fx(12) : (frame == 1 ? Fx(6) : (frame == 2 ? Fx(2) : 0));
        break;
    default:
        b.mT->mRot = kFxPi + kFxPi / 2;
        x = root.mX - Fx(6);
        y = root.mY + Fx(gPlayer->mB->mH);
        y -= frame == 0 ? Fx(8) : (frame == 1 ? Fx(5) : (frame == 2 ? Fx(2) : 0));
        break;
    }
    b.mT->mX = x;
    b.mT->mY = y;
    b.Data<BroomData>().mBehindPlayer = behind;
    b.mV->mLayerOffset = behind ? -1 : 1;
}

void BroomUpdate(Entity& self)
{
    BroomData& b = B();
    if (b.mCandidate)
    {
        b.mDust = b.mCandidate;
        b.mCarrying = true;
        b.mCandidate = nullptr;
        Ecs_Play(*b.mDust, "poof");
        if (Event_Get("SweptDust") == 0)
        {
            Event_Inc("SweptDust");
            Game_ShowDialogueFor("misc", "any", "dust", 0);
        }
    }
    if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
        if (b.mJustReleased)
        {
            b.mCarrying = false;
            b.mDust = nullptr;
            b.mJustReleased = false;
        }
    }
    else
    {
        BroomUpdatePos();
    }
    Ecs_BaseUpdate(self);
}

const EntityClass kBroomClass = {
    "Broom", BroomUpdate, nullptr, BroomCollided, nullptr, nullptr, nullptr, nullptr, GROUP_BROOM, GROUP_DUST, 0,
};

void BroomAttack()
{
    Entity& b = *gBroom;
    b.mExists = true;
    b.mFacing = gPlayer->mFacing;
    Ecs_Play(b, "stab", true);
    static const char* const kSwing[3] = { "swing_broom_1", "swing_broom_2", "swing_broom_3" };
    Game_PlaySfxOneOf(kSwing, 3);
    BroomUpdatePos();
    Ecs_FireEvent(GEV_BROOM_USED);

    BroomData& bd = B();
    if (bd.mCarrying)
    {
        // The tile in front of the player's centre.
        const int cx = FxFloor(Ecs_CenterX(*gPlayer)) / 16 + FacingX(static_cast<Facing>(gPlayer->mFacing));
        const int cy = FxFloor(Ecs_CenterY(*gPlayer)) / 16 + FacingY(static_cast<Facing>(gPlayer->mFacing));
        const fx x = Fx(cx * 16);
        const fx y = Fx(cy * 16);
        if (Map_CollisionAllowAt(x, y) == TOUCH_NONE)
        {
            if (!bd.mDust)
            {
                bd.mDust = Spawn_DustAt(x, y);
            }
            if (bd.mDust)
            {
                bd.mDust->mT->mX = bd.mDust->mT->mLastX = x;
                bd.mDust->mT->mY = bd.mDust->mT->mLastY = y;
                bd.mDust->mExists = true;
                Ecs_Play(*bd.mDust, "unpoof", true);
                bd.mJustReleased = true;
            }
        }
    }
}

// ---- Foot overlay ------------------------------------------------------------------------------------------------
struct FootData final
{
    bool mActivated;
    bool mGrass; // showing the grass sheet
};

bool IsWindmill()
{
    return strcmp(gMapName, "WINDMILL") == 0;
}

void FootUseWater()
{
    char name[48];
    snprintf(name, sizeof(name), "overlay_%s_water", gMapName);
    if (!Ecs_SetSheet(*gFoot, name, 24, 24))
    {
        Ecs_SetSheet(*gFoot, "overlay_water", 24, 24);
        gFoot->mV->mOffY = 0;
        gFoot->mV->mLayer = LAYER_FOOT_OVERLAY;
    }
    gFoot->Data<FootData>().mGrass = false;
    Ecs_Play(*gFoot, "water");
}

void FootMapEvent(Entity& self, MapEvent ev, uint8_t, fx, fx)
{
    FootData& d = self.Data<FootData>();
    if (ev == MAPEV_GRASS)
    {
        if (!d.mGrass)
        {
            char name[48];
            snprintf(name, sizeof(name), "overlay_%s_grass", gMapName);
            if (Ecs_SetSheet(self, name, 24, 24))
            {
                d.mGrass = true;
            }
            self.mV->mOffY = Fx(1);
            self.mV->mLayer = LAYER_FG_SPRITES;
        }
        d.mActivated = true;
        self.mV->mVisible = true;
        const Transform& t = *gPlayer->mT;
        Ecs_Play(self, (t.mVelX == 0 && t.mVelY == 0) ? "grass_stop" : "grass_go");
    }
    else if (ev == MAPEV_CONVEYOR || ev == MAPEV_PUDDLE || ev == MAPEV_REFLECTION)
    {
        if (d.mGrass)
        {
            FootUseWater();
        }
        d.mActivated = true;
        if (IsWindmill())
        {
            self.mV->mVisible = true;
        }
        else
        {
            Ecs_Flicker(self, FxSec(0.1f));
        }
    }
    // Fall: nothing
}

void FootUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (IsWindmill())
    {
        self.mV->mVisible = true;
    }
}

void FootPostUpdate(Entity& self)
{
    FootData& d = self.Data<FootData>();
    self.mT->mX = gPlayer->mT->mX - Fx(7) - (gPlayer->mFacing == FACE_RIGHT ? Fx(1) : 0);
    self.mT->mY = gPlayer->mT->mY - Fx(3);
    if (!d.mActivated || P().mState == PS_AIR)
    {
        self.mV->mVisible = false;
        self.mV->mFlickering = false;
    }
    d.mActivated = false;
}

const EntityClass kFootClass = {
    "FootOverlay", FootUpdate, FootPostUpdate, nullptr, nullptr, FootMapEvent, nullptr, nullptr, 0, 0,
    ECF_MAP_COLLISION,
};

// ---- Reflection --------------------------------------------------------------------------------------------------
struct ReflectionData final
{
    bool mSeen; // a reflection tile was under it in this frame's collisions
};

void ReflectionMapEvent(Entity& self, MapEvent ev, uint8_t, fx, fx)
{
    if (ev == MAPEV_REFLECTION)
    {
        self.Data<ReflectionData>().mSeen = true;
    }
}

void ReflectionUpdate(Entity& self)
{
    ReflectionData& d = self.Data<ReflectionData>();
    // Reflection() makes it visible for the frame it is drawn in.
    self.mV->mVisible = d.mSeen;
    d.mSeen = false;
    Ecs_BaseUpdate(self);
    self.mT->mX = gPlayer->mT->mX;
    self.mT->mY = gPlayer->mT->mY + Fx(7);
    self.mV->mOffX = gPlayer->mV->mOffX;
    self.mV->mOffY = -gPlayer->mV->mOffY;
    Ecs_SetFrame(self, Ecs_Frame(*gPlayer));

    Entity& br = *gBroomReflection;
    br.mV->mVisible = self.mV->mVisible;
    br.mExists = gBroom->mExists;
    if (br.mExists)
    {
        br.mT->mX = gBroom->mT->mX;
        br.mT->mY = gBroom->mT->mY + Fx(10);
        br.mV->mOffX = gBroom->mV->mOffX;
        br.mV->mOffY = gBroom->mV->mOffY;
        Ecs_SetFrame(br, Ecs_Frame(*gBroom));
        br.mV->mLayerOffset = gBroom->Data<BroomData>().mBehindPlayer ? -1 : 1;
        switch (gBroom->mFacing)
        {
        case FACE_RIGHT:
            br.mT->mRot = kFxPi;
            break;
        case FACE_UP:
            br.mT->mRot = kFxPi / 2;
            break;
        case FACE_DOWN:
            br.mT->mRot = kFxPi + kFxPi / 2;
            break;
        default:
            br.mT->mRot = 0;
            break;
        }
    }
}

const EntityClass kReflectionClass = {
    "PlayerReflection", ReflectionUpdate, nullptr, nullptr, nullptr, ReflectionMapEvent, nullptr, nullptr, 0, 0,
    ECF_MAP_COLLISION,
};

const EntityClass kBroomReflectionClass = {
    "BroomReflection", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

Entity* CreateOwned(const EntityClass& cls, uint8_t parts)
{
    Entity* e = Ecs_Create(cls, 0, 0, parts, nullptr, gPlayer);
    e->mPlayerOwned = true;
    return e;
}
} // namespace

Entity* Player_Create()
{
    gPlayer = Ecs_Create(kPlayerClass, 0, 0, PART_VISUAL | PART_BRAIN);
    gPlayer->mPlayerOwned = true;
    gPlayer->mB->mW = kHitW;
    gPlayer->mB->mH = kHitH;
    Ecs_SetSheet(*gPlayer, "young_player", 16, 16);
    gPlayer->mV->mOffX = Fx(3);
    gPlayer->mV->mOffY = Fx(kDefaultYOffset);
    gPlayer->mFacing = FACE_UP;
    PlayerPlay("idle_u");
    P().mAnimState = AS_IDLE;

    // Created before the player's own sub-entities' update order matters: broom, transformer (not yet), shadow,
    // foot overlay, reflection.
    gBroom = CreateOwned(kBroomClass, PART_VISUAL | PART_BRAIN);
    Ecs_SetSheet(*gBroom, "broom", 16, 16);
    gBroom->mB->mW = gBroom->mB->mH = 16;
    gBroom->mB->mImmovable = true;
    gBroom->mV->mLayerRef = gPlayer;
    gBroom->mV->mLayerOffset = 1;
    gBroom->mExists = false;

    gShadow = CreateOwned(kShadowClass, PART_VISUAL);
    Ecs_SetSheet(*gShadow, "8x8_shadow", 8, 8);
    gShadow->mB->mW = gShadow->mB->mH = 8;
    gShadow->mV->mLayer = LAYER_SHADOWS;
    gShadow->mV->mVisible = false;
    Ecs_SetFrame(*gShadow, 0);

    gFoot = CreateOwned(kFootClass, PART_VISUAL | PART_BRAIN);
    Ecs_SetSheet(*gFoot, "overlay_water", 24, 24);
    gFoot->mB->mW = gFoot->mB->mH = 24;
    gFoot->mV->mLayer = LAYER_FOOT_OVERLAY;
    gFoot->mV->mVisible = false;

    gReflection = CreateOwned(kReflectionClass, PART_VISUAL | PART_BRAIN);
    Ecs_SetSheet(*gReflection, "young_player_reflection", 16, 16);
    gReflection->mB->mW = gReflection->mB->mH = 16;
    gReflection->mV->mLayer = LAYER_PLAYER_REFLECTION;
    Ecs_SetFrame(*gReflection, 0);

    gBroomReflection = CreateOwned(kBroomReflectionClass, PART_VISUAL);
    Ecs_SetSheet(*gBroomReflection, "broom_reflection", 16, 16);
    gBroomReflection->mB->mW = gBroomReflection->mB->mH = 16;
    gBroomReflection->mV->mLayerRef = gReflection;
    gBroomReflection->mV->mLayerOffset = 1;
    gBroomReflection->mExists = false;
    Ecs_SetFrame(*gBroomReflection, 0);
    return gPlayer;
}

Entity* Player_Entity()
{
    return gPlayer;
}

void Player_BeIdle()
{
    PlayerData& d = P();
    d.mAnimState = AS_IDLE;
    d.mIdleTicks = 5;
    PlayerPlayFacing("idle");
}

void Player_Reset(const char* mapName)
{
    strncpy(gMapName, mapName ? mapName : "", sizeof(gMapName) - 1);
    gMapName[sizeof(gMapName) - 1] = '\0';
    PlayerData& d = P();
    Player_BeIdle();
    FootUseWater();
    gPlayer->mT->mVelX = gPlayer->mT->mVelY = 0;
    d.mState = PS_GROUND;
    gPlayer->mV->mOffX = Fx(3);
    gPlayer->mV->mOffY = Fx(kDefaultYOffset);
    gPlayer->mV->mYPush = 0;
    gBroom->mExists = false;
    d.mActionsDisabled = false;
    Ecs_SetSolid(*gPlayer, true);
    d.mIsSlipping = false;
    d.mHasFallen = false;
    d.mJustFell = false;
    d.mSinking = false;
    d.mJustLanded = false;
    d.mDontMove = false;
    d.mSlow = false;
    d.mJumpStage = 0;
}

void Player_PlaceAt(fx x, fx y, int facing)
{
    Transform& t = *gPlayer->mT;
    t.mX = t.mLastX = x;
    t.mY = t.mLastY = y;
    if (facing >= 0 && facing <= 3)
    {
        gPlayer->mFacing = static_cast<uint8_t>(facing);
    }
    Player_SetGridEntrance();
}

void Player_SetGridEntrance()
{
    P().mGridEntranceX = gPlayer->mT->mX;
    P().mGridEntranceY = gPlayer->mT->mY;
}

void Player_SetDontMove(bool on)
{
    P().mDontMove = on;
}

void Player_SetActionsDisabled(bool on)
{
    P().mActionsDisabled = on;
}

void Player_SetInvincible(bool on)
{
    P().mInvincible = on;
}

void Player_SetSkipBroom()
{
    P().mSkipBroom = true;
}

PlayerState Player_State()
{
    return static_cast<PlayerState>(P().mState);
}

bool Player_SkipBroom()
{
    return P().mSkipBroom;
}

bool Player_BroomOut()
{
    return gBroom->mExists;
}

bool Player_HasFallen()
{
    return P().mHasFallen;
}

void Player_GetProbe(PlayerProbe& out)
{
    const PlayerData& d = P();
    out.mState = d.mState;
    out.mAnimState = d.mAnimState;
    out.mDontMove = d.mDontMove;
    out.mOnConveyor = d.mOnConveyor;
    out.mSlow = d.mSlow;
    out.mAddVelX = d.mAddVelX;
    out.mAddVelY = d.mAddVelY;
    out.mBroomOut = gBroom->mExists;
    out.mBroomX = FxTrunc(gBroom->mT->mX);
    out.mBroomY = FxTrunc(gBroom->mT->mY);
    out.mBroomW = gBroom->mB->mW;
    out.mBroomH = gBroom->mB->mH;
    out.mJustReleasedDust = B().mJustReleased;
}

void Player_AutoJump(fx period, fx targetX, fx targetY, int height)
{
    PlayerData& d = P();
    d.mJumpPeriod = period;
    d.mJumpHeight = height;
    d.mJumpHasTarget = true;
    d.mJumpTargetX = targetX;
    d.mJumpTargetY = targetY;
    d.mJumpStage = 0;
    d.mState = PS_AUTO_JUMP;
    Ecs_SetSolid(*gPlayer, false);
    gPlayer->mT->mVelX = gPlayer->mT->mVelY = 0;
    gPlayer->mV->mYPush = 0;
    gShadow->mV->mVisible = true;
}

bool Player_JustLanded()
{
    return P().mJustLanded;
}

void Player_SetFacing(Facing f)
{
    gPlayer->mFacing = f;
}

Entity* Player_BroomDust()
{
    return B().mDust;
}

void Player_DropBroomDust()
{
    BroomData& b = B();
    b.mDust = nullptr;
    b.mCandidate = nullptr;
    b.mCarrying = false;
    b.mJustReleased = false;
}

void Player_DustMerged()
{
    B().mJustReleased = false;
}

void Player_DustFreed(Entity& dust)
{
    BroomData& b = B();
    if (b.mDust == &dust)
    {
        b.mDust = nullptr; // still carried
    }
    if (b.mCandidate == &dust)
    {
        b.mCandidate = nullptr;
    }
}

void Player_Fall(fx x, fx y)
{
    PlayerMapEvent(*gPlayer, MAPEV_FALL, 0, x, y);
}

Facing Player_Facing()
{
    return static_cast<Facing>(gPlayer->mFacing);
}

void Player_ReceiveDamage(int amount, bool knockback, bool playSound)
{
    PlayerData& d = P();
    if (d.mInvincible || d.mHasFallen)
    {
        return;
    }
    GameState_SetHealth(gGame.mCurHealth - amount);
    if (playSound)
    {
        Game_PlaySfx("player_hit_1");
    }
    if (gGame.mCurHealth > 0)
    {
        d.mInvincible = true;
        d.mInvincibilityTime = kInvincibleMax;
        Ecs_Flicker(*gPlayer, kInvincibleMax);
        if (knockback)
        {
            d.mBumpTimer = kBumpTimerMax;
        }
    }
}

void Player_Update()
{
    if (gPlayer->mExists)
    {
        Ecs_UpdateOne(*gPlayer);
    }
}
