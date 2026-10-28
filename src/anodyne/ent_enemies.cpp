// Enemies (AnodyneSharp Entities/Enemy): the ones met before the first boss.
#include "entity_classes.h"

#include "dialog.h"
#include "entities.h"
#include "player.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
bool IsPlayer(const Entity& e)
{
    return &e == World_Player();
}

bool IsBroom(const Entity& e)
{
    return (e.mClass->mGroups & GROUP_BROOM) != 0;
}

// HealthDropper.Die: the preset stays dead, and maybe a heart drops where it was (returned).
Entity* Die(Entity& self, fx dropChance, bool big = false)
{
    if (self.mPreset)
    {
        Preset_SetAlive(*self.mPreset, false);
    }
    self.mExists = false;
    if (gSettings.mGuaranteedHealth || Rng_Unit() < dropChance)
    {
        // (the C#'s pickup is the dropper's sub-entity, just after it in the lists)
        if (Entity* drop = Spawn_HealthDrop(self.mT->mX, self.mT->mY, big))
        {
            Ecs_MoveAfter(*drop, self);
            return drop;
        }
    }
    return nullptr;
}

// Parabola_Thing.Tick (from offset 0): the offset that lifts a sprite `height` px at the middle of `period` seconds;
// true when done.
bool ParabolaTick(fx& t, fx period, int height, fx& offY)
{
    t += kFxTick;
    if (t > period)
    {
        offY = 0;
        return true;
    }
    offY = ParabolaOffset(t, period, height);
    return false;
}

// ---- Slime (Slime.cs) -------------------------------------------------------------------------------------------
enum SlimeState : uint8_t
{
    SLIME_MOVE,
    SLIME_HURT,
    SLIME_DYING,
};

enum SlimeTimer : uint8_t
{
    SLIME_MOVE_TIMER,
    SLIME_SHOOT_TIMER,
};

constexpr int kGooPool = 8;
constexpr int kSlimeBulletPool = 4;

struct SlimeData final
{
    uint8_t mState;
    bool mBullet;
    bool mSoundSync;
    int mHealth;
    fx mSpeed;
    TimerState<3> mMove; // MoveTimer, and a ShootTimer for each time in Move
    uint32_t mExitFrame;
    // The EntityPools' entities are made with the slime, each picking its anim's framerate then.
    uint8_t mGooRate[kGooPool];
    uint8_t mBulletRate[kSlimeBulletPool];
};

// A pooled entity: its slot in the slime's EntityPool.
struct PoolData final
{
    uint8_t mSlot;
    bool mSplash;
    fx mT;
    fx mPeriod;
};

void GooUpdate(Entity& self)
{
    PoolData& d = self.Data<PoolData>();
    Ecs_BaseUpdate(self);
    if (!d.mSplash)
    {
        if (ParabolaTick(d.mT, d.mPeriod, 16, self.mV->mOffY))
        {
            d.mSplash = true;
            Game_PlaySfx("slime_splash");
            Ecs_Play(self, "splash");
            self.mT->mVelX = self.mT->mVelY = 0;
        }
    }
    else
    {
        self.mV->mOpacity -= FxF(0.05f);
        if (self.mV->mOpacity <= 0)
        {
            self.mExists = false;
        }
    }
}

const EntityClass kGoo = {
    "SlimeGoo", GooUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, ECF_MAP_COLLISION,
};

void SlimeBulletUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    self.mV->mOpacity -= FxStep(FxF(0.39f));
    if (self.mB->mTouching != TOUCH_NONE || self.mV->mOpacity < FxF(0.3f))
    {
        self.mExists = false;
    }
}

void SlimeBulletCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && Player_State() != PS_AIR)
    {
        Player_ReceiveDamage(1);
        self.mExists = false;
    }
}

const EntityClass kSlimeBullet = {
    "SlimeBullet", SlimeBulletUpdate, nullptr, SlimeBulletCollided, nullptr, nullptr, nullptr, nullptr,
    0,             GROUP_PLAYER,      ECF_MAP_COLLISION,
};

// EntityPool.Spawn: the first slot whose entity doesn't exist (-1: all in use). T is the pool entity's data, with its
// mSlot.
template <typename T>
int FreePoolSlot(const Entity& owner, const EntityClass& cls, int slots)
{
    uint32_t used = 0;
    for (Entity* e = Ecs_First(); e; e = e->mNext)
    {
        if (e->mParent == &owner && e->mClass == &cls && e->mExists && !e->mFreed)
        {
            used |= 1u << e->Data<T>().mSlot;
        }
    }
    for (int i = 0; i < slots; i++)
    {
        if (!(used & (1u << i)))
        {
            return i;
        }
    }
    return -1;
}

// The C#'s pool entities are in the lists from the start, just after their owner (and the pools before theirs, `before`)
// in slot order: a pooled entity made now goes there, so it updates and collides in the C#'s order (and, made during
// its owner's update, still updates this frame).
template <typename T>
void PlaceInPool(Entity& e, Entity& owner, const EntityClass* before = nullptr)
{
    const EntityClass& cls = *e.mClass;
    const uint8_t slot = e.Data<T>().mSlot;
    Entity* after = &owner;
    for (Entity* x = Ecs_First(); x; x = x->mNext)
    {
        if (x == &e || x->mParent != &owner || x->mFreed)
        {
            continue;
        }
        if ((before && x->mClass == before) || (x->mClass == &cls && x->Data<T>().mSlot < slot))
        {
            after = x;
        }
    }
    Ecs_MoveAfter(e, *after);
}

// Goo.Spawn: from the slime, into its Move state.
void SpawnGoo(Entity& slime)
{
    const int slot = FreePoolSlot<PoolData>(slime, kGoo, kGooPool);
    if (slot < 0)
    {
        return;
    }
    Entity* g = Ecs_Create(kGoo, slime.mT->mX, slime.mT->mY, PART_VISUAL | PART_BRAIN, nullptr, &slime);
    if (!g)
    {
        return;
    }
    g->mB->mW = g->mB->mH = 6;
    Ecs_SetSheet(*g, "slime_goo");
    Ecs_SetFrameBase(*g, Game_IsCell() ? 4 : 0);
    g->mV->mLayer = LAYER_PARTICLES;
    PoolData& d = g->Data<PoolData>();
    d.mSlot = static_cast<uint8_t>(slot);
    PlaceInPool<PoolData>(*g, slime);
    d.mPeriod = FxF(0.8f) + FxMul(FxF(0.3f), Rng_Unit());
    const int sx = Rng_Range(0, 2) ? 1 : -1;
    g->mT->mVelX = (Fx(10) + FxMul(Fx(5), Rng_Unit())) * sx;
    const int sy = Rng_Range(0, 2) ? 1 : -1;
    g->mT->mVelY = (Fx(10) + FxMul(Fx(5), Rng_Unit())) * sy;
    Ecs_Play(*g, "move", true, slime.Data<SlimeData>().mGooRate[slot]);
}

// Bullet.Spawn: from the slime, at the player.
void SlimeShoot(Entity& slime)
{
    const int slot = FreePoolSlot<PoolData>(slime, kSlimeBullet, kSlimeBulletPool);
    if (slot < 0)
    {
        return;
    }
    Entity* b = Ecs_Create(kSlimeBullet, slime.mT->mX, slime.mT->mY, PART_VISUAL | PART_BRAIN, nullptr, &slime);
    if (!b)
    {
        return;
    }
    b->Data<PoolData>().mSlot = static_cast<uint8_t>(slot);
    PlaceInPool<PoolData>(*b, slime, &kGoo);
    Game_PlaySfx("slime_shoot");
    b->mB->mW = b->mB->mH = 8;
    Ecs_SetSheet(*b, "slime_bullet");
    b->mV->mLayer = LAYER_PARTICLES;
    const Entity& p = *World_Player();
    Ecs_MoveTowards(*b, p.mT->mX, p.mT->mY, Fx(40));
    Ecs_Play(*b, "move", true, slime.Data<SlimeData>().mBulletRate[slot]);
}

void SlimeChangeDir(Entity& self)
{
    SlimeData& d = self.Data<SlimeData>();
    if (Ecs_Frame(self) == 1)
    {
        // more likely to stand still now and then
        self.mT->mVelX = self.mT->mVelY = 0;
    }
    else
    {
        self.mT->mVelX = FxMul(Rng_Unit() - kFxOne / 2, d.mSpeed);
        self.mT->mVelY = FxMul(Rng_Unit() - kFxOne / 2, d.mSpeed);
    }
}

// Move's Enter: the timers carry on from when it was left, and a bullet slime gets another shoot timer each time.
void SlimeEnterMove(Entity& self)
{
    SlimeData& d = self.Data<SlimeData>();
    d.mState = SLIME_MOVE;
    Ecs_Play(self, Game_IsCell() ? "Move@cell" : "Move");
    d.mMove.Advance(Game_SecondsSince(d.mExitFrame));
    if (d.mBullet)
    {
        d.mMove.Add(FxSec(1.8f), SLIME_SHOOT_TIMER);
    }
}

void SlimeUpdate(Entity& self)
{
    SlimeData& d = self.Data<SlimeData>();
    switch (d.mState)
    {
    case SLIME_MOVE:
        d.mMove.Tick();
        for (int id = d.mMove.Pop(); id >= 0; id = d.mMove.Pop())
        {
            if (id == SLIME_MOVE_TIMER)
            {
                SlimeChangeDir(self);
            }
            else
            {
                SlimeShoot(self);
            }
        }
        if (Ecs_Frame(self) == 1 && !d.mSoundSync)
        {
            Game_PlaySfx("slime_walk");
            d.mSoundSync = true;
        }
        else if (Ecs_Frame(self) == 0)
        {
            d.mSoundSync = false;
        }
        break;
    case SLIME_HURT:
        if (d.mHealth <= 0)
        {
            d.mState = SLIME_DYING;
            Ecs_Play(self, Game_IsCell() ? "Dying@cell" : "Dying");
        }
        else if (Ecs_AnimFinished(self))
        {
            SlimeEnterMove(self);
        }
        break;
    default:
        if (Ecs_AnimFinished(self))
        {
            Spawn_Explosion(self);
            Die(self, FxF(0.5f));
        }
        break;
    }
    Ecs_BaseUpdate(self);
}

void SlimeCollided(Entity& self, Entity& other)
{
    SlimeData& d = self.Data<SlimeData>();
    if (d.mState == SLIME_DYING)
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other) && d.mState == SLIME_MOVE)
    {
        Game_PlaySfx("hit_slime");
        SpawnGoo(self);
        SpawnGoo(self);
        d.mHealth--;
        const Facing f = static_cast<Facing>(other.mFacing);
        self.mT->mVelX = Fx(100) * FacingX(f);
        self.mT->mVelY = Fx(100) * FacingY(f);
        d.mState = SLIME_HURT;
        d.mExitFrame = Game_Frame();
        Ecs_Play(self, Game_IsCell() ? "Hurt@cell" : "Hurt");
    }
}

const EntityClass kSlime = {
    "Slime",      SlimeUpdate, nullptr, SlimeCollided, nullptr, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER | GROUP_BROOM,
    ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_ENEMY,
};

// ---- Annoyer (Annoyer.cs) -----------------------------------------------------------------------------------------
enum AnnoyerState : uint8_t
{
    ANNOYER_WAIT,
    ANNOYER_APPROACH,
    ANNOYER_CIRCLE,
    ANNOYER_SWOOP,
    ANNOYER_HIT,
};

enum AnnoyerTimer : uint8_t
{
    ANNOYER_FIRE,
    ANNOYER_APPROACH_CHECK,
    ANNOYER_SWOOP_TIMER,
    ANNOYER_END_KNOCKBACK,
    ANNOYER_DIE,
};

// Each state object's TimerState lives as long as the Annoyer: its clock only runs while the state does.
struct AnnoyerData final
{
    uint8_t mState;
    bool mStarted; // start_state is Approach after the first time in Active
    int mHealth;
    TimerState<1> mActive; // Fire
    TimerState<1> mWait;   // ApproachCheck
    TimerState<1> mCircle; // Swoop
    TimerState<1> mHit;    // EndKnockback or Die
    fx mAngle;
    fx mTargetX;
    fx mTargetY;
};

constexpr int kAnnoyerSuper = 2;

struct FireballData final
{
    uint8_t mSlot;
    bool mPoof;
};

void FireballUpdate(Entity& self)
{
    FireballData& d = self.Data<FireballData>();
    if (!d.mPoof)
    {
        self.mV->mOpacity -= FxStep(FxF(0.06f));
        if (self.mV->mOpacity <= FxF(0.6f))
        {
            d.mPoof = true;
            Ecs_Play(self, "poof");
        }
    }
    else if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
    Ecs_BaseUpdate(self);
}

void FireballCollided(Entity& self, Entity& other)
{
    FireballData& d = self.Data<FireballData>();
    if (d.mPoof)
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    d.mPoof = true;
    Ecs_Play(self, "poof");
}

const EntityClass kFireball = {
    "AnnoyerFireball", FireballUpdate, nullptr, FireballCollided, nullptr, nullptr, nullptr, nullptr,
    0,                 GROUP_PLAYER | GROUP_BROOM, 0,
};

// Annoyer.Explosion: a HealthDropper that dies when its animation ends.
void AnnoyerExplosionUpdate(Entity& self)
{
    if (Ecs_AnimFinished(self))
    {
        Die(self, FxF(0.5f));
    }
    Ecs_BaseUpdate(self);
}

const EntityClass kAnnoyerExplosion = {
    "Explosion", AnnoyerExplosionUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void AnnoyerApproachTarget(fx& x, fx& y)
{
    const Entity& p = *World_Player();
    x = Ecs_CenterX(p) + Fx(20);
    y = Ecs_CenterY(p);
}

void AnnoyerFire(Entity& self)
{
    Transform& t = *self.mT;
    const int slot = self.mPreset && self.mPreset->mFrame == kAnnoyerSuper ? FreePoolSlot<FireballData>(self, kFireball, 4) : -1;
    if (slot < 0)
    {
        return;
    }
    Entity* f = Ecs_Create(kFireball, t.mX, t.mY, PART_VISUAL | PART_BRAIN, nullptr, &self);
    if (f)
    {
        f->Data<FireballData>().mSlot = static_cast<uint8_t>(slot);
        PlaceInPool<FireballData>(*f, self);
        f->mB->mW = f->mB->mH = 8;
        f->mV->mOffX = f->mV->mOffY = Fx(4);
        Ecs_SetSolid(*f, false);
        Ecs_SetSheet(*f, "lion_fireballs");
        f->mV->mLayer = LAYER_FG_SPRITES;
        Ecs_Play(*f, "shoot");
        const Entity& p = *World_Player();
        Ecs_MoveTowards(*f, p.mT->mX, p.mT->mY, Fx(30));
    }
}

// ChangeState among Active's children: the old one's Exit, then the new one's Enter.
void AnnoyerChangeActive(Entity& self, uint8_t state)
{
    AnnoyerData& d = self.Data<AnnoyerData>();
    if (d.mState == ANNOYER_APPROACH)
    {
        self.mT->mVelX = self.mT->mVelY = 0;
    }
    d.mState = state;
    if (state == ANNOYER_WAIT)
    {
        d.mWait.Reset();
        d.mWait.Add(FxSec(0.25f), ANNOYER_APPROACH_CHECK);
    }
    else if (state == ANNOYER_CIRCLE)
    {
        d.mAngle = 0;
    }
    else if (state == ANNOYER_SWOOP)
    {
        const Entity& p = *World_Player();
        d.mTargetX = self.mT->mX + 3 * (p.mT->mX - self.mT->mX);
        d.mTargetY = self.mT->mY + 3 * (p.mT->mY - self.mT->mY);
    }
}

// Active's Enter: still, into start_state (Wait the first time, Approach after).
void AnnoyerEnterActive(Entity& self)
{
    AnnoyerData& d = self.Data<AnnoyerData>();
    self.mT->mVelX = self.mT->mVelY = 0;
    d.mState = ANNOYER_HIT; // (no child yet: nothing to exit)
    AnnoyerChangeActive(self, d.mStarted ? ANNOYER_APPROACH : ANNOYER_WAIT);
    d.mStarted = true;
}

void AnnoyerUpdate(Entity& self)
{
    AnnoyerData& d = self.Data<AnnoyerData>();
    Transform& t = *self.mT;
    if (d.mState == ANNOYER_HIT)
    {
        d.mHit.Tick();
        for (int id = d.mHit.Pop(); id >= 0; id = d.mHit.Pop())
        {
            if (id == ANNOYER_END_KNOCKBACK)
            {
                Ecs_SetSolid(self, false); // (Hit's Exit)
                AnnoyerEnterActive(self);
                break;
            }
            // Die: the explosion (a HealthDropper sub-entity, after the Annoyer) goes off where it is
            self.mExists = false;
            Preset_SetAlive(*self.mPreset, false);
            Entity* ex = Ecs_Create(kAnnoyerExplosion, t.mX, t.mY, PART_VISUAL);
            if (ex)
            {
                Entity* after = &self;
                for (Entity* x = Ecs_First(); x; x = x->mNext)
                {
                    after = x->mParent == &self && x->mClass == &kFireball && !x->mFreed ? x : after;
                }
                Ecs_MoveAfter(*ex, *after);
                Ecs_SetSolid(*ex, false);
                Ecs_SetSheet(*ex, "enemy_explode_2");
                // (GlobalState.IsCell: the CELL's frames, 5 on, at 10 fps)
                Ecs_SetFrameBase(*ex, Game_IsCell() ? 5 : 0);
                Ecs_Play(*ex, "explode", true, Game_IsCell() ? 10 : 0); // (forced: the rate only sticks on a Play that restarts)
            }
            Game_PlaySfx("hit_wall");
        }
        Ecs_BaseUpdate(self);
        return;
    }
    // ActiveState: its Fire timer, then the child state
    d.mActive.Tick();
    while (d.mActive.Pop() >= 0)
    {
        AnnoyerFire(self);
    }
    switch (d.mState)
    {
    case ANNOYER_WAIT:
        d.mWait.Tick();
        for (int id = d.mWait.Pop(); id >= 0; id = d.mWait.Pop())
        {
            const Entity& p = *World_Player();
            if (d.mState == ANNOYER_WAIT && FxLength(t.mX - p.mT->mX, t.mY - p.mT->mY) < Fx(64))
            {
                AnnoyerChangeActive(self, ANNOYER_APPROACH);
            }
        }
        break;
    case ANNOYER_APPROACH:
    {
        fx ax = 0;
        fx ay = 0;
        AnnoyerApproachTarget(ax, ay);
        MoveTo(t.mX, ax, Fx(36));
        MoveTo(t.mY, ay, Fx(36));
        AnnoyerApproachTarget(ax, ay);
        if (FxLength(t.mX - ax, t.mY - ay) < Fx(2))
        {
            AnnoyerChangeActive(self, ANNOYER_CIRCLE);
        }
        break;
    }
    case ANNOYER_CIRCLE:
    {
        // CirclingState.Update: the angle, its Swoop timer, then (even when that left the state) the position
        d.mAngle += FxStep(FxF(8.4f));
        d.mCircle.Tick();
        while (d.mCircle.Pop() >= 0)
        {
            AnnoyerChangeActive(self, ANNOYER_SWOOP);
        }
        const Entity& p = *World_Player();
        t.mX = Ecs_VisualCenterX(p) + FxMul(FxCos(d.mAngle), Fx(20));
        t.mY = Ecs_VisualCenterY(p) + FxMul(FxSin(d.mAngle), Fx(20));
        break;
    }
    default:
    {
        const bool x = MoveTo(t.mX, d.mTargetX, Fx(150));
        const bool y = MoveTo(t.mY, d.mTargetY, Fx(150));
        if (x && y)
        {
            AnnoyerChangeActive(self, ANNOYER_APPROACH);
        }
        break;
    }
    }
    Ecs_BaseUpdate(self);
}

void AnnoyerCollided(Entity& self, Entity& other)
{
    AnnoyerData& d = self.Data<AnnoyerData>();
    if (d.mState == ANNOYER_HIT)
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other))
    {
        const Facing f = static_cast<Facing>(other.mFacing);
        self.mT->mVelX = Fx(150) * FacingX(f);
        self.mT->mVelY = Fx(150) * FacingY(f);
        if (self.mT->mVelY < 0)
        {
            self.mT->mVelX = Fx(Rng_Range(-30, 31));
        }
        // ChangeState("Hit"): Active's Exit takes its child's (Approach's stops it dead) ...
        if (d.mState == ANNOYER_APPROACH)
        {
            self.mT->mVelX = self.mT->mVelY = 0;
        }
        // ... then Hit's Enter
        d.mState = ANNOYER_HIT;
        Game_PlaySfx("player_hit_1");
        d.mHit.Reset();
        Ecs_Flicker(self, FxSec(0.2f));
        if (--d.mHealth <= 0)
        {
            Ecs_SetSolid(self, true);
            d.mHit.Add(FxSec(0.25f), ANNOYER_DIE);
        }
        else
        {
            d.mHit.Add(FxSec(0.4f), ANNOYER_END_KNOCKBACK);
        }
    }
}

const EntityClass kAnnoyer = {
    "Annoyer",    AnnoyerUpdate, nullptr, AnnoyerCollided, nullptr, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER | GROUP_BROOM,
    ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_ENEMY,
};

// ---- PewLaser (PewLaser.cs) ---------------------------------------------------------------------------------------
struct PewLaserData final
{
    fx mTimer;
    bool mFast;
};

struct LaserData final
{
    uint8_t mSlot;
    bool mPoof;
    fx mSolidTimer;
};

void LaserPoof(Entity& self)
{
    LaserData& d = self.Data<LaserData>();
    d.mPoof = true;
    Ecs_Play(self, "poof");
    self.mT->mVelX = self.mT->mVelY = 0;
}

void LaserUpdate(Entity& self)
{
    LaserData& d = self.Data<LaserData>();
    if (!d.mPoof)
    {
        self.mV->mOpacity -= FxStep(FxF(0.06f));
        if (self.mB->mTouching != TOUCH_NONE)
        {
            LaserPoof(self);
        }
    }
    else if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
    d.mSolidTimer -= kFxTick;
    if (d.mSolidTimer < 0)
    {
        Ecs_SetSolid(self, true);
    }
    Ecs_BaseUpdate(self);
}

void LaserCollided(Entity& self, Entity& other)
{
    LaserData& d = self.Data<LaserData>();
    if (d.mPoof)
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (other.mClass->mGroups & GROUP_SHIELDY)
    {
        Shieldy_ReceiveDamage(other, 5);
    }
    LaserPoof(self);
}

const EntityClass kLaser = {
    "PewLaserBullet", LaserUpdate,   nullptr, LaserCollided, nullptr, nullptr, nullptr, nullptr,
    0,                GROUP_PLAYER | GROUP_DUST | GROUP_SHIELDY, ECF_MAP_COLLISION,
};

void PewLaserUpdate(Entity& self)
{
    PewLaserData& d = self.Data<PewLaserData>();
    d.mTimer -= kFxTick;
    if (d.mTimer < 0)
    {
        d.mTimer = FxSec(0.5f);
        const int slot = FreePoolSlot<LaserData>(self, kLaser, 5);
        if (slot >= 0)
        {
            const Facing f = static_cast<Facing>(self.mFacing);
            Entity* l = Ecs_Create(kLaser, self.mT->mX, self.mT->mY, PART_VISUAL | PART_BRAIN, nullptr, &self);
            if (l)
            {
                l->Data<LaserData>().mSlot = static_cast<uint8_t>(slot);
                PlaceInPool<LaserData>(*l, self);
                Ecs_SetSheet(*l, "pew_laser_bullet");
                l->mV->mLayer = LAYER_FG_SPRITES;
                l->mB->mMapInteraction = false;
                l->mB->mW = 16;
                l->mB->mH = 8;
                l->mFacing = f;
                switch (f)
                {
                case FACE_DOWN:
                    l->mT->mY += Fx(11);
                    break;
                case FACE_UP:
                    l->mT->mY += Fx(8);
                    l->mT->mRot = kFxPi;
                    break;
                case FACE_RIGHT:
                    l->mB->mW = 8;
                    l->mB->mH = 16;
                    l->mV->mOffX = Fx(4);
                    l->mV->mOffY = -Fx(4);
                    l->mT->mRot = -kFxPi / 2;
                    break;
                default:
                    l->mB->mW = 8;
                    l->mB->mH = 16;
                    l->mT->mX += Fx(8);
                    l->mV->mOffX = Fx(4);
                    l->mV->mOffY = -Fx(4);
                    l->mT->mRot = kFxPi / 2;
                    break;
                }
                l->mT->mLastX = l->mT->mX;
                l->mT->mLastY = l->mT->mY;
                Ecs_SetSolid(*l, false);
                l->Data<LaserData>().mSolidTimer = d.mFast ? FxSec(0.2f) : FxSec(0.4f);
                Game_PlaySfx("laser-pew");
                Ecs_Play(*l, "shoot");
                const fx speed = d.mFast ? Fx(70) : Fx(40);
                l->mT->mVelX = speed * FacingX(f);
                l->mT->mVelY = speed * FacingY(f);
            }
        }
    }
    Ecs_BaseUpdate(self);
}

const EntityClass kPewLaser = {
    "PewLaser", PewLaserUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- Shieldy (Shieldy.cs) -----------------------------------------------------------------------------------------
struct ShieldyData final
{
    int mHealth;
    fx mHitTimer;
};

void ShieldyUpdate(Entity& self)
{
    ShieldyData& d = self.Data<ShieldyData>();
    Ecs_BaseUpdate(self);
    if (d.mHitTimer > 0)
    {
        d.mHitTimer -= kFxTick;
        if (d.mHitTimer <= 0)
        {
            Ecs_Play(self, "walk");
        }
    }
    if (d.mHealth <= 0)
    {
        Spawn_Explosion(self);
        Die(self, FxF(0.5f));
    }
    if (self.mB->mTouching != TOUCH_NONE)
    {
        self.mT->mVelX = -self.mT->mVelX;
        self.mT->mVelY = -self.mT->mVelY;
    }
}

void ShieldyCollided(Entity& self, Entity& other)
{
    ShieldyData& d = self.Data<ShieldyData>();
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other) && d.mHitTimer <= 0)
    {
        const Facing f = static_cast<Facing>(other.mFacing);
        if (f == FACE_DOWN)
        {
            d.mHealth--;
            Ecs_Flicker(self, FxSec(0.2f));
            Ecs_Play(self, "back_hit");
            Game_PlaySfx("shieldy-hit");
        }
        else
        {
            Ecs_Play(self, "front_hit");
            Game_PlaySfx("shieldy_ineffective");
            fx nx = self.mT->mX;
            fx ny = self.mT->mY;
            if (f == FACE_LEFT)
            {
                nx -= Fx(8);
            }
            else if (f == FACE_UP)
            {
                ny -= Fx(8);
            }
            else if (f == FACE_RIGHT)
            {
                nx += Fx(8);
            }
            if (Map_CollisionAllowAt(nx, ny) == TOUCH_NONE &&
                Map_CollisionAllowAt(nx + Fx(self.mB->mW), ny + Fx(self.mB->mH)) == TOUCH_NONE)
            {
                self.mT->mX = nx;
                self.mT->mY = ny;
            }
        }
        d.mHitTimer = FxSec(0.4f);
    }
}

const EntityClass kShieldy = {
    "Shieldy",     ShieldyUpdate, nullptr, ShieldyCollided, nullptr, nullptr, nullptr, nullptr,
    GROUP_SHIELDY, GROUP_PLAYER | GROUP_BROOM,
    ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_ENEMY,
};

// ---- Dog (Dog.cs) -------------------------------------------------------------------------------------------------
enum DogState : uint8_t
{
    DOG_PACE,
    DOG_ALERT,
    DOG_ATTACK,
};

struct DogData final
{
    uint8_t mState;
    uint8_t mPhase;
    bool mSuper;
    int mHealth;
    fx mHitTimer;
    fx mAttackX[2];
    fx mAttackY[2];
    Entity* mRegion; // the DogRegion sub-entity: it only exists while pacing
    // Each state object's TimerState lives as long as the dog: its clock only runs while the state does.
    TimerState<1> mPace;   // MoveTimer
    TimerState<1> mAlert;  // AlertTimer
    TimerState<1> mAttack; // AttackTimer, armed once when the dog is made and never reset
};

// AttackSpeed: 60 * the Fix speeds, so 60 times each one's rounding (66.0002..., not 66)
const fx kDogAttackSpeeds[5] = { 60 * FxF(1.1f), 60 * FxF(1.4f), 60 * FxF(1.4f), 60 * FxF(1.6f), 60 * FxF(1.7f) };

void DogSetFlip(Entity& self)
{
    self.mV->mHFlip = self.mT->mVelX <= 0;
}

void DogEnter(Entity& self, uint8_t state)
{
    DogData& d = self.Data<DogData>();
    if (d.mRegion)
    {
        d.mRegion->mExists = state == DOG_PACE;
    }
    d.mState = state;
    Transform& t = *self.mT;
    if (state == DOG_PACE)
    {
        d.mPace.Reset();
        d.mPace.Add(FxSec(1.0f), 0);
        t.mVelX = t.mVelY = 0;
    }
    else if (state == DOG_ALERT)
    {
        d.mAlert.Reset();
        d.mAlert.Add(FxSec(1.0f), 0);
        t.mVelX = t.mVelY = 0;
        Ecs_Play(self, "alert");
        Game_PlaySfx("dog_bark");
    }
    else
    {
        d.mPhase = 0;
        const Entity& p = *World_Player();
        const fx px = p.mT->mX;
        const fx py = p.mT->mY;
        const fx tx = px - Fx(Rng_Range(38, 51));
        if (!d.mSuper)
        {
            d.mAttackX[0] = px + Fx(Rng_Range(30, 43));
            d.mAttackY[0] = py - Fx(8);
            d.mAttackX[1] = px - Fx(Rng_Range(38, 51));
            d.mAttackY[1] = py - Fx(8);
        }
        Ecs_MoveTowards(self, tx, py - Fx(8), kDogAttackSpeeds[0]);
        Ecs_Play(self, "attack");
        DogSetFlip(self);
    }
}

void DogUpdate(Entity& self)
{
    DogData& d = self.Data<DogData>();
    Ecs_BaseUpdate(self);
    Transform& t = *self.mT;
    switch (d.mState)
    {
    case DOG_PACE:
        d.mPace.Tick();
        while (d.mPace.Pop() >= 0)
        {
            if (Rng_NextDouble() > 0.33)
            {
                Ecs_Play(self, "walk");
                t.mVelX = Fx(20);
                DogSetFlip(self);
            }
            else if (Rng_NextDouble() > 0.5)
            {
                Ecs_Play(self, "walk");
                t.mVelX = -Fx(20);
                DogSetFlip(self);
            }
            else
            {
                Ecs_Play(self, "stop");
                t.mVelX = 0;
            }
        }
        break;
    case DOG_ALERT:
        d.mAlert.Tick();
        while (d.mAlert.Pop() >= 0)
        {
            DogEnter(self, DOG_ATTACK);
        }
        break;
    default:
        d.mAttack.Tick();
        while (d.mAttack.Pop() >= 0)
        {
            d.mPhase++;
            if (d.mPhase > (d.mSuper ? 4 : 2))
            {
                DogEnter(self, DOG_PACE);
                continue;
            }
            Game_PlaySfx("dog_dash");
            fx tx = 0;
            fx ty = 0;
            if (d.mSuper)
            {
                const Entity& p = *World_Player();
                tx = p.mT->mX;
                ty = p.mT->mY;
                if (p.mT->mX > t.mX)
                {
                    tx += Fx(25);
                    ty += Fx(8);
                }
                else
                {
                    tx -= Fx(28);
                    ty -= Fx(8);
                }
            }
            else
            {
                tx = d.mAttackX[d.mPhase - 1];
                ty = d.mAttackY[d.mPhase - 1];
            }
            Ecs_MoveTowards(self, tx, ty, kDogAttackSpeeds[d.mPhase]);
            DogSetFlip(self);
        }
        break;
    }
    if (d.mHitTimer > 0)
    {
        d.mHitTimer -= kFxTick;
    }
}

void DogCollided(Entity& self, Entity& other)
{
    DogData& d = self.Data<DogData>();
    if (IsPlayer(other) && Player_State() != PS_AIR)
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other) && d.mHitTimer <= 0)
    {
        d.mHealth--;
        d.mHitTimer = FxSec(1.5f);
        Ecs_Flicker(self, d.mHitTimer);
        Game_PlaySfx("broom_hit");
        if (d.mHealth == 0)
        {
            Die(self, FxF(1.0f));
            Spawn_Explosion(self);
        }
    }
}

const EntityClass kDog = {
    "Dog",        DogUpdate, nullptr, DogCollided, nullptr, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER | GROUP_BROOM,
    ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_ENEMY,
};

// ---- Burst_Plant (Hotel/Burst_Plant.cs) -------------------------------------------------------------------------
enum BurstState : uint8_t
{
    BURST_INITIAL,
    BURST_IDLE,
    BURST_CHARGING,
    BURST_BULLETS_FLYING,
};

constexpr int kBurstBullets = 8;

struct BurstPlantData final
{
    uint8_t mState;
    int mHealth;
    Entity* mBullets[kBurstBullets]; // its EntityPool, made with it
    // Each state object's TimerState lives as long as the plant: its clock only runs while the state does.
    TimerState<1> mInitial;  // goToIdle
    TimerState<1> mIdle;     // idleTimer
    TimerState<1> mCharging; // Fire, armed once when the plant is made and never reset
};

struct BurstBulletData final
{
    bool mSplash;
    fx mT; // its Parabola_Thing
    fx mPeriod;
    int mHeight;
};

void BurstBulletUpdate(Entity& self)
{
    BurstBulletData& d = self.Data<BurstBulletData>();
    Ecs_BaseUpdate(self);
    if (!d.mSplash)
    {
        if (ParabolaTick(d.mT, d.mPeriod, d.mHeight, self.mV->mOffY))
        {
            d.mSplash = true;
            Ecs_Play(self, "explode");
            self.mT->mVelX = self.mT->mVelY = 0;
        }
    }
    else if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
        Game_PlaySfx("4sht_pop");
    }
}

void BurstBulletCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && self.mV->mOffY <= Fx(4) && (self.mT->mVelX != 0 || self.mT->mVelY != 0))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kBurstBullet = {
    "BurstBullet", BurstBulletUpdate, nullptr, BurstBulletCollided, nullptr, nullptr, nullptr, nullptr,
    0,             GROUP_PLAYER,      0,
};

// EntityPool.Spawn: the bullet's Move state from the plant (Parabola_Thing from offset 0). `period` < 0: a random
// one, drawn after the height.
void BurstBulletStart(Entity& b, const Entity& plant, fx period)
{
    BurstBulletData& d = b.Data<BurstBulletData>();
    b.mExists = true;
    b.mT->mX = plant.mT->mX + Fx(5);
    b.mT->mY = plant.mT->mY + Fx(5);
    d.mHeight = Rng_Range(24, 34);
    d.mPeriod = period >= 0 ? period : FxF(0.7f) + Rng_Unit();
    d.mT = 0;
    d.mSplash = false;
    b.mV->mOffY = 0;
    b.mV->mOpacity = kFxOne;
    Ecs_Play(b, "move");
}

void BurstEnter(Entity& self, uint8_t state)
{
    BurstPlantData& d = self.Data<BurstPlantData>();
    d.mState = state;
    if (state == BURST_IDLE)
    {
        Ecs_Play(self, "idle");
        d.mIdle.Reset();
        d.mIdle.Add(FxF(0.4f) + Rng_Unit(), 0);
    }
    else if (state == BURST_CHARGING)
    {
        Ecs_Play(self, "charging");
    }
}

void BurstFire(Entity& self)
{
    BurstPlantData& d = self.Data<BurstPlantData>();
    const int sign = Rng_Range(0, 2) == 0 ? -1 : 1; // MathUtilities.OneRandomOf(-1, 1)
    const fx maxVel = Fx(10) + 30 * Rng_Unit();
    const Entity& p = *World_Player();
    int spawned = 0;
    for (Entity* b : d.mBullets)
    {
        if (!b || b->mExists)
        {
            continue;
        }
        if (spawned == 0)
        {
            // one straight to the player
            BurstBulletStart(*b, self, FxF(1.5f));
            Ecs_MoveTowards(*b, Ecs_CenterX(p), Ecs_CenterY(p), FxMul(maxVel, FxF(2.5f)));
        }
        else
        {
            BurstBulletStart(*b, self, -kFxOne);
            const fx vx = -maxVel + FxMul(2 * maxVel, Rng_Unit());
            b->mT->mVelX = vx;
            b->mT->mVelY = sign * FxSqrt(FxMul(maxVel, maxVel) - FxMul(vx, vx));
        }
        if (++spawned == 8)
        {
            break;
        }
    }
    Ecs_Play(self, "shoot");
    Game_PlaySfx("bubble_triple");
    BurstEnter(self, BURST_BULLETS_FLYING);
}

void BurstPlantUpdate(Entity& self)
{
    BurstPlantData& d = self.Data<BurstPlantData>();
    Ecs_BaseUpdate(self);
    switch (d.mState)
    {
    case BURST_INITIAL:
        d.mInitial.Tick();
        while (d.mInitial.Pop() >= 0)
        {
            BurstEnter(self, BURST_IDLE);
        }
        break;
    case BURST_IDLE:
        d.mIdle.Tick();
        while (d.mIdle.Pop() >= 0)
        {
            BurstEnter(self, BURST_CHARGING);
        }
        break;
    case BURST_CHARGING:
        d.mCharging.Tick();
        while (d.mCharging.Pop() >= 0)
        {
            BurstFire(self);
        }
        break;
    default:
    {
        bool alive = false;
        for (const Entity* b : d.mBullets)
        {
            alive = alive || (b && b->mExists);
        }
        if (!alive)
        {
            BurstEnter(self, BURST_IDLE);
        }
        break;
    }
    }
}

void BurstPlantCollided(Entity& self, Entity&)
{
    BurstPlantData& d = self.Data<BurstPlantData>();
    if (self.mV->mFlickering)
    {
        return;
    }
    d.mHealth--;
    Ecs_Flicker(self, FxSec(1.0f));
    Game_PlaySfx("broom_hit");
    if (d.mHealth <= 0)
    {
        Spawn_Explosion(self);
        Die(self, FxF(0.7f));
        for (Entity* b : d.mBullets)
        {
            if (b)
            {
                b->mExists = false;
            }
        }
    }
}

const EntityClass kBurstPlant = {
    "Burst_Plant", BurstPlantUpdate, nullptr, BurstPlantCollided, nullptr, nullptr, nullptr, nullptr,
    0,             GROUP_BROOM,      ECF_ENEMY,
};

// ---- Frog (Crowd/Frog.cs) ----------------------------------------------------------------------------------------
enum FrogState : uint8_t
{
    FROG_INITIAL,
    FROG_IDLE,
    FROG_BULLETS_FLYING,
};

constexpr int kFrogBullets = 3;

struct FrogData final
{
    uint8_t mState;
    int mHealth;
    Entity* mBullets[kFrogBullets]; // its EntityPool, made with it
    TimerState<1> mInitial;         // goToIdle
};

struct FrogBulletData final
{
    bool mSplash;
    int mMultiplier;
    fx mT; // its Parabola_Thing
    fx mPeriod;
};

void FrogBulletUpdate(Entity& self)
{
    FrogBulletData& d = self.Data<FrogBulletData>();
    Ecs_BaseUpdate(self);
    if (!d.mSplash)
    {
        if (ParabolaTick(d.mT, d.mPeriod, 16, self.mV->mOffY))
        {
            d.mSplash = true;
            Ecs_Play(self, "explode");
            self.mT->mVelX = self.mT->mVelY = 0;
        }
    }
    else if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
        static const char* const kSplash[4] = { "bubble_1", "bubble_1", "bubble_2", "bubble_3" };
        Game_PlaySfxOneOf(kSplash, 4);
    }
}

void FrogBulletCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && self.mV->mOffY <= Fx(8) && (self.mT->mVelX != 0 || self.mT->mVelY != 0))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kFrogBullet = {
    "BurstBullet", FrogBulletUpdate, nullptr, FrogBulletCollided, nullptr, nullptr, nullptr, nullptr,
    0,             GROUP_PLAYER,     0,
};

void FrogUpdate(Entity& self)
{
    FrogData& d = self.Data<FrogData>();
    Ecs_BaseUpdate(self);
    switch (d.mState)
    {
    case FROG_INITIAL:
        d.mInitial.Tick();
        while (d.mInitial.Pop() >= 0)
        {
            d.mState = FROG_IDLE;
            Ecs_Play(self, "idle");
        }
        break;
    case FROG_IDLE:
    {
        const Entity& p = *World_Player();
        const fx dx = Ecs_CenterX(self) - Ecs_CenterX(p);
        const fx dy = Ecs_CenterY(self) - Ecs_CenterY(p);
        if (FxLengthSq(dx, dy) < Fx(64 * 64))
        {
            // EntityPool.Spawn(b => b.Spawn(this, _player), 3)
            for (Entity* b : d.mBullets)
            {
                if (!b || b->mExists)
                {
                    continue;
                }
                FrogBulletData& bd = b->Data<FrogBulletData>();
                b->mExists = true;
                b->mT->mX = self.mT->mX;
                b->mT->mY = self.mT->mY;
                Ecs_MoveTowards(*b, Ecs_CenterX(p), Ecs_CenterY(p), Fx(15 * bd.mMultiplier));
                bd.mSplash = false;
                bd.mT = 0;
                b->mV->mOffY = 0;
                b->mV->mOpacity = kFxOne;
                Ecs_Play(*b, "move@2");
            }
            Ecs_FaceTowards(self, Ecs_CenterX(p), Ecs_CenterY(p));
            Ecs_PlayFacing(self, "shoot");
            self.mV->mHFlip = Ecs_AnimIs(self, "shoot_l");
            Game_PlaySfx("bubble_triple");
            d.mState = FROG_BULLETS_FLYING;
        }
        break;
    }
    default:
    {
        bool alive = false;
        for (const Entity* b : d.mBullets)
        {
            alive = alive || (b && b->mExists);
        }
        if (!alive)
        {
            d.mState = FROG_IDLE;
            Ecs_Play(self, "idle");
            self.mV->mHFlip = false;
        }
        break;
    }
    }
}

void FrogCollided(Entity& self, Entity& other)
{
    FrogData& d = self.Data<FrogData>();
    if (IsBroom(other))
    {
        if (!self.mV->mFlickering)
        {
            d.mHealth--;
            Ecs_Flicker(self, FxSec(1.0f));
            Game_PlaySfx("broom_hit");
            if (d.mHealth <= 0)
            {
                Spawn_Explosion(self);
                Die(self, FxF(0.7f));
            }
        }
    }
    else if (IsPlayer(other) && Player_State() != PS_AIR)
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kFrog = {
    "Frog", FrogUpdate, nullptr, FrogCollided, nullptr, nullptr, nullptr, nullptr,
    0,      GROUP_PLAYER | GROUP_BROOM, ECF_ENEMY,
};

// ---- BounceDashTrap, OnSightDashTrap (Apartment/Dash_Trap.cs) ----------------------------------------------------
constexpr int kDashVel = 80;

enum DashTrapState : uint8_t
{
    DASH_IDLE,
    DASH_CHARGING,
    DASH_RETURNING,
};

struct DashTrapData final
{
    uint8_t mState; // OnSightDashTrap
    fx mIdleX;
    fx mIdleY;
};

void BounceDashTrapTouch(Entity& self)
{
    Ecs_Play(self, "bounce@2");
    Game_PlaySfx("shieldy_ineffective");
    self.mT->mVelX = -self.mT->mVelX;
    self.mT->mVelY = -self.mT->mVelY;
}

// OnSightDashTrap's "bounce" event: only the Charging state handles it.
void OnSightDashTrapTouch(Entity& self)
{
    DashTrapData& d = self.Data<DashTrapData>();
    if (d.mState != DASH_CHARGING)
    {
        return;
    }
    Game_PlaySfx("shieldy_ineffective");
    self.mT->mVelX = -self.mT->mVelX / 2;
    self.mT->mVelY = -self.mT->mVelY / 2;
    d.mState = DASH_RETURNING;
}

bool IsOnSightDashTrap(const Entity& e);

void DashTrapTouch(Entity& self)
{
    if (IsOnSightDashTrap(self))
    {
        OnSightDashTrapTouch(self);
    }
    else
    {
        BounceDashTrapTouch(self);
    }
}

void DashTrapCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        if (Player_State() != PS_AIR)
        {
            Player_ReceiveDamage(1);
            if (IsOnSightDashTrap(self))
            {
                OnSightDashTrapTouch(self); // OnPlayer (the bounce trap ignores it)
            }
        }
    }
    else
    {
        DashTrapTouch(self);
    }
}

void BounceDashTrapUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (self.mB->mTouching != TOUCH_NONE)
    {
        DashTrapTouch(self);
    }
}

void OnSightDashTrapUpdate(Entity& self)
{
    DashTrapData& d = self.Data<DashTrapData>();
    Ecs_BaseUpdate(self);
    if (self.mB->mTouching != TOUCH_NONE)
    {
        DashTrapTouch(self);
    }
    Transform& t = *self.mT;
    if (d.mState == DASH_IDLE)
    {
        // SeePlayer: lined up with the player on either axis
        const Rect p = Ecs_Hitbox(*World_Player());
        const Rect h = Ecs_Hitbox(self);
        const bool seeX = p.mX <= h.mX + h.mW && p.mX + p.mW >= h.mX;
        const bool seeY = p.mY <= h.mY + h.mH && p.mY + p.mH >= h.mY;
        if (seeX || seeY)
        {
            const Entity& pl = *World_Player();
            Ecs_FaceTowards(self, Ecs_CenterX(pl), Ecs_CenterY(pl));
            const Facing f = static_cast<Facing>(self.mFacing);
            t.mVelX = Fx(kDashVel * FacingX(f));
            t.mVelY = Fx(kDashVel * FacingY(f));
            Game_PlaySfx("slasher_atk");
            Ecs_Play(self, "dash@2");
            d.mState = DASH_CHARGING;
        }
    }
    else if (d.mState == DASH_RETURNING)
    {
        const fx dx = d.mIdleX - t.mX;
        const fx dy = d.mIdleY - t.mY;
        if (FxLengthSq(dx, dy) < Fx(4))
        {
            t.mVelX = t.mVelY = 0;
            t.mX = d.mIdleX;
            t.mY = d.mIdleY;
            d.mState = DASH_IDLE;
            Ecs_Play(self, "idle@2");
        }
    }
}

const EntityClass kBounceDashTrap = {
    "BounceDashTrap",
    BounceDashTrapUpdate,
    nullptr,
    DashTrapCollided,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    GROUP_DASH_TRAP,
    GROUP_PLAYER | GROUP_DUST,
    ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN,
};

const EntityClass kOnSightDashTrap = {
    "OnSightDashTrap",
    OnSightDashTrapUpdate,
    nullptr,
    DashTrapCollided,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    GROUP_DASH_TRAP,
    GROUP_PLAYER | GROUP_DUST | GROUP_DASH_TRAP,
    ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN,
};

bool IsOnSightDashTrap(const Entity& e)
{
    return e.mClass == &kOnSightDashTrap;
}

// ---- Four_Shooter (Redcave/Four_Shooter.cs) ----------------------------------------------------------------------
constexpr int kFourShooterBullets = 12;
constexpr fx kFourShooterTime = FxF(1.5f);

struct FourShooterData final
{
    fx mTimer;
    Entity* mBullets[kFourShooterBullets]; // its EntityPool, made with it
};

void FourShooterBulletPop(Entity& self)
{
    self.mT->mVelX = self.mT->mVelY = 0;
    Ecs_Play(self, "explode");
    Game_PlaySfx("4sht_pop");
}

void FourShooterBulletUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
        return;
    }
    if (self.mB->mTouching != TOUCH_NONE && !Ecs_AnimIs(self, "explode"))
    {
        FourShooterBulletPop(self);
    }
}

void FourShooterBulletCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && Player_State() != PS_AIR && !Ecs_AnimIs(self, "explode"))
    {
        Player_ReceiveDamage(1);
        FourShooterBulletPop(self);
    }
}

const EntityClass kFourShooterBullet = {
    "Bullet", FourShooterBulletUpdate, nullptr, FourShooterBulletCollided, nullptr, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER,            ECF_MAP_COLLISION,
};

// EntityPool.Spawn(b => b.Spawn(Center, dir))
void FourShooterFire(FourShooterData& d, fx cx, fx cy, fx dx, fx dy)
{
    for (Entity* b : d.mBullets)
    {
        if (!b || b->mExists)
        {
            continue;
        }
        FxNormalize(dx, dy);
        b->mExists = true;
        b->mT->mX = cx + dx * 8 - Fx(2);
        b->mT->mY = cy + dy * 8 - Fx(2);
        b->mT->mVelX = dx * 50;
        b->mT->mVelY = dy * 50;
        Ecs_Play(*b, "move");
        return;
    }
}

void FourShooterUpdate(Entity& self)
{
    FourShooterData& d = self.Data<FourShooterData>();
    Ecs_BaseUpdate(self);
    d.mTimer -= kFxTick;
    if (d.mTimer <= 0)
    {
        d.mTimer = kFourShooterTime;
        Game_PlaySfx("4sht_shoot");
        const fx cx = Ecs_CenterX(self);
        const fx cy = Ecs_CenterY(self);
        if (Ecs_Frame(self) == 0)
        {
            Ecs_Play(self, "axis_to_diag");
            FourShooterFire(d, cx, cy, Fx(1), Fx(0));
            FourShooterFire(d, cx, cy, Fx(-1), Fx(0));
            FourShooterFire(d, cx, cy, Fx(0), Fx(1));
            FourShooterFire(d, cx, cy, Fx(0), Fx(-1));
        }
        else
        {
            Ecs_Play(self, "diag_to_axis");
            FourShooterFire(d, cx, cy, Fx(1), Fx(1));
            FourShooterFire(d, cx, cy, Fx(-1), Fx(-1));
            FourShooterFire(d, cx, cy, Fx(-1), Fx(1));
            FourShooterFire(d, cx, cy, Fx(1), Fx(-1));
        }
    }
}

void FourShooterCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kFourShooter = {
    "Four_Shooter", FourShooterUpdate, nullptr, FourShooterCollided, nullptr, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER,      0,
};

// ---- Mover (Redcave/Mover.cs) ------------------------------------------------------------------------------------
enum MoverState : uint8_t
{
    MOVER_FOOM,
    MOVER_DIE,
};

struct MoverData final
{
    uint8_t mState;
    TimerState<1> mMove; // MoveState: "MoveTimer" every second (armed once, by the state's constructor)
};

void MoverDie(Entity& self)
{
    MoverData& d = self.Data<MoverData>();
    if (!Preset_Alive(*self.mPreset))
    {
        return;
    }
    // ChangeState("Die")
    d.mState = MOVER_DIE;
    Preset_SetAlive(*self.mPreset, false);
    self.mT->mVelX = self.mT->mVelY = 0;
    Ecs_Play(self, "Die");
}

void MoverUpdate(Entity& self)
{
    MoverData& d = self.Data<MoverData>();
    Ecs_BaseUpdate(self);
    if (d.mState == MOVER_FOOM)
    {
        d.mMove.Tick();
        while (d.mMove.Pop() >= 0)
        {
            Game_PlaySfx("mover_move");
            Ecs_Play(self, "Foom");
            const Entity& p = *World_Player();
            const fx dx = p.mT->mX - self.mT->mX;
            const fx dy = p.mT->mY - self.mT->mY;
            const fx len = FxLength(dx, dy);
            const fx normal = FxMin(Fx(3), FxDiv(Fx(100), len));
            const fx rx = Fx(Rng_Range(-5, 0));
            const fx ry = Fx(Rng_Range(-5, 0));
            self.mT->mVelX = FxMul(dx, normal) + rx;
            self.mT->mVelY = FxMul(dy, normal) + ry;
        }
    }
    else if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
        Game_PlaySfx("mover_die");
        Spawn_Explosion(self);
    }
}

void MoverCollided(Entity& self, Entity& other)
{
    // TriggerEvent("Player"): only Foom has it
    if (self.Data<MoverData>().mState == MOVER_FOOM && IsPlayer(other))
    {
        Player_SetAdditionalVel(self.mT->mVelX, self.mT->mVelY);
    }
}

const EntityClass kMover = {
    "Mover", MoverUpdate, nullptr, MoverCollided, nullptr, nullptr, nullptr, nullptr,
    GROUP_MOVER, GROUP_PLAYER, ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_ENEMY,
};

// ---- OnOffLaser (Redcave/OnOffLaser.cs) ---------------------------------------------------------------------------
enum OnOffLaserState : uint8_t
{
    ONOFF_IDLE,
    ONOFF_EMERGING,
    ONOFF_HURTING,
    ONOFF_RECEDING,
};

struct OnOffLaserData final
{
    uint8_t mState;
    bool mB; // frames 3 and 4: the other shooter sprite
    Entity* mLaser;
    TimerState<1> mIdle;
    TimerState<1> mHurting;
};

void OnOffPlay(Entity& self, const char* anim)
{
    char name[24];
    snprintf(name, sizeof(name), self.Data<OnOffLaserData>().mB ? "%s_b" : "%s", anim);
    Ecs_Play(self, name);
}

void OnOffEnter(Entity& self, uint8_t state)
{
    OnOffLaserData& d = self.Data<OnOffLaserData>();
    Entity& laser = *d.mLaser;
    if (d.mState == ONOFF_RECEDING && state != ONOFF_RECEDING)
    {
        laser.mV->mVisible = false; // Receding's Exit
    }
    d.mState = state;
    switch (state)
    {
    case ONOFF_IDLE:
        d.mIdle.Reset();
        OnOffPlay(self, "shooter_closed");
        d.mIdle.Add(FxSec(2.0f), 0);
        break;
    case ONOFF_EMERGING:
        OnOffPlay(self, "shooter_open");
        Game_PlaySfx("on_off_laser_shoot");
        laser.mV->mVisible = true;
        Ecs_Play(laser, "emerge");
        Ecs_Flicker(laser, FxSec(0.55f));
        break;
    case ONOFF_HURTING:
        d.mHurting.Reset();
        Ecs_Play(laser, "steam_is_harmful");
        d.mHurting.Add(FxSec(2.0f), 0);
        break;
    default:
        Ecs_Play(laser, "recede");
        Ecs_Flicker(laser, FxSec(0.55f));
        break;
    }
}

void OnOffLaserUpdate(Entity& self)
{
    OnOffLaserData& d = self.Data<OnOffLaserData>();
    Ecs_BaseUpdate(self);
    switch (d.mState)
    {
    case ONOFF_IDLE:
        d.mIdle.Tick();
        while (d.mIdle.Pop() >= 0)
        {
            OnOffEnter(self, ONOFF_EMERGING);
        }
        break;
    case ONOFF_EMERGING:
        if (Ecs_AnimFinished(*d.mLaser))
        {
            OnOffEnter(self, ONOFF_HURTING);
        }
        break;
    case ONOFF_HURTING:
        d.mHurting.Tick();
        while (d.mHurting.Pop() >= 0)
        {
            OnOffEnter(self, ONOFF_RECEDING);
        }
        break;
    default:
        if (Ecs_AnimFinished(*d.mLaser))
        {
            OnOffEnter(self, ONOFF_IDLE);
        }
        break;
    }
}

const EntityClass kOnOffLaser = {
    "OnOffLaser", OnOffLaserUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void OnOffBeamCollided(Entity& self, Entity& other)
{
    if (!Ecs_AnimIs(self, "steam_is_harmful"))
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (other.mClass == &kMover)
    {
        MoverDie(other);
    }
}

const EntityClass kOnOffBeam = {
    "Laser", nullptr, nullptr, OnOffBeamCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER | GROUP_MOVER, 0,
};

// ---- Slasher (Redcave/Slasher.cs) --------------------------------------------------------------------------------
enum SlasherState : uint8_t
{
    SLASHER_MOVE,
    SLASHER_WARNING,
    SLASHER_WIDE_ATTACK,
    SLASHER_WIDE_ATTACK_DELAY,
    SLASHER_LONG_ATTACK,
};

constexpr int kSlasherWideAtkDistance = 36;

struct SlasherData final
{
    uint8_t mState;
    uint8_t mOldFacing;
    int mHealth;
    Entity* mHorizontal; // HorizontalSlash: 36x10 (f_slasher_wide)
    Entity* mVertical;   // VerticalSlash: 10x36 (f_slasher_long)
    // Each state object's TimerState lives as long as the slasher: its clock only runs while the state does.
    TimerState<1> mTimers[5];
};

// PlayFacingFix: right is the left animation flipped.
void SlasherPlayFacing(Entity& self, const char* anim)
{
    if (self.mFacing == FACE_RIGHT)
    {
        char name[24];
        snprintf(name, sizeof(name), "%s_l", anim);
        Ecs_Play(self, name);
        self.mV->mHFlip = true;
    }
    else
    {
        Ecs_PlayFacing(self, anim);
        self.mV->mHFlip = false;
    }
}

void SlasherMove(Entity& self)
{
    SlasherData& d = self.Data<SlasherData>();
    const Entity& p = *World_Player();
    MoveTo(self.mT->mX, p.mT->mX, Fx(20));
    MoveTo(self.mT->mY, p.mT->mY, Fx(20));
    d.mOldFacing = self.mFacing;
    Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
}

// Slash.Reset
void SlashReset(Entity& s, fx x, fx y)
{
    s.mExists = true;
    s.mT->mX = x;
    s.mT->mY = y;
    s.mV->mOffX = s.mV->mOffY = 0;
    s.mV->mHFlip = s.mV->mVFlip = false;
}

void SlasherEnter(Entity& self, uint8_t state)
{
    SlasherData& d = self.Data<SlasherData>();
    d.mState = state;
    TimerState<1>& timer = d.mTimers[state];
    timer.Reset();
    const fx x = self.mT->mX;
    const fx y = self.mT->mY;
    const bool vertical = self.mFacing == FACE_UP || self.mFacing == FACE_DOWN;
    switch (state)
    {
    case SLASHER_MOVE:
        SlasherPlayFacing(self, "float");
        timer.Add(FxSec(1.4f), 0);
        break;
    case SLASHER_WARNING:
        SlasherPlayFacing(self, "warning");
        timer.Add(FxSec(0.5f), 0);
        break;
    case SLASHER_WIDE_ATTACK:
        Game_PlaySfx("slasher_atk");
        if (vertical)
        {
            // HorizontalSlash.UpdateWidePosVertical
            Entity& s = *d.mHorizontal;
            SlashReset(s, x - Fx(10), y);
            s.mV->mOffX = Fx(6);
            Ecs_Play(s, "wide");
            if (self.mFacing == FACE_UP)
            {
                s.mV->mHFlip = s.mV->mVFlip = true;
                s.mT->mY -= Fx(4);
                s.mV->mOffY = Fx(4);
                s.mV->mOffX = Fx(5);
            }
            else
            {
                s.mT->mY += Fx(10);
                s.mV->mOffY = Fx(6);
            }
        }
        else
        {
            // VerticalSlash.UpdateWidePosHorizontal
            Entity& s = *d.mVertical;
            SlashReset(s, x, y - Fx(10));
            s.mV->mOffY = Fx(6);
            Ecs_Play(s, "wide");
            if (self.mFacing == FACE_RIGHT)
            {
                s.mT->mX += Fx(11);
            }
            else
            {
                s.mV->mHFlip = true;
                s.mT->mX -= Fx(5);
                s.mV->mOffX = Fx(6);
            }
        }
        SlasherPlayFacing(self, "attack");
        timer.Add(FxSec(0.4f), 0);
        break;
    case SLASHER_WIDE_ATTACK_DELAY:
        timer.Add(FxSec(0.3f), 0);
        break;
    default:
        Game_PlaySfx("slasher_atk");
        if (vertical)
        {
            // VerticalSlash.UpdateLongPosVertical
            Entity& s = *d.mVertical;
            SlashReset(s, x + Fx(3), y);
            s.mV->mOffX = Fx(2);
            Ecs_Play(s, "tall");
            if (self.mFacing == FACE_UP)
            {
                s.mT->mY -= Fx(36);
                s.mV->mOffY = Fx(10);
                s.mV->mOffX = Fx(3);
            }
            else
            {
                s.mV->mVFlip = true;
                s.mT->mY += Fx(16);
                s.mV->mOffY = Fx(3);
            }
        }
        else
        {
            // HorizontalSlash.UpdateLongPosHorizontal
            Entity& s = *d.mHorizontal;
            SlashReset(s, x, y + Fx(3));
            s.mV->mOffY = Fx(4);
            Ecs_Play(s, "tall");
            if (self.mFacing == FACE_RIGHT)
            {
                s.mT->mX += Fx(16);
                s.mV->mOffX = Fx(4);
            }
            else
            {
                s.mV->mHFlip = true;
                s.mT->mX -= Fx(36);
                s.mV->mOffX = Fx(11);
                s.mV->mOffY = Fx(3);
            }
        }
        SlasherPlayFacing(self, "attack");
        timer.Add(FxSec(0.8f), 0);
        break;
    }
}

void SlasherUpdate(Entity& self)
{
    SlasherData& d = self.Data<SlasherData>();
    Ecs_BaseUpdate(self);
    const uint8_t state = d.mState;
    TimerState<1>& timer = d.mTimers[state];
    timer.Tick();
    while (timer.Pop() >= 0)
    {
        switch (state)
        {
        case SLASHER_MOVE:
            SlasherEnter(self, SLASHER_WARNING);
            break;
        case SLASHER_WARNING:
        {
            const Entity& p = *World_Player();
            const fx dx = Ecs_CenterX(self) - Ecs_VisualCenterX(p);
            const fx dy = Ecs_CenterY(self) - Ecs_VisualCenterY(p);
            const bool close = FxLengthSq(dx, dy) < Fx(kSlasherWideAtkDistance * kSlasherWideAtkDistance);
            SlasherEnter(self, close ? SLASHER_WIDE_ATTACK : SLASHER_LONG_ATTACK);
            break;
        }
        case SLASHER_WIDE_ATTACK:
            SlasherEnter(self, SLASHER_WIDE_ATTACK_DELAY);
            break;
        default:
            SlasherEnter(self, SLASHER_MOVE);
            break;
        }
    }
    // The state that was updated runs its own Update after its timers, even when one of them changed the state.
    if (state == SLASHER_MOVE || state == SLASHER_WARNING)
    {
        SlasherMove(self);
        if (d.mOldFacing != self.mFacing)
        {
            SlasherPlayFacing(self, state == SLASHER_MOVE ? "float" : "warning");
        }
    }
}

void SlasherCollided(Entity& self, Entity&)
{
    SlasherData& d = self.Data<SlasherData>();
    if (self.mV->mFlickering)
    {
        return;
    }
    d.mHealth--;
    Ecs_Flicker(self, FxSec(0.5f));
    if (d.mHealth <= 0)
    {
        Die(self, kFxOne, true);
        Spawn_Explosion(self);
    }
}

const EntityClass kSlasher = {
    "Slasher", SlasherUpdate, nullptr, SlasherCollided, nullptr, nullptr, nullptr, nullptr,
    0,         GROUP_BROOM,   ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_ENEMY,
};

void SlashUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
}

void SlashCollided(Entity& self, Entity& other)
{
    if (self.mV->mVisible && IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kHorizontalSlash = {
    "HorizontalSlash", SlashUpdate, nullptr, SlashCollided, nullptr, nullptr, nullptr, nullptr,
    0,                 GROUP_PLAYER, 0,
};

const EntityClass kVerticalSlash = {
    "VerticalSlash", SlashUpdate, nullptr, SlashCollided, nullptr, nullptr, nullptr, nullptr,
    0,               GROUP_PLAYER, 0,
};

// Dog.DogRegion: a 96x96 box round the dog that wakes it when the player is in it.
void DogRegionUpdate(Entity& self)
{
    Entity* dog = self.mParent;
    if (!dog)
    {
        self.mExists = false;
        return;
    }
    self.mT->mX = Ecs_CenterX(*dog) - Fx(48);
    self.mT->mY = Ecs_CenterY(*dog) - Fx(48);
}

void DogRegionCollided(Entity& self, Entity&)
{
    Entity* dog = self.mParent;
    // TriggerEvent("PlayerInArea"): only Pace handles it, there and then.
    if (dog && dog->mExists && dog->Data<DogData>().mState == DOG_PACE)
    {
        DogEnter(*dog, DOG_ALERT);
    }
}

const EntityClass kDogRegion = {
    "DogRegion", DogRegionUpdate, nullptr, DogRegionCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// ---- Rotator (Rotator.cs) -----------------------------------------------------------------------------------------
void RotatorCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
}

const EntityClass kRotator = {
    "Rotator", nullptr, nullptr, RotatorCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

struct RotatorBulletData final
{
    fx mRadius;
    fx mAngle;
};

void RotatorBulletUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    Entity* parent = self.mParent;
    if (!parent)
    {
        self.mExists = false;
        return;
    }
    RotatorBulletData& d = self.Data<RotatorBulletData>();
    MoveTo(d.mRadius, Fx(59), Fx(30));
    d.mAngle += FxStep(FxF(1.2f));
    if (d.mAngle > kFxTau)
    {
        d.mAngle -= kFxTau;
    }
    self.mT->mX = Ecs_CenterX(*parent) + FxMul(FxCos(d.mAngle), d.mRadius);
    self.mT->mY = Ecs_CenterY(*parent) + FxMul(FxSin(d.mAngle), d.mRadius);
}

void RotatorBulletCollided(Entity&, Entity& other)
{
    if (IsPlayer(other) && Player_State() != PS_AIR)
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kRotatorBullet = {
    "RotatorBullet", RotatorBulletUpdate, nullptr, RotatorBulletCollided, nullptr, nullptr, nullptr, nullptr,
    0,               GROUP_PLAYER,        0,
};

// ---- Seer (Bedroom/Seer.cs) ----------------------------------------------------------------------------------------
// The Bedroom boss. Its orbs, wave and dust are runtime children; its IEnumerator state logic is a state machine.
enum SeerState : uint8_t
{
    SEER_START,
    SEER_APPROACH,  // waiting for the player to come close
    SEER_DIALOGUE,  // before_fight, fading in
    SEER_SHAKE,     // the music starts and the screen shakes
    SEER_STAGE1,    // floating left and right, orbs circling
    SEER_POSITION,  // stage 2: back to the start
    SEER_BOUNCE,    // stage 2: up and down, dropping dust
    SEER_CHARGE,    // stage 2: up into the corner, orbs gathering
    SEER_ORBS_OUT,  // stage 2: the wave charges, the orbs spread out
    SEER_WAVE,      // stage 2: the wave falls
    SEER_WAVE_GONE, // stage 2: the dust goes
    SEER_FADE,      // dead: everything fades to black
    SEER_AFTER,     // after_fight
    SEER_FLASH,     // three short flashes and a long one
    SEER_LIGHT,     // the light grows
};

struct SeerOrbData final
{
    uint8_t mIndex;
    fx mRadius;
    fx mSpeed; // rad/s
    fx mAngle;
};

struct SeerWaveData final
{
    bool mEnd;
};

struct SeerData final
{
    uint8_t mState;
    int8_t mHealth;
    bool mBossRush;
    bool mHorizontalBack; // MoveHorizontal: heading for the left end
    bool mDown;           // MoveVertical: heading down
    uint8_t mBounce;      // MoveVertical's i
    uint8_t mFlashes;
    fx mYTimer; // seconds
    fx mPresetX;
    fx mPresetY;
    // The C#'s SubEntities, in its order (orbs, dusts, wave): made with the Seer, kept while they don't exist.
    Entity* mOrbs[2];
    Entity* mDusts[3];
    Entity* mWave;
};

void SeerOrbCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && self.mV->mOpacity == kFxOne)
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kSeerOrb = {
    "SeerOrb", nullptr, nullptr, SeerOrbCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

void SeerWaveUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    SeerWaveData& d = self.Data<SeerWaveData>();
    if (d.mEnd && Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
    if (self.mT->mY > Fx(160))
    {
        d.mEnd = true;
        self.mExists = false;
    }
}

void SeerWaveCollided(Entity& self, Entity& other)
{
    SeerWaveData& d = self.Data<SeerWaveData>();
    if (IsPlayer(other) && !d.mEnd && self.mV->mOpacity == kFxOne)
    {
        Player_ReceiveDamage(1);
        if (self.mT->mVelX == 0 && self.mT->mVelY == 0)
        {
            self.mExists = false;
        }
    }
    Ecs_Play(self, "evaporate");
    d.mEnd = true;
}

const EntityClass kSeerWave = {
    "SeerWave", SeerWaveUpdate, nullptr, SeerWaveCollided, nullptr, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER | GROUP_DUST, 0,
};

void SeerLightScale(fx target, fx speed)
{
    Entity* light = Darkness_PlayerLight();
    if (light)
    {
        MoveTo(light->mV->mScale, target, speed);
    }
}

bool SeerLightScaleReached(fx target, fx speed)
{
    Entity* light = Darkness_PlayerLight();
    return !light || MoveTo(light->mV->mScale, target, speed);
}

// MoveHorizontal: back and forth between 60 px right and 50 px left of the start, at 50 px/s.
void SeerHorizontal(Entity& self)
{
    SeerData& d = self.Data<SeerData>();
    if (MoveTo(self.mT->mX, d.mPresetX + (d.mHorizontalBack ? -Fx(50) : Fx(60)), Fx(50)))
    {
        d.mHorizontalBack = !d.mHorizontalBack;
        MoveTo(self.mT->mX, d.mPresetX + (d.mHorizontalBack ? -Fx(50) : Fx(60)), Fx(50));
    }
}

// MoveVertical: up 32 px, then down to 80 px below the start, four times; each landing shakes the screen and drops
// a dust (the first three). Like the C# loop, a landing heads straight back up in the same frame. False once done.
bool SeerVertical(Entity& self)
{
    SeerData& d = self.Data<SeerData>();
    for (;;)
    {
        if (!d.mDown)
        {
            if (!MoveTo(self.mT->mY, d.mPresetY - Fx(32), Fx(120)))
            {
                return true;
            }
            d.mDown = true;
        }
        if (!MoveTo(self.mT->mY, d.mPresetY + Fx(80), Fx(90)))
        {
            return true;
        }
        Screen_Shake(FxF(0.05f), FxF(0.3f));
        if (d.mBounce < 3)
        {
            Entity& dust = *d.mDusts[d.mBounce];
            dust.mT->mX = dust.mT->mLastX = self.mT->mX;
            dust.mT->mY = dust.mT->mLastY = self.mT->mY;
            dust.mExists = true;
            Ecs_Play(dust, "unpoof");
        }
        Game_PlaySfx("hit_wall");
        d.mDown = false;
        if (++d.mBounce > 3)
        {
            return false;
        }
    }
}

void SeerRotateOrbs(Entity& self)
{
    SeerData& d = self.Data<SeerData>();
    for (Entity* orb : d.mOrbs)
    {
        SeerOrbData& o = orb->Data<SeerOrbData>();
        o.mAngle += FxStep(o.mSpeed); // (never wrapped, as in the C#: the float rounding follows it)
        orb->mT->mX = Ecs_CenterX(self) + FxMul(o.mRadius, FxCos(o.mAngle));
        orb->mT->mY = Ecs_CenterY(self) + FxMul(o.mRadius, FxSin(o.mAngle));
    }
}

void SeerFadeEverything(Entity& self)
{
    SeerData& d = self.Data<SeerData>();
    MoveTo(self.mV->mOpacity, 0, Fx(3));
    for (Entity* orb : d.mOrbs)
    {
        MoveTo(orb->mV->mOpacity, 0, Fx(2));
    }
    MoveTo(d.mWave->mV->mOpacity, 0, Fx(4));
}

bool SeerAnyDust(const SeerData& d)
{
    for (const Entity* dust : d.mDusts)
    {
        if (dust->mExists)
        {
            return true;
        }
    }
    return false;
}

void SeerEnter(Entity& self, uint8_t state)
{
    SeerData& d = self.Data<SeerData>();
    d.mState = state;
    if (state == SEER_BOUNCE)
    {
        d.mHorizontalBack = false;
        d.mDown = false;
        d.mBounce = 0;
    }
}

// StateLogic's IEnumerator: each case runs on, as the C# does until its next yield, into the next one in the same
// frame; a break is the yield.
void SeerUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    SeerData& d = self.Data<SeerData>();
    Transform& t = *self.mT;
    if (d.mHealth <= 0 && d.mState >= SEER_STAGE1 && d.mState <= SEER_WAVE_GONE)
    {
        Darkness_TargetAlpha(kFxOne, FxF(0.6f));
        SeerEnter(self, SEER_FADE);
    }
    switch (d.mState)
    {
    case SEER_START:
        if (!d.mBossRush)
        {
            Darkness_TargetAlpha(FxF(0.9f), FxF(0.36f));
            Game_PlaySong("");
        }
        else
        {
            self.mV->mOpacity = kFxOne;
        }
        SeerEnter(self, SEER_APPROACH);
        [[fallthrough]];
    case SEER_APPROACH:
        if (World_Player()->mT->mY > t.mY + Fx(48))
        {
            SeerLightScale(FxF(1.7f), FxF(1.2f));
            break;
        }
        if (d.mBossRush)
        {
            Game_PlaySong("bedroom-boss");
            Darkness_TargetAlpha(FxF(0.1f), FxF(1.2f));
            Screen_Shake(FxF(0.01f), Fx(3));
            SeerEnter(self, SEER_SHAKE);
            goto shake;
        }
        Game_ShowDialogue("sun_guy", "before_fight");
        SeerEnter(self, SEER_DIALOGUE);
        [[fallthrough]];
    case SEER_DIALOGUE:
        if (!Game_DialogueFinished())
        {
            SeerLightScale(FxF(1.7f), FxF(1.2f));
            MoveTo(self.mV->mOpacity, kFxOne, FxF(0.42f));
            break;
        }
        self.mV->mOpacity = kFxOne;
        Game_PlaySong("bedroom-boss");
        Darkness_TargetAlpha(FxF(0.1f), FxF(1.2f));
        Screen_Shake(FxF(0.01f), Fx(3));
        SeerEnter(self, SEER_SHAKE);
        [[fallthrough]];
    case SEER_SHAKE:
    shake:
        if (Screen_ShakeActive())
        {
            SeerLightScale(kFxOne, FxF(1.2f));
            break;
        }
        for (Entity* orb : d.mOrbs)
        {
            orb->mExists = true;
        }
        d.mHorizontalBack = false;
        d.mYTimer = 0;
        SeerEnter(self, SEER_STAGE1);
        [[fallthrough]];
    case SEER_STAGE1:
        if (d.mHealth > 6)
        {
            d.mYTimer += FxStep(kFxOne);
            t.mY = d.mPresetY + 10 * FxSin(d.mYTimer);
            SeerHorizontal(self);
            SeerRotateOrbs(self);
            break;
        }
        SeerEnter(self, SEER_POSITION);
        [[fallthrough]];
    case SEER_POSITION:
    position:
        // (&&: the y only moves once the x is there.)
        if (!(MoveTo(t.mX, d.mPresetX, Fx(90)) && MoveTo(t.mY, d.mPresetY, Fx(12))))
        {
            SeerRotateOrbs(self);
            break;
        }
        SeerEnter(self, SEER_BOUNCE);
        [[fallthrough]];
    case SEER_BOUNCE:
        if (SeerVertical(self))
        {
            for (Entity* orb : d.mOrbs)
            {
                SeerOrbData& o = orb->Data<SeerOrbData>();
                MoveTo(o.mRadius, Fx(8), Fx(30));
                MoveTo(o.mSpeed, Fx(240), FxF(0.6f));
            }
            SeerRotateOrbs(self);
            SeerHorizontal(self);
            break;
        }
        SeerEnter(self, SEER_CHARGE);
        [[fallthrough]];
    case SEER_CHARGE:
    {
        // (&: both move every frame.)
        const bool x = MoveTo(t.mX, d.mPresetX + Fx(30), Fx(60));
        const bool y = MoveTo(t.mY, d.mPresetY - Fx(20), Fx(60));
        if (!(x && y))
        {
            MoveTo(d.mOrbs[0]->mT->mX, t.mX + Fx(4), Fx(120));
            MoveTo(d.mOrbs[0]->mT->mY, t.mY + Fx(10), Fx(120));
            MoveTo(d.mOrbs[1]->mT->mX, t.mX - Fx(4), Fx(120));
            MoveTo(d.mOrbs[1]->mT->mY, t.mY + Fx(10), Fx(120));
            break;
        }
        Game_PlaySfx("sun_guy_charge");
        Entity& wave = *d.mWave;
        wave.mExists = true;
        wave.Data<SeerWaveData>().mEnd = false;
        wave.mT->mX = d.mPresetX - Fx(50);
        wave.mT->mY = d.mPresetY - Fx(10);
        wave.mT->mVelX = wave.mT->mVelY = 0;
        Ecs_Play(wave, "play");
        Ecs_Flicker(wave, FxSec(3.0f));
        SeerEnter(self, SEER_ORBS_OUT);
    }
        [[fallthrough]];
    case SEER_ORBS_OUT:
    {
        Entity& wave = *d.mWave;
        bool& end = wave.Data<SeerWaveData>().mEnd;
        // (&: both orbs move every frame; no shake on the frame they both get there.)
        const bool a = MoveTo(d.mOrbs[0]->mT->mX, wave.mT->mX, Fx(18));
        const bool b = MoveTo(d.mOrbs[1]->mT->mX, d.mPresetX + Fx(75), Fx(18));
        if (!(a && b))
        {
            Screen_Shake(FxF(0.02f), FxF(0.1f));
            if (!end)
            {
                break;
            }
        }
        if (!end)
        {
            Game_PlaySfx("sun_guy_death_short");
        }
        wave.mT->mVelY = Fx(30);
        d.mHorizontalBack = false;
        SeerEnter(self, SEER_WAVE);
    }
        [[fallthrough]];
    case SEER_WAVE:
        if (!d.mWave->Data<SeerWaveData>().mEnd)
        {
            SeerHorizontal(self);
            break;
        }
        if (d.mWave->mExists)
        {
            break;
        }
        for (Entity* dust : d.mDusts)
        {
            Ecs_Play(*dust, "poof");
        }
        Player_DropBroomDust();
        SeerEnter(self, SEER_WAVE_GONE);
        [[fallthrough]];
    case SEER_WAVE_GONE:
        if (SeerAnyDust(d))
        {
            break;
        }
        SeerEnter(self, SEER_POSITION);
        goto position;
    case SEER_FADE:
        if (!Darkness_ReachedAlpha())
        {
            SeerFadeEverything(self);
            break;
        }
        if (!d.mBossRush)
        {
            Game_ShowDialogue("sun_guy", "after_fight");
        }
        SeerEnter(self, SEER_AFTER);
        [[fallthrough]];
    case SEER_AFTER:
        if (!d.mBossRush && !Game_DialogueFinished())
        {
            SeerFadeEverything(self);
            break;
        }
        d.mFlashes = 0;
        SeerEnter(self, SEER_FLASH);
        [[fallthrough]];
    case SEER_FLASH:
        if (Screen_FlashActive())
        {
            break;
        }
        if (d.mFlashes < 3)
        {
            Screen_Flash(FxF(2.38f));
            Screen_Shake(FxF(0.01f), kFxOne);
            Game_PlaySfx("sun_guy_death_short");
            d.mFlashes++;
            break;
        }
        if (d.mFlashes == 3)
        {
            Screen_Flash(FxF(3.3f));
            Screen_Shake(FxF(0.02f), Fx(2));
            Game_PlaySfx("sun_guy_death_long");
            d.mFlashes++;
            break;
        }
        Darkness_TargetAlpha(FxF(0.75f), FxF(1.25f));
        SeerEnter(self, SEER_LIGHT);
        [[fallthrough]];
    case SEER_LIGHT:
        if (!SeerLightScaleReached(Fx(4), FxF(3.6f)))
        {
            break;
        }
        if (!d.mBossRush)
        {
            Game_SetBossDefeated("BEDROOM");
            Darkness_ForceAlpha(FxF(0.5f));
        }
        else
        {
            Darkness_ForceAlpha(0);
        }
        if (self.mPreset)
        {
            Preset_SetAlive(*self.mPreset, false);
        }
        self.mExists = false;
        for (Entity* orb : d.mOrbs)
        {
            orb->mExists = false;
        }
        for (Entity* dust : d.mDusts)
        {
            dust->mExists = false;
        }
        d.mWave->mExists = false;
        Game_PlaySong("bedroom");
        break;
    default:
        break;
    }
}

void SeerCollided(Entity& self, Entity& other)
{
    SeerData& d = self.Data<SeerData>();
    if (IsPlayer(other) && self.mV->mOpacity == kFxOne)
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other) && !self.mV->mFlickering && d.mHealth > 0)
    {
        d.mHealth--;
        Ecs_Flicker(self, FxSec(1.5f));
        Game_PlaySfx("sun_guy_scream2");
    }
}

const EntityClass kSeer = {
    "Seer",       SeerUpdate, nullptr, SeerCollided, nullptr, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER | GROUP_BROOM, ECF_ENEMY,
};

// ---- Red_Boss (Redcave/Red_Boss.cs) -----------------------------------------------------------------------------
// The boss and its sub-entities, in the C#'s SubEntities order: Ripple, SmallWave, BigWave, the four SplashBullets
// (each with its Shadow), the four ProximitySensors and the four Tentacles (each with its tentacle Entity).
constexpr int kRedBossPool = 4;
constexpr fx kRedBossPushTick = FxF(0.15f);

enum RedBossState : uint8_t
{
    RB_START,
    RB_WAIT_PLAYER, // until the player is 48 px into the room
    RB_DIALOGUE,    // rising while "before_fight" shows
    RB_RISE,        // (boss rush) rising without it
    RB_FIGHT,
    RB_AFTER,       // "after_fight"
    RB_SINK,
    RB_FINAL,
    RB_DONE,
};

enum RedBossFsm : uint8_t
{
    RBF_SPLASH,
    RBF_DASH,
    RBF_STUN,
};

enum RedBossTimer : uint8_t
{
    RBT_SPLASH,
    RBT_TENTACLES,
    RBT_END_DASH,
};

struct RedBossData final
{
    uint8_t mState;
    uint8_t mFsm;
    uint8_t mStun; // StunStateLogic's step
    int8_t mHealth;
    uint8_t mProximity;  // proximity_hits (Touching)
    uint8_t mSplashClose; // SplashState.got_too_close
    uint8_t mDashClose;   // DashState.got_too_close
    bool mBossRush;
    bool mLoopSfx;
    bool mVolume; // its VolumeEvent(0, 3) is fading the song out
    fx mInvincible;
    fx mAmp;
    fx mPushdown;
    fx mPushTimer;
    fx mFinalTimer;
    fx mTargetX; // StunStateLogic's target
    fx mTargetY;
    TimerState<2> mSplash; // Splash and Tentacles, every 3 s
    TimerState<2> mDash;   // Tentacles every 1.5 s, EndDash every 5 s
    Entity* mRipple;
    Entity* mSmallWave;
    Entity* mBigWave;
};

struct SplashBulletData final
{
    uint8_t mState;
    fx mStartX;
    fx mStartY;
    fx mT; // its Parabola_Thing
    fx mPeriod;
    Entity* mShadow;
};

enum SplashBulletState : uint8_t
{
    SB_SHADOW, // the shadow flickers where it will land
    SB_ARC,
    SB_EXPLODE,
    SB_DONE,
};

struct TentacleData final
{
    uint8_t mIndex;
    uint8_t mState;
    fx mDropTimer;
    fx mEndTimer;
    Entity* mTentacle; // the tentacle itself: just visual flair
};

enum TentacleState : uint8_t
{
    TENT_WARN, // flickering
    TENT_DROP,
    TENT_HOLD,
    TENT_RAISE,
    TENT_END,
    TENT_DONE,
};

struct ProximitySensorData final
{
    uint8_t mDir; // Touching
    fx mOffX;
    fx mOffY;
};

struct BigWaveData final
{
    bool mDisablePlayerHit;
};

fx RoomLeft()
{
    return Fx(gGame.mGridX * 160);
}

fx RoomTop()
{
    return Fx(gGame.mGridY * 160);
}

// MapUtilities.GetInGridPosition (each axis against its own room).
fx InGrid(fx v)
{
    return v - Fx(FxTrunc(v / 160) * 160);
}

void BubbleSfx()
{
    static const char* const kBubbles[] = { "bubble_1", "bubble_1", "bubble_2", "bubble_3" };
    Game_PlaySfxOneOf(kBubbles, 4);
}

// Shadow.UpdateShadow (ShadowType.Normal): jumpHeight 0..1.
void UpdateEntityShadow(Entity& shadow, fx jumpHeight)
{
    const int frame = FxCeil(jumpHeight * 4) - 1;
    if (frame < 0)
    {
        shadow.mV->mVisible = false;
    }
    else
    {
        shadow.mV->mVisible = true;
        Ecs_SetFrame(shadow, frame);
    }
}

// (the shadow's Update and flicker run in its bullet's base.Update)
void EntityShadowUpdate(Entity&)
{
}

const EntityClass kEntityShadow = {
    "Shadow", EntityShadowUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void RippleUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    self.mT->mX = self.mParent->mT->mX - Fx(11);
    self.mT->mY = self.mParent->mT->mY + Fx(17);
}

const EntityClass kRipple = {
    "Ripple", RippleUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- SmallWave
void SmallWaveUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimFinished(self))
    {
        self.mT->mVelX = self.mT->mVelY = 0;
        self.mExists = false;
        return;
    }
    if (InGrid(self.mT->mX) < Fx(32))
    {
        Ecs_Play(self, "fall");
    }
}

void SmallWaveCollided(Entity&, Entity& other)
{
    MoveTo(other.mT->mX, 0, Fx(30));
}

const EntityClass kSmallWave = {
    "SmallWave", SmallWaveUpdate, nullptr, SmallWaveCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

void SmallWaveRise(Entity& w)
{
    w.mT->mX = RoomLeft() + Fx(96);
    w.mT->mY = RoomTop() + Fx(48);
    Ecs_Play(w, "rise");
    w.mExists = true;
}

void SmallWaveLaunch(Entity& w)
{
    w.mT->mVelX = -Fx(20);
    Screen_Shake(FxF(0.03f), kFxOne);
    Ecs_Play(w, "move");
    Game_PlaySfx("small_wave");
}

// ---- BigWave
void BigWaveLaunch(Entity& w)
{
    w.mT->mVelX = -Fx(40);
    Screen_Shake(FxF(0.05f), kFxOne);
    Ecs_Play(w, "move");
    Game_PlaySfx("big_wave");
    w.Data<BigWaveData>().mDisablePlayerHit = false;
}

void BigWaveUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimFinished(self))
    {
        if (Ecs_AnimIs(self, "fall"))
        {
            self.mT->mVelX = self.mT->mVelY = 0;
            self.mExists = false;
            return;
        }
        BigWaveLaunch(self); // (rise)
    }
    if (InGrid(self.mT->mX) < Fx(32))
    {
        Ecs_Play(self, "fall");
        self.Data<BigWaveData>().mDisablePlayerHit = true;
    }
}

void BigWaveCollided(Entity& self, Entity& other)
{
    BigWaveData& d = self.Data<BigWaveData>();
    if (IsPlayer(other) && !d.mDisablePlayerHit)
    {
        Game_PlaySfx("player_hit_1");
        // target = p.Position + ((16, 128) - GetInGridPosition(p.Position))
        const fx px = other.mT->mX;
        const fx py = other.mT->mY;
        Player_AutoJump(kFxOne, px + (Fx(16) - InGrid(px)), py + (Fx(8 * 16) - InGrid(py)), 50, -Fx(500));
        d.mDisablePlayerHit = true;
    }
}

const EntityClass kBigWave = {
    "BigWave", BigWaveUpdate, nullptr, BigWaveCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

void BigWaveRise(Entity& w)
{
    w.mT->mX = RoomLeft() + Fx(80);
    w.mT->mY = RoomTop() + Fx(48);
    Ecs_Play(w, "rise");
    w.mExists = true;
}

// ---- SplashBullet
void SplashBulletSetExists(Entity& b, bool exists)
{
    b.mExists = exists;
    b.Data<SplashBulletData>().mShadow->mExists = exists;
}

// EntityPool.Spawn: Spawn, i.e. Logic up to its first yield.
void SplashBulletStart(Entity& b)
{
    SplashBulletData& d = b.Data<SplashBulletData>();
    Entity& shadow = *d.mShadow;
    SplashBulletSetExists(b, true);
    d.mT = 0;
    b.mT->mX = d.mStartX;
    b.mT->mY = d.mStartY;
    b.mT->mVelX = b.mT->mVelY = 0;
    b.mV->mVisible = false;
    shadow.mV->mVisible = true;
    Ecs_Flicker(shadow, FxSec(0.5f));
    Ecs_Play(b, "move");
    d.mState = SB_SHADOW;
}

void SplashBulletUpdate(Entity& self)
{
    SplashBulletData& d = self.Data<SplashBulletData>();
    Entity& shadow = *d.mShadow;
    Ecs_BaseUpdate(self);
    // shadow.Update: Position = parent.Position - (offset - sprite size / 4), then its flicker
    shadow.mT->mX = self.mT->mX + Fx(2);
    shadow.mT->mY = self.mT->mY + Fx(2);
    Ecs_BaseUpdate(shadow);
    switch (d.mState)
    {
    case SB_SHADOW:
        if (shadow.mV->mFlickering)
        {
            break;
        }
        self.mV->mVisible = true;
        BubbleSfx();
        self.mT->mVelX = Fx(Rng_Range(10, 18));
        d.mState = SB_ARC;
        [[fallthrough]];
    case SB_ARC:
    {
        // Parabola_Thing.Tick, with the shadow following the height
        d.mT += kFxTick;
        if (d.mT <= d.mPeriod)
        {
            self.mV->mOffY = ParabolaOffset(d.mT, d.mPeriod, 48);
            fx progress = FxDiv(d.mT, d.mPeriod);
            progress = progress > kFxOne ? kFxOne : progress;
            const fx fromMiddle = kFxOne / 2 - progress;
            UpdateEntityShadow(shadow, (fromMiddle < 0 ? -fromMiddle : fromMiddle) * 2);
            break;
        }
        self.mV->mOffY = 0;
        UpdateEntityShadow(shadow, 0);
        Ecs_Play(self, "explode");
        d.mState = SB_EXPLODE;
    }
        [[fallthrough]];
    case SB_EXPLODE:
        if (!Ecs_AnimFinished(self))
        {
            break;
        }
        BubbleSfx();
        SplashBulletSetExists(self, false);
        d.mState = SB_DONE;
        break;
    default:
        break;
    }
}

void SplashBulletCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && self.mV->mVisible && self.mV->mOffY < Fx(10))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kSplashBullet = {
    "SplashBullet", SplashBulletUpdate, nullptr, SplashBulletCollided, nullptr, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER,       0,
};

// ---- Tentacle (the tentacle's base, for collision: the tentacle itself is just visual flair)
const EntityClass kTentacleFlair = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void TentacleStart(Entity& t, uint8_t dir, const Entity& boss)
{
    TentacleData& d = t.Data<TentacleData>();
    const fx i = Fx(d.mIndex);
    const fx bx = boss.mT->mX;
    const fx by = boss.mT->mY;
    if (dir & TOUCH_UP)
    {
        t.mT->mX = bx + (16 * i - Fx(14));
        t.mT->mY = by - Fx(13);
    }
    else if (dir & TOUCH_LEFT)
    {
        t.mT->mX = bx - Fx(14);
        t.mT->mY = by + (16 * i - Fx(16));
    }
    else if (dir & TOUCH_RIGHT)
    {
        t.mT->mX = bx + Fx(boss.mB->mW + 2);
        t.mT->mY = by + (16 * i - Fx(16));
    }
    else if (dir & TOUCH_DOWN)
    {
        t.mT->mX = bx + (16 * i - Fx(14));
        t.mT->mY = by + Fx(boss.mB->mH + 2);
    }
    else
    {
        // the player isn't close in any direction, so random locations
        t.mT->mX = RoomLeft() + Fx(16) + Fx(12 * (1 + d.mIndex)) + Fx(Rng_Range(-5, 6));
        const int row = Rng_Range(1, 4);
        const int jitter = Rng_Range(-5, 6);
        t.mT->mY = RoomTop() + Fx(16 * row) + Fx(jitter) + Fx(64) - Fx(10);
    }
    Entity& flair = *d.mTentacle;
    flair.mT->mX = t.mT->mX;
    flair.mT->mY = t.mT->mY + Fx(10 - 3 - 64);
    flair.mV->mYPush = Fx(64);
    Ecs_Flicker(t, FxF(0.7f) + Rng_Unit());
    d.mState = TENT_WARN;
}

void TentacleUpdate(Entity& self)
{
    TentacleData& d = self.Data<TentacleData>();
    Entity& flair = *d.mTentacle;
    Ecs_BaseUpdate(self);
    switch (d.mState)
    {
    case TENT_WARN:
        if (self.mV->mFlickering)
        {
            break;
        }
        BubbleSfx();
        flair.mExists = true;
        d.mDropTimer = FxF(1.0f);
        d.mState = TENT_DROP;
        [[fallthrough]];
    case TENT_DROP:
        if (!MoveTo(flair.mV->mYPush, 0, Fx(120)))
        {
            d.mDropTimer -= kFxTick;
            break;
        }
        d.mState = TENT_HOLD;
        [[fallthrough]];
    case TENT_HOLD:
        if (d.mDropTimer > 0)
        {
            d.mDropTimer -= kFxTick;
            break;
        }
        d.mEndTimer = FxF(0.3f);
        d.mState = TENT_RAISE;
        [[fallthrough]];
    case TENT_RAISE:
        if (!MoveTo(flair.mV->mYPush, Fx(64), Fx(180)))
        {
            d.mEndTimer -= kFxTick;
            break;
        }
        d.mState = TENT_END;
        [[fallthrough]];
    case TENT_END:
        if (d.mEndTimer > 0)
        {
            d.mEndTimer -= kFxTick;
            break;
        }
        self.mExists = false;
        flair.mExists = false;
        d.mState = TENT_DONE;
        break;
    default:
        break;
    }
}

void TentacleCollided(Entity& self, Entity& other)
{
    if (!self.mV->mFlickering && IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kTentacle = {
    "Tentacle", TentacleUpdate, nullptr, TentacleCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

void TentacleDespawn(Entity& t)
{
    t.mExists = false;
    t.Data<TentacleData>().mTentacle->mExists = false;
}

// ---- ProximitySensor
void ProximitySensorUpdate(Entity& self)
{
    const ProximitySensorData& d = self.Data<ProximitySensorData>();
    Ecs_BaseUpdate(self);
    self.mT->mX = self.mParent->mT->mX + d.mOffX;
    self.mT->mY = self.mParent->mT->mY + d.mOffY;
}

void ProximitySensorCollided(Entity& self, Entity&)
{
    self.mParent->Data<RedBossData>().mProximity |= self.Data<ProximitySensorData>().mDir;
}

const EntityClass kProximitySensor = {
    "ProximitySensor", ProximitySensorUpdate, nullptr, ProximitySensorCollided, nullptr, nullptr, nullptr, nullptr,
    0,                 GROUP_PLAYER,          0,
};

// ---- Red_Boss
// The boss's pooled sub-entities of one class, in order.
template <typename Fn>
void ForRedBossPool(Entity& boss, const EntityClass& cls, Fn fn)
{
    for (Entity* e = Ecs_First(); e; e = e->mNext)
    {
        if (e->mParent == &boss && e->mClass == &cls)
        {
            fn(*e);
        }
    }
}

void RedBossSpawnBullets(Entity& self)
{
    ForRedBossPool(self, kSplashBullet, [](Entity& b) {
        if (!b.mExists)
        {
            SplashBulletStart(b);
        }
    });
}

void RedBossSpawnTentacles(Entity& self)
{
    if (Ecs_CountChildren(self, kTentacle) != 0)
    {
        return;
    }
    const uint8_t dir = self.Data<RedBossData>().mProximity;
    ForRedBossPool(self, kTentacle, [&](Entity& t) {
        t.mExists = true;
        TentacleStart(t, dir, self);
    });
}

void RedBossChangeState(Entity& self, uint8_t state)
{
    RedBossData& d = self.Data<RedBossData>();
    if (d.mFsm == RBF_DASH)
    {
        self.mT->mVelX = self.mT->mVelY = 0; // Dash's Exit
    }
    d.mFsm = state;
    switch (state)
    {
    case RBF_SPLASH:
        d.mAmp = Fx(5);
        break;
    case RBF_DASH:
        d.mAmp = 0;
        self.mT->mVelX = Fx(30);
        self.mT->mVelY = Fx(20);
        Ecs_Play(self, "bob");
        break;
    default:
        d.mStun = 0; // a new StunStateLogic
        break;
    }
}

// Dash's Update: bounce off the room's inner walls, shaking along the axes that bounced.
void RedBossDashUpdate(Entity& self)
{
    Transform& t = *self.mT;
    uint8_t dirs = SHAKE_NONE;
    const fx tlX = InGrid(t.mX);
    const fx tlY = InGrid(t.mY);
    const fx brX = InGrid(t.mX + Fx(self.mB->mW));
    const fx brY = InGrid(t.mY + Fx(self.mB->mH));
    if (tlY < Fx(2 * 16))
    {
        t.mVelY = Fx(60);
        dirs |= SHAKE_VERTICAL;
    }
    else if (brY > Fx(16 * 8))
    {
        t.mVelY = -Fx(60);
        dirs |= SHAKE_VERTICAL;
    }
    if (tlX < Fx(2 * 16))
    {
        t.mVelX = Fx(60);
        dirs |= SHAKE_HORIZONTAL;
    }
    else if (brX > Fx(16 * 8))
    {
        t.mVelX = -Fx(60);
        dirs |= SHAKE_HORIZONTAL;
    }
    Screen_Shake(FxF(0.05f), FxF(0.1f), dirs);
}

// StunStateLogic: each case runs on into the next in the same frame; a return true is the yield, false the end.
bool RedBossStunStep(Entity& self)
{
    RedBossData& d = self.Data<RedBossData>();
    Transform& t = *self.mT;
    const fx yPush = self.mV->mYPush;
    switch (d.mStun)
    {
    case 0:
        Ecs_Play(self, "warn");
        d.mAmp = Fx(5);
        d.mTargetX = RoomLeft() + Fx(6 * 16);
        d.mTargetY = RoomTop() + Fx(4 * 16);
        SmallWaveRise(*d.mSmallWave);
        d.mStun = 1;
        [[fallthrough]];
    case 1:
    {
        // (&: both move every frame)
        const bool x = MoveTo(t.mX, d.mTargetX, Fx(30));
        const bool y = MoveTo(t.mY, d.mTargetY, Fx(30));
        if (!(x && y))
        {
            return true;
        }
        d.mStun = 2;
    }
        [[fallthrough]];
    case 2:
        if (yPush != 0)
        {
            return true;
        }
        d.mLoopSfx = true;
        d.mAmp = Fx(13);
        d.mStun = 3;
        [[fallthrough]];
    case 3:
        if (yPush < d.mAmp)
        {
            return true;
        }
        SmallWaveLaunch(*d.mSmallWave);
        d.mStun = 4;
        [[fallthrough]];
    case 4:
        if (yPush != 0)
        {
            return true;
        }
        d.mAmp = Fx(20);
        d.mStun = 5;
        [[fallthrough]];
    case 5:
        if (yPush < Fx(18))
        {
            return true;
        }
        BigWaveRise(*d.mBigWave);
        d.mStun = 6;
        [[fallthrough]];
    case 6:
        if (d.mBigWave->mT->mVelX == 0 && d.mBigWave->mT->mVelY == 0)
        {
            return true;
        }
        Ecs_Play(self, "close_eyes");
        d.mStun = 7;
        [[fallthrough]];
    case 7:
        if (yPush > Fx(2))
        {
            return true;
        }
        d.mAmp = Fx(5);
        d.mStun = 8;
        [[fallthrough]];
    case 8:
        if (d.mSmallWave->mExists || d.mBigWave->mExists)
        {
            return true;
        }
        d.mTargetX -= Fx(16);
        d.mStun = 9;
        [[fallthrough]];
    case 9:
    {
        const bool x = MoveTo(t.mX, d.mTargetX, Fx(30));
        const bool y = MoveTo(t.mY, d.mTargetY, Fx(30));
        if (!(x && y))
        {
            return true;
        }
        d.mLoopSfx = false;
        d.mStun = 10;
        return false;
    }
    default:
        return false;
    }
}

// The FSM's Update: TimerState fires its timers (on the state that owns them, even once one changed the state),
// then base.Update runs that state's Update.
void RedBossFsmUpdate(Entity& self)
{
    RedBossData& d = self.Data<RedBossData>();
    switch (d.mFsm)
    {
    case RBF_SPLASH:
    {
        d.mSplash.Tick();
        int ev;
        while ((ev = d.mSplash.Pop()) >= 0)
        {
            if (ev == RBT_SPLASH)
            {
                RedBossSpawnBullets(self);
                continue;
            }
            RedBossSpawnTentacles(self);
            if (d.mProximity != TOUCH_NONE && ++d.mSplashClose == 2)
            {
                d.mSplashClose = 0;
                RedBossChangeState(self, RBF_STUN);
            }
        }
        break;
    }
    case RBF_DASH:
    {
        d.mDash.Tick();
        int ev;
        while ((ev = d.mDash.Pop()) >= 0)
        {
            if (ev == RBT_END_DASH)
            {
                RedBossChangeState(self, RBF_STUN);
                continue;
            }
            RedBossSpawnTentacles(self);
            if (d.mProximity != TOUCH_NONE && ++d.mDashClose == 2)
            {
                d.mDashClose = 0;
                RedBossChangeState(self, RBF_SPLASH);
            }
        }
        RedBossDashUpdate(self);
        break;
    }
    default:
        if (!RedBossStunStep(self))
        {
            RedBossChangeState(self, RBF_DASH);
        }
        break;
    }
}

// The intro's rise: a pixel up every 0.15 s, with a little shake.
void RedBossPushTick(Entity& self)
{
    RedBossData& d = self.Data<RedBossData>();
    d.mPushTimer += kFxTick;
    if (d.mPushTimer >= kRedBossPushTick)
    {
        d.mPushTimer = 0;
        if (self.mV->mYPush > 0)
        {
            Screen_Shake(FxF(0.021f), FxF(0.1f));
            self.mV->mYPush -= kFxOne;
        }
    }
}

// State()'s IEnumerator: each case runs on into the next one in the same frame; a break is the yield.
void RedBossStep(Entity& self)
{
    RedBossData& d = self.Data<RedBossData>();
    Visual& v = *self.mV;
    switch (d.mState)
    {
    case RB_START:
        d.mPushTimer = 0;
        v.mYPush = Fx(Ecs_SpriteHeight(self));
        Player_SetGridEntranceAt(RoomLeft() + Fx(20), RoomTop() + Fx(20));
        d.mVolume = true; // GlobalState.SpawnEntity(new VolumeEvent(0, 3))
        d.mState = RB_WAIT_PLAYER;
        [[fallthrough]];
    case RB_WAIT_PLAYER:
        if (InGrid(World_Player()->mT->mX) < Fx(48))
        {
            break;
        }
        d.mLoopSfx = true;
        if (d.mBossRush)
        {
            d.mState = RB_RISE;
            goto rise;
        }
        Game_ShowDialogue("redboss", "before_fight");
        d.mState = RB_DIALOGUE;
        [[fallthrough]];
    case RB_DIALOGUE:
        if (!Game_DialogueFinished())
        {
            RedBossPushTick(self);
            break;
        }
        goto fight;
    case RB_RISE:
    rise:
        if (v.mYPush > 0)
        {
            RedBossPushTick(self);
            break;
        }
    fight:
        d.mLoopSfx = false;
        Game_PlaySong("redcave-boss");
        Ecs_Play(self, "bob");
        RedBossChangeState(self, RBF_SPLASH);
        RedBossSpawnBullets(self); // TriggerEvent("Splash"): the first splash is instant
        d.mState = RB_FIGHT;
        [[fallthrough]];
    case RB_FIGHT:
        if (d.mHealth > 0)
        {
            RedBossFsmUpdate(self);
            d.mPushdown += kFxTick * 3;
            v.mYPush = Fx(FxTrunc(d.mAmp + FxMul(FxSin(d.mPushdown), d.mAmp)));
            break;
        }
        self.mT->mVelX = self.mT->mVelY = 0;
        Game_PlaySong("");
        if (!d.mBossRush)
        {
            Game_ShowDialogue("redboss", "after_fight");
        }
        Screen_Shake(FxF(0.05f), FxF(0.1f));
        Screen_Flash(kFxOne, FLASH_RED);
        d.mSmallWave->mExists = false;
        ForRedBossPool(self, kSplashBullet, [](Entity& b) { SplashBulletSetExists(b, false); });
        ForRedBossPool(self, kTentacle, [](Entity& t) { TentacleDespawn(t); });
        d.mBigWave->mExists = false;
        d.mState = RB_AFTER;
        [[fallthrough]];
    case RB_AFTER:
        if (!d.mBossRush && !Game_DialogueFinished())
        {
            break;
        }
        Ecs_Play(self, "die");
        Game_PlaySfx("redboss_death");
        Screen_SetWave(true); // the screen wobbles while it sinks
        v.mYPush = 0;
        d.mState = RB_SINK;
        [[fallthrough]];
    case RB_SINK:
        if (v.mYPush < Fx(Ecs_SpriteHeight(self)))
        {
            MoveTo(d.mRipple->mV->mOpacity, 0, FxF(0.3f));
            d.mPushTimer += kFxTick;
            if (d.mPushTimer >= kRedBossPushTick)
            {
                d.mPushTimer = 0;
                v.mYPush += kFxOne;
            }
            break;
        }
        d.mFinalTimer = FxF(2.0f);
        d.mState = RB_FINAL;
        [[fallthrough]];
    case RB_FINAL:
        if (d.mFinalTimer > 0)
        {
            d.mFinalTimer -= kFxTick;
            break;
        }
        if (self.mPreset)
        {
            Preset_SetAlive(*self.mPreset, false);
        }
        self.mExists = false;
        Screen_SetWave(false);
        if (!d.mBossRush)
        {
            Game_SetBossDefeated("REDCAVE");
            Game_PlaySong("redcave");
        }
        else
        {
            Game_PlaySong("bedroom");
        }
        d.mState = RB_DONE;
        break;
    default:
        break;
    }
}

void RedBossUpdate(Entity& self)
{
    RedBossData& d = self.Data<RedBossData>();
    Ecs_BaseUpdate(self);
    // (its VolumeEvent: spawned on the first update, so it starts on the next)
    if (d.mVolume && d.mState != RB_START)
    {
        fx volume = Game_SongVolume();
        d.mVolume = !MoveTo(volume, 0, Fx(3));
        Game_SetSongVolume(volume);
    }
    d.mInvincible -= kFxTick;
    RedBossStep(self);
    d.mProximity = TOUCH_NONE;
    if (d.mLoopSfx)
    {
        Game_PlaySfx("bubble_loop"); // each frame, to loop a sound effect
    }
}

void RedBossCollided(Entity& self, Entity& other)
{
    RedBossData& d = self.Data<RedBossData>();
    if (d.mHealth == 0)
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other) && d.mInvincible < 0)
    {
        Ecs_Flicker(self, FxSec(0.5f));
        d.mInvincible = FxF(1.3f);
        d.mHealth--;
        Game_PlaySfx("redboss_moan");
    }
}

const EntityClass kRedBoss = {
    "Red_Boss", RedBossUpdate, nullptr, RedBossCollided, nullptr, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER | GROUP_BROOM, ECF_ENEMY,
};

// ---- WallBoss (Crowd/WallBoss.cs) -------------------------------------------------------------------------------
// The boss and its sub-entities, in the C#'s SubEntities order: Wall, Face (its eight Bullets, each with its Shadow),
// LHand and RHand (each with its Shadow and DeathExplosion), Laser, then eight DeathExplosions. Each C# coroutine is a
// state and a step function: a step's cases run on into the next one in the same frame, and return true is the
// yield.
constexpr int kWallBossPool = 8;

// Parabola_Thing: arcs the entity's offset.Y; the shadow (if any) follows the height.
struct Parabola final
{
    fx mT;
    fx mHeight;
    fx mPeriod;
    fx mStart;
};

void ParabolaInit(Parabola& p, const Entity& e, fx height, fx period)
{
    p.mT = 0;
    p.mHeight = height;
    p.mPeriod = period;
    p.mStart = e.mV->mOffY;
}

fx ParabolaProgress(const Parabola& p)
{
    return FxMin(FxDiv(p.mT, p.mPeriod), kFxOne);
}

// Shadow.UpdateShadow, for a shadow sheet with frames 0..maxFrame.
void UpdateShadowFrames(Entity& shadow, fx jumpHeight, int maxFrame)
{
    const int frame = FxCeil(jumpHeight * (maxFrame + 1)) - 1;
    if (frame == -1)
    {
        shadow.mV->mVisible = false;
    }
    else
    {
        shadow.mV->mVisible = true;
        Ecs_SetFrame(shadow, frame);
    }
}

bool ParabolaTick(Parabola& p, Entity& e, Entity* shadow, int maxFrame)
{
    p.mT += kFxTick;
    if (p.mT > p.mPeriod)
    {
        e.mV->mOffY = p.mStart;
        if (shadow)
        {
            UpdateShadowFrames(*shadow, 0, maxFrame);
        }
        return true;
    }
    const fx half = p.mPeriod / 2;
    e.mV->mOffY = p.mStart + FxMul(FxDiv(FxMul(p.mT, p.mPeriod - p.mT), FxMul(half, half)), p.mHeight);
    if (shadow)
    {
        UpdateShadowFrames(*shadow, FxAbs(FxF(0.5f) - ParabolaProgress(p)) * 2, maxFrame);
    }
    return false;
}

// The current screen's top-left (GlobalState.CurrentMapGrid).
fx GridLeft()
{
    return Fx(gGame.mGridX * 160);
}

fx GridTop()
{
    return Fx(gGame.mGridY * 160);
}

// ---- DeathExplosion
void DeathExplosionUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
}

const EntityClass kDeathExplosion = {
    "DeathExplosion", DeathExplosionUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- Wall
void WallCollided(Entity& self, Entity& other)
{
    if (self.mV->mOpacity != 0)
    {
        Ecs_Separate(self, other);
    }
}

const EntityClass kWall = {
    "Wall", Ecs_BaseUpdate, nullptr, WallCollided, nullptr, nullptr, nullptr, nullptr, 0, 0, ECF_PART_OF_MAP,
};

// ---- Face and its Bullets
struct WallBulletData final
{
    Parabola mArc;
    Entity* mShadow;
};

void WallBulletSetExists(Entity& b, bool exists)
{
    b.mExists = exists;
    b.Data<WallBulletData>().mShadow->mExists = exists;
}

void WallBulletUpdate(Entity& self)
{
    WallBulletData& d = self.Data<WallBulletData>();
    Ecs_BaseUpdate(self);
    // shadow.Update: Position = parent.Position - (offset - sprite size / 4)
    d.mShadow->mT->mX = self.mT->mX + Fx(2);
    d.mShadow->mT->mY = self.mT->mY + Fx(2);
    if (Ecs_AnimIs(self, "move") && ParabolaTick(d.mArc, self, d.mShadow, 3))
    {
        Ecs_Play(self, "explode");
        self.mT->mVelX = self.mT->mVelY = 0;
        Game_PlaySfx("4sht_pop");
    }
    else if (Ecs_AnimFinished(self))
    {
        WallBulletSetExists(self, false);
    }
}

void WallBulletCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && Ecs_AnimIs(self, "move"))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kWallBullet = {
    "Bullet", WallBulletUpdate, nullptr, WallBulletCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// Bullet.Spawn
void WallBulletStart(Entity& b, const Entity& face)
{
    WallBulletData& d = b.Data<WallBulletData>();
    WallBulletSetExists(b, true);
    b.mT->mX = face.mT->mX + Fx(32);
    b.mT->mY = face.mT->mY + Fx(16);
    const int height = Rng_Range(16, 24);
    ParabolaInit(d.mArc, b, Fx(height), FxF(1.0f) + Rng_Unit() / 2);
    Ecs_Play(b, "move");
    b.mT->mVelX = Fx(Rng_Range(-30, 30));
    b.mT->mVelY = Fx(30);
}

struct FaceData final
{
    int8_t mHealth;
    uint8_t mStep;
    bool mInvincible;
    fx mHitTimeout;
    fx mCharge;
};

// The face's StateLogic
void FaceStep(Entity& self)
{
    FaceData& d = self.Data<FaceData>();
    for (;;)
    {
        switch (d.mStep)
        {
        case 0:
            d.mInvincible = false;
            d.mCharge = 0;
            Ecs_Play(self, "normal");
            d.mStep = 1;
            [[fallthrough]];
        case 1:
            if (d.mCharge < FxF(3.0f))
            {
                d.mCharge += kFxTick;
                if (d.mHitTimeout == 0)
                {
                    Ecs_Play(self, "normal");
                }
                return;
            }
            d.mInvincible = true;
            Ecs_Play(self, "charge");
            d.mStep = 2;
            [[fallthrough]];
        case 2:
            if (d.mCharge < FxF(4.0f))
            {
                d.mCharge += kFxTick;
                return;
            }
            Ecs_Play(self, "shoot");
            Game_PlaySfx("wb_shoot");
            ForRedBossPool(self, kWallBullet, [&](Entity& b) {
                if (!b.mExists)
                {
                    WallBulletStart(b, self);
                }
            });
            d.mStep = 3;
            [[fallthrough]];
        default:
            if (Ecs_CountChildren(self, kWallBullet) != 0)
            {
                return;
            }
            d.mStep = 0;
            break;
        }
    }
}

void FaceUpdate(Entity& self)
{
    FaceData& d = self.Data<FaceData>();
    Ecs_BaseUpdate(self);
    MoveTo(d.mHitTimeout, 0, kFxOne);
    if (self.mV->mOpacity == kFxOne)
    {
        FaceStep(self);
    }
}

void FaceCollided(Entity& self, Entity&)
{
    FaceData& d = self.Data<FaceData>();
    if (d.mInvincible || d.mHitTimeout > 0)
    {
        return;
    }
    if (--d.mHealth == 0)
    {
        Game_PlaySfx("wb_moan_2");
        ForRedBossPool(self, kWallBullet, [](Entity& b) { WallBulletSetExists(b, false); });
        self.mV->mOpacity = FxF(0.99f); // (stops its logic)
    }
    else
    {
        Game_PlaySfx("wb_moan");
    }
    d.mHitTimeout = kFxOne;
    Ecs_Play(self, "hurt");
}

const EntityClass kFace = {
    "Face", FaceUpdate, nullptr, FaceCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_BROOM, 0,
};

// ---- Laser
struct WallLaserData final
{
    uint8_t mStep;
    bool mAttacking; // state != null
    fx mStartY;
};

// Laser.Attack
bool WallLaserStep(Entity& self)
{
    WallLaserData& d = self.Data<WallLaserData>();
    Transform& t = *self.mT;
    switch (d.mStep)
    {
    case 0:
        Ecs_Play(self, "charge");
        Ecs_Flicker(self, FxSec(1.3f));
        Game_PlaySfx("sun_guy_charge");
        d.mStep = 1;
        [[fallthrough]];
    case 1:
        if (self.mV->mFlickering)
        {
            return true;
        }
        Ecs_Play(self, "attack");
        t.mVelY = Fx(50);
        d.mStep = 2;
        [[fallthrough]];
    case 2:
        if (InGrid(t.mY) < Fx(16 * 6 + 11))
        {
            return true;
        }
        t.mVelY = -Fx(65);
        d.mStep = 3;
        [[fallthrough]];
    case 3:
        if (InGrid(t.mY) > Fx(45))
        {
            return true;
        }
        t.mVelY = Fx(80);
        d.mStep = 4;
        [[fallthrough]];
    case 4:
        if (InGrid(t.mY) < Fx(16 * 6 + 11))
        {
            return true;
        }
        t.mVelX = t.mVelY = 0;
        d.mStep = 5;
        [[fallthrough]];
    default:
        return !MoveTo(t.mY, d.mStartY, Fx(110));
    }
}

void WallLaserUpdate(Entity& self)
{
    WallLaserData& d = self.Data<WallLaserData>();
    Ecs_BaseUpdate(self);
    if (d.mAttacking && !WallLaserStep(self))
    {
        d.mAttacking = false;
        self.mExists = false;
        self.mV->mVisible = false;
    }
}

void WallLaserCollided(Entity& self, Entity& other)
{
    if (!self.mV->mFlickering && IsPlayer(other) && Player_State() != PS_AIR)
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kWallLaser = {
    "Laser", WallLaserUpdate, nullptr, WallLaserCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// ---- Hands
enum HandCo : uint8_t
{
    HAND_NONE, // state == null
    HAND_FLOAT,
    HAND_PUSH,
    HAND_FOLLOW,
    HAND_GOTO,
    HAND_RESET,
    HAND_STOMP, // LHand.Stomp / RHand.Stomp
};

enum GroundPhase : uint8_t
{
    GROUND_NONE,
    GROUND_CRACKED,
    GROUND_FULL,
};

struct HandData final
{
    uint8_t mCo;
    uint8_t mStep;
    bool mRight;
    uint8_t mGround;  // LHand's ground_state
    uint8_t mPhase;   // Push / Stomp's phase
    int8_t mStomps;   // LHand.Stomp's nr_stomps
    uint8_t mCracked; // LHand's tiles
    uint8_t mHole;
    fx mT; // Float's t, Stomp's wait_time
    fx mSpeed;
    fx mInitX;
    fx mInitY;
    fx mGoX;
    fx mGoY;
    Parabola mStomp;
    Entity* mShadow;
    Entity* mExplosion;
    Entity* mLaser; // Follow's
};

void HandStart(Entity& h, uint8_t co)
{
    HandData& d = h.Data<HandData>();
    d.mCo = co;
    d.mStep = 0;
}

void HandGoTo(Entity& h, fx x, fx y, fx speed)
{
    HandData& d = h.Data<HandData>();
    HandStart(h, HAND_GOTO);
    d.mGoX = x;
    d.mGoY = y;
    d.mSpeed = speed;
}

void HandReset(Entity& h, fx speed)
{
    HandStart(h, HAND_RESET);
    h.Data<HandData>().mSpeed = speed;
}

// GoTo's MoveNext: true while it hasn't got there (both axes move every frame).
bool HandMoveStep(Entity& h, fx x, fx y, fx speed)
{
    const bool ax = MoveTo(h.mT->mX, x, speed);
    const bool ay = MoveTo(h.mT->mY, y, speed);
    return !(ax && ay);
}

void HandSpawnExplosion(Entity& h)
{
    Entity& e = *h.Data<HandData>().mExplosion;
    e.mExists = true;
    e.mT->mX = h.mT->mX;
    e.mT->mY = h.mT->mY;
    Ecs_Play(e, "explode@2", true);
}

// LHand.Stomp(phase, player)
bool LHandStompStep(Entity& h)
{
    HandData& d = h.Data<HandData>();
    for (;;)
    {
        switch (d.mStep)
        {
        case 0:
            d.mShadow->mV->mVisible = true;
            d.mStomps = static_cast<int8_t>(d.mPhase + 2);
            d.mStep = 1;
            [[fallthrough]];
        case 1:
            if (!(d.mStomps-- > 0))
            {
                return false;
            }
            d.mStomp.mT = 0;
            d.mStep = 2;
            [[fallthrough]];
        case 2:
            if (ParabolaProgress(d.mStomp) < FxF(0.5f))
            {
                const Entity& p = *World_Player();
                MoveTo(h.mT->mX, p.mT->mX, Fx(60));
                MoveTo(h.mT->mY, p.mT->mY - Fx(16), Fx(60));
                ParabolaTick(d.mStomp, h, d.mShadow, 4);
                return true;
            }
            Game_PlaySfx("fall_1");
            d.mT = 0;
            d.mStep = 3;
            [[fallthrough]];
        case 3:
            if (d.mT < FxF(0.7f))
            {
                d.mT += kFxTick;
                return true;
            }
            d.mStep = 4;
            [[fallthrough]];
        default:
            if (!ParabolaTick(d.mStomp, h, d.mShadow, 4))
            {
                return true;
            }
            Screen_Shake(FxF(0.01f), FxF(0.2f));
            HandSpawnExplosion(h);
            Game_PlaySfx("wb_hit_ground");
            if (d.mPhase != 0 && d.mGround != GROUND_FULL)
            {
                Screen_Shake(FxF(0.02f), FxF(0.5f));
                Game_PlaySfx("floor_crack");
                const int tile = d.mGround == GROUND_NONE ? d.mCracked : d.mHole;
                const int tlX = gGame.mGridX * 10;
                const int tlY = gGame.mGridY * 10;
                for (int x = 2; x < 8; ++x)
                {
                    World_ChangeTile(0, tlX + x, tlY + 6, tile);
                }
                d.mGround = d.mGround == GROUND_NONE ? GROUND_CRACKED : GROUND_FULL;
            }
            d.mStep = 1;
            return true;
        }
    }
}

// RHand.Stomp: taps along the floor, back and forth
bool RHandStompStep(Entity& h)
{
    HandData& d = h.Data<HandData>();
    Transform& t = *h.mT;
    if (d.mStep == 0)
    {
        d.mStomp.mT = 0;
        t.mVelX = -Fx(30);
        d.mShadow->mV->mVisible = true;
        d.mStep = 1;
    }
    const fx x = InGrid(t.mX);
    if ((t.mVelX < 0 && x < Fx(16)) || (t.mVelX > 0 && x > Fx(16 * 9 - h.mB->mW)))
    {
        t.mVelX = -t.mVelX;
        t.mVelY = -t.mVelY;
    }
    if (ParabolaTick(d.mStomp, h, d.mShadow, 4))
    {
        Game_PlaySfx("wb_tap_ground");
        HandSpawnExplosion(h);
        d.mStomp.mT = 0;
    }
    return true;
}

// state.MoveNext()
bool HandStep(Entity& h)
{
    HandData& d = h.Data<HandData>();
    Transform& t = *h.mT;
    switch (d.mCo)
    {
    case HAND_FLOAT:
        if (d.mStep == 0)
        {
            d.mT = 0;
            d.mStep = 1;
        }
        d.mT += kFxTick;
        t.mY = d.mInitY + Fx(4) + (d.mRight ? -1 : 1) * FxSin(FxMul(d.mT, kFxTau)) * 8;
        return true;
    case HAND_PUSH:
        if (d.mStep == 0)
        {
            Ecs_Play(h, "push");
            t.mVelX = Fx(Rng_Range(20, 60));
            t.mVelY = Fx(60 + d.mPhase * 30);
            if (d.mRight)
            {
                t.mVelX = -t.mVelX;
            }
            d.mStep = 1;
        }
        if (InGrid(t.mY) < Fx(6 * 16))
        {
            return true;
        }
        t.mVelX = t.mVelY = 0;
        return false;
    case HAND_FOLLOW:
        t.mY = d.mLaser->mT->mY - Fx(11);
        return true;
    case HAND_GOTO:
        return HandMoveStep(h, d.mGoX, d.mGoY, d.mSpeed);
    case HAND_RESET:
    {
        if (d.mStep == 0)
        {
            t.mVelX = t.mVelY = 0;
            d.mShadow->mV->mVisible = false;
            d.mStep = 1;
        }
        // (|: both run every frame)
        const bool moving = HandMoveStep(h, d.mInitX, d.mInitY, d.mSpeed);
        const bool lowered = MoveTo(h.mV->mOffY, 0, Fx(120));
        if (moving || !lowered)
        {
            return true;
        }
        Ecs_Play(h, "idle");
        return false;
    }
    case HAND_STOMP:
        return d.mRight ? RHandStompStep(h) : LHandStompStep(h);
    default:
        return false;
    }
}

void HandUpdate(Entity& self)
{
    HandData& d = self.Data<HandData>();
    Ecs_BaseUpdate(self);
    // shadow.Update: Position = parent.Position - ((8, -12) - sprite size / 4)
    d.mShadow->mT->mX = self.mT->mX;
    d.mShadow->mT->mY = self.mT->mY + Fx(20);
    if (d.mCo != HAND_NONE && !HandStep(self))
    {
        d.mCo = HAND_NONE;
    }
}

void HandPostUpdate(Entity& self)
{
    const fx off = self.mV->mOffY;
    const int frame = off >= Fx(29) ? 0 : off >= Fx(24) ? 1 : off >= Fx(16) ? 2 : off >= Fx(8) ? 3 : 4;
    Ecs_SetFrame(*self.Data<HandData>().mShadow, frame);
}

void HandCollided(Entity& self, Entity& other)
{
    const HandData& d = self.Data<HandData>();
    if (!d.mShadow->mV->mVisible)
    {
        Ecs_Separate(self, other);
    }
    else if (Ecs_Intersects(Ecs_Hitbox(*d.mShadow), Ecs_Hitbox(other)) && self.mV->mOffY < Fx(8) && IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kLHand = {
    "LHand", HandUpdate, HandPostUpdate, HandCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};
const EntityClass kRHand = {
    "RHand", HandUpdate, HandPostUpdate, HandCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// ---- WallBoss
enum WallBossState : uint8_t
{
    WB_START,
    WB_WAIT_PLAYER, // until the player is 80 px up the room
    WB_DIALOGUE,    // "before_fight"
    WB_FADE_IN,
    WB_FIGHT,
    WB_FLASH,
    WB_AFTER, // "after_fight"
    WB_FADE_OUT,
    WB_POST,
    WB_DONE,
};

enum WallAttack : uint8_t
{
    WA_STOMP,
    WA_LASER,
    WA_PUSH,
};

struct WallBossData final
{
    uint8_t mState;
    uint8_t mFight;      // FightLogic's step
    uint8_t mAttack;     // WallAttack
    uint8_t mAttackStep; // its step
    uint8_t mPushType;   // PushAttack: 0 right hand, 1 left hand, 2 both
    int8_t mPushes;      // num_pushes
    bool mBossRush;
    bool mVolume; // its VolumeEvent(0, 0.3) is fading the song out
    fx mT;     // FightLogic's t, the explosion timer, post_death_timer
    Entity* mWall;
    Entity* mFace;
    Entity* mLHand;
    Entity* mRHand;
    Entity* mLaser;
};

int WallBossPhase(const WallBossData& d)
{
    const int health = d.mFace->Data<FaceData>().mHealth;
    return health > 10 ? 0 : (health > 5 ? 1 : 2);
}

bool HandBusy(const Entity& h)
{
    return const_cast<Entity&>(h).Data<HandData>().mCo != HAND_NONE;
}

void WallBossSyncOpacity(WallBossData& d)
{
    const fx o = d.mFace->mV->mOpacity;
    d.mWall->mV->mOpacity = d.mRHand->mV->mOpacity = d.mLHand->mV->mOpacity = o;
}

bool StompAttackStep(WallBossData& d)
{
    Entity& l = *d.mLHand;
    Entity& r = *d.mRHand;
    switch (d.mAttackStep)
    {
    case 0:
        Ecs_Play(l, "stomp");
        Ecs_Play(r, "stomp");
        HandStart(l, HAND_NONE);
        HandGoTo(r, r.Data<HandData>().mInitX, r.Data<HandData>().mInitY, Fx(30));
        d.mAttackStep = 1;
        [[fallthrough]];
    case 1:
        if (HandBusy(r))
        {
            return true;
        }
        HandStart(r, HAND_STOMP);
        HandStart(l, HAND_STOMP);
        l.Data<HandData>().mPhase = static_cast<uint8_t>(WallBossPhase(d));
        d.mAttackStep = 2;
        [[fallthrough]];
    case 2:
        if (HandBusy(l))
        {
            return true;
        }
        HandReset(r, Fx(180));
        HandReset(l, Fx(180));
        d.mAttackStep = 3;
        [[fallthrough]];
    default:
        return HandBusy(r) || HandBusy(l);
    }
}

bool LaserAttackStep(WallBossData& d)
{
    Entity& l = *d.mLHand;
    Entity& r = *d.mRHand;
    switch (d.mAttackStep)
    {
    case 0:
        HandGoTo(l, l.Data<HandData>().mInitX, l.Data<HandData>().mInitY, Fx(180));
        HandGoTo(r, r.Data<HandData>().mInitX, r.Data<HandData>().mInitY, Fx(180));
        Ecs_Play(l, "shoot");
        Ecs_Play(r, "shoot");
        d.mAttackStep = 1;
        [[fallthrough]];
    case 1:
        if (HandBusy(l) || HandBusy(r))
        {
            return true;
        }
        d.mLaser->mExists = true;
        d.mLaser->Data<WallLaserData>().mAttacking = true;
        d.mLaser->Data<WallLaserData>().mStep = 0;
        HandStart(l, HAND_FOLLOW);
        HandStart(r, HAND_FOLLOW);
        d.mAttackStep = 2;
        [[fallthrough]];
    case 2:
        if (d.mLaser->Data<WallLaserData>().mAttacking)
        {
            return true;
        }
        HandReset(l, Fx(90));
        HandReset(r, Fx(90));
        d.mAttackStep = 3;
        [[fallthrough]];
    default:
        return HandBusy(l) || HandBusy(r);
    }
}

bool PushAttackStep(WallBossData& d)
{
    Entity& l = *d.mLHand;
    Entity& r = *d.mRHand;
    HandData& ld = l.Data<HandData>();
    HandData& rd = r.Data<HandData>();
    for (;;)
    {
        switch (d.mAttackStep)
        {
        case 0:
            d.mPushes = static_cast<int8_t>(WallBossPhase(d) == 0 ? 2 : 3);
            d.mAttackStep = 1;
            [[fallthrough]];
        case 1:
        {
            if (!(d.mPushes-- > 0))
            {
                return false;
            }
            HandStart(l, HAND_NONE);
            HandStart(r, HAND_NONE);
            const int phase = WallBossPhase(d);
            const double doublePushChance = phase == 0 ? 0.5 : (phase == 1 ? 0.7 : 0.8);
            if (Rng_NextDouble() < doublePushChance)
            {
                d.mPushType = 2;
            }
            else if (Rng_NextDouble() < 0.5)
            {
                d.mPushType = 1;
            }
            else
            {
                d.mPushType = 0;
            }
            if (d.mPushType != 0)
            {
                HandGoTo(l, ld.mInitX, ld.mInitY - Fx(16), Fx(30));
            }
            if (d.mPushType != 1)
            {
                HandGoTo(r, rd.mInitX, rd.mInitY - Fx(16), Fx(30));
            }
            d.mAttackStep = 2;
        }
            [[fallthrough]];
        case 2:
            if (HandBusy(r) || HandBusy(l))
            {
                return true;
            }
            Game_PlaySfx("slasher_atk");
            if (d.mPushType != 0)
            {
                HandStart(l, HAND_PUSH);
                ld.mPhase = static_cast<uint8_t>(WallBossPhase(d));
            }
            if (d.mPushType != 1)
            {
                HandStart(r, HAND_PUSH);
                rd.mPhase = static_cast<uint8_t>(WallBossPhase(d));
            }
            d.mAttackStep = 3;
            [[fallthrough]];
        case 3:
            if (HandBusy(r) || HandBusy(l))
            {
                return true;
            }
            HandReset(l, Fx(180));
            HandReset(r, Fx(180));
            d.mAttackStep = 4;
            [[fallthrough]];
        default:
            if (HandBusy(r) || HandBusy(l))
            {
                return true;
            }
            d.mAttackStep = 1;
            break;
        }
    }
}

// FightLogic
void WallBossFightStep(WallBossData& d)
{
    for (;;)
    {
        switch (d.mFight)
        {
        case 0:
            HandStart(*d.mLHand, HAND_FLOAT);
            HandStart(*d.mRHand, HAND_FLOAT);
            d.mT = FxF(2.0f);
            d.mFight = 1;
            [[fallthrough]];
        case 1:
        {
            if (d.mT > 0)
            {
                d.mT -= kFxTick;
                return;
            }
            const double r = Rng_NextDouble();
            switch (WallBossPhase(d))
            {
            case 0:
                d.mAttack = r <= 0.7 ? WA_STOMP : WA_PUSH;
                break;
            case 1:
                d.mAttack = r <= 0.35 ? WA_LASER : (r <= 0.7 ? WA_STOMP : WA_PUSH);
                break;
            default:
                d.mAttack = r <= 0.5 ? WA_LASER : (r <= 0.75 ? WA_STOMP : WA_PUSH);
                break;
            }
            d.mAttackStep = 0;
            d.mFight = 2;
        }
            [[fallthrough]];
        default:
        {
            const bool running = d.mAttack == WA_STOMP ? StompAttackStep(d)
                                 : d.mAttack == WA_LASER ? LaserAttackStep(d)
                                                         : PushAttackStep(d);
            if (running)
            {
                return;
            }
            d.mFight = 0;
            break;
        }
        }
    }
}

void WallBossStep(Entity& self)
{
    WallBossData& d = self.Data<WallBossData>();
    Visual& face = *d.mFace->mV;
    switch (d.mState)
    {
    case WB_START:
        d.mVolume = true; // GlobalState.SpawnEntity(new VolumeEvent(0, 0.3))
        d.mState = WB_WAIT_PLAYER;
        [[fallthrough]];
    case WB_WAIT_PLAYER:
        if (InGrid(World_Player()->mT->mY) > Fx(80))
        {
            break;
        }
        if (!d.mBossRush)
        {
            Game_ShowDialogue("wallboss", "before_fight");
        }
        d.mState = WB_DIALOGUE;
        [[fallthrough]];
    case WB_DIALOGUE:
        if (!d.mBossRush && !Game_DialogueFinished())
        {
            break;
        }
        d.mVolume = false;
        Game_PlaySong("crowd_boss");
        d.mState = WB_FADE_IN;
        [[fallthrough]];
    case WB_FADE_IN:
        if (!MoveTo(face.mOpacity, kFxOne, FxF(0.18f)))
        {
            WallBossSyncOpacity(d);
            break;
        }
        d.mFight = 0;
        d.mState = WB_FIGHT;
        [[fallthrough]];
    case WB_FIGHT:
        if (d.mFace->Data<FaceData>().mHealth > 0)
        {
            WallBossFightStep(d);
            break;
        }
        HandStart(*d.mLHand, HAND_NONE);
        HandStart(*d.mRHand, HAND_NONE);
        d.mLaser->mExists = false;
        Game_PlaySong("");
        Game_PlaySfx("sun_guy_death_short");
        Screen_Flash(Fx(3), FLASH_WHITE);
        d.mState = WB_FLASH;
        [[fallthrough]];
    case WB_FLASH:
        if (Screen_FlashActive())
        {
            break;
        }
        if (!d.mBossRush)
        {
            Game_ShowDialogue("wallboss", "after_fight");
            Game_PlaySfx("talk_death");
        }
        d.mState = WB_AFTER;
        [[fallthrough]];
    case WB_AFTER:
        if (!d.mBossRush && !Game_DialogueFinished())
        {
            break;
        }
        d.mT = 0;
        d.mState = WB_FADE_OUT;
        [[fallthrough]];
    case WB_FADE_OUT:
        if (!MoveTo(face.mOpacity, 0, FxF(0.18f)))
        {
            WallBossSyncOpacity(d);
            Screen_Shake(FxF(0.02f), FxF(0.1f));
            d.mT += kFxTick;
            if (d.mT > FxF(0.15f))
            {
                d.mT = 0;
                bool spawned = false;
                ForRedBossPool(self, kDeathExplosion, [&](Entity& e) {
                    if (!spawned && !e.mExists)
                    {
                        spawned = true;
                        e.mExists = true;
                        e.mT->mX = GridLeft() + Fx(Rng_Range(0, 160 - 24));
                        e.mT->mY = GridTop() + Fx(Rng_Range(0, 32));
                        Ecs_Play(e, "explode@2");
                    }
                });
                // (PlayPitchedSoundEffect("hit_wall") draws no number)
            }
            break;
        }
        d.mT = FxF(1.0f);
        d.mState = WB_POST;
        [[fallthrough]];
    case WB_POST:
        if (d.mT > 0)
        {
            d.mT -= kFxTick;
            break;
        }
        if (!d.mBossRush)
        {
            Game_SetBossDefeated("CROWD");
            Game_PlaySong("crowd");
        }
        else
        {
            Game_PlaySong("bedroom");
        }
        if (self.mPreset)
        {
            Preset_SetAlive(*self.mPreset, false);
        }
        self.mExists = false;
        for (Entity* e = Ecs_First(); e; e = e->mNext)
        {
            if (e->mParent == &self)
            {
                e->mExists = false;
            }
        }
        d.mState = WB_DONE;
        break;
    default:
        break;
    }
}

void WallBossUpdate(Entity& self)
{
    WallBossData& d = self.Data<WallBossData>();
    Ecs_BaseUpdate(self);
    // (its VolumeEvent: spawned on the first update, so it starts on the next)
    if (d.mVolume && d.mState != WB_START)
    {
        fx volume = Game_SongVolume();
        MoveTo(volume, 0, FxF(0.3f));
        Game_SetSongVolume(volume);
    }
    WallBossStep(self);
}

const EntityClass kWallBoss = {
    "WallBoss", WallBossUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, ECF_ENEMY,
};

// ---- Apartment enemies (Apartment/*.cs) --------------------------------------------------------------------------
// GlobalState.IsCell ? o : 0: the CELL's sprites sit `o` frames on.
int CellFrames(int o)
{
    return Game_IsCell() ? o : 0;
}

// AnimationChanged: the _l anims are the _r ones mirrored.
void FlipIfLeft(Entity& e)
{
    const char* name = Ecs_AnimName(e);
    const size_t n = name ? strlen(name) : 0;
    e.mV->mHFlip = n >= 2 && name[n - 2] == '_' && name[n - 1] == 'l';
}

void PlayFacingFlip(Entity& e, const char* prefix)
{
    Ecs_PlayFacing(e, prefix);
    FlipIfLeft(e);
}

// ---- Rat (Apartment/Rat.cs): walks until its lookahead, 7 px in front, bumps into something, then turns left
struct RatData final
{
    Entity* mLookahead;
};

void RatPlaceLookahead(Entity& self)
{
    Entity& la = *self.Data<RatData>().mLookahead;
    const Facing f = static_cast<Facing>(self.mFacing);
    la.mT->mX = Ecs_CenterX(self) + Fx(FacingX(f) * 7);
    la.mT->mY = Ecs_CenterY(self) + Fx(FacingY(f) * 7);
}

void RatUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    Entity& la = *self.Data<RatData>().mLookahead;
    if (la.mB->mTouching != TOUCH_NONE)
    {
        la.mB->mTouching = TOUCH_NONE;
        Transform& t = *self.mT;
        const fx vx = t.mVelX;
        t.mVelX = -t.mVelY;
        t.mVelY = vx;
        Ecs_FaceTowards(self, t.mX + t.mVelX, t.mY + t.mVelY);
        PlayFacingFlip(self, "walk");
        Game_PlaySfx("rat_move");
    }
    RatPlaceLookahead(self);
}

void RatCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        const Rect h = Ecs_Hitbox(self);
        const Rect inner = { h.mX + 4, h.mY + 4, 8, 8 };
        if (Ecs_Intersects(inner, Ecs_Hitbox(other)))
        {
            Player_ReceiveDamage(1);
        }
    }
    else
    {
        Spawn_Explosion(self);
        Die(self, FxF(0.5f));
    }
}

const EntityClass kRat = {
    "Rat",     RatUpdate, nullptr, RatCollided, nullptr, nullptr, nullptr, nullptr,
    GROUP_RAT, GROUP_PLAYER | GROUP_BROOM | GROUP_DASH_TRAP, ECF_ENEMY,
};

void LookaheadCollided(Entity& self, Entity&)
{
    self.mB->mTouching = TOUCH_ANY;
}

// Puddle, Fall, SlowTile, Reflection, Ladder, Conveyor, Grass: all just "touching".
void LookaheadMapEvent(Entity& self, MapEvent ev, uint8_t, fx, fx)
{
    if (ev != MAPEV_SPIKE)
    {
        self.mB->mTouching = TOUCH_ANY;
    }
}

const EntityClass kRatLookahead = {
    "Lookahead", nullptr, nullptr, LookaheadCollided, nullptr, LookaheadMapEvent, nullptr, nullptr,
    0,           GROUP_DUST,     ECF_MAP_COLLISION,
};

// ---- Silverfish (Apartment/Silverfish.cs): runs away from the player along the way it looks
enum SilverfishStep : uint8_t
{
    SF_WAIT, // until it first sees the player
    SF_RUN_START,
    SF_RUN, // until it hits something
    SF_LOOK, // turning on the spot until it sees the player
};

struct SilverfishData final
{
    uint8_t mStep;
    fx mTurnTimer;
};

bool SilverfishSeesPlayer(const Entity& self)
{
    const Entity& p = *World_Player();
    const Rect ph = Ecs_Hitbox(p);
    const Rect h = Ecs_Hitbox(self);
    const bool seeX = ph.mY <= h.mY + h.mH && ph.mY + ph.mH >= h.mY;
    const bool seeY = ph.mX <= h.mX + h.mW && ph.mX + ph.mW >= h.mX;
    if (FxLength(p.mT->mX - self.mT->mX, p.mT->mY - self.mT->mY) < Fx(30))
    {
        return true;
    }
    switch (self.mFacing)
    {
    case FACE_LEFT:
        return seeX && p.mT->mX < self.mT->mX;
    case FACE_RIGHT:
        return seeX && p.mT->mX > self.mT->mX;
    case FACE_UP:
        return seeY && p.mT->mY < self.mT->mY;
    default:
        return seeY && p.mT->mY > self.mT->mY;
    }
}

void SilverfishStepLogic(Entity& self)
{
    SilverfishData& d = self.Data<SilverfishData>();
    Transform& t = *self.mT;
    for (;;)
    {
        switch (d.mStep)
        {
        case SF_WAIT:
            if (!SilverfishSeesPlayer(self))
            {
                return;
            }
            d.mStep = SF_RUN_START;
            break;
        case SF_RUN_START:
        {
            Game_PlaySfx("sf_move");
            const Facing f = FlipFacing(static_cast<Facing>(self.mFacing));
            self.mFacing = f;
            PlayFacingFlip(self, "move");
            t.mVelX = Fx(FacingX(f) * 50);
            t.mVelY = Fx(FacingY(f) * 50);
            d.mStep = SF_RUN;
            break;
        }
        case SF_RUN:
            if (self.mB->mTouching == TOUCH_NONE)
            {
                return;
            }
            self.mB->mTouching = TOUCH_NONE;
            t.mVelX = t.mVelY = 0;
            PlayFacingFlip(self, "idle");
            d.mTurnTimer = 0;
            d.mStep = SF_LOOK;
            break;
        default:
            if (SilverfishSeesPlayer(self))
            {
                const Entity& p = *World_Player();
                Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
                d.mStep = SF_RUN_START;
                break;
            }
            d.mTurnTimer += kFxTick;
            if (d.mTurnTimer > FxF(0.8f))
            {
                d.mTurnTimer = 0;
                switch (self.mFacing)
                {
                case FACE_LEFT:
                    self.mFacing = FACE_UP;
                    break;
                case FACE_RIGHT:
                    self.mFacing = FACE_DOWN;
                    break;
                case FACE_UP:
                    self.mFacing = FACE_RIGHT;
                    break;
                default:
                    self.mFacing = FACE_LEFT;
                    break;
                }
                PlayFacingFlip(self, "idle");
            }
            return;
        }
    }
}

void SilverfishUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    SilverfishStepLogic(self);
}

void SilverfishGetHit(Entity& self)
{
    Die(self, FxF(0.5f));
    Spawn_Explosion(self);
}

void SilverfishCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other))
    {
        SilverfishGetHit(self);
    }
}

const EntityClass kSilverfish = {
    "Silverfish",     SilverfishUpdate, nullptr, SilverfishCollided, nullptr, nullptr, nullptr, nullptr,
    GROUP_SILVERFISH, GROUP_PLAYER | GROUP_BROOM, ECF_ENEMY | ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN,
};

// ---- GasGuy (Apartment/GasGuy.cs): drifts after the player, puffing gas that turns the controls round
enum GasGuyState : uint8_t
{
    GG_LATENCY,
    GG_NORMAL,
    GG_SHOOT,
};

constexpr int kGasClouds = 3;

struct GasGuyData final
{
    uint8_t mState;
    int mHealth;
    bool mInDelay;
    bool mDidFirstShot;
    fx mSoundDelay;
    fx mSoundDelayMax;
    TimerState<1> mLatency; // each FSM state's own clock
    TimerState<1> mNormal;
    TimerState<1> mShoot;
    Entity* mGas[kGasClouds];
};

void GasGuyEnter(Entity& self, uint8_t state)
{
    GasGuyData& d = self.Data<GasGuyData>();
    if (d.mState == GG_LATENCY && state != GG_LATENCY)
    {
        d.mInDelay = false; // InitialLatency's Exit
    }
    d.mState = state;
    switch (state)
    {
    case GG_LATENCY:
        d.mLatency.Reset();
        d.mLatency.Add(FxSec(1.5f), 0);
        d.mInDelay = true;
        break;
    case GG_NORMAL:
        d.mNormal.Reset();
        d.mNormal.Add(d.mDidFirstShot ? FxF(5.0f) : FxF(1.0f), 0);
        Ecs_Play(self, "float");
        break;
    default:
        d.mShoot.Reset();
        d.mShoot.Add(FxSec(1.2f), 0);
        break;
    }
}

void GasGuyUpdate(Entity& self)
{
    GasGuyData& d = self.Data<GasGuyData>();
    Ecs_BaseUpdate(self);
    const Entity& p = *World_Player();
    switch (d.mState)
    {
    case GG_LATENCY:
        d.mLatency.Tick();
        while (d.mLatency.Pop() >= 0)
        {
            GasGuyEnter(self, GG_NORMAL);
        }
        break;
    case GG_NORMAL:
        d.mNormal.Tick();
        while (d.mNormal.Pop() >= 0)
        {
            Ecs_Play(self, "release_gas");
            GasGuyEnter(self, GG_SHOOT);
            d.mDidFirstShot = true;
        }
        break;
    default:
        d.mShoot.Tick();
        while (d.mShoot.Pop() >= 0)
        {
            Ecs_Play(self, "float");
            GasGuyEnter(self, GG_NORMAL);
            Game_PlaySfx("gasguy_shoot");
            // gasClouds.Spawn(s => s.Spawn(this, _player), 3, true): all three, whether out or not
            for (Entity* g : d.mGas)
            {
                g->mExists = true;
                g->mT->mX = self.mT->mX;
                g->mT->mY = self.mT->mY;
                Ecs_MoveTowards(*g, p.mT->mX, p.mT->mY, Fx(30));
                g->mT->mVelX += Fx(Rng_Range(-10, 11));
                g->mT->mVelY += Fx(Rng_Range(-10, 11));
                Ecs_Flicker(*g, FxSec(9001));
                g->mV->mOpacity = FxF(0.8f);
            }
        }
        break;
    }
    d.mSoundDelay += kFxTick;
    if (d.mSoundDelay > d.mSoundDelayMax)
    {
        d.mSoundDelay = 0;
        Game_PlaySfx("gasguy_move");
    }
    if (!d.mInDelay)
    {
        MoveTo(self.mT->mX, p.mT->mX, Fx(12));
        MoveTo(self.mT->mY, p.mT->mY, Fx(12));
    }
}

void GasGuyCollided(Entity& self, Entity& other)
{
    GasGuyData& d = self.Data<GasGuyData>();
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other) && !self.mV->mFlickering)
    {
        d.mHealth--;
        Ecs_Flicker(self, FxSec(0.5f));
        const Entity& p = *World_Player();
        Ecs_MoveTowards(self, p.mT->mX, p.mT->mY, Fx(100));
        self.mT->mVelX = -self.mT->mVelX;
        self.mT->mVelY = -self.mT->mVelY;
        Game_PlaySfx("broom_hit");
        if (d.mHealth == 0)
        {
            // (its pickup is after the gas clouds in the lists)
            if (Entity* drop = Die(self, FxF(0.6f), true))
            {
                Ecs_MoveAfter(*drop, *d.mGas[kGasClouds - 1]);
            }
            Spawn_Explosion(self);
        }
    }
}

const EntityClass kGasGuy = {
    "GasGuy", GasGuyUpdate, nullptr, GasGuyCollided, nullptr, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER | GROUP_BROOM, ECF_ENEMY | ECF_KEEP_ON_SCREEN,
};

void GasUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    self.mV->mOpacity = self.mV->mOpacity - FxMul(FxF(0.06f), kFxTick);
    if (self.mV->mOpacity <= 0)
    {
        self.mExists = false;
    }
}

void GasCollided(Entity& self, Entity& other)
{
    if (self.mV->mOpacity > FxF(0.3f) && IsPlayer(other))
    {
        Player_Reverse();
        Screen_SetWave(true);
    }
    else if (IsBroom(other))
    {
        self.mV->mOpacity = self.mV->mOpacity - FxF(0.025f);
    }
    else if (other.mClass == &kSilverfish)
    {
        SilverfishGetHit(other);
    }
}

const EntityClass kGas = {
    "Gas", GasUpdate,    nullptr, GasCollided, nullptr, nullptr, nullptr, nullptr,
    0,     GROUP_PLAYER | GROUP_BROOM | GROUP_SILVERFISH, 0,
};

// ---- TeleGuy (Apartment/TeleGuy.cs): hit it and it pops up behind the player and lunges back
enum TeleGuyState : uint8_t
{
    TG_IDLE,
    TG_TELEPORTING,
    TG_PRE_ATTACK,
    TG_ATTACKING,
};

struct TeleGuyData final
{
    uint8_t mState;
    TimerState<1> mTeleporting;
    TimerState<1> mPreAttack;
    TimerState<1> mAttacking;
    fx mAtkX;
    fx mAtkY;
};

void TeleGuyEnter(Entity& self, uint8_t state)
{
    TeleGuyData& d = self.Data<TeleGuyData>();
    if (d.mState == TG_ATTACKING)
    {
        self.mT->mVelX = self.mT->mVelY = 0; // Attacking's Exit
    }
    d.mState = state;
    const Entity& p = *World_Player();
    switch (state)
    {
    case TG_TELEPORTING:
        d.mTeleporting.Reset();
        Game_PlaySfx("teleguy_up");
        Ecs_Play(self, "poof");
        FlipIfLeft(self);
        d.mTeleporting.Add(FxSec(0.5f), 0);
        break;
    case TG_PRE_ATTACK:
        d.mPreAttack.Reset();
        Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
        d.mPreAttack.Add(FxSec(0.5f), 0);
        break;
    case TG_ATTACKING:
        d.mAttacking.Reset();
        d.mAttacking.Add(FxSec(1.5f), 0);
        Ecs_MoveTowards(self, d.mAtkX, d.mAtkY, Fx(60));
        break;
    default:
        break;
    }
}

void TeleGuyTeleport(Entity& self)
{
    const Entity& p = *World_Player();
    const fx px = p.mT->mX;
    const fx py = p.mT->mY;
    switch (Player_Facing())
    {
    case FACE_LEFT:
        self.mT->mX = px + Fx(16);
        self.mT->mY = py;
        self.mFacing = FACE_LEFT;
        break;
    case FACE_RIGHT:
        self.mT->mX = px - Fx(16);
        self.mT->mY = py;
        self.mFacing = FACE_RIGHT;
        break;
    case FACE_UP:
        self.mT->mX = px;
        self.mT->mY = py + Fx(17);
        self.mFacing = FACE_UP;
        break;
    default:
        self.mT->mX = px;
        self.mT->mY = py - Fx(16);
        self.mFacing = FACE_DOWN;
        break;
    }
}

void TeleGuyUpdate(Entity& self)
{
    TeleGuyData& d = self.Data<TeleGuyData>();
    Ecs_BaseUpdate(self);
    switch (d.mState)
    {
    case TG_IDLE:
    {
        const Entity& p = *World_Player();
        Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
        PlayFacingFlip(self, "idle");
        break;
    }
    case TG_TELEPORTING:
        d.mTeleporting.Tick();
        while (d.mTeleporting.Pop() >= 0)
        {
            Game_PlaySfx("teleguy_down");
            d.mAtkX = self.mT->mX;
            d.mAtkY = self.mT->mY;
            TeleGuyTeleport(self);
            Ecs_Play(self, "unpoof");
            FlipIfLeft(self);
            TeleGuyEnter(self, TG_PRE_ATTACK);
        }
        break;
    case TG_PRE_ATTACK:
        d.mPreAttack.Tick();
        while (d.mPreAttack.Pop() >= 0)
        {
            PlayFacingFlip(self, "idle");
            TeleGuyEnter(self, TG_ATTACKING);
        }
        break;
    default:
        d.mAttacking.Tick();
        while (d.mAttacking.Pop() >= 0)
        {
            TeleGuyEnter(self, TG_IDLE);
        }
        // (Attacking's Update runs after its timers, even when one just left the state)
        if (FxLength(d.mAtkX - self.mT->mX, d.mAtkY - self.mT->mY) <= kFxOne)
        {
            self.mT->mX = d.mAtkX;
            self.mT->mY = d.mAtkY;
            self.mT->mVelX = self.mT->mVelY = 0;
        }
        break;
    }
}

void TeleGuyCollided(Entity& self, Entity& other)
{
    TeleGuyData& d = self.Data<TeleGuyData>();
    if (d.mState != TG_IDLE && d.mState != TG_ATTACKING)
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other))
    {
        if (d.mState == TG_IDLE || Rng_NextDouble() < 0.5f)
        {
            TeleGuyEnter(self, TG_TELEPORTING);
        }
        else
        {
            Die(self, FxF(0.7f));
            Spawn_Explosion(self);
        }
    }
}

const EntityClass kTeleGuy = {
    "TeleGuy", TeleGuyUpdate, nullptr, TeleGuyCollided, nullptr, nullptr, nullptr, nullptr,
    0,         GROUP_PLAYER | GROUP_BROOM, ECF_ENEMY,
};

// ---- SplitBoss, the Watcher (Apartment/SplitBoss.cs) -------------------------------------------------------------
constexpr int kSplitBullets = 12;

// BaseSplitBoss: damage_player, and AnimationChanged putting the offset back.
struct SplitCopyData final
{
    bool mDamagePlayer;
};

void SplitPlay(Entity& e, const char* anim, bool force = false)
{
    if (Ecs_Play(e, anim, force))
    {
        // (_r: offset.Y = 6; else CenterOffset(false): (4, 6) on a 24x32 sprite with a 16x20 hitbox)
        e.mV->mOffY = Fx(6);
        if (!Ecs_AnimIs(e, "idle_r") && !Ecs_AnimIs(e, "dash_r"))
        {
            e.mV->mOffX = Fx(4);
        }
    }
}

void SplitCopyCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && self.Data<SplitCopyData>().mDamagePlayer)
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kSplitCopy = {
    "Copy", nullptr, nullptr, SplitCopyCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// ---- Bullet
struct SplitBulletData final
{
    bool mActive;
};

void SplitBulletUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    Visual& v = *self.mV;
    if (Ecs_AnimFinished(self) || v.mOpacity == 0)
    {
        self.mExists = false;
    }
    if (v.mFlickering)
    {
        Ecs_Flicker(self, FxSec(0.05f));
    }
    if (v.mOpacity != kFxOne)
    {
        MoveTo(v.mOpacity, 0, FxF(0.3f));
    }
    if (v.mOpacity == kFxOne && InGrid(self.mT->mY) > Fx(8 * 16))
    {
        Ecs_Play(self, "fizzle");
    }
}

void SplitBulletCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && Player_State() != PS_AIR && self.Data<SplitBulletData>().mActive)
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kSplitBullet = {
    "Bullet", SplitBulletUpdate, nullptr, SplitBulletCollided, nullptr, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER,      0,
};

void SplitBulletSpawn(Entity& b, fx x, fx y, bool hurts)
{
    b.mExists = true;
    b.mT->mX = x;
    b.mT->mY = y;
    Ecs_Play(b, "pulsate");
    b.mT->mVelX = b.mT->mVelY = 0;
    b.Data<SplitBulletData>().mActive = hurts;
    b.mV->mOpacity = kFxOne;
}

// ---- FadeOutGameScreenFade: takes the white fade away after the fight
fx gSplitFade = 0; // GlobalState.gameScreenFade.alpha, while white

void SplitFadeOutUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    gSplitFade = gSplitFade - 3 * kFxTick;
    gSplitFade = gSplitFade < 0 ? 0 : (gSplitFade > Fx(1) ? Fx(1) : gSplitFade);
    Screen_SetFade(gSplitFade);
}

void SplitFadeOutEvent(Entity&, int gameEvent)
{
    if (gameEvent == GEV_START_WARP || gameEvent == GEV_START_SCREEN_TRANSITION)
    {
        gSplitFade = 0;
        Screen_SetFade(0);
    }
}

const EntityClass kSplitFadeOut = {
    "FadeOutGameScreenFade", SplitFadeOutUpdate, nullptr, nullptr, nullptr, nullptr, SplitFadeOutEvent, nullptr,
    0,                       0,                  0,
};

// ---- SplitBoss
enum SplitCoroutine : uint8_t
{
    SB_INTRO,
    SB_DIE,
    SB_DASH,
    SB_SHOOT,
    SB_SPLIT,
    SB_NONE, // yield break
};

struct SplitBossData final
{
    uint8_t mCo;   // the running coroutine
    uint8_t mStep; // its step
    uint8_t mGen;  // bumped by each new coroutine
    bool mBossRush;
    bool mHitThisFrame;
    bool mVulnerable;
    bool mDamagePlayer;
    bool mVolume; // its VolumeEvent(0, 0.3)
    int mHealth;
    fx mInvulnTime;
    fx mTlX; // the fight area's top left
    fx mTlY;
    // the coroutines' locals
    int mDash;
    int mNumDashes;
    int mNumDrops;
    int mMax;
    int mRound;
    int mNumRounds;
    int mPerRound;
    fx mWait;
    fx mT;
    fx mNextDrop;
    fx mSplitVel;
    int mSpawnedCount;
    uint8_t mSpawned[kSplitBullets]; // indices into mBullets
    // (the pool: there's only ever one Watcher, so it lives outside the brain data)
    static Entity* mBullets[kSplitBullets];
    static Entity* mCopies[2];
};

Entity* SplitBossData::mBullets[kSplitBullets];
Entity* SplitBossData::mCopies[2];

int SplitPhase(const SplitBossData& d)
{
    return d.mHealth > 8 ? 0 : (d.mHealth > 4 ? 1 : 2);
}

int SplitAlive(const SplitBossData& d)
{
    int n = 0;
    for (const Entity* b : d.mBullets)
    {
        n += b->mExists ? 1 : 0;
    }
    return n;
}

void SplitStart(SplitBossData& d, uint8_t co)
{
    d.mCo = co;
    d.mStep = 0;
    d.mGen++;
}

void SplitChooseNextAttack(SplitBossData& d, double dashChance, double splitChance)
{
    const double r = Rng_NextDouble();
    if (r < dashChance)
    {
        SplitStart(d, SB_DASH);
    }
    else if (r < dashChance + splitChance)
    {
        SplitStart(d, SB_SPLIT);
    }
    else
    {
        SplitStart(d, SB_SHOOT);
    }
}

void SplitPlaceOverPlayer(Entity& self)
{
    const Entity& p = *World_Player();
    self.mT->mX = p.mT->mX;
    self.mT->mY = p.mT->mY - Fx(24);
}

// Intro: waits for a hit, then splits in two and comes back together.
bool SplitIntroStep(Entity& self, SplitBossData& d)
{
    Entity& c0 = *d.mCopies[0];
    Entity& c1 = *d.mCopies[1];
    Visual& v = *self.mV;
    switch (d.mStep)
    {
    case 0:
        d.mVolume = true;
        d.mStep = 1;
        [[fallthrough]];
    case 1:
        if (!d.mHitThisFrame)
        {
            return true;
        }
        if (!d.mBossRush)
        {
            Game_ShowDialogue("splitboss", "before_fight");
        }
        d.mStep = 2;
        [[fallthrough]];
    case 2:
        if (!d.mBossRush && !Game_DialogueFinished())
        {
            return true;
        }
        d.mVolume = false;
        Game_PlaySfx("sb_split");
        Game_PlaySong("apartment-boss");
        v.mVisible = false;
        for (Entity* c : d.mCopies)
        {
            c->mExists = true;
            c->mT->mX = self.mT->mX;
            c->mT->mY = self.mT->mY;
            c->mT->mVelX = Fx(50);
        }
        c0.mT->mVelX = -c0.mT->mVelX;
        d.mStep = 3;
        [[fallthrough]];
    case 3:
        if (!MoveTo(c0.mV->mOpacity, 0, FxF(0.6f)))
        {
            c1.mV->mOpacity = c0.mV->mOpacity;
            return true;
        }
        v.mOpacity = 0;
        v.mVisible = true;
        self.mT->mY = d.mTlY - Fx(32);
        c0.mT->mX = self.mT->mX - Fx(40);
        c1.mT->mX = self.mT->mX + Fx(40);
        for (Entity* c : d.mCopies)
        {
            c->mT->mVelX = c->mT->mVelY = 0;
            c->mT->mY = self.mT->mY;
            SplitPlay(*c, "float", true);
        }
        SplitPlay(self, "float", true);
        d.mStep = 4;
        [[fallthrough]];
    case 4:
        if (!MoveTo(v.mOpacity, kFxOne, FxF(0.6f)))
        {
            c0.mV->mOpacity = c1.mV->mOpacity = v.mOpacity;
            return true;
        }
        d.mStep = 5;
        [[fallthrough]];
    default:
    {
        const bool c0There = MoveTo(c0.mT->mX, self.mT->mX, Fx(30));
        const bool c1There = MoveTo(c1.mT->mX, self.mT->mX, Fx(30));
        if (!c0There || !c1There)
        {
            Ecs_Flicker(self, FxSec(0.2f));
            Ecs_Flicker(c0, FxSec(0.2f));
            Ecs_Flicker(c1, FxSec(0.2f));
            return true;
        }
        c0.mExists = c1.mExists = false;
        SplitStart(d, SB_SHOOT);
        d.mVulnerable = true;
        d.mDamagePlayer = true;
        return false;
    }
    }
}

bool SplitDieStep(Entity& self, SplitBossData& d)
{
    switch (d.mStep)
    {
    case 0:
        self.mT->mVelX = self.mT->mVelY = 0;
        self.mV->mOpacity = kFxOne;
        d.mDamagePlayer = false;
        d.mVulnerable = false;
        for (Entity* b : d.mBullets)
        {
            b->Data<SplitBulletData>().mActive = false;
        }
        if (!d.mBossRush)
        {
            Game_ShowDialogue("splitboss", "after_fight");
        }
        d.mStep = 1;
        [[fallthrough]];
    case 1:
        if (!d.mBossRush && !Game_DialogueFinished())
        {
            return true;
        }
        Game_PlaySfx("big_door_locked");
        d.mT = 0;
        Screen_SetFadeWhite(true);
        SplitPlay(self, "die");
        Ecs_Flicker(self, FxSec(5.0f));
        d.mStep = 2;
        [[fallthrough]];
    case 2:
        if (d.mT < FxF(5.0f))
        {
            d.mT += kFxTick;
            Game_SetSongVolume(Game_SongVolume() - FxMul(kFxTick, FxF(0.3f)));
            gSplitFade = d.mT / 5;
            Screen_SetFade(gSplitFade);
            const int r = FxTrunc((65 * d.mT) / 5);
            const int rx = Rng_Range(-r, r);
            const int ry = Rng_Range(-r, r);
            self.mT->mX = d.mTlX + Fx(40) + Fx(rx);
            self.mT->mY = d.mTlY + Fx(40) + Fx(ry);
            for (Entity* b : d.mBullets)
            {
                MoveTo(b->mV->mOpacity, 0, Fx(6));
            }
            for (Entity* c : d.mCopies)
            {
                MoveTo(c->mV->mOpacity, 0, Fx(6));
            }
            return true;
        }
        if (self.mPreset)
        {
            Preset_SetAlive(*self.mPreset, false);
        }
        self.mExists = false;
        if (!d.mBossRush)
        {
            Game_SetBossDefeated("APARTMENT");
            Game_PlaySong("apartment");
        }
        else
        {
            Game_PlaySong("bedroom");
        }
        if (Entity* f = Ecs_Create(kSplitFadeOut, 0, 0, PART_BRAIN))
        {
            Ecs_MarkSpawned(*f);
        }
        return false;
    default:
        return false;
    }
}

bool SplitDashStep(Entity& self, SplitBossData& d)
{
    Transform& t = *self.mT;
    Visual& v = *self.mV;
    const Entity& p = *World_Player();
    for (;;)
    {
        switch (d.mStep)
        {
        case 0:
            d.mNumDashes = SplitPhase(d) + 2;
            d.mNumDrops = SplitPhase(d) + 2;
            d.mSpawnedCount = 0;
            if (FxTrunc(t.mX / 160) != gGame.mGridX || FxTrunc(t.mY / 160) != gGame.mGridY)
            {
                v.mOpacity = 0; // straight after another dash: start appearing on the left at once
            }
            d.mDash = 0;
            d.mStep = 1;
            break;
        case 1:
            if (d.mDash >= d.mNumDashes)
            {
                d.mStep = 6;
                break;
            }
            SplitPlay(self, "idle_r");
            d.mStep = 2;
            [[fallthrough]];
        case 2:
            if (!MoveTo(v.mOpacity, 0, Fx(6)))
            {
                return true;
            }
            t.mX = d.mTlX - Fx(32);
            d.mStep = 3;
            [[fallthrough]];
        case 3:
            if (!MoveTo(v.mOpacity, kFxOne, Fx(9)))
            {
                t.mY = p.mT->mY;
                return true;
            }
            d.mT = FxF(0.5f);
            d.mStep = 4;
            [[fallthrough]];
        case 4:
            if (d.mT > 0)
            {
                t.mY = p.mT->mY;
                d.mT -= kFxTick;
                return true;
            }
            SplitPlay(self, "dash_r");
            Game_PlaySfx("sb_dash");
            t.mVelX = Fx(80 + 20 * SplitPhase(d));
            t.mVelY = 0;
            d.mNextDrop = d.mTlX + Fx(Rng_Range(0, 16));
            d.mMax = d.mNumDrops;
            d.mStep = 5;
            [[fallthrough]];
        case 5:
            // CheckDashEnd
            if (t.mX < d.mTlX + Fx(16 * 8) || !MoveTo(v.mOpacity, 0, Fx(6)))
            {
                v.mOffY = Player_OffsetY() + Fx(2);
                if (d.mMax > 0 && t.mX > d.mNextDrop && t.mX < d.mTlX + Fx(6 * 16) && kSplitBullets > d.mSpawnedCount)
                {
                    d.mMax--;
                    for (int i = 0; i < kSplitBullets; i++)
                    {
                        Entity& b = *d.mBullets[i];
                        if (!b.mExists)
                        {
                            SplitBulletSpawn(b, t.mX, t.mY, false);
                            Ecs_Flicker(b, FxSec(0.05f));
                            d.mSpawned[d.mSpawnedCount++] = static_cast<uint8_t>(i);
                            break;
                        }
                    }
                    Game_PlaySfx("sb_ball_appear");
                    d.mNextDrop = t.mX + Fx(Rng_Range(16, 32));
                }
                return true;
            }
            t.mVelX = t.mVelY = 0;
            d.mDash++;
            d.mStep = 1;
            break;
        case 6:
            for (int i = 0; i < d.mSpawnedCount; i++)
            {
                Ecs_Flicker(*d.mBullets[d.mSpawned[i]], 0);
            }
            d.mT = FxF(0.5f);
            d.mStep = 7;
            [[fallthrough]];
        case 7:
            if (d.mT > 0)
            {
                d.mT -= kFxTick;
                return true;
            }
            Game_PlaySfx("sb_split");
            for (int i = 0; i < d.mSpawnedCount; i++)
            {
                Entity& b = *d.mBullets[d.mSpawned[i]];
                const int ox = Rng_Range(-10, 10);
                const int oy = Rng_Range(-10, 10);
                fx dx = p.mT->mX + Fx(ox) - b.mT->mX;
                fx dy = p.mT->mY + Fx(oy) - b.mT->mY;
                FxNormalize(dx, dy);
                const int speed = 80 + SplitPhase(d) * 20;
                b.Data<SplitBulletData>().mActive = true;
                b.mT->mVelX = speed * dx;
                b.mT->mVelY = speed * dy;
                b.mV->mOpacity = FxF(0.99f);
            }
            d.mStep = 8;
            [[fallthrough]];
        default:
            if (SplitAlive(d) > 0)
            {
                return true;
            }
            v.mOpacity = kFxOne;
            SplitChooseNextAttack(d, 0.3, 0.3);
            return false;
        }
    }
}

bool SplitShootStep(Entity& self, SplitBossData& d)
{
    static const int kRounds[3] = { 4, 10, 7 };
    static const int kPerRound[3] = { 2, 1, 4 };
    static const fx kWait[3] = { FxF(0.7f), FxF(0.2f), FxF(0.8f) };
    for (;;)
    {
        switch (d.mStep)
        {
        case 0:
        {
            self.mT->mVelX = self.mT->mVelY = 0;
            self.mT->mX = d.mTlX + Fx(40);
            self.mT->mY = d.mTlY - Fx(32);
            SplitPlay(self, "float");
            const int phase = SplitPhase(d);
            d.mNumRounds = kRounds[phase];
            d.mPerRound = kPerRound[phase];
            d.mWait = kWait[phase];
            d.mRound = 0;
            d.mStep = 1;
            break;
        }
        case 1:
        {
            if (d.mRound >= d.mNumRounds)
            {
                d.mStep = 3;
                break;
            }
            Game_PlaySfx("sb_ball_appear");
            int start = Rng_Range(0, 6);
            d.mSpawnedCount = 0;
            for (int i = 0; i < kSplitBullets && d.mSpawnedCount < d.mPerRound; i++)
            {
                Entity& b = *d.mBullets[i];
                if (b.mExists)
                {
                    continue;
                }
                SplitBulletSpawn(b, d.mTlX + Fx((16 - 6) / 2 + (start % 6) * 16), d.mTlY - Fx(6), true);
                start++;
                d.mSpawned[d.mSpawnedCount++] = static_cast<uint8_t>(i);
            }
            d.mT = 0;
            d.mStep = 2;
            [[fallthrough]];
        }
        case 2:
            if (d.mT < d.mWait)
            {
                d.mT += kFxTick;
                return true;
            }
            for (int i = 0; i < d.mSpawnedCount; i++)
            {
                Entity& b = *d.mBullets[d.mSpawned[i]];
                b.Data<SplitBulletData>().mActive = true;
                b.mT->mVelX = 0;
                b.mT->mVelY = Fx(40);
            }
            d.mRound++;
            d.mStep = 1;
            break;
        default:
            if (SplitAlive(d) != 0)
            {
                return true;
            }
            SplitChooseNextAttack(d, 0.37, 0.38);
            return false;
        }
    }
}

bool SplitSplitStep(Entity& self, SplitBossData& d)
{
    Entity& c0 = *d.mCopies[0];
    Entity& c1 = *d.mCopies[1];
    Visual& v = *self.mV;
    const Entity& p = *World_Player();
    switch (d.mStep)
    {
    case 0:
        v.mVisible = true;
        d.mVulnerable = false;
        d.mDamagePlayer = false;
        SplitPlay(self, "float");
        self.mT->mX = d.mTlX + Fx(40);
        self.mT->mY = d.mTlY - Fx(16);
        Ecs_Flicker(self, FxSec(1.5f));
        d.mStep = 1;
        [[fallthrough]];
    case 1:
        if (!d.mHitThisFrame)
        {
            return true;
        }
        d.mDamagePlayer = true;
        v.mOpacity = c0.mV->mOpacity = c1.mV->mOpacity = kFxOne;
        c0.mExists = c1.mExists = true;
        SplitPlay(c0, "idle_r");
        SplitPlay(c1, "idle_r");
        c0.mT->mX = c1.mT->mX = self.mT->mX;
        c0.mT->mY = c1.mT->mY = self.mT->mY;
        self.mT->mVelY = Fx(-40);
        c0.mT->mVelX = Fx(-40);
        c1.mT->mVelX = Fx(40);
        d.mStep = 2;
        [[fallthrough]];
    case 2:
        if (!MoveTo(v.mOpacity, 0, FxF(1.2f)))
        {
            c0.mV->mOpacity = c1.mV->mOpacity = v.mOpacity;
            return true;
        }
        self.mT->mVelX = self.mT->mVelY = 0;
        c0.mT->mVelX = c0.mT->mVelY = c1.mT->mVelX = c1.mT->mVelY = 0;
        v.mOpacity = c0.mV->mOpacity = c1.mV->mOpacity = FxF(0.7f);
        d.mT = 0;
        d.mStep = 3;
        [[fallthrough]];
    case 3:
        if (d.mT < FxF(1.0f))
        {
            d.mT += kFxTick;
            SplitPlaceOverPlayer(self);
            c0.mT->mX = p.mT->mX + Fx(25);
            c0.mT->mY = p.mT->mY;
            c1.mT->mX = p.mT->mX - Fx(25);
            c1.mT->mY = p.mT->mY;
            return true;
        }
        Ecs_Flicker(c0, FxSec(0.7f));
        Ecs_Flicker(c1, FxSec(0.7f));
        d.mStep = 4;
        [[fallthrough]];
    case 4:
        if (c0.mV->mFlickering)
        {
            SplitPlaceOverPlayer(self);
            return true;
        }
        c0.Data<SplitCopyData>().mDamagePlayer = c1.Data<SplitCopyData>().mDamagePlayer = true;
        d.mSplitVel = Fx(40 + 10 * SplitPhase(d));
        c0.mT->mVelX = -d.mSplitVel;
        c1.mT->mVelX = d.mSplitVel;
        Game_PlaySfx("sb_dash");
        d.mStep = 5;
        [[fallthrough]];
    case 5:
    {
        const bool c0Gone = MoveTo(c0.mV->mOpacity, 0, FxF(1.2f));
        const bool c1Gone = MoveTo(c1.mV->mOpacity, 0, FxF(1.2f));
        if (!c0Gone || !c1Gone)
        {
            SplitPlaceOverPlayer(self);
            return true;
        }
        c0.mExists = c1.mExists = false;
        c0.Data<SplitCopyData>().mDamagePlayer = c1.Data<SplitCopyData>().mDamagePlayer = false;
        Ecs_Flicker(self, kFxOne);
        d.mStep = 6;
        [[fallthrough]];
    }
    case 6:
        if (v.mFlickering)
        {
            return true;
        }
        d.mVulnerable = true;
        self.mT->mVelY = d.mSplitVel;
        d.mStep = 7;
        [[fallthrough]];
    default:
        if (!MoveTo(v.mOpacity, 0, FxF(1.2f)))
        {
            return true;
        }
        v.mOpacity = kFxOne;
        self.mT->mVelX = self.mT->mVelY = 0;
        if (SplitPhase(d) <= 1)
        {
            SplitChooseNextAttack(d, 0.3, 0.3);
        }
        else
        {
            SplitChooseNextAttack(d, 0.5, 0.25);
        }
        return false;
    }
}

void SplitBossUpdate(Entity& self)
{
    SplitBossData& d = self.Data<SplitBossData>();
    Ecs_BaseUpdate(self);
    // (its VolumeEvent: spawned on the first update, so it starts on the next)
    if (d.mVolume && !(d.mCo == SB_INTRO && d.mStep == 0))
    {
        fx volume = Game_SongVolume();
        MoveTo(volume, 0, FxF(0.3f));
        Game_SetSongVolume(volume);
    }
    const uint8_t gen = d.mGen;
    bool running = true;
    switch (d.mCo)
    {
    case SB_INTRO:
        running = SplitIntroStep(self, d);
        break;
    case SB_DIE:
        running = SplitDieStep(self, d);
        break;
    case SB_DASH:
        running = SplitDashStep(self, d);
        break;
    case SB_SHOOT:
        running = SplitShootStep(self, d);
        break;
    case SB_SPLIT:
        running = SplitSplitStep(self, d);
        break;
    default:
        break;
    }
    if (!running && d.mGen == gen)
    {
        d.mCo = SB_NONE;
    }
    MoveTo(d.mInvulnTime, 0, kFxOne);
}

void SplitBossPostUpdate(Entity& self)
{
    self.Data<SplitBossData>().mHitThisFrame = false;
}

void SplitBossCollided(Entity& self, Entity& other)
{
    SplitBossData& d = self.Data<SplitBossData>();
    if (IsPlayer(other))
    {
        if (d.mDamagePlayer)
        {
            Player_ReceiveDamage(1);
        }
    }
    else if (IsBroom(other))
    {
        d.mHitThisFrame = true;
        if (d.mVulnerable && d.mInvulnTime == 0)
        {
            d.mInvulnTime = FxF(0.8f);
            Ecs_Flicker(self, d.mInvulnTime);
            Game_PlaySfx("sb_hurt");
            Game_PlaySfx("broom_hit");
            d.mHealth--;
            if (d.mHealth == 0)
            {
                d.mVulnerable = false;
                d.mDamagePlayer = false;
                SplitStart(d, SB_DIE);
            }
        }
    }
}

const EntityClass kSplitBoss = {
    "SplitBoss", SplitBossUpdate, SplitBossPostUpdate, SplitBossCollided, nullptr, nullptr, nullptr, nullptr,
    0,           GROUP_PLAYER | GROUP_BROOM, ECF_ENEMY,
};

// ---- Hotel enemies (Hotel/*.cs) ----------------------------------------------------------------------------------
// ---- Dustmaid: wakes up and chases the player once the broom holds a dust
struct DustmaidData final
{
    uint8_t mStep;
    int mHealth;
    bool mChasing;
};

void DustmaidUpdate(Entity& self)
{
    DustmaidData& d = self.Data<DustmaidData>();
    Ecs_BaseUpdate(self);
    switch (d.mStep)
    {
    case 0:
        if (!Player_CarryingDust())
        {
            return;
        }
        Ecs_Play(self, "turn_dark");
        FlipIfLeft(self);
        Game_PlaySfx("dustmaid_alert");
        self.mV->mOpacity = kFxOne;
        d.mChasing = true;
        d.mStep = 1;
        [[fallthrough]];
    case 1:
        if (!Ecs_AnimFinished(self))
        {
            return;
        }
        d.mStep = 2;
        [[fallthrough]];
    default:
    {
        if (!self.mExists)
        {
            return;
        }
        const Entity& p = *World_Player();
        Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
        PlayFacingFlip(self, "move");
        Ecs_MoveTowards(self, p.mT->mX, p.mT->mY, Fx(20));
        return;
    }
    }
}

void DustmaidCollided(Entity& self, Entity& other)
{
    DustmaidData& d = self.Data<DustmaidData>();
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other) && d.mChasing && !self.mV->mFlickering)
    {
        d.mHealth--;
        Ecs_Flicker(self, kFxOne);
        Game_PlaySfx("broom_hit");
        if (d.mHealth <= 0)
        {
            Die(self, FxF(0.7f));
            Spawn_Explosion(self);
        }
    }
}

const EntityClass kDustmaid = {
    "Dustmaid", DustmaidUpdate, nullptr, DustmaidCollided, nullptr, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER | GROUP_BROOM, ECF_ENEMY | ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN,
};

// ---- SteamPipe: blows the player along unless a dust plugs it
constexpr int kSteamPuffs = 6;
constexpr int kSteamEjectVel = 50;

struct SteamPipeData final
{
    bool mDisabled;
    bool mPlayerCollided;
    Entity* mSteam[kSteamPuffs];
};

struct SteamData final
{
    fx mTimer;
    bool mDisabled;
    fx mStartX;
    fx mStartY;
};

void SteamSpawn(Entity& self)
{
    SteamData& d = self.Data<SteamData>();
    d.mTimer = d.mDisabled ? Fx(Rng_Range(3, 6)) / 60 : Fx(Rng_Range(30, 51)) / 60;
    Transform& t = *self.mT;
    t.mX = d.mStartX;
    t.mY = d.mStartY;
    switch (self.mFacing)
    {
    case FACE_DOWN:
        t.mVelY = Fx(kSteamEjectVel + Rng_Range(0, 21));
        t.mVelX = Fx(Rng_Range(-30, 31));
        break;
    case FACE_UP:
        t.mVelY = Fx(-kSteamEjectVel + Rng_Range(-20, 1));
        t.mVelX = Fx(Rng_Range(-30, 31));
        break;
    case FACE_LEFT:
        t.mVelX = Fx(-kSteamEjectVel + Rng_Range(-20, 1));
        t.mVelY = Fx(Rng_Range(-30, 31));
        break;
    default:
        t.mVelX = Fx(kSteamEjectVel + Rng_Range(0, 21));
        t.mVelY = Fx(Rng_Range(-30, 31));
        break;
    }
    Game_PlaySfx("fireball");
}

void SteamUpdate(Entity& self)
{
    SteamData& d = self.Data<SteamData>();
    Ecs_BaseUpdate(self);
    d.mTimer -= kFxTick;
    if (d.mTimer <= 0)
    {
        SteamSpawn(self);
    }
    d.mDisabled = false;
}

void SteamCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other) && self.mParent)
    {
        self.mParent->Data<SteamPipeData>().mPlayerCollided = true;
    }
}

const EntityClass kSteam = {
    "Steam", SteamUpdate, nullptr, SteamCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

void SteamPipeUpdate(Entity& self)
{
    SteamPipeData& d = self.Data<SteamPipeData>();
    Ecs_BaseUpdate(self);
    if (d.mPlayerCollided && !d.mDisabled)
    {
        switch (self.mFacing)
        {
        case FACE_DOWN:
            Player_SetAdditionalVelY(Fx(kSteamEjectVel));
            break;
        case FACE_UP:
            Player_SetAdditionalVelY(Fx(-kSteamEjectVel));
            break;
        case FACE_LEFT:
            Player_SetAdditionalVelX(Fx(-kSteamEjectVel));
            break;
        default:
            Player_SetAdditionalVelX(Fx(kSteamEjectVel));
            break;
        }
    }
    if (d.mDisabled)
    {
        for (Entity* s : d.mSteam)
        {
            s->Data<SteamData>().mDisabled = true;
        }
    }
    d.mDisabled = false;
    d.mPlayerCollided = false;
}

void SteamPipeCollided(Entity& self, Entity& other)
{
    SteamPipeData& d = self.Data<SteamPipeData>();
    if (d.mDisabled)
    {
        return;
    }
    if (IsPlayer(other))
    {
        d.mPlayerCollided = true;
    }
    else if (!Dust_IsRaft(other))
    {
        d.mDisabled = true;
    }
}

const EntityClass kSteamPipe = {
    "SteamPipe", SteamPipeUpdate, nullptr, SteamPipeCollided, nullptr, nullptr, nullptr, nullptr,
    0,           GROUP_PLAYER | GROUP_DUST, 0,
};

// ---- SpaceFace (Etc/SpaceFace.cs): drifts towards the player now and then
struct SpaceFaceData final
{
    fx mWait;
    fx mWaitMax;
};

void SpaceFaceUpdate(Entity& self)
{
    SpaceFaceData& d = self.Data<SpaceFaceData>();
    Ecs_BaseUpdate(self);
    d.mWait += kFxTick;
    if (d.mWait >= d.mWaitMax)
    {
        d.mWait = 0;
        const Entity& p = *World_Player();
        Ecs_MoveTowards(self, p.mT->mX, p.mT->mY, Fx(55));
        self.mT->mDragX = self.mT->mDragY = Fx(Rng_Range(15, 26));
    }
}

const EntityClass kSpaceFace = {
    "SpaceFace", SpaceFaceUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- Chaser (Cell/Chaser.cs): walks at the player along its row/column once lined up, faster after a broom swing
struct ChaserData final
{
    bool mHorizontal;
    fx mTargetVel;
};

void ChaserCheckStartMove(Entity& self, ChaserData& d)
{
    const Entity& p = *World_Player();
    Transform& t = *self.mT;
    if (d.mHorizontal)
    {
        if (p.mT->mY > t.mY - Fx(p.mB->mH) && p.mT->mY < t.mY + Fx(self.mB->mH))
        {
            Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
            t.mVelX = Fx(15 * FacingX(static_cast<Facing>(self.mFacing)));
            t.mVelY = 0;
            if (t.mVelX != 0)
            {
                Ecs_PlayFacing(self, "walking");
            }
        }
    }
    else
    {
        if (p.mT->mX > t.mX - Fx(p.mB->mW) && p.mT->mX < t.mX + Fx(self.mB->mW))
        {
            Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
            t.mVelX = 0;
            t.mVelY = Fx(15 * FacingY(static_cast<Facing>(self.mFacing)));
            if (t.mVelY != 0)
            {
                Ecs_PlayFacing(self, "walking");
            }
        }
    }
}

void ChaserUpdate(Entity& self)
{
    ChaserData& d = self.Data<ChaserData>();
    Ecs_BaseUpdate(self);
    Transform& t = *self.mT;
    if (t.mVelX == 0 && t.mVelY == 0)
    {
        ChaserCheckStartMove(self, d);
        return;
    }
    if (self.mB->mTouching != TOUCH_NONE)
    {
        t.mVelX = -t.mVelX;
        t.mVelY = -t.mVelY;
        self.mFacing = FlipFacing(static_cast<Facing>(self.mFacing));
        Ecs_PlayFacing(self, "walking");
    }
    if (d.mTargetVel != 0)
    {
        fx& v = d.mHorizontal ? t.mVelX : t.mVelY;
        const int sign = v > 0 ? 1 : (v < 0 ? -1 : 0);
        if (MoveTo(v, d.mTargetVel * sign, FxDiv(FxMul(d.mTargetVel, FxF(0.6f / 1.6f)), FxF(0.3f))))
        {
            d.mTargetVel = 0;
        }
    }
}

void ChaserCollided(Entity&, Entity& other)
{
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(6);
    }
}

void ChaserEvent(Entity& self, int gameEvent)
{
    if (gameEvent != GEV_BROOM_USED)
    {
        return;
    }
    ChaserData& d = self.Data<ChaserData>();
    Game_PlaySfx("mover_move");
    const fx vx = self.mT->mVelX;
    const fx vy = self.mT->mVelY;
    d.mTargetVel = FxMul(FxLength(vx, vy), FxF(1.6f));
    if (d.mTargetVel > Fx(100))
    {
        d.mTargetVel = Fx(100);
    }
}

const EntityClass kChaser = {
    "Chaser", ChaserUpdate, nullptr, ChaserCollided, nullptr, nullptr, ChaserEvent, nullptr,
    0,        GROUP_PLAYER, ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN,
};

// ---- FirePillar (Circus/FirePillar.cs): flames up out of its base every couple of seconds; dust or a roller on it
// puts it out (hides it until it next emerges)
enum PillarState : uint8_t
{
    PILLAR_IDLE,
    PILLAR_EMERGE,
    PILLAR_FLAME,
    PILLAR_RECEDE,
};

struct FirePillarData final
{
    uint8_t mState;
    // Each state object's TimerState lives as long as the pillar: its clock only runs while the state does.
    TimerState<1> mTimers[4];
};

void PillarEnter(Entity& self, uint8_t state)
{
    self.Data<FirePillarData>().mState = state;
    switch (state)
    {
    case PILLAR_IDLE:
        Ecs_Play(self, "idle");
        self.mV->mVisible = true;
        break;
    case PILLAR_EMERGE:
        Ecs_Play(self, "emerge");
        Ecs_Flicker(self, FxSec(0.25f));
        break;
    case PILLAR_FLAME:
        Ecs_Play(self, "flame");
        Game_PlaySfx("flame_pillar");
        break;
    default:
        Ecs_Play(self, "recede");
        break;
    }
}

void FirePillarUpdate(Entity& self)
{
    FirePillarData& d = self.Data<FirePillarData>();
    Ecs_BaseUpdate(self);
    TimerState<1>& timers = d.mTimers[d.mState];
    timers.Tick();
    const uint8_t from = d.mState;
    while (timers.Pop() >= 0)
    {
        PillarEnter(self, static_cast<uint8_t>((from + 1) % 4));
    }
}

void FirePillarCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        if (self.Data<FirePillarData>().mState == PILLAR_FLAME)
        {
            Player_ReceiveDamage(1);
        }
    }
    else
    {
        PillarEnter(self, PILLAR_IDLE);
        self.mV->mVisible = false;
    }
}

const EntityClass kFirePillar = {
    "FirePillar", FirePillarUpdate, nullptr, FirePillarCollided, nullptr, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER | GROUP_DUST | GROUP_SPIKE_ROLLER, 0,
};

const EntityClass kPillarBase = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- Contort (Circus/Contort.cs): bounces around the room; three hits and it splits into three smalls that hop out
// and roll about (ContortSmall, a HealthDropper)
constexpr fx kContortVel = Fx(35);
constexpr int kContortSmalls = 3;

struct ContortData final
{
    fx mDeathTimer;
    int mHealth;
    Entity* mSmalls[kContortSmalls];
};

struct ContortSmallData final
{
    fx mRollTimer;
    Parabola mArc;
    Entity* mShadow;
};

// Bounce off whatever it hit.
void ContortBounce(Entity& self)
{
    const uint8_t touching = self.mB->mTouching;
    if (touching & (TOUCH_RIGHT | TOUCH_LEFT))
    {
        self.mT->mVelX = -self.mT->mVelX;
    }
    if (touching & (TOUCH_UP | TOUCH_DOWN))
    {
        self.mT->mVelY = -self.mT->mVelY;
    }
}

fx ContortRandomVel()
{
    const int sign = Rng_NextDouble() > 0.5 ? 1 : -1;
    return kContortVel * sign + Fx(Rng_Range(-5, 5));
}

void ContortUpdate(Entity& self)
{
    ContortData& d = self.Data<ContortData>();
    Ecs_BaseUpdate(self);
    if (d.mDeathTimer > 0)
    {
        ContortBounce(self);
        if (d.mHealth == 0)
        {
            d.mDeathTimer -= kFxTick;
            if (d.mDeathTimer <= 0)
            {
                Ecs_Flicker(self, 0);
                // Split
                self.mV->mVisible = false;
                Spawn_Explosion(self);
                for (Entity* s : d.mSmalls)
                {
                    s->mT->mX = self.mT->mX;
                    s->mT->mY = self.mT->mY;
                    s->mExists = true;
                    s->Data<ContortSmallData>().mShadow->mExists = true;
                }
            }
        }
        return;
    }
    for (Entity* s : d.mSmalls)
    {
        if (s->mExists)
        {
            return;
        }
    }
    if (self.mPreset)
    {
        Preset_SetAlive(*self.mPreset, false);
    }
    self.mExists = false;
}

void ContortCollided(Entity& self, Entity& other)
{
    ContortData& d = self.Data<ContortData>();
    if (d.mHealth == 0)
    {
        return;
    }
    if (IsBroom(other) && !self.mV->mFlickering)
    {
        d.mHealth--;
        if (d.mHealth == 0)
        {
            self.mT->mVelX = self.mT->mVelY = 0;
        }
        Ecs_Flicker(self, FxSec(1.5f));
    }
    else if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
}

// Contort.Fall: it doesn't.
void ContortMapEvent(Entity&, MapEvent, uint8_t, fx, fx)
{
}

void ContortSmallUpdate(Entity& self)
{
    ContortSmallData& d = self.Data<ContortSmallData>();
    Ecs_BaseUpdate(self);
    // (Shadow.Update: it follows in base.Update, PositionOffset = (0, -5) - (16, 16) / 4)
    d.mShadow->mT->mX = self.mT->mX + Fx(4);
    d.mShadow->mT->mY = self.mT->mY + Fx(9);
    if (ParabolaTick(d.mArc, self, d.mShadow, 3))
    {
        d.mShadow->mV->mVisible = false;
        d.mRollTimer += kFxTick;
        if (d.mRollTimer >= FxF(0.8f))
        {
            d.mRollTimer = 0;
            self.mT->mVelX = fx(ContortRandomVel());
            self.mT->mVelY = fx(ContortRandomVel());
        }
    }
    ContortBounce(self);
}

void ContortSmallCollided(Entity& self, Entity& other)
{
    if (IsBroom(other) && !self.mV->mFlickering)
    {
        Die(self, FxF(0.4f));
        self.Data<ContortSmallData>().mShadow->mExists = false;
        Spawn_Explosion(self);
    }
    else if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kContort = {
    "Contort", ContortUpdate, nullptr, ContortCollided, nullptr, ContortMapEvent, nullptr, nullptr,
    0,         GROUP_PLAYER | GROUP_BROOM, ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_ENEMY,
};

const EntityClass kContortSmall = {
    "ContortSmall", ContortSmallUpdate, nullptr, ContortSmallCollided, nullptr, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER | GROUP_BROOM, ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN,
};

// ---- Lion (Circus/Lion.cs): paces, spits fireballs at the player or backs off and pounces
enum LionState : uint8_t
{
    LION_PACE,
    LION_SHOOT_WARNING,
    LION_SHOOT,
    LION_CHARGE_WALK,
    LION_CHARGE_WARN,
    LION_CHARGE,
    LION_DYING,
};

constexpr int kLionMaxShots = 18;
constexpr int kLionFireballs = 10;
// Each state's TimerState (one timer each; Dying has none): due at mAt on a clock that only runs while it's active.
constexpr fx kLionTimerMax[LION_DYING] = { FxF(0.8f), FxF(0.8f), FxF(0.165f), FxF(1.4f), FxF(0.6f), FxF(0.6f) };

struct LionData final
{
    fx mClock[LION_DYING];
    fx mAt[LION_DYING];
    Parabola mArc;
    Entity* mShadowV;
    Entity* mShadowH;
    Entity* mShadow; // Entity.shadow: whichever the last anim picked (null until then)
    int mHealth;
    int mShotsFired;
    uint8_t mState;
};

extern const EntityClass kLionFireball;

struct LionFireballData final
{
    fx mStartX;
    fx mStartY;
};

// AnimationChanged: horizontal anims get the wide box and shadow, vertical ones the tall.
void LionPlay(Entity& self, const char* prefix)
{
    static const char kSuffix[4] = { 'l', 'r', 'u', 'd' };
    char name[12];
    snprintf(name, sizeof(name), "%s_%c", prefix, kSuffix[self.mFacing & 3]);
    if (!Ecs_Play(self, name))
    {
        return;
    }
    LionData& d = self.Data<LionData>();
    const char last = kSuffix[self.mFacing & 3];
    self.mV->mHFlip = last == 'l';
    if (last == 'l' || last == 'r')
    {
        d.mShadow = d.mShadowH;
        self.mB->mW = 26;
        self.mB->mH = 13;
        self.mV->mOffX = Fx(3);
        self.mV->mOffY = Fx(11);
    }
    else
    {
        d.mShadow = d.mShadowV;
        self.mB->mW = 10;
        self.mB->mH = 20;
        self.mV->mOffX = Fx(10);
        self.mV->mOffY = Fx(4);
    }
    d.mShadowH->mExists = d.mShadow == d.mShadowH;
    d.mShadowV->mExists = d.mShadow == d.mShadowV;
    d.mShadow->mV->mVisible = false;
}

void LionFireballSpawn(Entity& b, const Entity& lion)
{
    Game_PlaySfx("fireball");
    Transform& t = *b.mT;
    const fx px = lion.mT->mX;
    const fx py = lion.mT->mY;
    fx vx = 0;
    fx vy = 0;
    switch (lion.mFacing)
    {
    case FACE_UP:
        t.mX = px + Fx(8);
        t.mY = py - Fx(2);
        vx = Fx(Rng_Range(-16, 17));
        vy = Fx(-32);
        break;
    case FACE_DOWN:
        t.mX = px + Fx(8);
        t.mY = py + Fx(lion.mB->mH) + Fx(2);
        vx = Fx(Rng_Range(-16, 17));
        vy = Fx(32);
        break;
    case FACE_RIGHT:
        t.mX = px + Fx(30);
        t.mY = py + Fx(2);
        vx = Fx(32);
        vy = Fx(Rng_Range(-26, 27));
        break;
    default:
        t.mX = px;
        t.mY = py + Fx(2);
        vx = Fx(-32);
        vy = Fx(Rng_Range(-26, 27));
        break;
    }
    // Vector2.Normalize, then * 88
    FxNormalize(vx, vy);
    t.mVelX = FxMul(vx, Fx(88));
    t.mVelY = FxMul(vy, Fx(88));
    LionFireballData& d = b.Data<LionFireballData>();
    d.mStartX = t.mX;
    d.mStartY = t.mY;
    Ecs_Play(b, "shoot");
}

void LionEnter(Entity& self, uint8_t state)
{
    LionData& d = self.Data<LionData>();
    const Entity& p = *World_Player();
    Transform& t = *self.mT;
    if (d.mState == LION_CHARGE)
    {
        t.mVelX = t.mVelY = 0; // Charge's Exit
    }
    d.mState = state;
    switch (state)
    {
    case LION_SHOOT_WARNING:
        t.mVelX = t.mVelY = 0;
        self.mFacing = p.mT->mX > t.mX + Fx(16) ? FACE_RIGHT : FACE_LEFT;
        LionPlay(self, "warn");
        break;
    case LION_CHARGE_WALK:
        t.mVelX = Fx(20);
        t.mVelY = 0;
        if (p.mT->mX > t.mX)
        {
            self.mFacing = FACE_RIGHT;
            t.mVelX = -t.mVelX;
        }
        else
        {
            self.mFacing = FACE_LEFT;
        }
        LionPlay(self, "walk");
        break;
    case LION_CHARGE_WARN:
        t.mVelX = Fx(self.mFacing == FACE_LEFT ? -10 : 10);
        t.mVelY = 0;
        LionPlay(self, "warn");
        break;
    case LION_CHARGE:
        Ecs_MoveTowards(self, p.mT->mX, p.mT->mY, Fx(110));
        Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
        d.mShadow->mV->mVisible = true;
        LionPlay(self, "pounce");
        break;
    default:
        break;
    }
}

void LionGetHit(Entity& self)
{
    LionData& d = self.Data<LionData>();
    if (self.mV->mFlickering)
    {
        return;
    }
    Game_PlaySfx("broom_hit");
    Ecs_Flicker(self, FxSec(1.0f));
    d.mHealth--;
    if (d.mHealth <= 0)
    {
        LionEnter(self, LION_DYING);
    }
}

// The timer event of `state` (the state that owned the timer, even once something changed it).
void LionTimerEvent(Entity& self, uint8_t state)
{
    LionData& d = self.Data<LionData>();
    switch (state)
    {
    case LION_PACE:
    {
        const double r = Rng_NextDouble();
        if (r < 0.25)
        {
            LionEnter(self, LION_SHOOT_WARNING);
        }
        else if (r < 0.55)
        {
            LionEnter(self, LION_CHARGE_WALK);
        }
        else
        {
            const int paceR = Rng_Range(0, 6);
            if (paceR < 4)
            {
                static const Facing kFacings[4] = { FACE_RIGHT, FACE_DOWN, FACE_LEFT, FACE_UP };
                self.mFacing = kFacings[paceR];
                LionPlay(self, "walk");
                self.mT->mVelX = Fx(43 * FacingX(kFacings[paceR]));
                self.mT->mVelY = Fx(43 * FacingY(kFacings[paceR]));
            }
        }
        break;
    }
    case LION_SHOOT_WARNING:
        LionEnter(self, LION_SHOOT);
        break;
    case LION_SHOOT:
        if (d.mShotsFired < kLionMaxShots)
        {
            // fireballs.Spawn: the first one not out
            for (Entity* b = self.mNext; b; b = b->mNext)
            {
                if (b->mParent == &self && b->mClass == &kLionFireball && !b->mExists)
                {
                    b->mExists = true;
                    LionFireballSpawn(*b, self);
                    d.mShotsFired++;
                    break;
                }
            }
        }
        else
        {
            d.mShotsFired = 0;
            LionEnter(self, LION_PACE);
        }
        break;
    case LION_CHARGE_WALK:
        LionEnter(self, LION_CHARGE_WARN);
        break;
    case LION_CHARGE_WARN:
        LionEnter(self, LION_CHARGE);
        break;
    default:
        break; // Charge has ChargeWarn's timer too, and no handler for it
    }
}

void LionUpdate(Entity& self)
{
    LionData& d = self.Data<LionData>();
    Ecs_BaseUpdate(self);
    if (d.mShadow)
    {
        // (Shadow.Update: PositionOffset = (8, -6) - (32, 32) / 4)
        d.mShadow->mT->mX = self.mT->mX;
        d.mShadow->mT->mY = self.mT->mY + Fx(14);
    }
    const uint8_t state = d.mState;
    if (state != LION_DYING)
    {
        // TimerState.Update: fire what's due, then the state's own Update
        d.mClock[state] += kFxTick;
        if (d.mAt[state] <= d.mClock[state])
        {
            d.mAt[state] = d.mClock[state] + kLionTimerMax[state];
            LionTimerEvent(self, state);
        }
    }
    const Entity& p = *World_Player();
    switch (state)
    {
    case LION_SHOOT:
        Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
        LionPlay(self, "shoot");
        break;
    case LION_CHARGE:
        if (ParabolaTick(d.mArc, self, d.mShadow, 4) ||
            (self.mB->mTouching != TOUCH_NONE && self.mV->mOffY > Fx(4)))
        {
            self.mV->mOffY = 0;
            d.mArc.mT = 0;
            d.mShadowH->mV->mVisible = false;
            LionPlay(self, "walk");
            LionEnter(self, LION_PACE);
        }
        break;
    case LION_DYING:
        self.mV->mOpacity -= FxF(0.05f);
        if (self.mV->mOpacity <= 0)
        {
            Spawn_Explosion(self);
            self.mExists = false;
            d.mShadowH->mExists = d.mShadowV->mExists = false;
        }
        break;
    default:
        break;
    }
}

void LionCollided(Entity& self, Entity& other)
{
    LionData& d = self.Data<LionData>();
    if (d.mState == LION_DYING)
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (IsBroom(other))
    {
        LionGetHit(self);
        if (d.mState == LION_SHOOT)
        {
            d.mShotsFired = kLionMaxShots;
        }
    }
}

void LionFireballUpdate(Entity& self)
{
    if (Ecs_AnimFinished(self) && Ecs_AnimIs(self, "poof"))
    {
        self.mExists = false;
        return;
    }
    const LionFireballData& d = self.Data<LionFireballData>();
    const fx dx = d.mStartX - self.mT->mX;
    const fx dy = d.mStartY - self.mT->mY;
    if ((dx < 0 ? -dx : dx) > Fx(80) || (dy < 0 ? -dy : dy) > Fx(80))
    {
        Ecs_Play(self, "poof");
    }
}

void LionFireballCollided(Entity& self, Entity& other)
{
    if (Ecs_AnimIs(self, "poof"))
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    Ecs_Play(self, "poof");
}

const EntityClass kLion = {
    "Lion", LionUpdate, nullptr, LionCollided, nullptr, nullptr, nullptr, nullptr,
    0,      GROUP_PLAYER | GROUP_BROOM, ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_ENEMY,
};

const EntityClass kLionFireball = {
    "Fireball", LionFireballUpdate, nullptr, LionFireballCollided, nullptr, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER | GROUP_BROOM | GROUP_DUST, 0,
};

// ---- ArthurDanger / JavieraDanger (Npc/Circus/*Danger.cs): the acrobats stuck up on the circus walls. Arthur wobbles
// on his ledge until dust is under him, then drops onto the dust pillow; Javiera waits for the room's enemies to die.
// The C#'s StateLogic coroutines, as steps.
enum FolksStep : uint8_t
{
    FOLKS_TRANSITION,
    FOLKS_TALK,
    FOLKS_WOBBLE,
    FOLKS_FALL,
    FOLKS_WAIT_KILLS,
    FOLKS_WAIT,
    FOLKS_TALK_AGAIN,
    FOLKS_WALK_DOWN,
    FOLKS_HOP,
    FOLKS_WALK_LEFT,
};

struct FolksData final
{
    fx mHurtTimer;
    fx mWaitTimer;
    fx mInitX;
    fx mInitY;
    Parabola mArc;
    Entity* mShadow;
    Entity* mPillow;
    bool mCollided;
    uint8_t mStep;
};

// Play, plus AnimationChanged: the left anims are the right ones flipped.
void FolksPlay(Entity& self, const char* anim)
{
    if (Ecs_Play(self, anim))
    {
        self.mV->mHFlip = anim[strlen(anim) - 1] == 'l';
    }
}

void FolksLeave(Entity& self)
{
    if (self.mPreset)
    {
        Preset_SetAlive(*self.mPreset, false);
    }
    self.mExists = false;
}

void ArthurUpdate(Entity& self)
{
    FolksData& d = self.Data<FolksData>();
    Ecs_BaseUpdate(self);
    Visual& v = *self.mV;
    Transform& t = *self.mT;
    // (Shadow.Update: PositionOffset = (0, -2) - (16, 16) / 4)
    d.mShadow->mT->mX = t.mX + Fx(4);
    d.mShadow->mT->mY = t.mY + Fx(6);
    switch (d.mStep)
    {
    case FOLKS_TRANSITION:
        if (World_InTransition())
        {
            break;
        }
        Game_ShowDialogue("arthur", "alone", 0);
        d.mStep = FOLKS_TALK;
        [[fallthrough]];
    case FOLKS_TALK:
        if (!Game_DialogueFinished())
        {
            break;
        }
        d.mStep = FOLKS_WOBBLE;
        [[fallthrough]];
    case FOLKS_WOBBLE:
        if (!(d.mCollided && v.mOffX < FxF(0.5f)))
        {
            d.mHurtTimer += kFxTick;
            v.mOffX = -3 * FxSin(FxMul(FxDiv(d.mHurtTimer, FxF(1.3f)), FxF(6.28f)));
            d.mShadow->mV->mOffX = v.mOffX;
            break;
        }
        v.mOffX = d.mShadow->mV->mOffX = 0;
        FolksPlay(self, "fall_1");
        Game_PlaySfx("fall_1");
        d.mStep = FOLKS_FALL;
        [[fallthrough]];
    case FOLKS_FALL:
        if (v.mOffY > 0)
        {
            const fx y = v.mOffY;
            v.mOffY = y - FxMul(kFxTick * 30, kFxOne + 3 * ((Fx(80) - y) / 80));
            const fx h = v.mOffY / 80;
            const int frame = FxCeil(h * 4) - 1;
            if (frame < 0)
            {
                d.mShadow->mV->mVisible = false;
            }
            else
            {
                UpdateShadowFrames(*d.mShadow, h, 3);
            }
            break;
        }
        Ecs_Play(*d.mPillow, "poof");
        Game_PlaySfx("dustpoof");
        FolksPlay(self, "stunned");
        gGame.mPuzzlesSolved++;
        Screen_Shake(FxF(0.01f), FxF(0.2f));
        d.mShadow->mV->mVisible = false;
        d.mStep = FOLKS_WAIT;
        [[fallthrough]];
    case FOLKS_WAIT:
        if (d.mWaitTimer < Fx(1))
        {
            d.mWaitTimer += kFxTick;
            break;
        }
        Game_ShowDialogue("arthur", "alone", 1);
        d.mStep = FOLKS_TALK_AGAIN;
        [[fallthrough]];
    case FOLKS_TALK_AGAIN:
        if (!Game_DialogueFinished())
        {
            break;
        }
        FolksPlay(self, "walk_d");
        d.mStep = FOLKS_WALK_DOWN;
        [[fallthrough]];
    case FOLKS_WALK_DOWN:
        if (!MoveTo(t.mY, d.mInitY + Fx(32), Fx(60)))
        {
            break;
        }
        FolksPlay(self, "walk_l");
        d.mStep = FOLKS_HOP;
        [[fallthrough]];
    case FOLKS_HOP:
        if (!ParabolaTick(d.mArc, self, d.mShadow, 3))
        {
            break;
        }
        d.mStep = FOLKS_WALK_LEFT;
        [[fallthrough]];
    case FOLKS_WALK_LEFT:
        if (!MoveTo(t.mX, d.mInitX - Fx(40), Fx(60)))
        {
            break;
        }
        FolksLeave(self);
        d.mShadow->mExists = false;
        break;
    default:
        break;
    }
}

void ArthurCollided(Entity& self, Entity&)
{
    self.Data<FolksData>().mCollided = true;
}

const EntityClass kArthurDanger = {
    "ArthurDanger", ArthurUpdate, nullptr, ArthurCollided, nullptr, nullptr, nullptr, nullptr,
    0,              GROUP_DUST,   0,
};

void JavieraUpdate(Entity& self)
{
    FolksData& d = self.Data<FolksData>();
    Ecs_BaseUpdate(self);
    Transform& t = *self.mT;
    switch (d.mStep)
    {
    case FOLKS_TRANSITION:
        if (World_InTransition())
        {
            break;
        }
        Game_ShowDialogue("javiera", "alone", 0);
        d.mStep = FOLKS_TALK;
        [[fallthrough]];
    case FOLKS_TALK:
        if (!Game_DialogueFinished())
        {
            break;
        }
        Ecs_SetSheet(self, "javiera", 16, 16);
        FolksPlay(self, "walk_d");
        d.mStep = FOLKS_WAIT_KILLS;
        [[fallthrough]];
    case FOLKS_WAIT_KILLS:
        if (gGame.mEnemiesKilled < 2)
        {
            break;
        }
        d.mStep = FOLKS_WAIT;
        [[fallthrough]];
    case FOLKS_WAIT:
        if (d.mWaitTimer < Fx(1))
        {
            d.mWaitTimer += kFxTick;
            break;
        }
        Game_ShowDialogue("javiera", "alone", 1);
        d.mStep = FOLKS_TALK_AGAIN;
        [[fallthrough]];
    case FOLKS_TALK_AGAIN:
        if (!Game_DialogueFinished())
        {
            break;
        }
        d.mStep = FOLKS_WALK_DOWN;
        [[fallthrough]];
    case FOLKS_WALK_DOWN:
        if (!MoveTo(t.mY, d.mInitY, Fx(60)))
        {
            break;
        }
        FolksPlay(self, "walk_l");
        d.mStep = FOLKS_WALK_LEFT;
        [[fallthrough]];
    case FOLKS_WALK_LEFT:
        if (!MoveTo(t.mX, d.mInitX - Fx(66), Fx(60)))
        {
            break;
        }
        FolksLeave(self);
        gGame.mPuzzlesSolved++;
        break;
    default:
        break;
    }
}

const EntityClass kJavieraDanger = {
    "JavieraDanger", JavieraUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ArthurDead / JavieraDead (Npc/Circus/CircusFolksDead.cs): what's left of them after the boss.
const EntityClass kArthurDead = {
    "ArthurDead", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kJavieraDead = {
    "JavieraDead", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- CircusFolks (Circus/CircusFolks.cs): the circus boss, Javiera riding on Arthur's shoulders. Walks the edge of
// the room, jumps to a corner (shockwaves) or throws Arthur at the player and sends Javiera running round to him.
// The C#'s coroutines as steps; a coroutine that hands over to another (_stateLogic = X(); yield break) only starts
// the next one the frame after.
enum CircusStep : uint8_t
{
    CF_INTRO_WAIT_PLAYER,
    CF_INTRO_WAIT_VOLUME,
    CF_INTRO_WALK_IN,
    CF_WALK_START,
    CF_WALK,
    CF_JUMP_START,
    CF_JUMP,
    CF_THROW_START,
    CF_THROW_SWITCH,
    CF_THROW_SWITCH_FLASH,
    CF_THROW_THROW,
    CF_THROW_WAIT_ARTHUR,
    CF_THROW_WAIT_JAVIERA,
    CF_THROW_MERGE_FLASH,
    CF_DYING_START,
    CF_DYING_TALK,
    CF_DYING_WOBBLE,
    CF_DYING_FALL,
    CF_DYING_FLASH,
    CF_DONE,
};

enum CircusVolume : uint8_t
{
    CFV_NONE,
    CFV_SPAWNED, // its VolumeEvent only updates from the next frame, after the boss
    CFV_RUNNING,
    CFV_REACHED,
};

struct CircusFolksData final
{
    fx mTopLeftX;
    fx mTopLeftY;
    fx mEntranceY;
    fx mTimer; // walkTimer / fallTimer
    Parabola mArc;
    fx mTargetX;
    fx mTargetY;
    Entity* mArthur;
    Entity* mJaviera;
    Entity* mShadow;
    int mHealth;
    uint8_t mStep;
    uint8_t mVolume;
    uint8_t mJavieraFacing;
    bool mBossRush;
};

// CircusFolks.Arthur / CircusFolks.Javiera: the stateLogic coroutines (BeThrown / Run), as steps (0 = null).
struct CircusPartData final
{
    Parabola mArc;
    fx mTargetX;
    fx mTargetY;
    int mDamage;
    uint8_t mStep;
    uint8_t mRunFacing;
    bool mHurts;
    bool mTouchedArthur;
};

enum CircusPartStep : uint8_t
{
    CP_NONE,
    CP_START,
    CP_THROWN_FLY,
    CP_THROWN_LAND,
    CP_RUN,
};

extern const EntityClass kCircusArthur;
extern const EntityClass kShockWave;

// WalkAroundEntity.WalkAboutParameter: round the room's edge, turning at the walls.
void WalkAboutParameter(Entity& self, fx speed, bool playAnim = true)
{
    const fx cx = GridLeft() + Fx(80);
    const fx cy = GridTop() + Fx(80);
    const Transform& t = *self.mT;
    switch (self.mB->mTouching)
    {
    case TOUCH_LEFT:
    case TOUCH_RIGHT:
        self.mFacing = t.mY > cy ? FACE_UP : FACE_DOWN;
        break;
    case TOUCH_UP:
    case TOUCH_DOWN:
        self.mFacing = t.mX > cx ? FACE_LEFT : FACE_RIGHT;
        break;
    default:
        break;
    }
    if (playAnim)
    {
        static const char* const kWalks[4] = { "walk_l", "walk_r", "walk_u", "walk_d" };
        FolksPlay(self, kWalks[self.mFacing & 3]);
    }
    const Facing f = static_cast<Facing>(self.mFacing);
    self.mT->mVelX = FacingX(f) * speed;
    self.mT->mVelY = FacingY(f) * speed;
}

template <typename F>
void ForShockWaves(Entity& boss, F&& f)
{
    for (Entity* e = boss.mNext; e; e = e->mNext)
    {
        if (e->mParent == &boss && e->mClass == &kShockWave)
        {
            f(*e);
        }
    }
}

// _shockWaves.Spawn(b => b.Spawn(pos, direction)): the first one not out.
void SpawnShockWave(Entity& boss, fx x, fx y, Facing direction)
{
    bool done = false;
    ForShockWaves(boss, [&](Entity& s) {
        if (done || s.mExists)
        {
            return;
        }
        done = true;
        s.mExists = true;
        s.mT->mX = x;
        s.mT->mY = y;
        s.mT->mVelX = Fx(70 * FacingX(direction));
        s.mT->mVelY = Fx(70 * FacingY(direction));
        s.mFacing = direction;
        Ecs_Play(s, "move");
        s.mV->mVisible = true;
    });
}

Facing CircusMovementDirection(const CircusFolksData& d, fx x, fx y)
{
    if (x < d.mTopLeftX + Fx(16))
    {
        return FACE_DOWN;
    }
    if (x > d.mTopLeftX + Fx(105))
    {
        return FACE_UP;
    }
    if (y > d.mTopLeftY + Fx(106))
    {
        return FACE_RIGHT;
    }
    return FACE_LEFT;
}

// Vector2.Normalize(target - from) * speed
void VelocityTowards(Entity& e, fx tx, fx ty, fx speed)
{
    fx dx = tx - e.mT->mX;
    fx dy = ty - e.mT->mY;
    FxNormalize(dx, dy);
    e.mT->mVelX = FxMul(dx, speed);
    e.mT->mVelY = FxMul(dy, speed);
}

void CircusFolksStep(Entity& self)
{
    CircusFolksData& d = self.Data<CircusFolksData>();
    Transform& t = *self.mT;
    Visual& v = *self.mV;
    Entity& arthur = *d.mArthur;
    Entity& javiera = *d.mJaviera;
    CircusPartData& ad = arthur.Data<CircusPartData>();
    CircusPartData& jd = javiera.Data<CircusPartData>();
    const Entity& p = *World_Player();
    switch (d.mStep)
    {
    case CF_INTRO_WAIT_PLAYER:
        if (p.mT->mY > d.mEntranceY)
        {
            break;
        }
        d.mVolume = CFV_SPAWNED; // GlobalState.SpawnEntity(new VolumeEvent(0, 0.6f))
        if (!d.mBossRush)
        {
            Game_ShowDialogue("circus_folks", "before_fight");
        }
        d.mStep = CF_INTRO_WAIT_VOLUME;
        [[fallthrough]];
    case CF_INTRO_WAIT_VOLUME:
        if (d.mVolume != CFV_REACHED || (!d.mBossRush && !Game_DialogueFinished()))
        {
            break;
        }
        Game_PlaySong("circus-boss");
        FolksPlay(arthur, "walk_r");
        arthur.mT->mVelX = Fx(20);
        FolksPlay(javiera, "walk_l");
        javiera.mT->mVelX = -Fx(20);
        d.mStep = CF_INTRO_WALK_IN;
        [[fallthrough]];
    case CF_INTRO_WALK_IN:
        if (arthur.mT->mX < d.mTopLeftX + Fx(64))
        {
            break;
        }
        arthur.mT->mVelX = arthur.mT->mVelY = javiera.mT->mVelX = javiera.mT->mVelY = 0;
        arthur.mExists = javiera.mExists = false;
        v.mVisible = true;
        FolksPlay(self, "walk_l");
        t.mX = d.mTopLeftX + Fx(56);
        t.mY = d.mTopLeftY;
        d.mStep = CF_WALK_START;
        break;
    case CF_WALK_START:
        d.mTimer = 0;
        d.mStep = CF_WALK;
        [[fallthrough]];
    case CF_WALK:
        if (d.mTimer <= FxF(3.0f))
        {
            d.mTimer += kFxTick;
            WalkAboutParameter(self, Fx(90));
            break;
        }
        d.mStep = Rng_NextDouble() > 0.6 ? CF_JUMP_START : CF_THROW_START;
        break;
    case CF_JUMP_START:
    {
        d.mTargetX = d.mTopLeftX + Fx(p.mT->mX > d.mTopLeftX + Fx(60) ? 0 : 112);
        d.mTargetY = d.mTopLeftY + Fx(p.mT->mY > d.mTopLeftY + Fx(60) ? 0 : 112);
        const fx dx = d.mTargetX - t.mX;
        const fx dy = d.mTargetY - t.mY;
        const fx distance = FxLength(dx, dy);
        // (target - Position) / distance * jumpVel: Vector2 / float multiplies by 1 / distance
        const fx inv = FxDiv(kFxOne, distance);
        t.mVelX = FxMul(dx, inv) * 100;
        t.mVelY = FxMul(dy, inv) * 100;
        ParabolaInit(d.mArc, self, Fx(48), distance / 100);
        d.mShadow->mV->mVisible = true;
        Game_PlaySfx("player_jump_up");
        d.mStep = CF_JUMP;
        [[fallthrough]];
    }
    case CF_JUMP:
        if (!ParabolaTick(d.mArc, self, d.mShadow, 3))
        {
            break;
        }
        d.mShadow->mV->mVisible = false;
        Game_PlaySfx("player_jump_down");
        Game_PlaySfx("wb_tap_ground");
        t.mVelX = t.mVelY = 0;
        t.mX = d.mTargetX;
        t.mY = d.mTargetY;
        if (d.mTargetX == d.mTopLeftX && d.mTargetY == d.mTopLeftY)
        {
            self.mFacing = FACE_DOWN;
        }
        else if (d.mTargetX > d.mTopLeftX && d.mTargetY == d.mTopLeftY)
        {
            self.mFacing = FACE_LEFT;
        }
        else if (d.mTargetX > d.mTopLeftX && d.mTargetY > d.mTopLeftY)
        {
            self.mFacing = FACE_UP;
        }
        else
        {
            self.mFacing = FACE_RIGHT;
        }
        if (Ecs_CountChildren(self, kShockWave) <= 5)
        {
            SpawnShockWave(self, t.mX, t.mY, FACE_LEFT);
            SpawnShockWave(self, t.mX, t.mY, FACE_RIGHT);
        }
        d.mStep = CF_WALK_START;
        break;
    case CF_THROW_START:
        t.mVelX = t.mVelY = 0;
        FolksPlay(self, "switch");
        d.mStep = CF_THROW_SWITCH;
        [[fallthrough]];
    case CF_THROW_SWITCH:
    {
        if (!Ecs_AnimFinished(self))
        {
            break;
        }
        Screen_Flash(FxF(0.2f), FLASH_WHITE);
        d.mStep = CF_THROW_SWITCH_FLASH;
        [[fallthrough]];
    }
    case CF_THROW_SWITCH_FLASH:
    {
        if (!Screen_FlashFull())
        {
            break;
        }
        d.mJavieraFacing = self.mFacing;
        Ecs_FaceTowards(self, p.mT->mX, p.mT->mY);
        static const char* const kThrows[4] = { "throw_l", "throw_r", "throw_u", "throw_d" };
        FolksPlay(self, kThrows[self.mFacing & 3]);
        d.mStep = CF_THROW_THROW;
        [[fallthrough]];
    }
    case CF_THROW_THROW:
        if (!Ecs_AnimFinished(self))
        {
            break;
        }
        Game_PlaySfx("slasher_atk");
        Ecs_Flicker(self, 0);
        v.mVisible = false;
        arthur.mExists = true;
        Ecs_Flicker(arthur, 0);
        // _arthur.stateLogic = _arthur.BeThrown(Position - (0, 16), _player): it runs in Arthur's update, this frame
        ad.mStep = CP_START;
        ad.mTargetX = t.mX;
        ad.mTargetY = t.mY - Fx(16);
        javiera.mExists = true;
        Ecs_Flicker(javiera, 0);
        javiera.mT->mX = t.mX;
        javiera.mT->mY = t.mY;
        d.mStep = CF_THROW_WAIT_ARTHUR;
        [[fallthrough]];
    case CF_THROW_WAIT_ARTHUR:
    {
        if (ad.mStep != CP_NONE)
        {
            break;
        }
        const Facing f = CircusMovementDirection(d, arthur.mT->mX, arthur.mT->mY);
        if (Ecs_CountChildren(self, kShockWave) < 8 - 1)
        {
            SpawnShockWave(self, arthur.mT->mX, arthur.mT->mY, f);
            SpawnShockWave(self, arthur.mT->mX, arthur.mT->mY, FlipFacing(f));
        }
        jd.mStep = CP_START;
        jd.mRunFacing = d.mJavieraFacing;
        d.mStep = CF_THROW_WAIT_JAVIERA;
        break; // (javiera's Run starts in her update, later this frame: she isn't done yet)
    }
    case CF_THROW_WAIT_JAVIERA:
    {
        if (jd.mStep != CP_NONE)
        {
            break;
        }
        FolksPlay(arthur, "walk_d");
        Screen_Flash(FxF(0.2f), FLASH_WHITE);
        d.mStep = CF_THROW_MERGE_FLASH;
        [[fallthrough]];
    }
    case CF_THROW_MERGE_FLASH:
    {
        if (!Screen_FlashFull())
        {
            break;
        }
        t.mX = javiera.mT->mX;
        t.mY = javiera.mT->mY;
        d.mHealth -= ad.mDamage + jd.mDamage;
        if (d.mHealth < 0)
        {
            d.mHealth = 0;
        }
        v.mVisible = true;
        arthur.mExists = javiera.mExists = false;
        Ecs_Flicker(arthur, 0);
        Ecs_Flicker(javiera, 0);
        self.mFacing = javiera.mFacing;
        static const char* const kWalks[4] = { "walk_l", "walk_r", "walk_u", "walk_d" };
        FolksPlay(self, kWalks[self.mFacing & 3]);
        d.mStep = CF_WALK_START;
        break;
    }
    case CF_DYING_START:
        Game_PlaySong("");
        t.mVelX = t.mVelY = 0;
        FolksPlay(self, "dying");
        Game_PlaySfx("sun_guy_death_short");
        Screen_Shake(FxF(0.04f), kFxOne);
        ForShockWaves(self, [](Entity& s) {
            s.mT->mVelX = s.mT->mVelY = 0;
            Ecs_Play(s, "evaporate");
        });
        Ecs_Flicker(arthur, 0);
        Ecs_Flicker(javiera, 0);
        ad.mHurts = false;
        jd.mHurts = false;
        javiera.mV->mLayer = LAYER_FG_SPRITES;
        if (!d.mBossRush)
        {
            Game_ShowDialogue("circus_folks", "after_fight");
        }
        d.mStep = CF_DYING_TALK;
        [[fallthrough]];
    case CF_DYING_TALK:
        if (!d.mBossRush && !Game_DialogueFinished())
        {
            break;
        }
        Ecs_Flicker(self, 0);
        v.mVisible = false;
        arthur.mExists = javiera.mExists = true;
        arthur.mT->mX = t.mX;
        arthur.mT->mY = t.mY;
        javiera.mT->mX = t.mX;
        javiera.mT->mY = t.mY - Fx(16);
        ParabolaInit(ad.mArc, arthur, Fx(24), FxF(1.0f));
        ParabolaInit(jd.mArc, javiera, Fx(18), FxF(0.8f));
        FolksPlay(arthur, "pre-fall");
        FolksPlay(javiera, "pre-fall");
        d.mTimer = 0;
        d.mStep = CF_DYING_WOBBLE;
        [[fallthrough]];
    case CF_DYING_WOBBLE:
        if (d.mTimer <= Fx(2))
        {
            d.mTimer += kFxTick;
            const fx n = FxSin(FxMul(d.mTimer, FxF(6.28f)));
            arthur.mV->mOffX = -3 * n;
            javiera.mV->mOffX = 3 * n;
            break;
        }
        Game_PlaySfx("floor_crack");
        VelocityTowards(arthur, d.mTopLeftX + Fx(64), d.mTopLeftY + Fx(40), Fx(40));
        VelocityTowards(javiera, d.mTopLeftX + Fx(36), d.mTopLeftY + Fx(50), Fx(70));
        d.mStep = CF_DYING_FALL;
        [[fallthrough]];
    case CF_DYING_FALL:
        if (arthur.mV->mOpacity != 0 || javiera.mV->mOpacity != 0)
        {
            Entity* const parts[2] = { &arthur, &javiera };
            for (Entity* part : parts)
            {
                if (ParabolaTick(part->Data<CircusPartData>().mArc, *part, nullptr, 0))
                {
                    part->mT->mVelX = part->mT->mVelY = 0;
                    MoveTo(part->mV->mOpacity, 0, FxF(0.3f));
                    if (!Ecs_AnimIs(*part, "fall"))
                    {
                        FolksPlay(*part, "fall");
                    }
                }
            }
            break;
        }
        Game_PlaySfx("wb_hit_ground");
        Screen_Flash(kFxOne, FLASH_RED);
        d.mStep = CF_DYING_FLASH;
        [[fallthrough]];
    case CF_DYING_FLASH:
        if (!Screen_FlashFull())
        {
            break;
        }
        if (self.mPreset)
        {
            Preset_SetAlive(*self.mPreset, false);
        }
        self.mExists = arthur.mExists = javiera.mExists = false;
        d.mShadow->mExists = false;
        if (!d.mBossRush)
        {
            Game_SetBossDefeated(gGame.mMapName);
            Game_PlaySong("circus");
        }
        else
        {
            Game_PlaySong("bedroom");
        }
        d.mStep = CF_DONE;
        break;
    default:
        break;
    }
}

void CircusFolksUpdate(Entity& self)
{
    CircusFolksData& d = self.Data<CircusFolksData>();
    Ecs_BaseUpdate(self);
    // (Shadow.Update: PositionOffset = (0, 0) - (16, 32) / 4)
    d.mShadow->mT->mX = self.mT->mX + Fx(4);
    d.mShadow->mT->mY = self.mT->mY + Fx(8);
    if (d.mHealth == 0)
    {
        d.mHealth = -1;
        d.mStep = CF_DYING_START;
    }
    else
    {
        CircusFolksStep(self);
    }
    // its VolumeEvent's Update, after the boss's
    if (d.mVolume == CFV_RUNNING)
    {
        fx volume = Game_SongVolume();
        if (MoveTo(volume, 0, FxF(0.6f)))
        {
            d.mVolume = CFV_REACHED;
        }
        Game_SetSongVolume(volume);
    }
    else if (d.mVolume == CFV_SPAWNED)
    {
        d.mVolume = CFV_RUNNING;
    }
}

void CircusFolksCollided(Entity& self, Entity& other)
{
    CircusFolksData& d = self.Data<CircusFolksData>();
    if (!self.mV->mVisible)
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (!self.mV->mFlickering && IsBroom(other))
    {
        d.mHealth--;
        Ecs_Flicker(self, FxSec(1.0f));
        Game_PlaySfx("broom_hit");
    }
}

void CircusArthurUpdate(Entity& self)
{
    CircusPartData& d = self.Data<CircusPartData>();
    Ecs_BaseUpdate(self);
    Transform& t = *self.mT;
    switch (d.mStep)
    {
    case CP_START:
    {
        // BeThrown(position, player)
        t.mX = d.mTargetX;
        t.mY = d.mTargetY;
        self.mV->mVisible = true;
        FolksPlay(self, "roll");
        d.mDamage = 0;
        const Entity& p = *World_Player();
        const fx fromX = t.mX;
        const fx fromY = t.mY;
        d.mTargetX = p.mT->mX;
        d.mTargetY = p.mT->mY;
        fx dx = d.mTargetX - fromX;
        fx dy = d.mTargetY - fromY;
        FxNormalize(dx, dy);
        t.mVelX = dx * 120;
        t.mVelY = dy * 120;
        self.mB->mImmovable = true;
        d.mStep = CP_THROWN_FLY;
        [[fallthrough]];
    }
    case CP_THROWN_FLY:
    {
        const fx dx = t.mX - d.mTargetX;
        const fx dy = t.mY - d.mTargetY;
        if (FxLengthSq(dx, dy) > Fx(16 * 16))
        {
            break;
        }
        self.mB->mImmovable = false;
        d.mStep = CP_THROWN_LAND;
        [[fallthrough]];
    }
    case CP_THROWN_LAND:
        if (self.mB->mTouching == TOUCH_NONE)
        {
            break;
        }
        t.mVelX = t.mVelY = 0;
        FolksPlay(self, "stunned");
        Game_PlaySfx("hit_ground_1");
        Screen_Shake(FxF(0.03f), FxF(0.1f));
        d.mStep = CP_NONE;
        break;
    default:
        break;
    }
}

void CircusArthurCollided(Entity& self, Entity& other)
{
    CircusPartData& d = self.Data<CircusPartData>();
    if (!self.mV->mVisible || !d.mHurts || d.mStep != CP_NONE || self.mV->mFlickering)
    {
        return;
    }
    if (IsBroom(other))
    {
        d.mDamage++;
        Ecs_Flicker(self, FxSec(3.0f));
    }
    else if (IsPlayer(other) && Player_State() == PS_GROUND)
    {
        Player_ReceiveDamage(1);
    }
}

void CircusJavieraUpdate(Entity& self)
{
    CircusPartData& d = self.Data<CircusPartData>();
    Ecs_BaseUpdate(self);
    switch (d.mStep)
    {
    case CP_START:
    {
        // Run(direction)
        self.mFacing = d.mRunFacing;
        static const char* const kWalks[4] = { "walk_l", "walk_r", "walk_u", "walk_d" };
        FolksPlay(self, kWalks[self.mFacing & 3]);
        d.mDamage = 0;
        d.mTouchedArthur = false;
        d.mStep = CP_RUN;
        [[fallthrough]];
    }
    case CP_RUN:
        if (!d.mTouchedArthur)
        {
            WalkAboutParameter(self, 90 * FxF(1.3f));
            break;
        }
        self.mT->mVelX = self.mT->mVelY = 0;
        d.mStep = CP_NONE;
        break;
    default:
        break;
    }
}

void CircusJavieraCollided(Entity& self, Entity& other)
{
    CircusPartData& d = self.Data<CircusPartData>();
    if (!self.mV->mVisible || !d.mHurts)
    {
        return;
    }
    if (d.mStep != CP_NONE && other.mClass == &kCircusArthur)
    {
        d.mTouchedArthur = true;
    }
    else if (!self.mV->mFlickering)
    {
        if (IsBroom(other))
        {
            d.mDamage++;
            Ecs_Flicker(self, FxSec(3.0f));
        }
        else if (IsPlayer(other) && Player_State() == PS_GROUND)
        {
            Player_ReceiveDamage(1);
        }
    }
}

void ShockWaveUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimIs(self, "evaporate"))
    {
        if (Ecs_AnimFinished(self))
        {
            self.mExists = false;
        }
    }
    else
    {
        WalkAboutParameter(self, Fx(70), false);
    }
}

void ShockWaveCollided(Entity& self, Entity& other)
{
    if (Ecs_AnimIs(self, "evaporate"))
    {
        return;
    }
    bool disappear = false;
    if (IsPlayer(other) && Player_State() == PS_GROUND)
    {
        Player_ReceiveDamage(1);
        disappear = true;
    }
    else if (IsBroom(other))
    {
        Game_PlaySfx("broom_hit");
        disappear = true;
    }
    if (disappear)
    {
        self.mT->mVelX = self.mT->mVelY = 0;
        Ecs_Play(self, "evaporate");
    }
}

const EntityClass kCircusFolks = {
    "CircusFolks", CircusFolksUpdate, nullptr, CircusFolksCollided, nullptr, ContortMapEvent, nullptr, nullptr,
    0,             GROUP_PLAYER | GROUP_BROOM, ECF_MAP_COLLISION | ECF_ENEMY,
};

const EntityClass kCircusArthur = {
    "Arthur",          CircusArthurUpdate,         nullptr, CircusArthurCollided, nullptr, ContortMapEvent, nullptr,
    nullptr,           GROUP_CIRCUS_ARTHUR,        GROUP_PLAYER | GROUP_BROOM, ECF_MAP_COLLISION,
};

const EntityClass kCircusJaviera = {
    "Javiera", CircusJavieraUpdate, nullptr, CircusJavieraCollided, nullptr, ContortMapEvent, nullptr, nullptr,
    0,         GROUP_PLAYER | GROUP_BROOM | GROUP_CIRCUS_ARTHUR, ECF_MAP_COLLISION,
};

const EntityClass kShockWave = {
    "ShockWave", ShockWaveUpdate, nullptr, ShockWaveCollided, nullptr, ContortMapEvent, nullptr, nullptr,
    0,           GROUP_PLAYER | GROUP_BROOM, ECF_MAP_COLLISION,
};

// ---- SageBoss (Etc/SageBoss.cs): the sage, fought in the terminal. Teleports in, then four stages: lasers down the
// middle, orbiting dust while it bounces round, a wider orbit, and shots from the top. The coroutines as steps; a stage
// that hands over to the next (_stateLogic = X(); yield break) starts it the frame after.
enum SageBossStep : uint8_t
{
    SGB_INTRO,
    SGB_INTRO_WAIT_VOLUME,
    SGB_INTRO_WAIT_PLAYER,
    SGB_INTRO_TALK,
    SGB_INTRO_FADE_HALF,
    SGB_INTRO_FADE_FULL,
    SGB_INTRO_FLASH1,
    SGB_INTRO_FLASH2,
    SGB_INTRO_FLASH3,
    SGB_INTRO_FADE_LAST,
    SGB_STAGE1_START,
    SGB_STAGE1,
    SGB_STAGE2_START,
    SGB_STAGE2_WAIT_FLICKER,
    SGB_STAGE2_WAIT_EDGES,
    SGB_STAGE2,
    SGB_STAGE2_TURNED,
    SGB_STAGE3_START,
    SGB_STAGE3_WAIT,
    SGB_STAGE3_BULLETS,
    SGB_STAGE3,
    SGB_STAGE4_START,
    SGB_STAGE4,
    SGB_DYING,
};

constexpr int kSageBullets = 6;

struct SageBossData final
{
    fx mTopLeftX;
    fx mTopLeftY;
    fx mTimer; // bClock / timer
    fx mFlickerLength;
    Entity* mLong[kSageBullets];  // _lBullets
    Entity* mShort[kSageBullets]; // _sBullets
    int mHealth;
    int mSpeedIndex;  // Stage2's i, kept over its yield after turning
    int mBulletsMade; // Stage3
    uint8_t mStep;
    uint8_t mDyingStep;
    uint8_t mVolume; // CircusVolume
    bool mCanHurt;
    bool mMovedRight;
    bool mBossRush;
};

// SageBoss.HurtingEntity (LongAttack / ShortAttack).
struct SageAttackData final
{
    bool mHurting;
    bool mMapCollision; // ShortAttack.MapColission
};

extern const EntityClass kSageLongAttack;

// _pool.Spawn: the first one not out.
Entity* SpawnFromPool(Entity* const (&pool)[kSageBullets])
{
    for (Entity* e : pool)
    {
        if (!e->mExists)
        {
            e->mExists = true;
            return e;
        }
    }
    return nullptr;
}

// HurtingEntity.Poof
void SageAttackPoof(Entity& e)
{
    if (!Ecs_AnimIs(e, "poof"))
    {
        Ecs_Play(e, "poof");
    }
    e.mT->mVelX = e.mT->mVelY = 0;
    e.Data<SageAttackData>().mHurting = false;
}

// LongAttack.SpawnTopBottom
void SageSpawnEdge(SageBossData& d, fx x, fx y)
{
    Entity* l = SpawnFromPool(d.mLong);
    if (!l)
    {
        return;
    }
    Ecs_Play(*l, "spin");
    l->mT->mX = x + Fx((64 - l->mB->mW) / 2);
    l->mT->mY = y + Fx((16 - l->mB->mH) / 2);
    l->mV->mVisible = true;
    l->Data<SageAttackData>().mHurting = true;
    Ecs_Flicker(*l, FxSec(1.0f));
}

// ShortAttack.Spawn(): an orbiting one.
void SageSpawnOrbit(SageBossData& d)
{
    Entity* s = SpawnFromPool(d.mShort);
    if (!s)
    {
        return;
    }
    Ecs_Play(*s, "spin");
    s->mV->mVisible = true;
    SageAttackData& sd = s->Data<SageAttackData>();
    sd.mHurting = true;
    sd.mMapCollision = false;
}

// SetBulletsPosition: the first four short ones round the sage.
void SageSetBulletsPosition(Entity& self, SageBossData& d, fx offset)
{
    const Transform& t = *self.mT;
    for (int i = 0; i < 4; i++)
    {
        Entity& s = *d.mShort[i];
        if (!s.mExists)
        {
            continue;
        }
        const fx middleX = t.mX + Fx((self.mB->mW - s.mB->mW) / 2);
        switch (i)
        {
        case 0:
            s.mT->mX = middleX;
            s.mT->mY = t.mY - Fx(10) - fx(offset);
            break;
        case 1:
            s.mT->mX = middleX;
            s.mT->mY = t.mY + Fx(12) + fx(offset);
            break;
        case 2:
            s.mT->mX = t.mX - Fx(12) - fx(offset);
            s.mT->mY = t.mY;
            break;
        default:
            s.mT->mX = t.mX + Fx(14) + fx(offset);
            s.mT->mY = t.mY;
            break;
        }
    }
}

// SageBoss.Movement: bounce off the walls.
void SageMovement(Entity& self, fx xSpeed, fx ySpeed)
{
    Transform& t = *self.mT;
    const uint8_t touching = self.mB->mTouching;
    if (touching & TOUCH_RIGHT)
    {
        t.mVelX = fx(-xSpeed);
    }
    else if (touching & TOUCH_LEFT)
    {
        t.mVelX = fx(xSpeed);
    }
    if (touching & TOUCH_DOWN)
    {
        t.mVelY = fx(-ySpeed);
        Ecs_Play(self, "dash_u");
    }
    else if (touching & TOUCH_UP)
    {
        t.mVelY = fx(ySpeed);
        Ecs_Play(self, "dash_d");
    }
}

int SageSpeedIndex(int maxHealth, int health)
{
    const int i = maxHealth - health;
    return i < 0 ? 0 : i;
}

// DyingState: talks, then walks off the top of the room.
bool SageDyingCutscene(Entity& self)
{
    SageBossData& d = self.Data<SageBossData>();
    Transform& t = *self.mT;
    switch (d.mDyingStep)
    {
    case 0:
        for (int i = 0; i < kSageBullets; i++)
        {
            d.mLong[i]->mExists = false;
            d.mShort[i]->mExists = false;
        }
        self.mB->mImmovable = true;
        Game_PlaySong("");
        Ecs_Play(self, "idle_d");
        d.mDyingStep = 1;
        if (!d.mBossRush)
        {
            Game_ShowCutsceneDialogue("sage", "after_fight");
            return false;
        }
        [[fallthrough]];
    case 1:
        Ecs_Play(self, "walk_u");
        t.mVelY = -Fx(20);
        d.mDyingStep = 2;
        [[fallthrough]];
    default:
        if (t.mY + Fx(16) >= d.mTopLeftY)
        {
            return false;
        }
        if (!d.mBossRush)
        {
            Game_PlaySong("terminal_alt");
            Game_SetBossDefeated("TERMINAL");
            Event_Set("SageDied", 1);
        }
        else
        {
            Game_PlaySong("bedroom");
        }
        if (self.mPreset)
        {
            Preset_SetAlive(*self.mPreset, false);
        }
        self.mExists = false;
        return true;
    }
}

void SageBossStep(Entity& self)
{
    SageBossData& d = self.Data<SageBossData>();
    Transform& t = *self.mT;
    Visual& v = *self.mV;
    const Entity& p = *World_Player();
    switch (d.mStep)
    {
    case SGB_INTRO:
        d.mCanHurt = false;
        self.mFacing = FACE_DOWN;
        Ecs_Play(self, "idle");
        d.mVolume = CFV_SPAWNED; // GlobalState.SpawnEntity(new VolumeEvent(0, 0.6f))
        d.mStep = SGB_INTRO_WAIT_VOLUME;
        [[fallthrough]];
    case SGB_INTRO_WAIT_VOLUME:
        if (d.mVolume != CFV_REACHED)
        {
            break;
        }
        d.mStep = SGB_INTRO_WAIT_PLAYER;
        [[fallthrough]];
    case SGB_INTRO_WAIT_PLAYER:
    {
        const fx dx = t.mX - p.mT->mX;
        const fx dy = t.mY - p.mT->mY;
        if (FxLength(dx, dy) >= Fx(32) || Player_State() != PS_GROUND)
        {
            break;
        }
        Ecs_Play(self, "idle_d");
        Game_PlaySfx("teleguy_up");
        if (!d.mBossRush)
        {
            Game_ShowDialogue("sage", "before_fight");
        }
        d.mStep = SGB_INTRO_TALK;
        [[fallthrough]];
    }
    case SGB_INTRO_TALK:
        if (!d.mBossRush && !Game_DialogueFinished())
        {
            break;
        }
        v.mVisible = false;
        v.mOpacity = 0;
        t.mX = d.mTopLeftX + Fx(80 - self.mB->mW / 2);
        t.mY = d.mTopLeftY + Fx(24);
        d.mStep = SGB_INTRO_FADE_HALF;
        [[fallthrough]];
    case SGB_INTRO_FADE_HALF:
        if (!MoveTo(v.mOpacity, FxF(0.4f), FxF(0.48f)))
        {
            break;
        }
        v.mVisible = true;
        Game_PlaySfx("teleguy_down");
        d.mStep = SGB_INTRO_FADE_FULL;
        [[fallthrough]];
    case SGB_INTRO_FADE_FULL:
        if (!MoveTo(v.mOpacity, kFxOne, FxF(0.48f)))
        {
            break;
        }
        Game_PlaySong("sagefight");
        Screen_Flash(FxF(2.18f), FLASH_BLACK);
        Screen_Shake(FxF(0.02f), FxF(1.5f));
        d.mStep = SGB_INTRO_FLASH1;
        [[fallthrough]];
    case SGB_INTRO_FLASH1:
        if (Screen_FlashActive())
        {
            break;
        }
        Screen_Flash(FxF(2.15f), FLASH_BLACK);
        Screen_Shake(FxF(0.025f), FxF(1.5f));
        d.mStep = SGB_INTRO_FLASH2;
        [[fallthrough]];
    case SGB_INTRO_FLASH2:
        if (Screen_FlashActive())
        {
            t.mX = d.mTopLeftX + Fx(Rng_Range(40, 100));
            t.mY = d.mTopLeftY + Fx(Rng_Range(30, 70));
            break;
        }
        t.mX = p.mT->mX - Fx(2);
        t.mY = p.mT->mY - Fx(16);
        Screen_Flash(FxF(2.15f), FLASH_BLACK);
        Screen_Shake(FxF(0.03f), FxF(1.5f));
        d.mStep = SGB_INTRO_FLASH3;
        [[fallthrough]];
    case SGB_INTRO_FLASH3:
        if (Screen_FlashActive())
        {
            break;
        }
        Screen_Flash(Fx(6), FLASH_BLACK);
        Screen_Shake(FxF(0.035f), FxF(1.7f));
        t.mX = d.mTopLeftX + Fx(80 - self.mB->mW / 2);
        t.mY = d.mTopLeftY + Fx(25);
        v.mOpacity = 0;
        d.mStep = SGB_INTRO_FADE_LAST;
        [[fallthrough]];
    case SGB_INTRO_FADE_LAST:
        if (!MoveTo(v.mOpacity, kFxOne, FxF(0.3f)))
        {
            break;
        }
        d.mStep = SGB_STAGE1_START;
        break;
    case SGB_STAGE1_START:
        d.mHealth = 3;
        d.mCanHurt = true;
        d.mFlickerLength = Fx(1);
        d.mTimer = 0;
        d.mStep = SGB_STAGE1;
        [[fallthrough]];
    case SGB_STAGE1:
    {
        if (d.mHealth > 0)
        {
            static const fx kDelay[3] = { FxF(1.6f), FxF(1.4f), FxF(1.2f) };
            d.mTimer += kFxTick;
            if (d.mTimer > kDelay[SageSpeedIndex(3, d.mHealth)])
            {
                d.mTimer = 0;
                Entity* l = SpawnFromPool(d.mLong);
                if (l)
                {
                    // LongAttack.Spawn(pos, velocity)
                    Ecs_Play(*l, "spin");
                    l->mT->mX = d.mTopLeftX + Fx(48 + (64 - l->mB->mW) / 2);
                    l->mT->mY = d.mTopLeftY + Fx(20);
                    l->mT->mVelX = 0;
                    l->mT->mVelY = Fx(50);
                    l->mV->mVisible = true;
                    l->Data<SageAttackData>().mHurting = true;
                }
            }
            break;
        }
        for (Entity* l : d.mLong)
        {
            SageAttackPoof(*l);
        }
        d.mCanHurt = false;
        d.mStep = SGB_STAGE2_START;
        break;
    }
    case SGB_STAGE2_START:
        d.mHealth = 3;
        d.mCanHurt = false;
        d.mFlickerLength = Fx(1);
        self.mB->mImmovable = false;
        d.mMovedRight = false;
        d.mStep = SGB_STAGE2_WAIT_FLICKER;
        [[fallthrough]];
    case SGB_STAGE2_WAIT_FLICKER:
        if (v.mFlickering)
        {
            break;
        }
        SageSpawnEdge(d, d.mTopLeftX + Fx(48), d.mTopLeftY + Fx(16));
        SageSpawnEdge(d, d.mTopLeftX + Fx(48), d.mTopLeftY + Fx(8 * 16));
        d.mStep = SGB_STAGE2_WAIT_EDGES;
        [[fallthrough]];
    case SGB_STAGE2_WAIT_EDGES:
        if (d.mLong[0]->mV->mFlickering)
        {
            break;
        }
        t.mVelX = Fx(40);
        d.mCanHurt = true;
        SageSpawnOrbit(d);
        SageSpawnOrbit(d);
        d.mStep = SGB_STAGE2;
        [[fallthrough]];
    case SGB_STAGE2:
    {
        if (d.mHealth > 0)
        {
            static const fx kSpeed[3] = { Fx(50), Fx(70), Fx(90) };
            d.mSpeedIndex = SageSpeedIndex(3, d.mHealth);
            SageSetBulletsPosition(self, d, 0);
            if (!d.mMovedRight && (self.mB->mTouching & TOUCH_RIGHT))
            {
                d.mMovedRight = true;
                t.mVelY = fx(kSpeed[d.mSpeedIndex]);
                d.mStep = SGB_STAGE2_TURNED;
                break;
            }
            SageMovement(self, kSpeed[d.mSpeedIndex], kSpeed[d.mSpeedIndex]);
            break;
        }
        d.mCanHurt = false;
        t.mVelX = t.mVelY = 0;
        d.mStep = SGB_STAGE3_START;
        break;
    }
    case SGB_STAGE2_TURNED:
    {
        static const fx kSpeed[3] = { Fx(50), Fx(70), Fx(90) };
        SageMovement(self, kSpeed[d.mSpeedIndex], kSpeed[d.mSpeedIndex]);
        d.mStep = SGB_STAGE2;
        break;
    }
    case SGB_STAGE3_START:
        d.mHealth = 4;
        d.mCanHurt = false;
        d.mFlickerLength = Fx(1);
        self.mB->mImmovable = false;
        d.mTimer = 0;
        t.mX = d.mTopLeftX + Fx(80 - self.mB->mW / 2);
        t.mY = d.mTopLeftY + Fx(25);
        d.mStep = SGB_STAGE3_WAIT;
        [[fallthrough]];
    case SGB_STAGE3_WAIT:
        if (d.mTimer <= Fx(1))
        {
            d.mTimer += kFxTick;
            break;
        }
        d.mTimer = 0;
        for (Entity* s : d.mShort)
        {
            s->mExists = false;
        }
        d.mBulletsMade = 0;
        d.mStep = SGB_STAGE3_BULLETS;
        [[fallthrough]];
    case SGB_STAGE3_BULLETS:
        if (d.mBulletsMade < 4)
        {
            d.mTimer += kFxTick;
            if (d.mTimer > FxF(0.8f))
            {
                SageSpawnOrbit(d);
                SageSetBulletsPosition(self, d, 0);
                d.mTimer = 0;
                d.mBulletsMade++;
            }
            break;
        }
        t.mVelX = Fx(20);
        t.mVelY = Fx(80);
        d.mCanHurt = true;
        d.mStep = SGB_STAGE3;
        [[fallthrough]];
    case SGB_STAGE3:
    {
        if (d.mHealth > 0)
        {
            static const fx kSpeed[4] = { Fx(80), Fx(90), Fx(100), Fx(115) };
            const int i = SageSpeedIndex(4, d.mHealth);
            d.mTimer += kFxTick;
            if (d.mTimer > Fx(2))
            {
                d.mTimer = 0;
            }
            const fx r = d.mTimer < Fx(1) ? d.mTimer : Fx(2) - d.mTimer;
            SageSetBulletsPosition(self, d, r * 34);
            SageMovement(self, Fx(20), kSpeed[i]);
            break;
        }
        d.mCanHurt = false;
        t.mVelX = t.mVelY = 0;
        d.mStep = SGB_STAGE4_START;
        break;
    }
    case SGB_STAGE4_START:
        d.mHealth = 3;
        d.mCanHurt = false;
        d.mFlickerLength = FxF(2.5f);
        self.mB->mImmovable = false;
        d.mTimer = 0;
        t.mX = d.mTopLeftX + Fx(80 - self.mB->mW / 2);
        t.mY = d.mTopLeftY + Fx(38);
        Game_PlaySfx("teleguy_up");
        for (int i = 0; i < kSageBullets; i++)
        {
            d.mLong[i]->mExists = false;
            d.mShort[i]->mExists = false;
        }
        d.mCanHurt = true;
        d.mStep = SGB_STAGE4;
        [[fallthrough]];
    case SGB_STAGE4:
        if (d.mHealth > 0)
        {
            static const fx kSpeed[3] = { Fx(50), Fx(60), Fx(70) };
            const int i = SageSpeedIndex(3, d.mHealth);
            d.mTimer += kFxTick;
            if (d.mTimer > FxF(0.55f))
            {
                d.mTimer = 0;
                const fx x = d.mTopLeftX + Fx(4 + 48 + 16 * Rng_Range(0, 4));
                Entity* s = SpawnFromPool(d.mShort);
                if (s)
                {
                    // ShortAttack.Spawn(pos, yVel)
                    Ecs_Play(*s, "shoot");
                    s->mV->mVisible = true;
                    s->mT->mX = x;
                    s->mT->mY = d.mTopLeftY + Fx(16);
                    s->mT->mVelY = fx(kSpeed[i]);
                    SageAttackData& sd = s->Data<SageAttackData>();
                    sd.mHurting = true;
                    sd.mMapCollision = true;
                }
            }
            break;
        }
        d.mCanHurt = false;
        d.mDyingStep = 0;
        Game_StartCutscene(self, SageDyingCutscene);
        d.mStep = SGB_DYING;
        break;
    default:
        break;
    }
}

void SageBossUpdate(Entity& self)
{
    SageBossData& d = self.Data<SageBossData>();
    Ecs_BaseUpdate(self);
    SageBossStep(self);
    // its VolumeEvent's Update, after the boss's
    if (d.mVolume == CFV_RUNNING)
    {
        fx volume = Game_SongVolume();
        if (MoveTo(volume, 0, FxF(0.6f)))
        {
            d.mVolume = CFV_REACHED;
        }
        Game_SetSongVolume(volume);
    }
    else if (d.mVolume == CFV_SPAWNED)
    {
        d.mVolume = CFV_RUNNING;
    }
}

void SageBossCollided(Entity& self, Entity& other)
{
    SageBossData& d = self.Data<SageBossData>();
    if (!d.mCanHurt)
    {
        return;
    }
    if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
    else if (!self.mV->mFlickering && IsBroom(other))
    {
        Ecs_Flicker(self, d.mFlickerLength);
        d.mHealth--;
        Game_PlaySfx("broom_hit");
        // (flash.Flash(0.4f, Color.Black): the sprite's own flash isn't drawn)
    }
}

void SageAttackUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimIs(self, "poof") && Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
    const bool mapStops = self.mClass == &kSageLongAttack || self.Data<SageAttackData>().mMapCollision;
    if (mapStops && self.mB->mTouching != TOUCH_NONE)
    {
        SageAttackPoof(self);
    }
}

void SageAttackCollided(Entity& self, Entity& other)
{
    if (!self.Data<SageAttackData>().mHurting)
    {
        return;
    }
    if (IsPlayer(other) && Player_State() == PS_GROUND)
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kSageBoss = {
    "SageBoss", SageBossUpdate, nullptr, SageBossCollided, nullptr, ContortMapEvent, nullptr, nullptr,
    0,          GROUP_PLAYER | GROUP_BROOM, ECF_MAP_COLLISION | ECF_ENEMY,
};

const EntityClass kSageLongAttack = {
    "LongAttack", SageAttackUpdate, nullptr, SageAttackCollided, nullptr, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER | GROUP_BROOM, ECF_KEEP_ON_SCREEN,
};

const EntityClass kSageShortAttack = {
    "ShortAttack", SageAttackUpdate, nullptr, SageAttackCollided, nullptr, nullptr, nullptr, nullptr,
    0,             GROUP_PLAYER | GROUP_BROOM, ECF_KEEP_ON_SCREEN,
};

// ---- The Briar fight (Enemy/Go/BriarBossMain.cs, BriarBossFight.cs, BriarBossBody.cs, BigThorn.cs, BlueThorn.cs,
// HappyThorn.cs). BriarBossMain waits at the top of the room behind its ThornGate; past the gate the water comes in,
// the map shifts two screens over and BriarBossFight (with its body, core and the two arms) is spawned. Pools are
// their owner's children, found in list order (EntityPool.Spawn takes the first one not out).
Entity* BriarPoolSpawn(const Entity& owner, const EntityClass& cls)
{
    for (Entity* e = Ecs_First(); e; e = e->mNext)
    {
        if (e->mParent == &owner && e->mClass == &cls && !e->mExists && !e->mFreed)
        {
            e->mExists = true;
            return e;
        }
    }
    return nullptr;
}

// A rectangle's own random point: Position + (RNG.NextSingle() * width, RNG.NextSingle() * height).
void BriarRandomPos(const Entity& target, fx& x, fx& y)
{
    const fx rx = Rng_Unit();
    const fx ry = Rng_Unit();
    x = target.mT->mX + rx * target.mB->mW;
    y = target.mT->mY + ry * target.mB->mH;
}

// The C#'s `float t = 0; while (!MoveTo(ref t, tm, 1)) yield return null;`, one call per MoveNext.
bool BriarWait(fx& t, fx seconds)
{
    return MoveTo(t, seconds, kFxOne);
}

// ---- BriarBossBody: drops bullets and raises thorns under the player ----------------------------------------------
struct BriarBodyData final
{
    fx mBulletT;
    fx mThornT;
    int mPhase;
    int mAttack;  // ThornAttack's i
    int mThorn;   // Thorns' i
    uint8_t mStep; // ThornAttack
    bool mActive; // State != null
    bool mBulletWaiting;
    bool mThornWaiting;
    bool mThornsStarted;
};

extern const EntityClass kBriarBullet;
extern const EntityClass kBriarThorn;

// Bullets(phase): one volley a MoveNext.
void BriarBodyVolley(Entity& body, int phase)
{
    struct Shot
    {
        int mX;
        int mY;
        int mVx;
        int mVy;
        int mAx;
        int mAy;
    };
    static const int kBaseX[] = { 36, 52, 69, 84 };
    auto spawn = [&](const Shot& s) {
        Entity* b = BriarPoolSpawn(body, kBriarBullet);
        if (b)
        {
            b->mT->mX = body.mT->mX + Fx(s.mX);
            b->mT->mY = body.mT->mY + Fx(s.mY);
            b->mT->mVelX = Fx(s.mVx);
            b->mT->mVelY = Fx(s.mVy);
            b->mT->mAccX = Fx(s.mAx);
            b->mT->mAccY = Fx(s.mAy);
        }
    };
    for (int x : kBaseX)
    {
        spawn({ x, 60, 0, 40, 0, 25 });
    }
    if (phase >= 2)
    {
        spawn({ 20, 44, -10, 45, 5, 20 });
        spawn({ 101, 44, 10, 45, -5, 20 });
    }
    if (phase >= 4)
    {
        spawn({ 4, 44, -10, 45, 5, 20 });
        spawn({ 116, 44, 10, 45, -5, 20 });
    }
}

// Thorns(phase)'s MoveNext: a thorn a step under the player's row, eight across.
bool BriarBodyThorns(Entity& body, BriarBodyData& d)
{
    if (!d.mThornsStarted)
    {
        d.mThornsStarted = true;
        d.mThorn = 0;
        Game_PlaySfx("briar_shine");
    }
    if (d.mThorn >= 8)
    {
        return false;
    }
    const int i = d.mThorn;
    int row = FxTrunc(World_Player()->mT->mY - body.mT->mY) / 16;
    const int lo = (i > 1 && i < 6) ? 4 : 5;
    row = row < lo ? lo : (row > 8 ? 8 : row);
    if (Entity* t = BriarPoolSpawn(body, kBriarThorn))
    {
        t->mT->mX = body.mT->mX + Fx(i * 16) + t->mV->mOffX;
        t->mT->mY = body.mT->mY + Fx(row * 16);
        Ecs_Play(*t, "attack", false, 12 + d.mPhase);
    }
    d.mThorn++;
    return true;
}

// Attack(phase)'s MoveNext.
bool BriarBodyAttack(Entity& body, BriarBodyData& d)
{
    static const fx kTimes[] = { FxF(0.18f), FxF(0.16f), FxF(0.14f), FxF(0.13f), FxF(0.11f), FxF(0.09f) };
    static const int kAttacks[] = { 2, 2, 2, 3, 3, 4 };
    // thorn_attack.MoveNext()
    bool more = false;
    for (;;)
    {
        if (d.mStep == 0)
        {
            if (d.mAttack >= kAttacks[d.mPhase])
            {
                break;
            }
            d.mThornsStarted = false;
            d.mThornWaiting = false;
            d.mStep = 1;
        }
        if (d.mStep == 1)
        {
            // OnceEvery(Thorns(phase), times[phase])
            if (d.mThornWaiting)
            {
                if (!BriarWait(d.mThornT, kTimes[d.mPhase]))
                {
                    more = true;
                    break;
                }
                d.mThornWaiting = false;
            }
            if (BriarBodyThorns(body, d))
            {
                d.mThornWaiting = true;
                d.mThornT = 0;
                more = true;
                break;
            }
            d.mStep = 2;
        }
        if (Ecs_CountChildren(body, kBriarThorn) != 0)
        {
            more = true;
            break;
        }
        d.mAttack++;
        d.mStep = 0;
    }
    if (!more)
    {
        return false;
    }
    // bullet_attack.MoveNext(): OnceEvery(Bullets(phase), 1.4f - 0.2f * phase / 2)
    if (d.mBulletWaiting)
    {
        const fx tm = FxF(1.4f) - FxF(0.2f) * d.mPhase / 2;
        if (!BriarWait(d.mBulletT, tm))
        {
            return true;
        }
    }
    BriarBodyVolley(body, d.mPhase);
    d.mBulletWaiting = true;
    d.mBulletT = 0;
    return true;
}

void BriarBodyStart(Entity& body, int phase)
{
    BriarBodyData& d = body.Data<BriarBodyData>();
    d = BriarBodyData();
    d.mPhase = phase;
    d.mActive = true;
}

void BriarBodyUpdate(Entity& self)
{
    BriarBodyData& d = self.Data<BriarBodyData>();
    Ecs_BaseUpdate(self);
    if (d.mActive && !BriarBodyAttack(self, d))
    {
        d.mActive = false;
    }
}

void BriarBodyCollided(Entity& self, Entity& other)
{
    const Rect h = Ecs_Hitbox(self);
    other.mT->mY = Fx(h.mY + h.mH);
    Player_ReceiveDamage(1);
}

void BriarThornUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
}

void BriarThornCollided(Entity& self, Entity&)
{
    if (Player_State() == PS_GROUND && Ecs_Frame(self) >= 9)
    {
        Player_ReceiveDamage(1);
    }
}

void BriarBulletUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (InGrid(self.mT->mY) > Fx(150))
    {
        self.mExists = false;
    }
}

void BriarBulletCollided(Entity&, Entity&)
{
    if (Player_State() == PS_GROUND)
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kBriarBody = {
    "BriarBossBody", BriarBodyUpdate, nullptr, BriarBodyCollided, nullptr, nullptr, nullptr, nullptr,
    0,               GROUP_PLAYER,    0,
};

const EntityClass kBriarThorn = {
    "Thorn", BriarThornUpdate, nullptr, BriarThornCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

const EntityClass kBriarBullet = {
    "Bullet", BriarBulletUpdate, nullptr, BriarBulletCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// ---- BigThorn (BlueThorn / HappyThorn): an arm; hurt, the broom knocks a health off it -------------------------------
enum BriarFireEyeState : uint8_t
{
    FES_NONE,
    FES_ATTACK,
    FES_END,
};

struct BigThornData final
{
    Entity* mHelper; // BlueThorn's FireEye / HappyThorn's IceCrystal
    Entity* mAttacker;
    fx mTimer;
    int mHealth;
    uint8_t mStep;
    bool mBlue;
    bool mActive; // state != null
};

struct FireEyeData final
{
    fx mShotT;
    fx mEndT;
    int mExplosions; // num_explosions
    uint8_t mState;
    bool mEndStarted;
};

struct IceCrystalData final
{
    Entity* mThorn; // its HappyThorn
    fx mTargetX;
    fx mTargetY;
    fx mBaseVel;
    fx mVelMul;
    int mCount; // n
    int mI;
    uint8_t mStep;
    bool mActive;  // state != null
    bool mWaiting; // state is Wait()
    bool mHitPlayer;
    bool mHitBroom;
};

extern const EntityClass kBriarFireball;
extern const EntityClass kBriarDustExplosion;
extern const EntityClass kBriarMist;
extern const EntityClass kBriarIceExplosion;
const EntityClass* gBriarDustClass = nullptr; // Dust's (the FireEye's pooled ones)

Entity* BriarDustSpawn(const Entity& eye)
{
    return gBriarDustClass ? BriarPoolSpawn(eye, *gBriarDustClass) : nullptr;
}

// FireEye.Start
void FireEyeStart(Entity& eye, int phase)
{
    FireEyeData& d = eye.Data<FireEyeData>();
    const fx left = Fx(gGame.mGridX * 160);
    const fx top = Fx(gGame.mGridY * 160);
    Ecs_Play(eye, "grow");
    eye.mT->mX = left + Fx(phase == 0 ? 70 : 50);
    eye.mT->mY = top + Fx(phase == 0 ? 80 : 100);
    d.mExplosions = 0;
    static const int kDust[4][2] = { { 1, 7 }, { 8, 7 }, { 3, 5 }, { 1, 5 } };
    for (const auto& c : kDust)
    {
        if (Entity* dust = BriarDustSpawn(eye))
        {
            dust->mExists = true;
            dust->mT->mX = left + Fx(c[0] * 16);
            dust->mT->mY = top + Fx(c[1] * 16);
            Ecs_Play(*dust, "unpoof");
        }
    }
    auto mist = [&](int x, int y) {
        if (Entity* m = BriarPoolSpawn(eye, kBriarMist))
        {
            m->mT->mX = left + Fx(x);
            m->mT->mY = top + Fx(y);
            Ecs_Flicker(*m, FxSec(1));
        }
    };
    if (phase == 1)
    {
        // (the original spawns one at 5 * 16 - 12 first, which the second overwrites)
        mist(6 * 16 - 7, 7 * 16 - 12);
    }
    else if (phase == 2)
    {
        mist(5 * 16 + 8, 4 * 16 + 5);
        mist(6 * 16, 6 * 16 + 3);
    }
    d.mShotT = 0;
    d.mState = FES_ATTACK;
}

void FireEyeEndAttack(Entity& eye)
{
    FireEyeData& d = eye.Data<FireEyeData>();
    d.mState = FES_END;
    d.mEndStarted = false;
}

// FireEye.ExplodeAt
void FireEyeExplodeAt(Entity& eye, Entity& dust)
{
    eye.Data<FireEyeData>().mExplosions++;
    dust.mExists = false;
    if (Entity* ex = BriarPoolSpawn(eye, kBriarDustExplosion))
    {
        Game_PlaySfx("dust_explode");
        ex->mT->mX = dust.mT->mX - Fx(16);
        ex->mT->mY = dust.mT->mY - Fx(16);
        Ecs_Play(*ex, "explode");
    }
    for (int f = FACE_LEFT; f <= FACE_DOWN; f++)
    {
        if (Entity* b = BriarPoolSpawn(eye, kBriarFireball))
        {
            b->mT->mX = dust.mT->mX;
            b->mT->mY = dust.mT->mY;
            b->mT->mVelX = Fx(FacingX(static_cast<Facing>(f)) * 70);
            b->mT->mVelY = Fx(FacingY(static_cast<Facing>(f)) * 70);
        }
    }
}

void FireEyeUpdate(Entity& self)
{
    FireEyeData& d = self.Data<FireEyeData>();
    Ecs_BaseUpdate(self);
    if (Ecs_AnimIs(self, "grow") && Ecs_AnimFinished(self))
    {
        Ecs_Play(self, "shoot");
    }
    if (d.mState == FES_ATTACK)
    {
        if (d.mExplosions < 4 || Ecs_CountChildren(self, kBriarDustExplosion) != 0)
        {
            // OnceEvery(fire at the player, 0.4f)
            if (BriarWait(d.mShotT, FxF(0.4f)))
            {
                if (Entity* b = BriarPoolSpawn(self, kBriarFireball))
                {
                    const Entity& p = *World_Player();
                    b->mT->mX = self.mT->mX + Fx(4);
                    b->mT->mY = self.mT->mY + Fx(4);
                    Ecs_MoveTowards(*b, p.mT->mX, p.mT->mY, Fx(45));
                }
                d.mShotT = 0;
                BriarWait(d.mShotT, FxF(0.4f));
            }
            return;
        }
        FireEyeEndAttack(self); // (End() runs from the next update)
        return;
    }
    if (d.mState == FES_END)
    {
        if (!d.mEndStarted)
        {
            d.mEndStarted = true;
            for (Entity* e = Ecs_First(); e; e = e->mNext)
            {
                if (e->mParent != &self || !e->mExists)
                {
                    continue;
                }
                if (gBriarDustClass && e->mClass == gBriarDustClass)
                {
                    Ecs_Play(*e, "poof");
                }
                else if (e->mClass == &kBriarMist)
                {
                    Ecs_Flicker(*e, FxSec(1));
                }
            }
            Ecs_Play(self, "ungrow");
            d.mEndT = 0;
        }
        if (!BriarWait(d.mEndT, FxF(1.3f)))
        {
            return;
        }
        for (Entity* e = Ecs_First(); e; e = e->mNext)
        {
            if (e->mParent == &self && e->mClass == &kBriarMist)
            {
                e->mExists = false;
            }
        }
        self.mExists = false;
        d.mState = FES_NONE;
    }
}

void BriarFireballUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (self.mB->mTouching != TOUCH_NONE)
    {
        self.mExists = false; // (the edge of the screen)
    }
}

void BriarFireballCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        if (Player_State() == PS_GROUND)
        {
            Player_ReceiveDamage(1);
        }
    }
    else if (other.mClass->mGroups & GROUP_DUST)
    {
        if (self.mParent)
        {
            FireEyeExplodeAt(*self.mParent, other);
        }
        self.mExists = false;
    }
    else if (other.mClass->mGroups & GROUP_MIST)
    {
        self.mExists = false;
    }
}

void BriarAnimOnceUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
}

// BlueThorn.GetHit
void BlueThornGetHit(Entity& blue)
{
    if (!Ecs_AnimIs(blue, "off"))
    {
        return;
    }
    Ecs_Play(blue, "hurt");
    Screen_Shake(FxF(0.01f), FxSec(0.2f));
    Game_PlaySfx("wb_hit_ground");
    BigThornData& d = blue.Data<BigThornData>();
    if (d.mHelper)
    {
        d.mHelper->Data<FireEyeData>().mState = FES_NONE; // shooter.Stop()
    }
}

void BriarDustExplosionCollided(Entity&, Entity& other)
{
    BlueThornGetHit(other);
}

// IceCrystal.DoAttack / Wait, one MoveNext.
bool IceCrystalStep(Entity& self)
{
    IceCrystalData& d = self.Data<IceCrystalData>();
    Transform& t = *self.mT;
    auto explode = [&]() {
        if (Entity* ex = BriarPoolSpawn(self, kBriarIceExplosion))
        {
            ex->mT->mX = t.mX - Fx(4);
            ex->mT->mY = t.mY - Fx(4);
            Ecs_Play(*ex, "explode");
        }
    };
    auto wait = [&]() {
        if (Ecs_CountChildren(self, kBriarIceExplosion) > 0)
        {
            return true;
        }
        self.mExists = false;
        t.mVelX = t.mVelY = 0;
        return false;
    };
    if (d.mWaiting)
    {
        return wait();
    }
    const Entity& p = *World_Player();
    for (;;)
    {
        switch (d.mStep)
        {
        case 0:
            d.mTargetX = d.mThorn->mT->mX + Fx(6);
            d.mTargetY = d.mThorn->mT->mY + Fx(52);
            Ecs_Flicker(self, FxSec(1.5f));
            d.mStep = 1;
            [[fallthrough]];
        case 1:
            if (self.mV->mFlickering)
            {
                return true;
            }
            explode();
            Game_PlaySfx("teleguy_down");
            d.mVelMul = Fx(1);
            d.mI = 0;
            d.mStep = 2;
            [[fallthrough]];
        case 2:
            if (d.mI >= d.mCount)
            {
                d.mStep = 6;
                continue;
            }
            Ecs_MoveTowards(self, p.mT->mX, p.mT->mY, FxMul(d.mBaseVel, d.mVelMul));
            d.mStep = 3;
            [[fallthrough]];
        case 3:
            if (!(d.mHitBroom || d.mHitPlayer))
            {
                const Rect screen = { gGame.mGridX * 160, gGame.mGridY * 160, 160, 160 };
                if (!Ecs_Intersects(Ecs_Hitbox(self), screen))
                {
                    d.mWaiting = true; // state = Wait()
                }
                return true;
            }
            explode();
            Screen_Shake(FxF(0.01f), FxSec(0.2f));
            if (d.mHitPlayer)
            {
                Player_ReceiveDamage(1);
                d.mWaiting = true;
                return true;
            }
            if (d.mHitBroom)
            {
                Game_PlaySfx("broom_hit");
                d.mVelMul += FxF(0.15f);
                Ecs_MoveTowards(self, d.mTargetX, d.mTargetY, FxMul(d.mBaseVel, d.mVelMul));
            }
            d.mStep = 4;
            [[fallthrough]];
        case 4:
        {
            const fx dx = t.mX - d.mTargetX;
            const fx dy = t.mY - d.mTargetY;
            if (FxLengthSq(dx, dy) > Fx(25))
            {
                return true;
            }
            Screen_Shake(FxF(0.01f), FxSec(0.2f));
            explode();
            Game_PlaySfx("sb_ball_appear");
            d.mVelMul += FxF(0.15f);
            d.mI++;
            d.mStep = 2;
            continue;
        }
        case 6:
            Ecs_Play(*d.mThorn, "hurt");
            self.mV->mVisible = false;
            d.mStep = 7;
            [[fallthrough]];
        case 7:
            if (Ecs_AnimIs(*d.mThorn, "hurt"))
            {
                return true;
            }
            d.mWaiting = true;
            return wait();
        default:
            return false;
        }
    }
}

void IceCrystalUpdate(Entity& self)
{
    IceCrystalData& d = self.Data<IceCrystalData>();
    Ecs_BaseUpdate(self);
    if (d.mActive && !IceCrystalStep(self))
    {
        d.mActive = false;
    }
    d.mHitPlayer = d.mHitBroom = false;
}

void IceCrystalCollided(Entity& self, Entity& other)
{
    IceCrystalData& d = self.Data<IceCrystalData>();
    if (IsPlayer(other))
    {
        d.mHitPlayer = true;
    }
    else if (IsBroom(other))
    {
        d.mHitBroom = true;
    }
}

// BigThorn.GetAttacked's MoveNext.
bool BigThornAttacked(Entity& self, BigThornData& d)
{
    if (d.mBlue)
    {
        Entity& eye = *d.mHelper;
        switch (d.mStep)
        {
        case 0:
            d.mTimer = 0;
            d.mStep = 1;
            [[fallthrough]];
        case 1:
            if (!BriarWait(d.mTimer, FxF(1.5f)))
            {
                return true;
            }
            eye.mExists = true;
            FireEyeStart(eye, 3 - d.mHealth);
            d.mStep = 2;
            [[fallthrough]];
        default:
            if (!eye.mExists)
            {
                return false;
            }
            if (Ecs_AnimFinished(self))
            {
                FireEyeEndAttack(eye); // got hit by the broom: end the attack
            }
            return true;
        }
    }
    Entity& crystal = *d.mHelper;
    if (d.mStep == 0)
    {
        const int phase = 3 - d.mHealth;
        crystal.mExists = true;
        crystal.mT->mX = d.mAttacker->mT->mX + Fx(5);
        crystal.mT->mY = d.mAttacker->mT->mY + Fx(52);
        IceCrystalData& c = crystal.Data<IceCrystalData>();
        c.mCount = 2 + phase;
        c.mBaseVel = Fx(50 + phase * 20);
        c.mStep = 0;
        c.mWaiting = false;
        c.mActive = true;
        d.mStep = 1;
    }
    return crystal.Data<IceCrystalData>().mActive;
}

void BigThornUpdate(Entity& self)
{
    BigThornData& d = self.Data<BigThornData>();
    Ecs_BaseUpdate(self);
    if (d.mActive && !BigThornAttacked(self, d))
    {
        d.mActive = false;
    }
    if (Ecs_AnimFinished(self))
    {
        Ecs_Play(self, "off");
    }
}

void BigThornCollided(Entity& self, Entity& other)
{
    if (!IsBroom(other) || !Ecs_AnimIs(self, "hurt"))
    {
        return;
    }
    Ecs_Play(self, "hit");
    self.Data<BigThornData>().mHealth--;
    Screen_Shake(FxF(0.03f), FxSec(0.4f));
    Screen_Flash(FxSec(0.6f), FLASH_BRIAR_RED);
    Game_PlaySfx("wb_hit_ground");
}

const EntityClass kBlueThorn = {
    "BlueThorn", BigThornUpdate, nullptr, BigThornCollided, nullptr, nullptr, nullptr, nullptr,
    GROUP_BLUE_THORN, GROUP_BROOM, 0,
};

const EntityClass kHappyThorn = {
    "HappyThorn", BigThornUpdate, nullptr, BigThornCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_BROOM, 0,
};

const EntityClass kFireEye = {
    "FireEye", FireEyeUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kBriarFireball = {
    "Fireball", BriarFireballUpdate, nullptr, BriarFireballCollided, nullptr, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER | GROUP_DUST | GROUP_MIST, ECF_KEEP_ON_SCREEN,
};

const EntityClass kBriarDustExplosion = {
    "DustExplosion", BriarAnimOnceUpdate, nullptr, BriarDustExplosionCollided, nullptr, nullptr, nullptr, nullptr,
    0,               GROUP_BLUE_THORN,    0,
};

const EntityClass kBriarMist = {
    "Mist", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, GROUP_MIST, 0, 0,
};

const EntityClass kIceCrystal = {
    "IceCrystal", IceCrystalUpdate, nullptr, IceCrystalCollided, nullptr, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER | GROUP_BROOM, 0,
};

const EntityClass kBriarIceExplosion = {
    "IceExplosion", BriarAnimOnceUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// The Die cutscene's own explosions (CutsceneState entities: the C# lists them nowhere).
const EntityClass kBriarDieIce = {
    "CutsceneIceExplosion", BriarAnimOnceUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kBriarDieDust = {
    "CutsceneDustExplosion", BriarAnimOnceUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kBriarDieBoom = {
    "CutsceneExplosion", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kBriarCore = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// ---- BriarBossFight: the arms take turns, and when both are spent the Briar comes apart ---------------------------
struct BriarFightData final
{
    Entity* mBody;
    Entity* mCore;
    Entity* mBlue;
    Entity* mHappy;
    Entity* mAttacker;
    Entity* mAttacked;
    fx mTimer;
    int mHealth; // MainAttack's health
    int mDieCount;
    uint8_t mStep;
    uint8_t mDieStep;
    bool mBossRush;
};

int BigThornHealth(const Entity& e)
{
    return const_cast<Entity&>(e).Data<BigThornData>().mHealth;
}

// MainAttack: attacked.state = attacked.GetAttacked(attacker, player)
void BriarStartAttack(BriarFightData& d, Entity& attacker, Entity& attacked)
{
    d.mAttacker = &attacker;
    d.mAttacked = &attacked;
    Ecs_Play(attacker, "active");
    BigThornData& t = attacked.Data<BigThornData>();
    t.mAttacker = &attacker;
    t.mStep = 0;
    t.mActive = true;
    d.mHealth = t.mHealth;
}

bool BriarDie(Entity& self);

// BriarBossFight.StateLogic, one MoveNext.
void BriarFightStep(Entity& self)
{
    BriarFightData& d = self.Data<BriarFightData>();
    for (;;)
    {
        switch (d.mStep)
        {
        case 0:
            if (!d.mBossRush)
            {
                Game_ShowDialogue("briar", "before_fight");
                d.mStep = 1;
                return;
            }
            d.mTimer = 0;
            d.mStep = 10;
            continue;
        case 10:
            if (!BriarWait(d.mTimer, Fx(1)))
            {
                return;
            }
            d.mStep = 2;
            continue;
        case 1:
            if (!Game_DialogueFinished())
            {
                return;
            }
            d.mStep = 2;
            [[fallthrough]];
        case 2:
        {
            const int hh = BigThornHealth(*d.mHappy);
            const int bh = BigThornHealth(*d.mBlue);
            if (hh + bh <= 0)
            {
                d.mDieStep = 0;
                Game_StartCutscene(self, BriarDie);
                d.mStep = 99;
                return;
            }
            BriarBodyStart(*d.mBody, 6 - hh - bh);
            d.mStep = 3;
            return;
        }
        case 3:
        {
            if (d.mBody->Data<BriarBodyData>().mActive)
            {
                return;
            }
            const bool happy = BigThornHealth(*d.mHappy) > 0;
            const bool blue = BigThornHealth(*d.mBlue) > 0;
            if (!happy || (blue && Rng_Range(0, 2) == 0))
            {
                BriarStartAttack(d, *d.mHappy, *d.mBlue);
            }
            else
            {
                BriarStartAttack(d, *d.mBlue, *d.mHappy);
            }
            d.mStep = 4;
            [[fallthrough]];
        }
        case 4:
        {
            Entity& attacked = *d.mAttacked;
            if (attacked.Data<BigThornData>().mActive)
            {
                if (BigThornHealth(attacked) == d.mHealth)
                {
                    return;
                }
                Ecs_Play(*d.mCore, "flash");
                d.mStep = 5;
            }
            else
            {
                d.mStep = 6;
                continue;
            }
            [[fallthrough]];
        }
        case 5:
            if (Ecs_AnimIs(*d.mAttacked, "hit"))
            {
                return;
            }
            Ecs_Play(*d.mCore, "glow");
            d.mStep = 6;
            [[fallthrough]];
        case 6:
            if (d.mAttacked->Data<BigThornData>().mActive)
            {
                return;
            }
            Ecs_Play(*d.mAttacker, "off");
            d.mStep = 2;
            continue;
        default:
            return;
        }
    }
}

void BriarFightUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    BriarFightStep(self);
}

const EntityClass kBriarFight = {
    "BriarBossFight", BriarFightUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// BriarBossFight.Die (a cutscene)
bool BriarDie(Entity& self)
{
    BriarFightData& d = self.Data<BriarFightData>();
    auto pooled = [&](const EntityClass& cls, int count, int layer) {
        for (int i = 0; i < count; i++)
        {
            Entity* e = Ecs_Create(cls, 0, 0, PART_VISUAL, nullptr, &self);
            if (!e)
            {
                return;
            }
            if (&cls == &kBriarDieIce)
            {
                Ecs_SetSheet(*e, "briar_ice_explosion", 24, 24);
            }
            else
            {
                Ecs_SetSheet(*e, "briar_dust_explosion", 48, 48);
            }
            Ecs_Play(*e, "explode");
            Ecs_SetSolid(*e, false);
            e->mV->mLayer = static_cast<uint8_t>(layer);
            Ecs_KeepPooled(*e);
        }
    };
    auto burst = [&](Entity& thorn, const EntityClass& cls) {
        Ecs_Play(thorn, "hitloop");
        if (Entity* e = BriarPoolSpawn(self, cls))
        {
            BriarRandomPos(thorn, e->mT->mX, e->mT->mY);
            Ecs_Play(*e, "explode");
        }
        Game_PlaySfx("hit_wall");
    };
    switch (d.mDieStep)
    {
    case 0:
        Game_PlaySong("");
        pooled(kBriarDieIce, 4, LAYER_FG_SPRITES);
        pooled(kBriarDieDust, 5, LAYER_FG_SPRITES);
        d.mDieCount = 0;
        d.mDieStep = 1;
        return false;
    case 1:
        d.mTimer = 0;
        d.mDieStep = 2;
        [[fallthrough]];
    case 2:
        if (!BriarWait(d.mTimer, FxF(0.2f)))
        {
            return false;
        }
        burst(*d.mHappy, kBriarDieDust);
        if (++d.mDieCount < 10)
        {
            d.mDieStep = 1;
            return BriarDie(self);
        }
        Screen_Flash(FxSec(1.5f), FLASH_BRIAR_BLUE); // (0xffff1111)
        Game_PlaySfx("sun_guy_death_short");
        d.mDieStep = 20;
        [[fallthrough]];
    case 20:
        if (!Screen_FlashFull())
        {
            return false;
        }
        d.mHappy->mExists = false;
        d.mDieCount = 0;
        d.mDieStep = 3;
        [[fallthrough]];
    case 3:
        d.mTimer = 0;
        d.mDieStep = 4;
        [[fallthrough]];
    case 4:
        if (!BriarWait(d.mTimer, FxF(0.2f)))
        {
            return false;
        }
        burst(*d.mBlue, kBriarDieIce);
        if (++d.mDieCount < 10)
        {
            d.mDieStep = 3;
            return BriarDie(self);
        }
        Screen_Flash(FxSec(1.5f), FLASH_BRIAR_RED); // (0xff1111ff)
        Game_PlaySfx("sun_guy_death_short");
        d.mDieStep = 21;
        [[fallthrough]];
    case 21:
        if (!Screen_FlashFull())
        {
            return false;
        }
        d.mBlue->mExists = false;
        d.mTimer = 0;
        d.mDieStep = 5;
        [[fallthrough]];
    case 5:
        if (!BriarWait(d.mTimer, FxF(1.5f)))
        {
            return false;
        }
        d.mDieCount = 0;
        d.mDieStep = 6;
        [[fallthrough]];
    case 6:
        d.mTimer = 0;
        d.mDieStep = 7;
        [[fallthrough]];
    case 7:
        if (!BriarWait(d.mTimer, FxF(0.15f)))
        {
            return false;
        }
        Ecs_Play(*d.mCore, "flash");
        {
            // new Explosion(body) { Position = randomizePos(body) }
            Game_PlaySfx("hit_wall");
            fx x;
            fx y;
            BriarRandomPos(*d.mBody, x, y);
            if (Entity* e = Ecs_Create(kBriarDieBoom, x, y, PART_VISUAL, nullptr, &self))
            {
                Ecs_SetSolid(*e, false);
                Ecs_SetSheet(*e, "enemy_explode_2");
                e->mV->mLayerRef = d.mBody;
                e->mV->mLayerOffset = 1;
                Ecs_Play(*e, "explode");
                e->mKeep = true;
            }
        }
        d.mDieStep = ++d.mDieCount < 15 ? 6 : 8;
        return false;
    case 8:
        Screen_Flash(FxSec(5), FLASH_WHITE);
        Game_PlaySfx("sun_guy_death_long");
        d.mDieStep = 22;
        [[fallthrough]];
    case 22:
        if (!Screen_FlashFull())
        {
            return false;
        }
        d.mBody->mExists = false;
        d.mCore->mExists = false;
        self.mExists = false;
        World_SetMapOffsetX(0);
        // (the cutscene's own entities go with it)
        for (Entity* e = Ecs_First(); e; e = e->mNext)
        {
            if (e->mParent == &self && (e->mClass == &kBriarDieIce || e->mClass == &kBriarDieDust ||
                                         e->mClass == &kBriarDieBoom))
            {
                e->mExists = false;
            }
        }
        d.mDieStep = 9;
        return false;
    default:
        return true;
    }
}

// ---- BriarBossMain: talks, lets the water in, and sends in the fight ---------------------------------------------
enum BriarMainState : uint8_t
{
    BMS_INTRO,
    BMS_FIGHT,
    BMS_POST,
};

struct BriarMainData final
{
    fx mTopLeftX;
    fx mTopLeftY;
    Entity* mGate;
    Entity* mFight;
    WaterFlow mWater;
    fx mWaterT;
    fx mTimer;
    uint8_t mState;
    uint8_t mStep;
    uint8_t mWaterStep; // DoWaterAnim(tile)
    bool mWaterWaiting;
    bool mVolume; // its VolumeEvent(0, 0.6f) still fading
    bool mBossRush;
};

Entity* SpawnBriarFight(Entity& main);

// DoWaterAnim(tile)'s MoveNext: the HAPPY side's water, the BLUE side's, then the four tiles that open the way.
bool BriarWaterStep(BriarMainData& d)
{
    const int tile = d.mBossRush ? 267 : 194;
    const int cx = FxTrunc(d.mTopLeftX) / 16;
    const int cy = FxTrunc(d.mTopLeftY) / 16;
    switch (d.mWaterStep)
    {
    case 0:
        if (WaterStep(d.mWater, d.mTopLeftX, d.mTopLeftY + Fx(16 * 4)))
        {
            return true;
        }
        d.mWater = WaterFlow();
        d.mWaterStep = 1;
        [[fallthrough]];
    case 1:
        if (WaterStep(d.mWater, d.mTopLeftX + Fx(16 * 9), d.mTopLeftY + Fx(16 * 4)))
        {
            return true;
        }
        World_ChangeTile(0, cx + 5, cy + 1, tile);
        d.mWaterStep = 2;
        return true;
    case 2:
        World_ChangeTile(0, cx + 5, cy, tile);
        d.mWaterStep = 3;
        return true;
    case 3:
        World_ChangeTile(0, cx + 4, cy + 1, tile);
        d.mWaterStep = 4;
        return true;
    case 4:
        World_ChangeTile(0, cx + 4, cy, tile);
        d.mWaterStep = 5;
        return false;
    default:
        return false;
    }
}

// BriarBossMain.Intro
bool BriarIntro(Entity& self)
{
    BriarMainData& d = self.Data<BriarMainData>();
    switch (d.mStep)
    {
    case 0:
        d.mStep = 1;
        if (!d.mBossRush)
        {
            Game_ShowCutsceneDialogue("briar", "before_fight");
            return false;
        }
        [[fallthrough]];
    case 1:
        Game_PlaySfx("stream");
        Game_PlaySong("");
        d.mWater = WaterFlow();
        d.mWaterStep = 0;
        d.mWaterWaiting = false;
        d.mStep = 2;
        [[fallthrough]];
    case 2:
        // OnceEvery(DoWaterAnim(tile), 0.2f), shaking the screen while it runs
        if (d.mWaterWaiting)
        {
            if (!BriarWait(d.mWaterT, FxF(0.2f)))
            {
                Screen_Shake(FxF(0.01f), FxSec(0.1f));
                return false;
            }
            d.mWaterWaiting = false;
        }
        if (BriarWaterStep(d))
        {
            d.mWaterWaiting = true;
            d.mWaterT = 0;
            Screen_Shake(FxF(0.01f), FxSec(0.1f));
            return false;
        }
        Ecs_Play(self, "oh_no");
        if (!d.mBossRush)
        {
            Game_ShowCutsceneDialogue("briar", "before_fight");
            d.mStep = 4;
            return false;
        }
        d.mTimer = 0;
        d.mStep = 3;
        [[fallthrough]];
    case 3:
        if (!BriarWait(d.mTimer, FxF(0.5f)))
        {
            return false;
        }
        [[fallthrough]];
    case 4:
        Screen_Flash(FxSec(1), FLASH_WHITE);
        d.mStep = 40;
        [[fallthrough]];
    case 40:
        if (!Screen_FlashFull())
        {
            return false;
        }
        Game_PlaySong("briar-fight");
        self.mV->mVisible = false;
        World_SetMapOffsetX(160 * 2);
        d.mGate->mT->mY += Fx(9 * 16);
        d.mFight = SpawnBriarFight(self);
        d.mStep = 5;
        return true;
    default:
        return true;
    }
}

// BriarBossMain.End
bool BriarEnd(Entity& self)
{
    BriarMainData& d = self.Data<BriarMainData>();
    switch (d.mStep)
    {
    case 0:
        d.mTimer = 0;
        d.mStep = 1;
        [[fallthrough]];
    case 1:
        if (!BriarWait(d.mTimer, FxF(1.5f)))
        {
            return false;
        }
        d.mStep = 2;
        if (!d.mBossRush)
        {
            Game_ShowCutsceneDialogue("briar", "after_fight");
            return false;
        }
        [[fallthrough]];
    case 2:
        d.mTimer = 0;
        d.mStep = 3;
        [[fallthrough]];
    case 3:
        if (!BriarWait(d.mTimer, Fx(1)))
        {
            return false;
        }
        self.mT->mVelX = 0;
        self.mT->mVelY = -Fx(20);
        Ecs_Play(self, "walk_u");
        if (!d.mBossRush)
        {
            Game_SetBossDefeated(gGame.mMapName);
            Game_PlaySong("go");
        }
        else
        {
            Game_PlaySong("bedroom");
            Preset_SetAlive(*self.mPreset, false);
        }
        d.mStep = 4;
        return true;
    default:
        return true;
    }
}

void BriarMainUpdate(Entity& self)
{
    BriarMainData& d = self.Data<BriarMainData>();
    Ecs_BaseUpdate(self);
    Entity& p = *World_Player();
    if (!World_InTransition())
    {
        switch (d.mState)
        {
        case BMS_INTRO:
            if (p.mT->mY < d.mTopLeftY + Fx(6 * 16))
            {
                d.mState = BMS_FIGHT;
                d.mStep = 0;
                Game_StartCutscene(self, BriarIntro);
            }
            break;
        case BMS_FIGHT:
            if (d.mFight && !d.mFight->mExists)
            {
                d.mState = BMS_POST;
                d.mGate->mExists = false;
                self.mV->mVisible = true;
                d.mStep = 0;
                Game_StartCutscene(self, BriarEnd);
            }
            break;
        default:
            if (p.mT->mY > d.mTopLeftY + Fx(120))
            {
                p.mT->mY = d.mTopLeftY + Fx(120);
            }
            if (self.mT->mY < d.mTopLeftY - Fx(16))
            {
                self.mV->mVisible = false;
                self.mT->mVelX = self.mT->mVelY = 0;
            }
            break;
        }
    }
    // its VolumeEvent(0, 0.6f), after the gate
    if (d.mVolume)
    {
        fx volume = Game_SongVolume();
        if (MoveTo(volume, 0, FxF(0.6f)))
        {
            d.mVolume = false;
        }
        Game_SetSongVolume(volume);
    }
}

void ThornGateCollided(Entity& self, Entity& other)
{
    Ecs_Separate(self, other);
    Player_ReceiveDamage(1);
}

const EntityClass kBriarMain = {
    "BriarBossMain", BriarMainUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

const EntityClass kThornGate = {
    "ThornGate", nullptr, nullptr, ThornGateCollided, nullptr, nullptr, nullptr, nullptr, 0, GROUP_PLAYER, 0,
};

// new BriarBossFight(player, bossRush), GlobalState.SpawnEntity'd with its sub-entities in their list order.
Entity* SpawnBriarFight(Entity& main)
{
    const fx left = main.Data<BriarMainData>().mTopLeftX;
    const fx top = main.Data<BriarMainData>().mTopLeftY;
    Entity* made[64];
    int count = 0;
    bool ok = true;
    auto make = [&](const EntityClass& cls, fx x, fx y, Entity* parent, const char* sheet, int w, int h,
                    uint8_t parts) -> Entity* {
        Entity* e = ok ? Ecs_Create(cls, x, y, parts, nullptr, parent) : nullptr;
        if (!e)
        {
            ok = false;
            return nullptr;
        }
        if (sheet)
        {
            Ecs_SetSheet(*e, sheet, w, h);
        }
        e->mB->mW = static_cast<int16_t>(w);
        e->mB->mH = static_cast<int16_t>(h);
        e->mKeep = true; // (the fight's entities all stay till the screen goes)
        if (count < 64)
        {
            made[count++] = e;
        }
        return e;
    };
    Entity* fight = make(kBriarFight, left, top, &main, "briar_overhang", 160, 48, PART_VISUAL | PART_BRAIN);
    if (!fight)
    {
        return nullptr;
    }
    fight->mV->mLayer = LAYER_BG_ENTITIES;
    BriarFightData& fd = fight->Data<BriarFightData>();
    fd = BriarFightData();
    fd.mBossRush = main.Data<BriarMainData>().mBossRush;

    Entity* body = make(kBriarBody, left + Fx(16), top, fight, "briar_body", 128, 80, PART_VISUAL | PART_BRAIN);
    if (!body)
    {
        return nullptr;
    }
    body->mB->mH = 60;
    body->Data<BriarBodyData>() = BriarBodyData();
    for (int i = 0; i < 16; i++)
    {
        Entity* b = make(kBriarBullet, 0, 0, body, "briar_thorn_bullet", 16, 16, PART_VISUAL);
        if (b)
        {
            b->mB->mW = b->mB->mH = 6;
            b->mV->mOffX = b->mV->mOffY = Fx(5);
            Ecs_Play(*b, "move");
            Ecs_KeepPooled(*b);
        }
    }
    for (int i = 0; i < 8; i++)
    {
        Entity* t = make(kBriarThorn, 0, 0, body, "briar_ground_thorn", 16, 16, PART_VISUAL);
        if (t)
        {
            t->mB->mW = t->mB->mH = 6;
            t->mV->mOffX = Fx(5);
            t->mV->mOffY = Fx(4);
            Ecs_Play(*t, "attack"); // (its first spawn finds it unfinished, so keeps the default frame rate)
            Ecs_KeepPooled(*t);
        }
    }
    Entity* core = make(kBriarCore, left + Fx(64), top + Fx(32), fight, "briar_core", 32, 18, PART_VISUAL);
    if (core)
    {
        core->mV->mLayerRef = body;
        core->mV->mLayerOffset = 1;
        Ecs_Play(*core, "glow");
    }
    // BlueThorn and its FireEye (which exists from the start, out of sight at 0,0)
    Entity* blue = make(kBlueThorn, left + Fx(5 * 16 + 35), top + Fx(16 + 4), fight, "briar_arm_right", 64, 80,
                        PART_VISUAL | PART_BRAIN);
    Entity* eye = make(kFireEye, 0, 0, blue, "briar_fire_eye", 16, 16, PART_VISUAL | PART_BRAIN);
    if (!blue || !eye)
    {
        return nullptr;
    }
    blue->mB->mW = 24;
    blue->mB->mH = 63;
    blue->mV->mOffX = Fx(35);
    blue->mV->mOffY = Fx(4);
    blue->mV->mLayerRef = body;
    blue->mV->mLayerOffset = 1;
    blue->mB->mImmovable = true;
    Ecs_Play(*blue, "off");
    eye->mV->mLayer = LAYER_BG_ENTITIES;
    eye->Data<FireEyeData>() = FireEyeData();
    Ecs_Play(*eye, "shoot");
    for (int i = 0; i < 12; i++)
    {
        Entity* f = make(kBriarFireball, 0, 0, eye, "briar_fire_eye", 16, 16, PART_VISUAL);
        if (f)
        {
            f->mB->mW = f->mB->mH = 6;
            f->mV->mOffX = f->mV->mOffY = Fx(5);
            Ecs_Play(*f, "move");
            Ecs_KeepPooled(*f);
        }
    }
    for (int i = 0; ok && i < 4; i++)
    {
        Entity* dust = Spawn_DustAt(0, 0);
        if (!dust)
        {
            ok = false;
            break;
        }
        dust->mParent = eye;
        Ecs_KeepPooled(*dust);
        gBriarDustClass = dust->mClass;
        if (count < 64)
        {
            made[count++] = dust;
        }
    }
    for (int i = 0; i < 4; i++)
    {
        Entity* x = make(kBriarDustExplosion, 0, 0, eye, "briar_dust_explosion", 48, 48, PART_VISUAL);
        if (x)
        {
            Ecs_Play(*x, "explode");
            Ecs_KeepPooled(*x);
        }
    }
    for (int i = 0; i < 2; i++)
    {
        Entity* m = make(kBriarMist, 0, 0, eye, "briar_mist", 24, 24, PART_VISUAL);
        if (m)
        {
            m->mV->mLayer = LAYER_FG_SPRITES;
            m->mV->mOpacity = FxF(0.7f);
            Ecs_Play(*m, "a");
            Ecs_KeepPooled(*m);
        }
    }
    // HappyThorn and its IceCrystal
    Entity* happy = make(kHappyThorn, left + Fx(16 + 4), top + Fx(16 + 4), fight, "briar_arm_left", 64, 80,
                         PART_VISUAL | PART_BRAIN);
    Entity* crystal = make(kIceCrystal, 0, 0, happy, "briar_ice_crystal", 16, 16, PART_VISUAL | PART_BRAIN);
    if (!happy || !crystal)
    {
        return nullptr;
    }
    happy->mB->mW = 24;
    happy->mB->mH = 63;
    happy->mV->mOffX = happy->mV->mOffY = Fx(4);
    happy->mV->mLayerRef = body;
    happy->mV->mLayerOffset = 1;
    happy->mB->mImmovable = true;
    Ecs_Play(*happy, "off");
    Ecs_Play(*crystal, "move");
    IceCrystalData& cd = crystal->Data<IceCrystalData>();
    cd = IceCrystalData();
    cd.mThorn = happy;
    Ecs_KeepPooled(*crystal);
    for (int i = 0; i < 5; i++)
    {
        Entity* x = make(kBriarIceExplosion, 0, 0, crystal, "briar_ice_explosion", 24, 24, PART_VISUAL);
        if (x)
        {
            x->mV->mLayer = LAYER_FG_SPRITES;
            Ecs_Play(*x, "explode");
            Ecs_KeepPooled(*x);
        }
    }
    if (!ok)
    {
        return nullptr;
    }
    BigThornData& bd = blue->Data<BigThornData>();
    bd = BigThornData();
    bd.mHealth = 3;
    bd.mBlue = true;
    bd.mHelper = eye;
    BigThornData& hd = happy->Data<BigThornData>();
    hd = BigThornData();
    hd.mHealth = 3;
    hd.mHelper = crystal;
    fd.mBody = body;
    fd.mCore = core;
    fd.mBlue = blue;
    fd.mHappy = happy;
    for (int i = 0; i < count; i++)
    {
        Ecs_MarkSpawned(*made[i]);
    }
    return fight;
}

// ---- EyebossPreview (Npc/Hotel/EyebossPreview.cs): the eye boss peeks in, blinks and fades away
// (its CompositeSpriteRenderer: the sclera is the entity, the pupil and the eyelids are "Entity" children)
struct EyePreviewData final
{
    uint8_t mStep;
    Entity* mPupil;
    Entity* mEyelids;
};

const EntityClass kEyePart = {
    "Entity", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// The pupil looks at the player: Eye_Center (7, 8) + 2 px towards them.
void EyeLookAtPlayer(const Entity& eye, Entity& pupil)
{
    const Entity& p = *World_Player();
    fx nx = Ecs_VisualCenterX(p) - Ecs_VisualCenterX(eye);
    fx ny = Ecs_VisualCenterY(p) - Ecs_VisualCenterY(eye);
    FxNormalize(nx, ny);
    pupil.mT->mX = eye.mT->mX - eye.mV->mOffX + (Fx(7) + nx * 2);
    pupil.mT->mY = eye.mT->mY - eye.mV->mOffY + (Fx(8) + ny * 2);
}

void EyePreviewUpdate(Entity& self)
{
    EyePreviewData& d = self.Data<EyePreviewData>();
    Ecs_BaseUpdate(self);
    EyeLookAtPlayer(self, *d.mPupil);
    Visual& v = *self.mV;
    switch (d.mStep)
    {
    case 0:
        if (v.mOpacity < kFxOne)
        {
            v.mOpacity = v.mOpacity + FxMul(FxF(0.48f), kFxTick);
            break;
        }
        Ecs_Play(*d.mEyelids, "open");
        d.mStep = 1;
        [[fallthrough]];
    case 1:
        if (!Ecs_AnimFinished(*d.mEyelids))
        {
            break;
        }
        Ecs_Play(*d.mEyelids, "blink");
        d.mStep = 2;
        [[fallthrough]];
    case 2:
        if (!Ecs_AnimFinished(*d.mEyelids))
        {
            break;
        }
        d.mStep = 3;
        [[fallthrough]];
    default:
        if (v.mOpacity > 0)
        {
            v.mOpacity = v.mOpacity - FxMul(FxF(3.0f), kFxTick);
            break;
        }
        self.mExists = false;
        break;
    }
    d.mPupil->mV->mOpacity = d.mEyelids->mV->mOpacity = v.mOpacity;
    d.mPupil->mExists = d.mEyelids->mExists = self.mExists;
}

const EntityClass kEyePreview = {
    "EyebossPreview", EyePreviewUpdate, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

// The sclera, pupil and eyelids of the eye boss and its preview.
bool MakeEyeParts(Entity& eye, Entity*& pupil, Entity*& eyelids)
{
    Ecs_SetSheet(eye, "sclera", 24, 24);
    eye.mB->mW = eye.mB->mH = 24;
    pupil = Ecs_Create(kEyePart, eye.mT->mX, eye.mT->mY, PART_VISUAL, nullptr, &eye);
    eyelids = Ecs_Create(kEyePart, eye.mT->mX, eye.mT->mY, PART_VISUAL, nullptr, &eye);
    if (!pupil || !eyelids)
    {
        return false;
    }
    Ecs_SetSheet(*pupil, "pupil", 10, 8);
    Ecs_SetSheet(*eyelids, "eyelids", 24, 24);
    Ecs_SetSolid(*pupil, false);
    Ecs_SetSolid(*eyelids, false);
    pupil->mV->mLayerRef = &eye;
    pupil->mV->mLayerOffset = 1;
    eyelids->mV->mLayerRef = &eye;
    eyelids->mV->mLayerOffset = 2;
    return true;
}

// ---- The eye boss (Hotel/Boss/*.cs): WaterPhase swims round the pool, then LandPhase (on the screen below) walks out
// of it. Each phase has an EnemyMarker, an invisible [Enemy] the gates count once the whole boss is beaten.
constexpr int kEyeBullets = 8;
constexpr int kEyeSplashes = 18;

const EntityClass kEnemyMarker = {
    "EnemyMarker", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, ECF_ENEMY,
};

Entity* MakeEnemyMarker(Entity& owner)
{
    return Ecs_Create(kEnemyMarker, 0, 0, 0, nullptr, &owner);
}

// WaterPhase.KeepInRect: bounces the velocity off the rectangle's sides; true if it bounced.
bool KeepInRect(Entity& e, const Rect& r)
{
    bool ret = false;
    const Rect h = Ecs_Hitbox(e);
    Transform& t = *e.mT;
    if ((h.mX < r.mX && t.mVelX < 0) || (h.mX + h.mW > r.mX + r.mW && t.mVelX > 0))
    {
        t.mVelX = -t.mVelX;
        ret = true;
    }
    if ((h.mY < r.mY && t.mVelY < 0) || (h.mY + h.mH > r.mY + r.mH && t.mVelY > 0))
    {
        t.mVelY = -t.mVelY;
        ret = true;
    }
    return ret;
}

// The other preset of the boss's link group (EntityManager.GetLinkGroup).
const MapEntity* EyeBossOtherPhase(const MapEntity& preset)
{
    const MapEntityList& list = Entities_Presets();
    for (uint32_t i = 0; i < list.mCount; i++)
    {
        const MapEntity& p = list.mEntities[i];
        if (&p != &preset && p.mLinkId == preset.mLinkId && (p.mType == EPT_EyeBossWater || p.mType == EPT_EyeBossLand))
        {
            return &p;
        }
    }
    return nullptr;
}

// ---- WaterPhase and its Bullets
struct EyeWaterBulletData final
{
    Rect mArea;
};

void EyeWaterBulletUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
    KeepInRect(self, self.Data<EyeWaterBulletData>().mArea);
}

// Bullet.Collided (also called with no one to pop them all when the eye goes down).
void EyeWaterBulletPop(Entity& self, Entity* other)
{
    if (self.mT->mVelX == 0 && self.mT->mVelY == 0)
    {
        return;
    }
    self.mT->mVelX = self.mT->mVelY = 0;
    Ecs_Play(self, "pop@2");
    Game_PlaySfx("dustpoof");
    if (other && IsPlayer(*other))
    {
        Player_ReceiveDamage(1);
    }
}

void EyeWaterBulletCollided(Entity& self, Entity& other)
{
    EyeWaterBulletPop(self, &other);
}

const EntityClass kEyeWaterBullet = {
    "Bullet", EyeWaterBulletUpdate, nullptr, EyeWaterBulletCollided, nullptr, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER | GROUP_BROOM, 0,
};

enum WaterPhaseStep : uint8_t
{
    WP_START,
    WP_WAIT_PLAYER, // until the player walks in
    WP_DIALOGUE,
    WP_FIGHT_START,
    WP_FIGHT,
    WP_SINK,
    WP_FADE,
    WP_DONE,
};

struct WaterPhaseData final
{
    uint8_t mStep;
    bool mSpeedUp; // FightLogic: in SpeedUpOnHit (else WaitingOnHit)
    int8_t mHealth;
    bool mBossRush;
    bool mBroomHit;
    bool mPlayerHit;
    bool mVolume; // its VolumeEvent(0, 0.42), quiet, is fading the song out
    bool mVolumeLive;
    fx mT; // SpeedUpOnHit's tm, WaitingOnHit's t
    fx mTlX;  // the pool's top left
    fx mTlY;
    Entity* mPupil;
    Entity* mEyelids;
    Entity* mMarker;
    // (the pool: there's only ever one eye boss, so it lives outside the brain data)
    static Entity* mBullets[kEyeBullets];
};

Entity* WaterPhaseData::mBullets[kEyeBullets];

// The eye's CompositeSpriteRenderer: the pupil and eyelids are drawn with the sclera.
void EyePartsFollow(const Entity& eye, Entity& pupil, Entity& eyelids)
{
    const Visual& v = *eye.mV;
    eyelids.mT->mX = eye.mT->mX - v.mOffX;
    eyelids.mT->mY = eye.mT->mY - v.mOffY;
    Entity* const parts[2] = { &pupil, &eyelids };
    for (Entity* part : parts)
    {
        part->mExists = eye.mExists;
        part->mV->mVisible = v.mVisible;
        part->mV->mOpacity = v.mOpacity;
        part->mV->mYPush = v.mYPush;
    }
}

// bullets.Spawn(b => b.Spawn(Position))
bool EyeWaterShoot(const Entity& self)
{
    for (Entity* b : WaterPhaseData::mBullets)
    {
        if (b->mExists)
        {
            continue;
        }
        b->mExists = true;
        b->mT->mX = self.mT->mX;
        b->mT->mY = self.mT->mY;
        b->mT->mVelX = b->mT->mVelY = Fx(50);
        Ecs_Play(*b, "move@2");
        Game_PlaySfx("gasguy_shoot");
        return true;
    }
    return false;
}

// FightLogic: SpeedUpOnHit then WaitingOnHit, round and round, each running on into the next in the same frame.
void EyeFightStep(Entity& self, WaterPhaseData& d)
{
    static const fx kShootTimes[7] = { 0, FxF(0.65f), FxF(0.7f), FxF(0.8f), FxF(0.9f), FxF(0.95f), FxF(1.1f) };
    Transform& t = *self.mT;
    for (;;)
    {
        if (d.mSpeedUp)
        {
            // SpeedUpOnHit: each hit speeds the eye up, until 0.8 s pass without one
            if (d.mT < FxF(0.8f))
            {
                d.mT += kFxTick;
                if (d.mPlayerHit)
                {
                    Player_ReceiveDamage(1);
                }
                if (d.mBroomHit)
                {
                    d.mT = 0;
                    t.mVelX = FxMul(t.mVelX, FxF(1.5f));
                    t.mVelY = FxMul(t.mVelY, FxF(1.5f));
                }
                return;
            }
            Ecs_Play(*d.mEyelids, "blink");
            t.mVelX = t.mVelY = Fx(6 - d.mHealth) * 15;
            d.mSpeedUp = false;
            d.mT = 0;
        }
        // WaitingOnHit: shoots until the broom hits it, then it's knocked away
        if (!d.mBroomHit)
        {
            d.mT += kFxTick;
            if (d.mT >= kShootTimes[d.mHealth])
            {
                d.mT = 0;
                if (EyeWaterShoot(self))
                {
                    Ecs_Play(*d.mEyelids, "blink");
                }
            }
            return;
        }
        Ecs_Play(*d.mEyelids, "closed");
        fx vx = FxMin(Fx(20), Fx(Rng_Range(0, 100)));
        fx vy = FxSqrt(Fx(100 * 100) - FxMul(vx, vx));
        if (Player_Facing() == FACE_UP)
        {
            vy = -vy;
        }
        if (Player_Facing() == FACE_RIGHT)
        {
            vx = -vx;
        }
        t.mVelX = vx;
        t.mVelY = vy;
        d.mSpeedUp = true;
        d.mT = 0;
    }
}

void WaterPhaseStep(Entity& self, WaterPhaseData& d)
{
    Transform& t = *self.mT;
    Visual& v = *self.mV;
    switch (d.mStep)
    {
    case WP_START:
        d.mVolume = true;
        d.mStep = WP_WAIT_PLAYER;
        [[fallthrough]];
    case WP_WAIT_PLAYER:
        if (World_Player()->mT->mY < d.mTlY + Fx(24))
        {
            return;
        }
        d.mStep = WP_FIGHT_START;
        if (!d.mBossRush && !Dialog_IsSceneDirty("eyeboss", "before_fight"))
        {
            Game_ShowDialogue("eyeboss", "before_fight");
            d.mStep = WP_DIALOGUE;
        }
        [[fallthrough]];
    case WP_DIALOGUE:
        if (d.mStep == WP_DIALOGUE && !Game_DialogueFinished())
        {
            return;
        }
        [[fallthrough]];
    case WP_FIGHT_START:
        d.mVolume = false;
        Game_PlaySong("hotel-boss");
        Game_IgnoreMusicNextUpdate();
        Ecs_Play(*d.mEyelids, "open");
        d.mSpeedUp = true;
        d.mT = 0;
        d.mStep = WP_FIGHT;
        [[fallthrough]];
    case WP_FIGHT:
        if (d.mHealth > 0)
        {
            EyeFightStep(self, d);
            const Rect box = { FxTrunc(d.mTlX) + 12, FxTrunc(d.mTlY) + 16, 16 * 7 + 4 - 12, 16 * 8 - 16 };
            if (KeepInRect(self, box))
            {
                Game_PlaySfx("wb_tap_ground");
            }
            return;
        }
        t.mVelX = 0;
        t.mVelY = Fx(40);
        if (!d.mBossRush)
        {
            if (Entity* h = Spawn_HealthDrop(t.mX, t.mY, true))
            {
                Ecs_MarkSpawned(*h);
            }
        }
        for (Entity* b : d.mBullets)
        {
            EyeWaterBulletPop(*b, nullptr);
        }
        d.mStep = WP_SINK;
        [[fallthrough]];
    case WP_SINK:
        if (t.mY < d.mTlY + Fx(16 * 6))
        {
            return;
        }
        d.mStep = WP_FADE;
        [[fallthrough]];
    case WP_FADE:
        if (!MoveTo(v.mOpacity, 0, Fx(3)))
        {
            v.mYPush = v.mYPush + kFxTick * 60;
            return;
        }
        gGame.mPuzzlesSolved++;
        self.mExists = false;
        Preset_SetAlive(*self.mPreset, false);
        if (const MapEntity* land = EyeBossOtherPhase(*self.mPreset))
        {
            Preset_SetActivated(*land, false); // the land phase climbs out of the pool
        }
        d.mStep = WP_DONE;
        return;
    default:
        return;
    }
}

void WaterPhaseUpdate(Entity& self)
{
    WaterPhaseData& d = self.Data<WaterPhaseData>();
    Ecs_BaseUpdate(self);
    WaterPhaseStep(self, d);
    // (its VolumeEvent: spawned on the first update, so it starts on the next, after the eye)
    if (d.mVolume)
    {
        if (d.mVolumeLive)
        {
            fx volume = Game_SongVolume();
            MoveTo(volume, 0, FxF(0.007f * 60));
            Game_SetSongVolume(volume);
        }
        d.mVolumeLive = true;
    }
    d.mBroomHit = d.mPlayerHit = false;
    EyeLookAtPlayer(self, *d.mPupil);
    EyePartsFollow(self, *d.mPupil, *d.mEyelids);
}

void WaterPhaseCollided(Entity& self, Entity& other)
{
    WaterPhaseData& d = self.Data<WaterPhaseData>();
    if (IsBroom(other) && !self.mV->mFlickering)
    {
        d.mBroomHit = true;
        d.mHealth--;
        Game_PlaySfx("broom_hit");
        Ecs_Flicker(self, FxSec(2.3f));
    }
    else if (IsPlayer(other))
    {
        d.mPlayerHit = true;
    }
}

const EntityClass kWaterPhase = {
    "WaterPhase", WaterPhaseUpdate, nullptr, WaterPhaseCollided, nullptr, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER | GROUP_BROOM, 0,
};

// ---- LandPhase, its Bullets and their BulletSplashes
struct EyeArcData final
{
    Parabola mArc;
    Entity* mShadow;
};

void EyeArcSetExists(Entity& e, bool exists)
{
    e.mExists = exists;
    e.Data<EyeArcData>().mShadow->mExists = exists;
}

// shadow.Update: Position = parent.Position - (offset - sprite size / 4)
void EyeArcShadowFollow(Entity& self, int dx, int dy)
{
    Entity& shadow = *self.Data<EyeArcData>().mShadow;
    shadow.mT->mX = self.mT->mX + Fx(dx);
    shadow.mT->mY = self.mT->mY + Fx(dy);
}

void EyeSplashUpdate(Entity& self)
{
    EyeArcData& d = self.Data<EyeArcData>();
    Ecs_BaseUpdate(self);
    EyeArcShadowFollow(self, 2, 2);
    if (Ecs_AnimIs(self, "move") && ParabolaTick(d.mArc, self, d.mShadow, 3))
    {
        Ecs_Play(self, "pop");
    }
    else if (Ecs_AnimFinished(self))
    {
        EyeArcSetExists(self, false);
        Game_PlaySfx("4sht_pop");
    }
}

void EyeSplashCollided(Entity& self, Entity&)
{
    if (self.mV->mOffY < Fx(9) && !Player_Invincible())
    {
        Player_SetSlowFor(100, FxF(0.3f));
        BubbleSfx();
    }
}

const EntityClass kEyeSplash = {
    "BulletSplash", EyeSplashUpdate, nullptr, EyeSplashCollided, nullptr, nullptr, nullptr, nullptr,
    0,              GROUP_PLAYER,    0,
};

void EyeSplashSpawn(Entity& s, fx x, fx y)
{
    EyeArcData& d = s.Data<EyeArcData>();
    EyeArcSetExists(s, true);
    s.mT->mX = x;
    s.mT->mY = y;
    Ecs_Play(s, "move");
    d.mArc.mT = 0;
    const fx vx = Fx(Rng_Range(-20, 21));
    fx vy = FxSqrt(Fx(20 * 20) - FxMul(vx, vx));
    if (Rng_Unit() < FxF(0.5f))
    {
        vy = -vy;
    }
    s.mT->mVelX = vx;
    s.mT->mVelY = vy;
}

enum LandCoroutine : uint8_t
{
    LP_TO_LAND,
    LP_LAND,
    LP_DIE,
    LP_NONE, // yield break
};

struct LandPhaseData final
{
    uint8_t mCo;   // the running coroutine
    uint8_t mStep; // its step
    uint8_t mGen;  // bumped by each new coroutine
    uint8_t mDeathRound;
    int8_t mHealth;
    bool mBossRush;
    bool mBroomHit;
    bool mPaceWaiting;  // Pace: inside its MoveTo(t, 1, 1) loop
    bool mBlinkWaiting; // Blink: inside its wait
    fx mT;              // Die's t
    fx mPaceT;
    fx mBlinkT;
    fx mPaceX; // Pace's pos
    fx mPaceY;
    fx mBaseX; // LandLogic's base_pt
    fx mBaseY;
    fx mTlX;
    fx mTlY;
    Parabola mJump;
    Entity* mShadow;
    Entity* mMarker;
    static Entity* mBullets[kEyeBullets];
    static Entity* mSplashes[kEyeSplashes];
};

Entity* LandPhaseData::mBullets[kEyeBullets];
Entity* LandPhaseData::mSplashes[kEyeSplashes];

void EyeLandBulletUpdate(Entity& self)
{
    EyeArcData& d = self.Data<EyeArcData>();
    Ecs_BaseUpdate(self);
    EyeArcShadowFollow(self, 2, 4);
    if (Ecs_AnimIs(self, "move"))
    {
        if (ParabolaTick(d.mArc, self, d.mShadow, 3))
        {
            Ecs_Play(self, "pop");
            self.mT->mVelX = self.mT->mVelY = 0;
        }
    }
    else if (Ecs_AnimFinished(self))
    {
        EyeArcSetExists(self, false);
        Game_PlaySfx("4sht_shoot");
        // splashes.Spawn(s => s.Spawn(Position), 4)
        int n = 0;
        for (Entity* s : LandPhaseData::mSplashes)
        {
            if (n == 4)
            {
                break;
            }
            if (!s->mExists)
            {
                EyeSplashSpawn(*s, self.mT->mX, self.mT->mY);
                n++;
            }
        }
    }
}

void EyeLandBulletCollided(Entity& self, Entity&)
{
    if (self.mV->mOffY < Fx(7))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kEyeLandBullet = {
    "Bullet", EyeLandBulletUpdate, nullptr, EyeLandBulletCollided, nullptr, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER,        0,
};

// ShootBullet: bullets.Spawn(b => b.Spawn(Position + (width, 2), player.Position), amount)
void LandShoot(const Entity& self, int amount)
{
    const Entity& p = *World_Player();
    const fx x = self.mT->mX + Fx(self.mB->mW);
    const fx y = self.mT->mY + Fx(2);
    for (Entity* b : LandPhaseData::mBullets)
    {
        if (amount == 0)
        {
            break;
        }
        if (b->mExists)
        {
            continue;
        }
        amount--;
        EyeArcData& d = b->Data<EyeArcData>();
        EyeArcSetExists(*b, true);
        Ecs_Play(*b, "move");
        b->mT->mX = x;
        b->mT->mY = y;
        // (Vector2 / duration: times 1 / duration)
        const fx inv = FxDiv(kFxOne, FxF(1.5f));
        const fx vx = FxMul(p.mT->mX - x, inv);
        const fx vy = FxMul(p.mT->mY - y, inv);
        const fx rx = Rng_Unit() * 5;
        const fx ry = Rng_Unit() * 5;
        b->mT->mVelX = vx + rx;
        b->mT->mVelY = vy + ry;
        d.mArc.mT = 0;
    }
}

void LandStart(LandPhaseData& d, uint8_t co)
{
    d.mCo = co;
    d.mStep = 0;
    d.mGen++;
}

// shadowPos: the shadow shrinks the higher the eye is.
void LandShadowFrame(const Entity& self, Entity& shadow)
{
    const fx off = self.mV->mOffY;
    Ecs_SetFrame(shadow, off <= Fx(20) ? 4 : (off <= Fx(40) ? 3 : 2));
}

// ToLand: rises out of the pool, hops up and crashes down onto the land.
bool LandToLandStep(Entity& self, LandPhaseData& d)
{
    Transform& t = *self.mT;
    Visual& v = *self.mV;
    switch (d.mStep)
    {
    case 0:
        // (the constructor's MoveNext: the initial setup)
        Ecs_Play(self, "closed");
        Ecs_SetSolid(self, false);
        t.mX = d.mTlX + Fx(44);
        t.mY = d.mTlY - Fx(32);
        d.mStep = 1;
        return true;
    case 1:
        t.mVelY = Fx(8);
        d.mStep = 2;
        [[fallthrough]];
    case 2:
    {
        const bool pushed = MoveTo(v.mYPush, Fx(22), Fx(40));
        const bool shown = MoveTo(v.mOpacity, kFxOne, FxF(1.8f));
        if (!pushed || !shown)
        {
            return true;
        }
        d.mStep = 3;
    }
        [[fallthrough]];
    case 3:
        if (t.mY < d.mTlY)
        {
            return true;
        }
        t.mVelX = t.mVelY = 0;
        Ecs_Play(self, "walk");
        v.mYPush = 0;
        Game_PlaySfx("bubble_triple");
        d.mShadow->mV->mVisible = true;
        d.mStep = 4;
        [[fallthrough]];
    case 4:
        if (!MoveTo(v.mOffY, Fx(90), Fx(90)))
        {
            LandShadowFrame(self, *d.mShadow);
            return true;
        }
        t.mX = d.mTlX + Fx(16);
        t.mY = d.mTlY + Fx(75);
        Game_PlaySfx("fall_1");
        d.mStep = 5;
        [[fallthrough]];
    default:
        if (!MoveTo(v.mOffY, 0, Fx(180)))
        {
            LandShadowFrame(self, *d.mShadow);
            return true;
        }
        d.mShadow->mV->mVisible = false;
        Screen_Shake(FxF(0.03f), FxF(0.5f));
        Preset_SetActivated(*self.mPreset, true); // back from a quick look upstairs, it's already on land
        LandStart(d, LP_LAND);
        return false;
    }
}

// Pace's MoveNext: a new spot on a 3x3 grid now and then (70%), each second.
void LandPaceNext(LandPhaseData& d)
{
    for (;;)
    {
        if (!d.mPaceWaiting)
        {
            d.mPaceT = 0;
            d.mPaceWaiting = true;
        }
        if (!MoveTo(d.mPaceT, kFxOne, kFxOne))
        {
            return;
        }
        d.mPaceWaiting = false;
        if (Rng_NextDouble() < 0.7)
        {
            d.mPaceX = d.mBaseX;
            d.mPaceY = d.mBaseY;
            d.mPaceX += Fx(Rng_Range(0, 3) * 16);
            d.mPaceY += Fx(Rng_Range(0, 3) * 16);
        }
    }
}

// Blink's MoveNext: blinks and shoots, quicker as it gets hurt.
void LandBlinkNext(Entity& self, LandPhaseData& d)
{
    static const fx kTimes[6] = { kFxOne, kFxOne, FxF(0.9f), FxF(0.8f), FxF(0.7f), FxF(0.65f) };
    for (;;)
    {
        if (!d.mBlinkWaiting)
        {
            d.mBlinkT = 0;
            d.mBlinkWaiting = true;
        }
        if (d.mBlinkT < kTimes[6 - d.mHealth])
        {
            d.mBlinkT += kFxTick;
            if (Ecs_AnimIs(self, "blink_land") && Ecs_AnimFinished(self))
            {
                Ecs_Play(self, "walk");
            }
            return;
        }
        d.mBlinkWaiting = false;
        Ecs_Play(self, "blink_land");
        Game_PlaySfx("slasher_atk");
        LandShoot(self, 1);
    }
}

// LandLogic: paces about blinking bullets; a hit sends it hopping to the right edge to fire three.
bool LandLogicStep(Entity& self, LandPhaseData& d)
{
    Transform& t = *self.mT;
    Visual& v = *self.mV;
    for (;;)
    {
        switch (d.mStep)
        {
        case 0:
            Ecs_SetSolid(self, true);
            self.mB->mH = 11;
            v.mOffY = Fx(Ecs_SpriteHeight(self) - 11);
            t.mX += v.mOffX;
            t.mY += v.mOffY;
            d.mBaseX = t.mX - Fx(16);
            d.mBaseY = t.mY - Fx(16);
            d.mPaceX = d.mBaseX + Fx(16);
            d.mPaceY = d.mBaseY + Fx(16);
            d.mPaceWaiting = d.mBlinkWaiting = false;
            d.mStep = 1;
            [[fallthrough]];
        case 1:
            if (!d.mBroomHit)
            {
                LandPaceNext(d);
                MoveTo(t.mX, d.mPaceX, Fx(60));
                MoveTo(t.mY, d.mPaceY, Fx(60));
                LandBlinkNext(self, d);
                return true;
            }
            if (World_Player()->mT->mX <= t.mX)
            {
                d.mStep = 3;
                break;
            }
            d.mShadow->mV->mVisible = true;
            d.mShadow->mV->mOffX = Fx(4);
            d.mShadow->mV->mOffY = Fx(10);
            ParabolaInit(d.mJump, self, Fx(16), FxF(0.6f));
            t.mVelX = -Fx(20);
            d.mStep = 2;
            [[fallthrough]];
        case 2:
            if (!ParabolaTick(d.mJump, self, d.mShadow, 4))
            {
                return true;
            }
            t.mVelX = t.mVelY = 0;
            Game_PlaySfx("wb_tap_ground");
            d.mShadow->mV->mVisible = false;
            d.mStep = 3;
            [[fallthrough]];
        default:
        {
            const bool xDone = MoveTo(t.mX, d.mTlX + Fx(16 * 8 - self.mB->mW), FxF(1.7f * 60));
            const bool yDone = MoveTo(t.mY, d.mTlY + Fx(57), Fx(60));
            if (!xDone || !yDone)
            {
                return true;
            }
            Screen_Shake(FxF(0.05f), FxF(0.3f));
            Game_PlaySfx("hit_ground_1");
            LandShoot(self, 3);
            d.mStep = 1;
            return true;
        }
        }
    }
}

// DeathCutscene: the player can walk about (still not hit by the last bullets) while the eye blows up.
bool LandDeathCutscene(Entity& owner)
{
    if (!owner.mExists)
    {
        return true;
    }
    Player_SetDontMove(false);
    Player_SetActionsDisabled(false);
    return false;
}

int EyeLandAlive()
{
    int n = 0;
    for (const Entity* b : LandPhaseData::mBullets)
    {
        n += b->mExists ? 1 : 0;
    }
    for (const Entity* s : LandPhaseData::mSplashes)
    {
        n += s->mExists ? 1 : 0;
    }
    return n;
}

bool LandDieStep(Entity& self, LandPhaseData& d)
{
    Transform& t = *self.mT;
    Visual& v = *self.mV;
    for (;;)
    {
        switch (d.mStep)
        {
        case 0:
            Ecs_SetSolid(self, false);
            Game_StartCutscene(self, LandDeathCutscene);
            Game_PlaySong("");
            Game_PlaySfx("sun_guy_death_long");
            Screen_Flash(kFxOne, FLASH_WHITE);
            t.mVelX = t.mVelY = 0;
            if (!d.mBossRush)
            {
                Game_ShowDialogue("eyeboss", "after_fight");
            }
            d.mStep = 1;
            [[fallthrough]];
        case 1:
            if (!d.mBossRush && !Game_DialogueFinished())
            {
                return true;
            }
            v.mOffX = v.mOffY = Fx(5);
            d.mDeathRound = 0;
            d.mStep = 2;
            [[fallthrough]];
        case 2:
            if (d.mDeathRound >= 10)
            {
                d.mStep = 4;
                break;
            }
            d.mT = 0;
            d.mStep = 3;
            [[fallthrough]];
        case 3:
            if (!MoveTo(d.mT, FxF(0.3f), kFxOne))
            {
                return true;
            }
            t.mX += Fx(Rng_Range(-4, 5));
            t.mY += Fx(Rng_Range(-4, 5));
            Spawn_Explosion(self);
            d.mDeathRound++;
            d.mStep = 2;
            return true;
        case 4:
            Screen_Flash(Fx(2), FLASH_WHITE);
            Game_PlaySfx("sun_guy_death_long");
            d.mShadow->mExists = false;
            d.mStep = 5;
            [[fallthrough]];
        default:
            if (Screen_FlashFull())
            {
                v.mVisible = false; // (the flash's onFull)
            }
            if (EyeLandAlive() > 0)
            {
                return true;
            }
            self.mExists = false;
            if (d.mBossRush)
            {
                Game_PlaySong("bedroom");
                Event_Set("BossRushLandDefeated", 1);
            }
            else
            {
                Preset_SetAlive(*self.mPreset, false);
                Game_SetBossDefeated(gGame.mMapName);
                Game_PlaySong("hotel");
            }
            d.mMarker->mExists = false;
            return false;
        }
    }
}

void LandPhaseUpdate(Entity& self)
{
    LandPhaseData& d = self.Data<LandPhaseData>();
    Ecs_BaseUpdate(self);
    // shadow.Update: Position = parent.Position - ((6, -8) - sprite size / 4)
    d.mShadow->mT->mX = self.mT->mX;
    d.mShadow->mT->mY = self.mT->mY + Fx(14);
    const uint8_t gen = d.mGen;
    bool running = true;
    switch (d.mCo)
    {
    case LP_TO_LAND:
        running = LandToLandStep(self, d);
        break;
    case LP_LAND:
        running = LandLogicStep(self, d);
        break;
    case LP_DIE:
        running = LandDieStep(self, d);
        break;
    default:
        break;
    }
    if (!running && d.mGen == gen)
    {
        d.mCo = LP_NONE;
    }
    d.mBroomHit = false;
}

void LandPhaseCollided(Entity& self, Entity& other)
{
    LandPhaseData& d = self.Data<LandPhaseData>();
    if (!Ecs_Solid(self))
    {
        return;
    }
    if (IsBroom(other) && !self.mV->mFlickering)
    {
        d.mBroomHit = true;
        d.mHealth--;
        Game_PlaySfx("broom_hit");
        Ecs_Flicker(self, FxSec(1.0f));
        if (d.mHealth == 0)
        {
            LandStart(d, LP_DIE);
        }
    }
    else if (IsPlayer(other))
    {
        Player_ReceiveDamage(1);
    }
}

const EntityClass kLandPhase = {
    "LandPhase", LandPhaseUpdate, nullptr, LandPhaseCollided, nullptr, nullptr, nullptr, nullptr,
    0,           GROUP_PLAYER | GROUP_BROOM, 0,
};
} // namespace

int Enemies_ProbeFields(const Entity& e, char* buf, int cap)
{
    // (the C# bot's names and units: seconds as 16.16, so its stand-in gets what the C# field would hold)
    if (e.mClass == &kPewLaser)
    {
        const PewLaserData& d = const_cast<Entity&>(e).Data<PewLaserData>();
        return snprintf(buf, static_cast<size_t>(cap), "\"_bulletTimer\":%d,\"_isFast\":%d", FxRaw16(d.mTimer),
                        d.mFast ? 1 : 0);
    }
    if (e.mClass == &kSeer)
    {
        return snprintf(buf, static_cast<size_t>(cap), "\"_health\":%d", const_cast<Entity&>(e).Data<SeerData>().mHealth);
    }
    if (e.mClass == &kAnnoyer)
    {
        return snprintf(buf, static_cast<size_t>(cap), "\"_health\":%d", const_cast<Entity&>(e).Data<AnnoyerData>().mHealth);
    }
    if (e.mClass == &kRedBoss)
    {
        return snprintf(buf, static_cast<size_t>(cap), "\"health\":%d", const_cast<Entity&>(e).Data<RedBossData>().mHealth);
    }
    if (e.mClass == &kLaser && e.mParent && e.mParent->mClass == &kPewLaser)
    {
        const PewLaserData& d = e.mParent->Data<PewLaserData>();
        return snprintf(buf, static_cast<size_t>(cap), "\"_isFast\":%d", d.mFast ? 1 : 0);
    }
    return 0;
}

const char* Enemies_ProbeFsm(const Entity& e)
{
    if (e.mClass == &kAnnoyer)
    {
        // Annoyer.cs: Active (Wait, Approach, Circle, Swoop) and Hit
        switch (const_cast<Entity&>(e).Data<AnnoyerData>().mState)
        {
        case ANNOYER_WAIT:
            return "ActiveState/TimerState";
        case ANNOYER_APPROACH:
            return "ActiveState/State";
        case ANNOYER_CIRCLE:
            return "ActiveState/CirclingState";
        case ANNOYER_SWOOP:
            return "ActiveState/SwoopState";
        default:
            return "TimerState";
        }
    }
    return nullptr;
}

void Shieldy_ReceiveDamage(Entity& shieldy, int damage)
{
    if (shieldy.mClass == &kShieldy)
    {
        shieldy.Data<ShieldyData>().mHealth -= damage;
    }
}

Entity* Spawn_Slime(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSlime, preset, "slime");
    if (!e)
    {
        return nullptr;
    }
    SlimeData& d = e->Data<SlimeData>();
    d.mBullet = preset.mFrame == 3;
    d.mHealth = 2;
    d.mSpeed = d.mBullet ? Fx(40) : Fx(20);
    for (uint8_t& rate : d.mGooRate)
    {
        rate = static_cast<uint8_t>(Rng_Range(5, 10));
    }
    if (d.mBullet)
    {
        for (uint8_t& rate : d.mBulletRate)
        {
            rate = static_cast<uint8_t>(Rng_Range(5, 10));
        }
    }
    d.mMove.Add(FxSec(0.5f), SLIME_MOVE_TIMER);
    d.mExitFrame = Game_Frame();
    SlimeEnterMove(*e);
    return e;
}

Entity* Spawn_BurstPlant(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kBurstPlant, preset, "burst_plant");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    BurstPlantData& d = e->Data<BurstPlantData>();
    d.mHealth = 2;
    for (Entity*& b : d.mBullets)
    {
        b = Ecs_Create(kBurstBullet, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!b)
        {
            return nullptr;
        }
        Ecs_SetSolid(*b, false);
        b->mB->mW = b->mB->mH = 8;
        Ecs_SetSheet(*b, "burst_plant_bullet");
        b->mV->mLayer = LAYER_FG_SPRITES;
        Ecs_KeepPooled(*b);
    }
    d.mCharging.Add(FxSec(0.5f), 0);
    d.mState = BURST_INITIAL;
    d.mInitial.Add(FxSec(0.4f), 0);
    return e;
}

Entity* Spawn_Frog(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kFrog, preset, "frog");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    FrogData& d = e->Data<FrogData>();
    d.mHealth = 2;
    for (int i = 0; i < kFrogBullets; ++i)
    {
        Entity* b = Ecs_Create(kFrogBullet, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        d.mBullets[i] = b;
        if (!b)
        {
            return nullptr;
        }
        Ecs_SetSolid(*b, false);
        b->mB->mW = b->mB->mH = 8;
        Ecs_SetSheet(*b, "frog_bullet");
        b->mV->mLayer = LAYER_FG_SPRITES;
        Ecs_KeepPooled(*b);
        FrogBulletData& bd = b->Data<FrogBulletData>();
        bd.mMultiplier = i + 1;
        bd.mPeriod = FxF(0.8f) + Rng_Unit();
    }
    d.mState = FROG_INITIAL;
    d.mInitial.Add(FxSec(0.8f), 0);
    return e;
}

Entity* Spawn_DashTrap(const MapEntity& preset, Entity&)
{
    const bool onSight = preset.mFrame == 0;
    Entity* e = Spawn_Basic(onSight ? kOnSightDashTrap : kBounceDashTrap, preset, "dash_trap");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 14;
    Ecs_CenterOffset(*e);
    e->mB->mMapInteraction = false;
    e->mB->mHoleAsWall = true;
    DashTrapData& d = e->Data<DashTrapData>();
    d.mIdleX = e->mT->mX;
    d.mIdleY = e->mT->mY;
    if (onSight)
    {
        Ecs_Play(*e, "idle@2");
    }
    else
    {
        Ecs_Play(*e, "dash@2");
        if (preset.mFrame == 1)
        {
            e->mT->mVelX = Fx(kDashVel);
        }
        else
        {
            e->mT->mVelY = Fx(kDashVel);
        }
    }
    return e;
}

Entity* Spawn_FourShooter(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kFourShooter, preset, "f_four_shooter");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    Ecs_SetSolid(*e, true);
    Ecs_Play(*e, "idle");
    FourShooterData& d = e->Data<FourShooterData>();
    d.mTimer = kFourShooterTime;
    for (Entity*& b : d.mBullets)
    {
        b = Ecs_Create(kFourShooterBullet, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!b)
        {
            return nullptr;
        }
        Ecs_SetSolid(*b, true);
        b->mB->mW = b->mB->mH = 4;
        b->mB->mMapInteraction = false;
        Ecs_SetSheet(*b, "f_four_shooter_bullet");
        b->mV->mOffX = b->mV->mOffY = Fx(2);
        b->mV->mLayer = LAYER_FG_SPRITES;
        Ecs_KeepPooled(*b);
    }
    return e;
}

Entity* Spawn_Slasher(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSlasher, preset, "f_slasher", 24, 24);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 16;
    e->mB->mMapInteraction = false;
    Ecs_CenterOffset(*e);
    SlasherData& d = e->Data<SlasherData>();
    d.mHealth = 3;
    d.mVertical = Ecs_Create(kVerticalSlash, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
    d.mHorizontal = Ecs_Create(kHorizontalSlash, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
    if (!d.mVertical || !d.mHorizontal)
    {
        return nullptr;
    }
    Ecs_SetSheet(*d.mVertical, "f_slasher_long", 16, 48);
    d.mVertical->mB->mW = 10;
    d.mVertical->mB->mH = 36;
    Ecs_SetSheet(*d.mHorizontal, "f_slasher_wide", 48, 16);
    d.mHorizontal->mB->mW = 36;
    d.mHorizontal->mB->mH = 10;
    Entity* const slashes[2] = { d.mVertical, d.mHorizontal };
    for (Entity* s : slashes)
    {
        Ecs_SetSolid(*s, false);
        Ecs_KeepPooled(*s);
    }
    // SubEntities: _horizontal, then _vertical
    Ecs_MoveAfter(*d.mHorizontal, *e);
    e->mFacing = FACE_LEFT; // the C#'s default facing
    SlasherEnter(*e, SLASHER_MOVE);
    return e;
}

Entity* Spawn_Annoyer(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kAnnoyer, preset, "annoyer");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mMapInteraction = false;
    Ecs_SetSolid(*e, false);
    e->mB->mW = 8;
    e->mB->mH = 7;
    e->mV->mOffX = Fx(3);
    e->mV->mOffY = Fx(2);
    AnnoyerData& d = e->Data<AnnoyerData>();
    d.mHealth = preset.mFrame == kAnnoyerSuper ? 2 : 1;
    d.mActive.Add(FxSec(2.3f), ANNOYER_FIRE);
    d.mCircle.Add(FxSec(3.0f), ANNOYER_SWOOP_TIMER);
    AnnoyerEnterActive(*e);
    Ecs_Play(*e, Game_IsCell() ? "flap@cell" : (preset.mFrame == kAnnoyerSuper ? "flap_super" : "flap"));
    return e;
}

Entity* Spawn_PewLaser(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kPewLaser, preset, "pew_laser");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    const int frame = preset.mFrame % 4;
    Ecs_SetFrame(*e, frame);
    static const Facing kFacing[4] = { FACE_DOWN, FACE_RIGHT, FACE_UP, FACE_LEFT };
    e->mFacing = kFacing[frame];
    PewLaserData& d = e->Data<PewLaserData>();
    d.mFast = preset.mFrame >= 4;
    d.mTimer = FxSec(1.0f);
    return e;
}

Entity* Spawn_Shieldy(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kShieldy, preset, "shieldy");
    if (!e)
    {
        return nullptr;
    }
    e->mT->mX += Fx(2);
    e->mT->mLastX = e->mT->mX;
    e->mB->mW = e->mB->mH = 10;
    e->mV->mOffX = Fx(3);
    e->mV->mOffY = Fx(4);
    e->Data<ShieldyData>().mHealth = 2;
    constexpr fx kSpeed = Fx(30);
    switch (preset.mFrame)
    {
    case 4:
        e->mT->mVelX = -kSpeed;
        break;
    case 5:
        e->mT->mVelY = -kSpeed;
        break;
    case 6:
        e->mT->mVelX = kSpeed;
        break;
    case 7:
        e->mT->mVelY = kSpeed;
        break;
    default:
        break;
    }
    Ecs_Play(*e, "walk");
    return e;
}

Entity* Spawn_Dog(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kDog, preset, "dog");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 12;
    e->mV->mOffX = e->mV->mOffY = Fx(2);
    e->mT->mDragX = Fx(20);
    DogData& d = e->Data<DogData>();
    d.mHealth = 3;
    d.mSuper = Preset_TypeValue(preset)[0] == '1';
    d.mAttack.Add(FxSec(1.0f), 0);
    Ecs_Play(*e, "stop");
    // (at the origin until its first Update moves it round the dog)
    Entity* region = Ecs_Create(kDogRegion, 0, 0, 0, nullptr, e);
    if (region)
    {
        region->mB->mW = region->mB->mH = 96;
        Ecs_SetSolid(*region, false);
        Ecs_KeepPooled(*region);
        d.mRegion = region;
    }
    DogEnter(*e, DOG_PACE);
    return e;
}

Entity* Spawn_Rotator(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kRotator, preset, "f_rotator");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    e->mB->mW = 6;
    e->mB->mH = 5;
    e->mV->mOffX = Fx(5);
    e->mV->mOffY = Fx(2);
    e->mT->mX += Fx(5);
    e->mT->mY += Fx(2);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    e->mV->mLayer = LAYER_BG_ENTITIES;
    Ecs_Play(*e, "pulse");
    Entity* b = Ecs_Create(kRotatorBullet, e->mT->mX, e->mT->mY, PART_VISUAL | PART_BRAIN, nullptr, e);
    if (b)
    {
        Ecs_SetSheet(*b, "f_rotator_ball");
        b->mB->mW = b->mB->mH = 4;
        b->mB->mImmovable = true;
        b->mV->mOffX = b->mV->mOffY = Fx(2); // CenterOffset
        Ecs_Play(*b, "pulse");
    }
    return e;
}

Entity* Spawn_Seer(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSeer, preset, "sun_guy", 16, 24);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 16;
    e->mB->mH = 24;
    e->mB->mImmovable = true;
    Ecs_SetSolid(*e, false);
    e->mV->mOpacity = 0;
    Ecs_Play(*e, "float");
    SeerData& d = e->Data<SeerData>();
    d.mHealth = 7;
    d.mBossRush = strcmp(Preset_TypeValue(preset), "boss_rush") == 0;
    d.mPresetX = e->mT->mX;
    d.mPresetY = e->mT->mY;
    static const fx kRadius[2] = { Fx(16), Fx(32) };
    static const fx kSpeed[2] = { FxF(3.0f), FxF(2.4f) };
    for (int i = 0; i < 2; i++)
    {
        Entity* orb = Ecs_Create(kSeerOrb, e->mT->mX + kRadius[i], e->mT->mY, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!orb)
        {
            return nullptr;
        }
        Ecs_SetSheet(*orb, "light_orb");
        orb->mV->mLayer = LAYER_FG_SPRITES;
        Ecs_Play(*orb, "glow");
        Ecs_SetSolid(*orb, false);
        SeerOrbData& o = orb->Data<SeerOrbData>();
        o.mIndex = static_cast<uint8_t>(i);
        o.mRadius = kRadius[i];
        o.mSpeed = kSpeed[i];
        Ecs_KeepPooled(*orb);
        d.mOrbs[i] = orb;
    }
    for (Entity*& dust : d.mDusts)
    {
        dust = Spawn_DustAt(0, 0);
        if (!dust)
        {
            return nullptr;
        }
        dust->mParent = e;
        Ecs_KeepPooled(*dust);
    }
    Entity* wave = Ecs_Create(kSeerWave, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
    if (!wave)
    {
        return nullptr;
    }
    Ecs_SetSheet(*wave, "sun_guy_wave");
    wave->mB->mW = 128;
    wave->mB->mH = 8;
    wave->mV->mLayer = LAYER_FG_SPRITES;
    Ecs_SetSolid(*wave, false);
    Ecs_Play(*wave, "play");
    Ecs_KeepPooled(*wave);
    d.mWave = wave;
    return e;
}

Entity* Spawn_Mover(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kMover, preset, "f_mover");
    if (!e)
    {
        return nullptr;
    }
    e->mT->mDragX = e->mT->mDragY = Fx(100);
    MoverData& d = e->Data<MoverData>();
    d = MoverData();
    d.mMove.Add(FxSec(1.0f), 0);
    return e;
}

Entity* Spawn_OnOffLaser(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kOnOffLaser, preset, "on_off_shooter");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    OnOffLaserData& d = e->Data<OnOffLaserData>();
    d = OnOffLaserData();
    d.mB = preset.mFrame == 3 || preset.mFrame == 4;
    static const uint8_t kFacing[5] = { FACE_UP, FACE_RIGHT, FACE_DOWN, FACE_LEFT, FACE_UP };
    e->mFacing = (preset.mFrame >= 0 && preset.mFrame < 5) ? kFacing[preset.mFrame] : FACE_UP;
    const bool horizontal = e->mFacing == FACE_LEFT || e->mFacing == FACE_RIGHT;
    e->mT->mRot = horizontal ? -kFxPi / 2 : (e->mFacing == FACE_UP ? -kFxPi : 0);

    Entity* laser = Ecs_Create(kOnOffBeam, e->mT->mX, e->mT->mY, PART_VISUAL | PART_BRAIN, nullptr, e);
    if (!laser)
    {
        return nullptr;
    }
    d.mLaser = laser;
    Ecs_SetSheet(*laser, horizontal ? "f_on_off_h" : "f_on_off_v", horizontal ? 144 : 16, horizontal ? 16 : 144);
    Transform& t = *laser->mT;
    Body& b = *laser->mB;
    Visual& v = *laser->mV;
    switch (e->mFacing)
    {
    case FACE_LEFT:
        b.mW = 112;
        b.mH = 8;
        t.mX -= Fx(112);
        t.mY += Fx(4);
        v.mOffX = Fx(16);
        v.mOffY = Fx(4);
        break;
    case FACE_UP:
        v.mVFlip = true;
        b.mW = 8;
        b.mH = 112;
        t.mX += Fx(4);
        t.mY -= Fx(112);
        v.mOffX = Fx(4);
        v.mOffY = Fx(16);
        break;
    case FACE_RIGHT:
        v.mHFlip = true;
        b.mW = 128;
        b.mH = 8;
        t.mX += Fx(4);
        t.mY += Fx(4);
        v.mOffX = Fx(4);
        v.mOffY = Fx(4);
        break;
    default:
        b.mW = 8;
        b.mH = 112;
        t.mX += Fx(4);
        t.mY += Fx(16);
        v.mOffX = Fx(4);
        v.mOffY = Fx(16);
        break;
    }
    t.mLastX = t.mX;
    t.mLastY = t.mY;
    v.mVisible = false;
    Ecs_Play(*laser, "idle");
    OnOffEnter(*e, ONOFF_IDLE);
    return e;
}

Entity* Spawn_RedBoss(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kRedBoss, preset, "red_boss", 32, 32);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 26;
    e->mB->mH = 19;
    e->mV->mOffX = Fx(3);
    e->mV->mOffY = Fx(13);
    Ecs_Play(*e, "close_eyes");
    RedBossData& d = e->Data<RedBossData>();
    d = RedBossData();
    d.mHealth = 12;
    d.mBossRush = strcmp(Preset_TypeValue(preset), "boss_rush") == 0;
    d.mSplash.Add(FxSec(3.0f), RBT_SPLASH);
    d.mSplash.Add(FxSec(3.0f), RBT_TENTACLES);
    d.mDash.Add(FxSec(1.5f), RBT_TENTACLES);
    d.mDash.Add(FxSec(5.0f), RBT_END_DASH);

    // (a sub-entity of the boss: made now, kept while it doesn't exist)
    auto make = [e](const EntityClass& cls, const char* sheet, int w, int h, uint8_t layer, uint8_t parts) -> Entity* {
        Entity* s = Ecs_Create(cls, 0, 0, parts, nullptr, e);
        if (!s)
        {
            return nullptr;
        }
        if (sheet)
        {
            Ecs_SetSheet(*s, sheet, w, h);
            s->mV->mLayer = layer;
        }
        s->mB->mW = static_cast<int16_t>(w);
        s->mB->mH = static_cast<int16_t>(h);
        return s;
    };

    Entity* ripple = make(kRipple, "red_boss_ripple", 48, 8, LAYER_BG_ENTITIES, PART_VISUAL | PART_BRAIN);
    if (!ripple)
    {
        return nullptr;
    }
    ripple->mT->mX = ripple->mT->mLastX = e->mT->mX;
    ripple->mT->mY = ripple->mT->mLastY = e->mT->mY;
    Ecs_Play(*ripple, "a");
    d.mRipple = ripple;

    d.mSmallWave = make(kSmallWave, "red_boss_small_wave", 16, 64, LAYER_BG_ENTITIES, PART_VISUAL | PART_BRAIN);
    d.mBigWave = make(kBigWave, "red_boss_big_wave", 32, 80, LAYER_BG_ENTITIES, PART_VISUAL | PART_BRAIN);
    if (!d.mSmallWave || !d.mBigWave)
    {
        return nullptr;
    }
    Entity* const waves[2] = { d.mSmallWave, d.mBigWave };
    for (Entity* w : waves)
    {
        w->mB->mImmovable = true;
        Ecs_Play(*w, "move");
        Ecs_KeepPooled(*w);
    }
    d.mBigWave->Data<BigWaveData>().mDisablePlayerHit = true;

    for (int i = 0; i < kRedBossPool; i++)
    {
        Entity* b = make(kSplashBullet, "red_boss_bullet", 8, 8, LAYER_FG_SPRITES, PART_VISUAL | PART_BRAIN);
        Entity* shadow = make(kEntityShadow, "8x8_shadow", 8, 8, LAYER_SHADOWS, PART_VISUAL);
        if (!b || !shadow)
        {
            return nullptr;
        }
        shadow->mParent = b;
        SplashBulletData& sb = b->Data<SplashBulletData>();
        sb = SplashBulletData();
        sb.mShadow = shadow;
        // (the screen being loaded: the boss's own)
        sb.mStartX = e->mT->mX - InGrid(e->mT->mX) + Fx(2 * 16) + Fx(3);
        sb.mStartY = e->mT->mY - InGrid(e->mT->mY) + Fx((3 + i) * 16) + Fx(3);
        b->mT->mX = b->mT->mLastX = sb.mStartX;
        b->mT->mY = b->mT->mLastY = sb.mStartY;
        sb.mPeriod = FxF(1.2f) + Rng_Unit();
        sb.mState = SB_DONE;
        Ecs_Play(*b, "move");
        Ecs_KeepPooled(*b);
        Ecs_KeepPooled(*shadow);
    }

    // ProximitySensor: (direction, width, height, offset from the boss)
    struct SensorDef
    {
        uint8_t mDir;
        int mW;
        int mH;
        int mX;
        int mY;
    };
    const int bw = e->mB->mW;
    const int bh = e->mB->mH;
    const SensorDef kSensors[4] = {
        { TOUCH_LEFT, 12, bh + 20, -18, -10 },
        { TOUCH_RIGHT, 8, bh + 16, bw + 7, -8 },
        { TOUCH_UP, bw, 8, 0, -21 },
        { TOUCH_DOWN, bw + 4, 12, 0, bh + 8 },
    };
    for (const SensorDef& def : kSensors)
    {
        Entity* s = make(kProximitySensor, nullptr, def.mW, def.mH, 0, PART_BRAIN);
        if (!s)
        {
            return nullptr;
        }
        ProximitySensorData& sd = s->Data<ProximitySensorData>();
        sd.mDir = def.mDir;
        sd.mOffX = Fx(def.mX);
        sd.mOffY = Fx(def.mY);
    }

    for (int i = 0; i < kRedBossPool; i++)
    {
        Entity* t = make(kTentacle, "red_boss_warning", 10, 10, LAYER_ENTITIES, PART_VISUAL | PART_BRAIN);
        Entity* flair = make(kTentacleFlair, "red_boss_tentacle", 10, 64, LAYER_ENTITIES, PART_VISUAL);
        if (!t || !flair)
        {
            return nullptr;
        }
        flair->mParent = t;
        t->mB->mImmovable = true;
        Ecs_Play(*t, "move");
        Ecs_Play(*flair, "move");
        TentacleData& td = t->Data<TentacleData>();
        td = TentacleData();
        td.mIndex = static_cast<uint8_t>(i);
        td.mTentacle = flair;
        td.mState = TENT_DONE;
        Ecs_KeepPooled(*t);
        Ecs_KeepPooled(*flair);
    }
    return e;
}

Entity* Spawn_WallBoss(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kWallBoss, Fx(preset.mX), Fx(preset.mY), PART_BRAIN, &preset);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 16;
    e->mB->mH = 16;
    WallBossData& d = e->Data<WallBossData>();
    d = WallBossData();
    d.mBossRush = strcmp(Preset_TypeValue(preset), "boss_rush") == 0;
    // (the screen being loaded: the boss's own)
    const fx left = Fx(preset.mX / 160 * 160);
    const fx top = Fx(preset.mY / 160 * 160);
    Player_SetGridEntranceAt(left + Fx(86), top + Fx(81));

    // (a sub-entity: made now, kept while it doesn't exist)
    auto make = [](Entity* parent, const EntityClass& cls, fx x, fx y, const char* sheet, int w, int h,
                   uint8_t layer, uint8_t parts) -> Entity* {
        Entity* s = Ecs_Create(cls, x, y, parts, nullptr, parent);
        if (!s)
        {
            return nullptr;
        }
        Ecs_SetSheet(*s, sheet, w, h);
        s->mV->mLayer = layer;
        s->mB->mW = static_cast<int16_t>(w);
        s->mB->mH = static_cast<int16_t>(h);
        return s;
    };
    constexpr uint8_t kParts = PART_VISUAL | PART_BRAIN;

    d.mWall = make(e, kWall, left, top, "wallboss_wall", 160, 32, LAYER_VERY_BG_ENTITIES, kParts);
    if (!d.mWall)
    {
        return nullptr;
    }
    d.mWall->mB->mImmovable = true;
    Ecs_SetSolid(*d.mWall, true);
    Ecs_Play(*d.mWall, "move");

    d.mFace = make(e, kFace, left + Fx((160 - 64) / 2), top, "f_wallboss_face", 64, 32, LAYER_ENTITIES, kParts);
    if (!d.mFace)
    {
        return nullptr;
    }
    Ecs_Play(*d.mFace, "normal");
    FaceData& fd = d.mFace->Data<FaceData>();
    fd = FaceData();
    fd.mHealth = 12;
    fd.mInvincible = true;
    for (int i = 0; i < kWallBossPool; i++)
    {
        const fx cx = d.mFace->mT->mX + Fx(32);
        const fx cy = d.mFace->mT->mY + Fx(16);
        Entity* b = make(d.mFace, kWallBullet, cx, cy, "wallboss_bullet", 8, 8, LAYER_FG_SPRITES, kParts);
        Entity* shadow = make(b, kEntityShadow, cx, cy, "8x8_shadow", 8, 8, LAYER_SHADOWS, PART_VISUAL);
        if (!b || !shadow)
        {
            return nullptr;
        }
        WallBulletData& bd = b->Data<WallBulletData>();
        bd = WallBulletData();
        bd.mShadow = shadow;
        Ecs_Play(*b, "move");
        Ecs_KeepPooled(*b);
        Ecs_KeepPooled(*shadow);
    }

    Entity* hands[2] = {};
    for (int right = 0; right < 2; right++)
    {
        const fx hx = left + Fx(right ? 7 * 16 : 16);
        const fx hy = top + Fx(32);
        Entity* h = make(e, right ? kRHand : kLHand, hx, hy, "f_wallboss_l_hand", 32, 32, LAYER_ENTITIES, kParts);
        if (!h)
        {
            return nullptr;
        }
        Entity* shadow = make(h, kEntityShadow, hx, hy + Fx(20), "28x10_shadow", 28, 10, LAYER_SHADOWS, PART_VISUAL);
        Entity* explosion = make(h, kDeathExplosion, 0, 0, "enemy_explode_2", 24, 24, LAYER_FG_SPRITES, kParts);
        if (!shadow || !explosion)
        {
            return nullptr;
        }
        h->mV->mHFlip = right != 0;
        h->mB->mImmovable = true;
        Ecs_Play(*h, "idle");
        shadow->mV->mVisible = false;
        Ecs_KeepPooled(*explosion);
        HandData& hd = h->Data<HandData>();
        hd = HandData();
        hd.mRight = right != 0;
        hd.mInitX = hx;
        hd.mInitY = hy;
        hd.mCracked = d.mBossRush ? 78 : 71;
        hd.mHole = d.mBossRush ? 91 : 81;
        ParabolaInit(hd.mStomp, *h, right ? FxF(32.0f) : FxF(64.0f), right ? FxF(1.0f) : FxF(2.0f));
        hd.mShadow = shadow;
        hd.mExplosion = explosion;
        hands[right] = h;
    }
    d.mLHand = hands[0];
    d.mRHand = hands[1];

    d.mLaser = make(e, kWallLaser, left + Fx(48), top + Fx(32 + 11), "wallboss_laser", 64, 10, LAYER_BG_ENTITIES,
                    kParts);
    if (!d.mLaser)
    {
        return nullptr;
    }
    d.mLaser->mV->mVisible = false;
    d.mLaser->Data<WallLaserData>() = WallLaserData();
    d.mLaser->Data<WallLaserData>().mStartY = d.mLaser->mT->mY;
    Ecs_KeepPooled(*d.mLaser);
    d.mLHand->Data<HandData>().mLaser = d.mLaser;
    d.mRHand->Data<HandData>().mLaser = d.mLaser;

    for (int i = 0; i < kWallBossPool; i++)
    {
        Entity* x = make(e, kDeathExplosion, 0, 0, "enemy_explode_2", 24, 24, LAYER_FG_SPRITES, kParts);
        if (!x)
        {
            return nullptr;
        }
        Ecs_KeepPooled(*x);
    }

    Entity* const faded[4] = { d.mWall, d.mFace, d.mLHand, d.mRHand };
    for (Entity* f : faded)
    {
        f->mV->mOpacity = 0;
    }
    return e;
}

// ---- Person (Crowd/Person.cs): wanders and chatters, and the player wades through them at half speed -------------
namespace
{
constexpr int kPersonSpeed = 10;

struct PersonData final
{
    fx mSwitchDirMax;
    fx mSwitchDir;
    fx mTalk;
};

void PersonSetFlip(Entity& self)
{
    self.mV->mHFlip = self.mT->mVelX < 0;
}

void PersonFace(Entity& self, Facing f)
{
    self.mFacing = f;
    Ecs_PlayFacing(self, "walk");
    self.mT->mVelX = Fx(FacingX(f) * kPersonSpeed);
    self.mT->mVelY = Fx(FacingY(f) * kPersonSpeed);
    PersonSetFlip(self);
}

void PersonUpdate(Entity& self)
{
    PersonData& d = self.Data<PersonData>();
    Ecs_BaseUpdate(self);
    d.mSwitchDir -= kFxTick;
    if (d.mSwitchDir < 0)
    {
        d.mSwitchDir = d.mSwitchDirMax;
        PersonFace(self, static_cast<Facing>(Rng_Range(0, 4)));
    }
    d.mTalk -= kFxTick;
    if (d.mTalk < 0)
    {
        d.mTalk = FxF(0.5f) + Rng_Unit();
        static const char* const kTalk[] = { "talk_1", "talk_1", "talk_2", "talk_3", "talk_3" };
        Game_PlaySfxOneOf(kTalk, 5);
    }
}

void PersonCollided(Entity& self, Entity& other)
{
    if (IsPlayer(other))
    {
        Player_SetSlowFor(7);
        Ecs_Separate(self, other);
    }
}

const EntityClass kPerson = {
    "Person", PersonUpdate, nullptr, PersonCollided, nullptr, nullptr, nullptr, nullptr,
    0,        GROUP_PLAYER, ECF_MAP_COLLISION,
};
} // namespace

Entity* Spawn_Person(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kPerson, preset, "person");
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 6;
    e->mB->mH = 4;
    Ecs_CenterOffset(*e);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    e->mV->mOffY += Fx(4);
    PersonData& d = e->Data<PersonData>();
    d.mSwitchDirMax = FxF(1.3f);
    d.mSwitchDir = FxF(2.0f);
    d.mTalk = FxF(0.5f) + Rng_Unit();
    switch (preset.mFrame)
    {
    case 0:
        PersonFace(*e, FACE_DOWN);
        break;
    case 1:
        PersonFace(*e, FACE_RIGHT);
        break;
    case 2:
        PersonFace(*e, FACE_UP);
        break;
    case 3:
        PersonFace(*e, FACE_LEFT);
        break;
    case 4:
    {
        // new Vector2(v): the same on both axes
        const fx v = Fx(Rng_Range(-kPersonSpeed, kPersonSpeed));
        e->mT->mVelX = e->mT->mVelY = v;
        Ecs_Play(*e, v > 0 ? "walk_r" : "walk_l");
        d.mSwitchDirMax = FxF(0.5f);
        PersonSetFlip(*e);
        break;
    }
    default:
        Ecs_Play(*e, "walk_d");
        break;
    }
    return e;
}

// ---- Spike rollers (Crowd/SpikeRoller.cs) ------------------------------------------------------------------------
// FallingSpikeRoller (frames 0-3): hidden until the player walks under it, then drops (its shadow flickering where it
// lands) and rolls 112 px. BouncySpikeRoller (frames 4-7): rolls back and forth between walls.
namespace
{
enum FallingRollerState : uint8_t
{
    ROLLER_HIDDEN,
    ROLLER_FALL,
    ROLLER_ROLL,
    ROLLER_STOPPED,
};

struct SpikeRollerData final
{
    uint8_t mState;
    bool mCollisionOn;
    fx mVelX; // BaseSpikeRoller.vel
    fx mVelY;
    fx mInitX;
    fx mInitY;
    Entity* mShadow;
};

// BaseSpikeRoller.ChangedVelocity: the sprite faces the way it rolls.
void RollerChangedVelocity(Entity& self)
{
    const Transform& t = *self.mT;
    self.mV->mHFlip = t.mVelX < 0;
    self.mV->mVFlip = t.mVelX == 0 && t.mVelY < 0;
}

void RollerCollided(Entity& self, Entity& other)
{
    if (self.Data<SpikeRollerData>().mCollisionOn && IsPlayer(other) && Player_State() != PS_AIR)
    {
        Player_ReceiveDamage(1);
    }
}

void FallingRollerEnter(Entity& self, uint8_t state)
{
    SpikeRollerData& d = self.Data<SpikeRollerData>();
    if (d.mState == ROLLER_FALL)
    {
        d.mShadow->mExists = false; // Fall's Exit
    }
    d.mState = state;
    switch (state)
    {
    case ROLLER_FALL:
        self.mV->mVisible = true;
        d.mShadow->mV->mVisible = true;
        Ecs_Flicker(*d.mShadow, -kFxOne); // (for good)
        Game_PlaySfx("fall_1");
        break;
    case ROLLER_ROLL:
        d.mCollisionOn = true;
        self.mT->mVelX = d.mVelX;
        self.mT->mVelY = d.mVelY;
        self.mV->mLayer = LAYER_BG_ENTITIES;
        RollerChangedVelocity(self);
        Ecs_Play(self, "roll");
        Game_PlaySfx("hit_ground_1");
        break;
    case ROLLER_STOPPED:
        Ecs_Play(self, "idle");
        self.mT->mVelX = self.mT->mVelY = 0;
        Game_PlaySfx("hit_ground_1");
        break;
    default:
        break;
    }
}

void FallingRollerUpdate(Entity& self)
{
    SpikeRollerData& d = self.Data<SpikeRollerData>();
    Ecs_BaseUpdate(self);
    if (d.mState == ROLLER_FALL && MoveTo(self.mV->mOffY, 0, Fx(60)))
    {
        FallingRollerEnter(self, ROLLER_ROLL);
    }
    else if (d.mState == ROLLER_ROLL && FxLength(d.mInitX - self.mT->mX, d.mInitY - self.mT->mY) >= Fx(112))
    {
        FallingRollerEnter(self, ROLLER_STOPPED);
    }
}

void FallingRollerCollided(Entity& self, Entity& other)
{
    RollerCollided(self, other);
    if (self.Data<SpikeRollerData>().mState == ROLLER_HIDDEN)
    {
        FallingRollerEnter(self, ROLLER_FALL); // TriggerEvent("PlayerOverlaps")
    }
}

const EntityClass kFallingSpikeRoller = {
    "FallingSpikeRoller", FallingRollerUpdate, nullptr, FallingRollerCollided, nullptr, nullptr, nullptr, nullptr,
    GROUP_SPIKE_ROLLER,   GROUP_PLAYER,        0,
};

const EntityClass kRollerShadow = {
    "RollerShadow", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0,
};

void BouncyRollerUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    if (self.mB->mTouching != TOUCH_NONE)
    {
        self.mT->mVelX = -self.mT->mVelX;
        self.mT->mVelY = -self.mT->mVelY;
        Game_PlaySfx("hit_ground_1");
        RollerChangedVelocity(self);
    }
}

const EntityClass kBouncySpikeRoller = {
    "BouncySpikeRoller", BouncyRollerUpdate, nullptr, RollerCollided, nullptr, nullptr, nullptr, nullptr,
    GROUP_SPIKE_ROLLER,  GROUP_PLAYER,       ECF_MAP_COLLISION,
};

// BaseSpikeRoller's constructor: frame 0 rolls up, 1 right, 2 left, 3 down.
Entity* SpawnSpikeRoller(const EntityClass& cls, const MapEntity& preset, int frame)
{
    const bool horizontal = frame == 0 || frame == 3;
    Entity* e = Spawn_Basic(cls, preset, horizontal ? "spike_roller_horizontal" : "spike_roller", horizontal ? 128 : 16,
                            horizontal ? 16 : 128);
    if (!e)
    {
        return nullptr;
    }
    SpikeRollerData& d = e->Data<SpikeRollerData>();
    d = SpikeRollerData();
    e->mB->mW = static_cast<int16_t>(horizontal ? 128 : 16);
    e->mB->mH = static_cast<int16_t>(horizontal ? 16 : 128);
    switch (frame)
    {
    case 0:
        e->mB->mH = 12;
        d.mVelY = -Fx(20);
        break;
    case 1:
        e->mB->mW = 12;
        d.mVelX = Fx(20);
        break;
    case 2:
        e->mB->mW = 12;
        d.mVelX = -Fx(20);
        break;
    default:
        e->mB->mH = 12;
        d.mVelY = Fx(20);
        break;
    }
    // CenterOffset(), then Position += offset again (as the C# does)
    Ecs_CenterOffset(*e);
    e->mT->mX += e->mV->mOffX;
    e->mT->mY += e->mV->mOffY;
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    e->mV->mLayer = LAYER_ROLLERS;
    e->mB->mImmovable = true;
    Ecs_Play(*e, "idle");
    return e;
}
} // namespace

Entity* Spawn_SpikeRoller(const MapEntity& preset, Entity&)
{
    if (preset.mFrame >= 4)
    {
        Entity* e = SpawnSpikeRoller(kBouncySpikeRoller, preset, preset.mFrame % 4);
        if (!e)
        {
            return nullptr;
        }
        SpikeRollerData& d = e->Data<SpikeRollerData>();
        d.mCollisionOn = true;
        e->mT->mVelX = d.mVelX * 2;
        e->mT->mVelY = d.mVelY * 2;
        e->mB->mMapInteraction = false;
        e->mB->mImmovable = false;
        Ecs_Play(*e, "roll");
        RollerChangedVelocity(*e);
        return e;
    }
    Entity* e = SpawnSpikeRoller(kFallingSpikeRoller, preset, preset.mFrame);
    if (!e)
    {
        return nullptr;
    }
    SpikeRollerData& d = e->Data<SpikeRollerData>();
    const bool horizontal = preset.mFrame == 0 || preset.mFrame == 3;
    Entity* shadow = Ecs_Create(kRollerShadow, e->mT->mX - e->mV->mOffX, e->mT->mY - e->mV->mOffY, PART_VISUAL,
                                nullptr, e);
    if (!shadow)
    {
        return nullptr;
    }
    Ecs_SetSheet(*shadow, horizontal ? "spike_roller_horizontal_shadow" : "spike_roller_shadow", horizontal ? 128 : 16,
                 horizontal ? 16 : 128);
    shadow->mB->mW = static_cast<int16_t>(horizontal ? 128 : 16);
    shadow->mB->mH = static_cast<int16_t>(horizontal ? 16 : 128);
    shadow->mV->mLayer = LAYER_ENTITIES;
    shadow->mV->mVisible = false;
    d.mShadow = shadow;
    e->mV->mVisible = false;
    e->mV->mOffY = Fx(80);
    e->mV->mLayer = LAYER_FG_SPRITES;
    d.mInitX = e->mT->mX;
    d.mInitY = e->mT->mY;
    d.mState = ROLLER_HIDDEN;
    return e;
}

Entity* Spawn_Rat(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kRat, preset, "rat");
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrameBase(*e, CellFrames(6));
    e->mB->mW = e->mB->mH = 12;
    Ecs_CenterOffset(*e);
    // (and Position += offset once more)
    e->mT->mX += e->mV->mOffX;
    e->mT->mY += e->mV->mOffY;
    e->mFacing = FACE_DOWN;
    e->mT->mVelY = Fx(40);
    Entity* la = Ecs_Create(kRatLookahead, 0, 0, PART_BRAIN, nullptr, e);
    if (!la)
    {
        return nullptr;
    }
    la->mB->mW = la->mB->mH = 1;
    e->Data<RatData>().mLookahead = la;
    RatPlaceLookahead(*e); // (its lastPosition stays (0, 0), where it was made)
    return e;
}

Entity* Spawn_Silverfish(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSilverfish, preset, "silverfish");
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetFrameBase(*e, CellFrames(6));
    static const Facing kFacing[4] = { FACE_LEFT, FACE_DOWN, FACE_RIGHT, FACE_UP };
    e->mFacing = kFacing[preset.mFrame < 3 ? preset.mFrame : 3];
    PlayFacingFlip(*e, "idle");
    e->Data<SilverfishData>() = SilverfishData();
    return e;
}

Entity* Spawn_GasGuy(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kGasGuy, preset, "gas_guy", 16, 24);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = 16;
    e->mB->mH = 24;
    e->mT->mDragX = e->mT->mDragY = Fx(30);
    GasGuyData& d = e->Data<GasGuyData>();
    d = GasGuyData();
    d.mHealth = 3;
    d.mSoundDelayMax = FxF(1.5f) + Rng_Unit();
    for (Entity*& g : d.mGas)
    {
        g = Ecs_Create(kGas, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!g)
        {
            return nullptr;
        }
        Ecs_SetSheet(*g, "gas_guy_cloud", 24, 24);
        Ecs_Play(*g, "move");
        g->mV->mLayer = LAYER_FG_SPRITES;
        g->mV->mOffX = g->mV->mOffY = Fx(4);
        g->mB->mW = g->mB->mH = 16;
        Ecs_KeepPooled(*g);
    }
    d.mState = GG_LATENCY;
    GasGuyEnter(*e, GG_LATENCY);
    return e;
}

Entity* Spawn_TeleGuy(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kTeleGuy, preset, "teleport_guy", 16, 24);
    if (!e)
    {
        return nullptr;
    }
    e->mV->mOffY = Fx(6);
    e->mB->mW = 16;
    e->mB->mH = 16;
    e->mB->mImmovable = true;
    TeleGuyData& d = e->Data<TeleGuyData>();
    d = TeleGuyData();
    d.mState = TG_IDLE;
    return e;
}

Entity* Spawn_SplitBoss(const MapEntity& preset, Entity&)
{
    // (the SubEntities, bullets then copies, are made first: field initialisers)
    Entity* e = Spawn_Basic(kSplitBoss, preset, "splitboss", 24, 32);
    if (!e)
    {
        return nullptr;
    }
    SplitBossData& d = e->Data<SplitBossData>();
    d = SplitBossData();
    for (Entity*& b : d.mBullets)
    {
        b = Ecs_Create(kSplitBullet, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!b)
        {
            return nullptr;
        }
        Ecs_SetSheet(*b, "splitboss_fireball", 16, 16);
        b->mV->mLayer = LAYER_BG_ENTITIES;
        b->mB->mW = b->mB->mH = 6;
        Ecs_CenterOffset(*b);
        Ecs_KeepPooled(*b);
    }
    for (int i = 0; i < 2; i++)
    {
        Entity* c = Ecs_Create(kSplitCopy, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        d.mCopies[i] = c;
        if (!c)
        {
            return nullptr;
        }
        Ecs_SetSheet(*c, "splitboss", 24, 32);
        c->mB->mW = 16;
        c->mB->mH = 20;
        Ecs_CenterOffset(*c);
        Ecs_SetSolid(*c, false);
        c->mB->mImmovable = true;
        c->mV->mHFlip = i == 0;
        c->Data<SplitCopyData>().mDamagePlayer = false;
        Ecs_KeepPooled(*c);
    }
    e->mB->mW = 16;
    e->mB->mH = 20;
    Ecs_CenterOffset(*e);
    Ecs_SetSolid(*e, false);
    e->mB->mImmovable = true;
    // (the screen being loaded: the boss's own)
    d.mTlX = Fx(preset.mX / 160 * 160 + 32);
    d.mTlY = Fx(preset.mY / 160 * 160 + 48);
    e->mT->mX = d.mTlX + Fx(48 - 16 / 2);
    e->mT->mY = d.mTlY + Fx(32);
    Player_SetGridEntranceAt(d.mTlX + Fx(42), d.mTlY + Fx(40));
    d.mHealth = 13;
    d.mBossRush = strcmp(Preset_TypeValue(preset), "boss_rush") == 0;
    SplitStart(d, SB_INTRO);
    return e;
}

Entity* Spawn_Dustmaid(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kDustmaid, preset, "dustmaid", 16, 24);
    if (!e)
    {
        return nullptr;
    }
    e->mV->mOpacity = FxF(0.7f);
    e->mB->mW = 8;
    e->mB->mH = 18;
    Ecs_CenterOffset(*e);
    e->mV->mOffX += Fx(1);
    e->mV->mOffY += Fx(2);
    DustmaidData& d = e->Data<DustmaidData>();
    d = DustmaidData();
    d.mHealth = 3;
    return e;
}

Entity* Spawn_SteamPipe(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSteamPipe, preset, "steam_pipe");
    if (!e)
    {
        return nullptr;
    }
    const int frame = preset.mFrame;
    Ecs_SetFrame(*e, frame);
    int offX = 0;
    int offY = 0;
    int startX = 0;
    int startY = 0;
    switch (frame)
    {
    case 0:
        e->mB->mW = 10;
        offX = 3;
        offY = 16;
        startX = 2;
        startY = 10;
        e->mFacing = FACE_DOWN;
        break;
    case 1:
        e->mB->mH = 10;
        offX = 14;
        offY = 5;
        startX = 14;
        startY = 5;
        e->mFacing = FACE_RIGHT;
        break;
    case 2:
        e->mB->mW = 10;
        offX = 3;
        offY = -4;
        startX = 3;
        startY = -2;
        e->mFacing = FACE_UP;
        break;
    default:
        e->mB->mH = 10;
        offX = -6;
        offY = 3;
        startX = -6;
        startY = 3;
        e->mFacing = FACE_LEFT;
        break;
    }
    const fx sx = e->mT->mX + Fx(startX);
    const fx sy = e->mT->mY + Fx(startY);
    e->mV->mOffX = Fx(offX);
    e->mV->mOffY = Fx(offY);
    e->mT->mX += Fx(offX);
    e->mT->mY += Fx(offY);
    SteamPipeData& d = e->Data<SteamPipeData>();
    d = SteamPipeData();
    for (Entity*& s : d.mSteam)
    {
        s = Ecs_Create(kSteam, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!s)
        {
            return nullptr;
        }
        Ecs_SetSheet(*s, "steam", 16, 16);
        Ecs_Play(*s, "s");
        s->mV->mLayer = LAYER_FG_SPRITES;
        Ecs_SetSolid(*s, false);
        const fx scale = Fx(Rng_Range(8, 10)) / 10;
        s->mV->mScale = scale;
        s->mB->mW = s->mB->mH = static_cast<int16_t>(FxTrunc(16 * scale));
        s->mFacing = e->mFacing;
        SteamData& sd = s->Data<SteamData>();
        sd = SteamData();
        sd.mStartX = sx;
        sd.mStartY = sy;
    }
    // _steam.Spawn(s => s.Spawn(), 6)
    for (Entity* s : d.mSteam)
    {
        SteamSpawn(*s);
    }
    return e;
}

Entity* Spawn_EyebossPreview(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kEyePreview, Fx(preset.mX), Fx(preset.mY), PART_VISUAL | PART_BRAIN, &preset);
    if (!e)
    {
        return nullptr;
    }
    EyePreviewData& d = e->Data<EyePreviewData>();
    d = EyePreviewData();
    if (!MakeEyeParts(*e, d.mPupil, d.mEyelids))
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    d.mPupil->mT->mX = e->mT->mX + Fx(7);
    d.mPupil->mT->mY = e->mT->mY + Fx(8);
    e->mV->mOpacity = d.mPupil->mV->mOpacity = d.mEyelids->mV->mOpacity = 0;
    if (Game_BossDefeated("HOTEL") || (Event_Get("EyebossPreviewPlayed") > 0 && Rng_NextDouble() > 0.3))
    {
        e->mExists = d.mPupil->mExists = d.mEyelids->mExists = false;
    }
    else
    {
        Event_Inc("EyebossPreviewPlayed");
    }
    return e;
}

Entity* Spawn_EyeBossWater(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kWaterPhase, Fx(preset.mX), Fx(preset.mY), PART_VISUAL | PART_BRAIN, &preset);
    if (!e)
    {
        return nullptr;
    }
    WaterPhaseData& d = e->Data<WaterPhaseData>();
    d = WaterPhaseData();
    if (!MakeEyeParts(*e, d.mPupil, d.mEyelids))
    {
        return nullptr;
    }
    e->mV->mLayer = LAYER_BG_ENTITIES;
    d.mPupil->mT->mX = e->mT->mX + Fx(7);
    d.mPupil->mT->mY = e->mT->mY + Fx(8);
    d.mMarker = MakeEnemyMarker(*e);
    if (!d.mMarker)
    {
        return nullptr;
    }
    d.mHealth = 6;
    if (Game_BossDefeated(gGame.mMapName) || Event_Get("BossRushLandDefeated") == 1)
    {
        // the whole boss is beaten: both gates open
        e->mExists = false;
        d.mMarker->mExists = false;
        gGame.mPuzzlesSolved++;
        EyePartsFollow(*e, *d.mPupil, *d.mEyelids);
        return e;
    }
    if (strcmp(Game_CurrentSong(), "hotel-boss") == 0)
    {
        // back up from the land phase's screen
        Game_IgnoreMusicNextUpdate();
        gGame.mPuzzlesSolved++;
        Preset_SetAlive(preset, false);
        e->mExists = false;
        EyePartsFollow(*e, *d.mPupil, *d.mEyelids);
        return e;
    }
    Preset_SetAlive(preset, true);
    // (the screen being loaded: the boss's own)
    d.mTlX = Fx(preset.mX / 160 * 160 + 16);
    d.mTlY = Fx(preset.mY / 160 * 160 + 16);
    Player_SetGridEntranceAt(d.mTlX + Fx(80), d.mTlY + Fx(20));
    const Rect area = { FxTrunc(d.mTlX), FxTrunc(d.mTlY), 16 * 8, 16 * 9 };
    for (Entity*& b : d.mBullets)
    {
        b = Ecs_Create(kEyeWaterBullet, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!b)
        {
            return nullptr;
        }
        Ecs_SetSheet(*b, "eye_boss_bullet", 16, 16);
        b->mV->mLayer = LAYER_FG_SPRITES;
        b->mB->mW = b->mB->mH = 8;
        Ecs_CenterOffset(*b);
        b->Data<EyeWaterBulletData>().mArea = area;
        Ecs_KeepPooled(*b);
    }
    d.mBossRush = strcmp(Preset_TypeValue(preset), "boss_rush") == 0;
    return e;
}

Entity* Spawn_EyeBossLand(const MapEntity& preset, Entity&)
{
    Entity* e = Ecs_Create(kLandPhase, Fx(preset.mX), Fx(preset.mY), PART_VISUAL | PART_BRAIN, &preset);
    if (!e)
    {
        return nullptr;
    }
    Ecs_SetSheet(*e, "eye_boss_water", 24, 24);
    e->mB->mW = e->mB->mH = 24;
    LandPhaseData& d = e->Data<LandPhaseData>();
    d = LandPhaseData();
    // (the water phase starts the boss song, and keeps its preset alive until it is beaten)
    const MapEntity* water = EyeBossOtherPhase(preset);
    const bool waterDead = water && !Preset_Alive(*water);
    if (strcmp(Game_CurrentSong(), "hotel-boss") != 0 || !waterDead || Event_Get("BossRushLandDefeated") == 1)
    {
        // (no sub-entities, so no marker; the C# sets ENEMIES_KILLED itself so the key shows)
        gGame.mEnemiesKilled = 1;
        e->mExists = false;
        return e;
    }
    Game_IgnoreMusicNextUpdate(); // the music stays if the player goes back up
    d.mTlX = Fx(preset.mX / 160 * 160 + 16);
    d.mTlY = Fx(preset.mY / 160 * 160 + 16);
    Player_SetGridEntranceAt(d.mTlX + Fx(70), d.mTlY + Fx(50));
    d.mShadow = Ecs_Create(kEntityShadow, e->mT->mX, e->mT->mY + Fx(14), PART_VISUAL, nullptr, e);
    d.mMarker = MakeEnemyMarker(*e);
    if (!d.mShadow || !d.mMarker)
    {
        return nullptr;
    }
    Ecs_SetSheet(*d.mShadow, "28x10_shadow", 28, 10);
    d.mShadow->mV->mLayer = LAYER_SHADOWS;
    d.mShadow->mV->mVisible = false;
    Ecs_SetSolid(*d.mShadow, false);
    // (the bullets come before the splashes in SubEntities; the splashes, made first, roll their arcs' periods)
    auto makeArc = [e](Entity*& out, const EntityClass& cls, const char* sheet, int size, int w) -> bool {
        out = Ecs_Create(cls, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        Entity* shadow = out ? Ecs_Create(kEntityShadow, 0, 0, PART_VISUAL, nullptr, out) : nullptr;
        if (!shadow)
        {
            return false;
        }
        Ecs_SetSheet(*out, sheet, size, size);
        out->mV->mLayer = LAYER_FG_SPRITES;
        out->mB->mW = out->mB->mH = static_cast<int16_t>(w);
        Ecs_SetSheet(*shadow, "8x8_shadow", 8, 8);
        shadow->mV->mLayer = LAYER_SHADOWS;
        Ecs_SetSolid(*shadow, false);
        EyeArcData& ad = out->Data<EyeArcData>();
        ad = EyeArcData();
        ad.mShadow = shadow;
        Ecs_KeepPooled(*out);
        Ecs_KeepPooled(*shadow);
        return true;
    };
    for (Entity*& b : d.mBullets)
    {
        if (!makeArc(b, kEyeLandBullet, "eye_boss_bullet", 16, 8))
        {
            return nullptr;
        }
        Ecs_CenterOffset(*b);
        ParabolaInit(b->Data<EyeArcData>().mArc, *b, Fx(45), FxF(1.5f));
    }
    for (Entity*& s : d.mSplashes)
    {
        if (!makeArc(s, kEyeSplash, "eye_boss_splash", 8, 8))
        {
            return nullptr;
        }
        ParabolaInit(s->Data<EyeArcData>().mArc, *s, Fx(36), kFxOne + FxMul(FxF(0.4f), Rng_Unit()));
    }
    d.mHealth = 6;
    if (Preset_Activated(preset))
    {
        Ecs_Play(*e, "walk");
        e->mT->mX = d.mTlX + Fx(16);
        e->mT->mY = d.mTlY + Fx(75);
        LandStart(d, LP_LAND);
    }
    else
    {
        LandStart(d, LP_TO_LAND);
        LandToLandStep(*e, d); // the initial setup
    }
    d.mBossRush = strcmp(Preset_TypeValue(preset), "boss_rush") == 0;
    return e;
}

Entity* Spawn_SpaceFace(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSpaceFace, preset, "space_npcs", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    e->mV->mLayer = LAYER_BG_ENTITIES;
    Ecs_Play(*e, preset.mFrame == 0 ? "a@2" : "a");
    SpaceFaceData& d = e->Data<SpaceFaceData>();
    d = SpaceFaceData();
    d.mWaitMax = FxF(0.5f) + FxMul(FxF(1.5f), Rng_Unit());
    return e;
}

Entity* Spawn_Chaser(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kChaser, preset, "chaser", 16, 32);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 8;
    e->mV->mOffX = Fx(4);
    e->mV->mOffY = Fx(20);
    e->mT->mX += Fx(4);
    e->mT->mY += Fx(20);
    ChaserData& d = e->Data<ChaserData>();
    d = ChaserData();
    d.mHorizontal = preset.mFrame == 0;
    Ecs_Play(*e, d.mHorizontal ? "face_r" : "face_d");
    return e;
}

Entity* Spawn_FirePillar(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kFirePillar, preset, "fire_pillar", 16, 32);
    if (!e)
    {
        return nullptr;
    }
    Entity* base = Ecs_Create(kPillarBase, Fx(preset.mX), Fx(preset.mY + 16), PART_VISUAL, nullptr, e);
    if (!base)
    {
        return nullptr;
    }
    Ecs_SetSheet(*base, "fire_pillar_base", 16, 16);
    base->mV->mLayer = LAYER_VERY_BG_ENTITIES;
    Ecs_SetSolid(*base, false);
    Ecs_Play(*base, "dormant");
    e->mB->mH = 9;
    e->mV->mOffY = Fx(16);
    e->mT->mY += Fx(16);
    FirePillarData& d = e->Data<FirePillarData>();
    d = FirePillarData();
    d.mTimers[PILLAR_IDLE].Add(FxSec(0.74f), 0);
    d.mTimers[PILLAR_EMERGE].Add(FxSec(0.3f), 0);
    d.mTimers[PILLAR_FLAME].Add(FxSec(1.0f), 0);
    d.mTimers[PILLAR_RECEDE].Add(FxSec(0.3f), 0);
    PillarEnter(*e, PILLAR_IDLE);
    return e;
}

Entity* Spawn_Contort(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kContort, preset, "contort_big", 16, 32);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 12;
    e->mV->mOffX = Fx(2);
    e->mV->mOffY = Fx(20);
    Ecs_Play(*e, "move");
    ContortData& d = e->Data<ContortData>();
    d = ContortData();
    d.mDeathTimer = Fx(1);
    d.mHealth = 3;
    e->mT->mVelX = Fx(Rng_NextDouble() > 0.5 ? 35 : -35);
    e->mT->mVelY = Fx(Rng_NextDouble() > 0.5 ? 35 : -35);
    static const int kStartFrames[kContortSmalls] = { 0, 2, 4 };
    static const int kHeights[kContortSmalls] = { 0, 2, 1 };
    for (int i = 0; i < kContortSmalls; i++)
    {
        Entity* s = Ecs_Create(kContortSmall, e->mT->mX, e->mT->mY, PART_VISUAL | PART_BRAIN, nullptr, e);
        Entity* shadow = s ? Ecs_Create(kEntityShadow, 0, 0, PART_VISUAL, nullptr, s) : nullptr;
        if (!shadow)
        {
            return nullptr;
        }
        s->mB->mW = s->mB->mH = 16;
        Ecs_SetSheet(*s, "contort_small", 16, 16);
        Ecs_SetFrameBase(*s, kStartFrames[i]);
        Ecs_Play(*s, "move");
        Ecs_SetSheet(*shadow, "8x8_shadow", 8, 8);
        shadow->mV->mLayer = LAYER_SHADOWS;
        Ecs_SetSolid(*shadow, false);
        ContortSmallData& sd = s->Data<ContortSmallData>();
        sd = ContortSmallData();
        ParabolaInit(sd.mArc, *s, Fx(16 + 8 * kHeights[i]), FxF(1.2f));
        sd.mShadow = shadow;
        s->mT->mVelX = fx(ContortRandomVel());
        s->mT->mVelY = fx(ContortRandomVel());
        Ecs_Flicker(*s, FxSec(1.0f));
        Ecs_KeepPooled(*s);
        Ecs_KeepPooled(*shadow);
        d.mSmalls[i] = s;
    }
    return e;
}

Entity* Spawn_Lion(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kLion, preset, "lion", 32, 32);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mW = e->mB->mH = 32;
    LionData& d = e->Data<LionData>();
    d = LionData();
    d.mHealth = 4;
    for (int i = 0; i < LION_DYING; i++)
    {
        d.mAt[i] = kLionTimerMax[i];
    }
    ParabolaInit(d.mArc, *e, Fx(12), kFxOne);
    for (int i = 0; i < kLionFireballs; i++)
    {
        Entity* b = Ecs_Create(kLionFireball, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!b)
        {
            return nullptr;
        }
        Ecs_SetSheet(*b, "lion_fireballs", 16, 16);
        b->mV->mLayer = LAYER_FG_SPRITES;
        b->mV->mFpsOverride = 10;
        b->mB->mW = b->mB->mH = 8;
        b->mV->mOffX = b->mV->mOffY = Fx(4);
        b->Data<LionFireballData>() = LionFireballData();
        Ecs_KeepPooled(*b);
    }
    d.mShadowV = Ecs_Create(kEntityShadow, 0, 0, PART_VISUAL, nullptr, e);
    d.mShadowH = Ecs_Create(kEntityShadow, 0, 0, PART_VISUAL, nullptr, e);
    if (!d.mShadowV || !d.mShadowH)
    {
        return nullptr;
    }
    Ecs_SetSheet(*d.mShadowV, "10x28_shadow", 10, 28);
    Ecs_SetSheet(*d.mShadowH, "28x10_shadow", 28, 10);
    Entity* const shadows[2] = { d.mShadowV, d.mShadowH };
    for (Entity* s : shadows)
    {
        s->mV->mLayer = LAYER_SHADOWS;
        Ecs_SetSolid(*s, false);
        Ecs_KeepPooled(*s);
    }
    d.mState = LION_PACE;
    return e;
}

Entity* Spawn_ArthurDanger(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kArthurDanger, preset, "arthur", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    Ecs_Play(*e, "wobble");
    FolksData& d = e->Data<FolksData>();
    d = FolksData();
    ParabolaInit(d.mArc, *e, Fx(32), kFxOne);
    d.mShadow = Ecs_Create(kEntityShadow, 0, 0, PART_VISUAL, nullptr, e);
    if (!d.mShadow)
    {
        return nullptr;
    }
    Ecs_SetSheet(*d.mShadow, "8x8_shadow", 8, 8);
    d.mShadow->mV->mLayer = LAYER_SHADOWS;
    Ecs_SetSolid(*d.mShadow, false);
    e->mT->mY -= Fx(32);
    e->mV->mOffY = Fx(5 * 16);
    d.mInitX = e->mT->mX;
    d.mInitY = e->mT->mY;
    d.mShadow->mT->mX = e->mT->mX + Fx(4);
    d.mShadow->mT->mY = e->mT->mY + Fx(6);
    // (the room it's in: GlobalState.CurrentMapGrid isn't the new screen yet while it scrolls in)
    const fx roomX = Fx(preset.mX) - InGrid(Fx(preset.mX));
    const fx roomY = Fx(preset.mY) - InGrid(Fx(preset.mY));
    d.mPillow = Spawn_DustAt(roomX + Fx(46), roomY + Fx(16));
    if (!d.mPillow)
    {
        return nullptr;
    }
    return e;
}

Entity* Spawn_JavieraDanger(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kJavieraDanger, preset, "javiera_juggle", 16, 24);
    if (!e)
    {
        return nullptr;
    }
    e->mB->mH = 24;
    Ecs_Play(*e, "juggle");
    FolksData& d = e->Data<FolksData>();
    d = FolksData();
    d.mInitX = e->mT->mX;
    d.mInitY = e->mT->mY;
    return e;
}

Entity* Spawn_ArthurDead(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kArthurDead, preset, "arthur", 16, 16);
    if (e)
    {
        Ecs_SetFrame(*e, 18);
    }
    return e;
}

Entity* Spawn_JavieraDead(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kJavieraDead, preset, "javiera", 16, 16);
    if (e)
    {
        Ecs_SetFrame(*e, 12);
    }
    return e;
}

Entity* Spawn_CircusFolks(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kCircusFolks, preset, "arthur_javiera", 16, 32);
    if (!e)
    {
        return nullptr;
    }
    CircusFolksData& d = e->Data<CircusFolksData>();
    d = CircusFolksData();
    d.mTopLeftX = Fx(preset.mX) - InGrid(Fx(preset.mX)) + Fx(16);
    d.mTopLeftY = Fx(preset.mY) - InGrid(Fx(preset.mY)) + Fx(16);
    d.mEntranceY = d.mTopLeftY + Fx(16 * 7 + 4);
    Player_SetGridEntranceAt(d.mTopLeftX + Fx(75), d.mEntranceY);
    e->mB->mH = 16;
    e->mV->mOffY = Fx(16);
    auto part = [&](const EntityClass& cls, const char* sheet, fx x, const char* anim) -> Entity* {
        Entity* p = Ecs_Create(cls, x, d.mTopLeftY, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!p)
        {
            return nullptr;
        }
        p->mB->mW = p->mB->mH = 16;
        Ecs_SetSheet(*p, sheet, 16, 16);
        Ecs_Play(*p, anim);
        p->mKeep = true; // (pooled: in and out of existence for the whole fight)
        CircusPartData& pd = p->Data<CircusPartData>();
        pd = CircusPartData();
        pd.mHurts = true;
        return p;
    };
    d.mArthur = part(kCircusArthur, "arthur", d.mTopLeftX + Fx(64 - 8 - 16), "wobble");
    d.mJaviera = part(kCircusJaviera, "javiera", d.mTopLeftX + Fx(64 + 8), "juggle");
    if (!d.mArthur || !d.mJaviera)
    {
        return nullptr;
    }
    e->mV->mVisible = false;
    ParabolaInit(d.mArc, *e, Fx(48), kFxOne);
    d.mShadow = Ecs_Create(kEntityShadow, e->mT->mX + Fx(4), e->mT->mY + Fx(8), PART_VISUAL, nullptr, e);
    if (!d.mShadow)
    {
        return nullptr;
    }
    Ecs_SetSheet(*d.mShadow, "8x8_shadow", 8, 8);
    d.mShadow->mV->mLayer = LAYER_SHADOWS;
    d.mShadow->mV->mVisible = false;
    Ecs_SetSolid(*d.mShadow, false);
    for (int i = 0; i < 8; i++)
    {
        Entity* s = Ecs_Create(kShockWave, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
        if (!s)
        {
            return nullptr;
        }
        s->mB->mW = s->mB->mH = 16;
        Ecs_SetSheet(*s, "shockwave", 16, 16);
        s->mV->mVisible = false;
        Ecs_KeepPooled(*s);
    }
    d.mHealth = 7;
    d.mStep = CF_INTRO_WAIT_PLAYER;
    d.mBossRush = strcmp(Preset_TypeValue(preset), "boss_rush") == 0;
    return e;
}

Entity* Spawn_SageBoss(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kSageBoss, preset, "sage_boss", 16, 24);
    if (!e)
    {
        return nullptr;
    }
    SageBossData& d = e->Data<SageBossData>();
    d = SageBossData();
    d.mTopLeftX = Fx(preset.mX) - InGrid(Fx(preset.mX));
    d.mTopLeftY = Fx(preset.mY) - InGrid(Fx(preset.mY));
    e->mB->mW = e->mB->mH = 10;
    e->mV->mOffX = Fx(3);
    e->mV->mOffY = Fx(8);
    e->mT->mX = d.mTopLeftX + Fx(72 + 3);
    e->mT->mY = Fx(preset.mY) + Fx(3);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    auto pool = [&](Entity* (&out)[kSageBullets], const EntityClass& cls, const char* sheet, int w, int h, int bw,
                    int bh, int ox, int oy) {
        for (Entity*& b : out)
        {
            b = Ecs_Create(cls, 0, 0, PART_VISUAL | PART_BRAIN, nullptr, e);
            if (!b)
            {
                return false;
            }
            Ecs_SetSheet(*b, sheet, w, h);
            b->mB->mW = bw;
            b->mB->mH = bh;
            b->mV->mOffX = Fx(ox);
            b->mV->mOffY = Fx(oy);
            b->mV->mVisible = false;
            b->Data<SageAttackData>() = SageAttackData();
            Ecs_KeepPooled(*b);
        }
        return true;
    };
    if (!pool(d.mLong, kSageLongAttack, "sage_fight_long_dust", 64, 16, 56, 10, 4, 3) ||
        !pool(d.mShort, kSageShortAttack, "sage_attacks", 16, 16, 8, 8, 4, 4))
    {
        return nullptr;
    }
    e->mB->mImmovable = true;
    d.mStep = SGB_INTRO;
    d.mBossRush = strcmp(Preset_TypeValue(preset), "boss_rush") == 0;
    return e;
}

Entity* Spawn_BriarBoss(const MapEntity& preset, Entity&)
{
    Entity* e = Spawn_Basic(kBriarMain, preset, "briar", 16, 16);
    if (!e)
    {
        return nullptr;
    }
    BriarMainData& d = e->Data<BriarMainData>();
    d = BriarMainData();
    d.mTopLeftX = Fx(preset.mX) - InGrid(Fx(preset.mX));
    d.mTopLeftY = Fx(preset.mY) - InGrid(Fx(preset.mY));
    e->mT->mX = d.mTopLeftX + Fx(80 - 16 / 2);
    e->mT->mY = d.mTopLeftY + Fx(32);
    e->mT->mLastX = e->mT->mX;
    e->mT->mLastY = e->mT->mY;
    Ecs_Play(*e, "idle");
    d.mBossRush = strcmp(Preset_TypeValue(preset), "boss_rush") == 0;
    d.mVolume = true;
    Entity* gate = Ecs_Create(kThornGate, d.mTopLeftX + Fx(64), d.mTopLeftY, PART_VISUAL, &preset, e);
    if (!gate)
    {
        return nullptr;
    }
    Ecs_SetSheet(*gate, "briar_ground_thorn", 32, 16);
    gate->mB->mW = 32;
    gate->mB->mH = 16;
    gate->mB->mImmovable = true;
    Ecs_Play(*gate, "move");
    d.mGate = gate;
    if (!d.mBossRush && (Event_Get("HappyDone") == 0 || Event_Get("BlueDone") == 0))
    {
        e->mExists = false;
        d.mVolume = false;
    }
    return e;
}
