// Enemies (AnodyneSharp Entities/Enemy): the ones met before the first boss.
#include "entity_classes.h"

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

// HealthDropper.Die: the preset stays dead, and maybe a heart drops where it was.
void Die(Entity& self, float dropChance, bool big = false)
{
    if (self.mPreset)
    {
        Preset_SetAlive(*self.mPreset, false);
    }
    self.mExists = false;
    if (Rng_NextDouble() < dropChance)
    {
        // (the C#'s pickup is the dropper's sub-entity, just after it in the lists)
        if (Entity* drop = Spawn_HealthDrop(self.mT->mX, self.mT->mY, big))
        {
            Ecs_MoveAfter(*drop, self);
        }
    }
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
    Ecs_Play(self, "Move");
    d.mMove.Advance(Game_SecondsSince(d.mExitFrame));
    if (d.mBullet)
    {
        d.mMove.Add(1.8f, SLIME_SHOOT_TIMER);
    }
}

void SlimeUpdate(Entity& self)
{
    SlimeData& d = self.Data<SlimeData>();
    switch (d.mState)
    {
    case SLIME_MOVE:
        d.mMove.Tick(kDeltaTime);
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
            Ecs_Play(self, "Dying");
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
            Die(self, 0.5f);
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
        Ecs_Play(self, "Hurt");
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
        Die(self, 0.5f);
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
        d.mWait.Add(0.25f, ANNOYER_APPROACH_CHECK);
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
        d.mHit.Tick(kDeltaTime);
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
                Ecs_Play(*ex, "explode");
            }
            Game_PlaySfx("hit_wall");
        }
        Ecs_BaseUpdate(self);
        return;
    }
    // ActiveState: its Fire timer, then the child state
    d.mActive.Tick(kDeltaTime);
    while (d.mActive.Pop() >= 0)
    {
        AnnoyerFire(self);
    }
    switch (d.mState)
    {
    case ANNOYER_WAIT:
        d.mWait.Tick(kDeltaTime);
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
        d.mCircle.Tick(kDeltaTime);
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
            d.mHit.Add(0.25f, ANNOYER_DIE);
        }
        else
        {
            d.mHit.Add(0.4f, ANNOYER_END_KNOCKBACK);
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
        Die(self, 0.5f);
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

const fx kDogAttackSpeeds[5] = { FxF(66.0f), FxF(84.0f), FxF(84.0f), FxF(96.0f), FxF(102.0f) };

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
        d.mPace.Add(1.0f, 0);
        t.mVelX = t.mVelY = 0;
    }
    else if (state == DOG_ALERT)
    {
        d.mAlert.Reset();
        d.mAlert.Add(1.0f, 0);
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
        d.mPace.Tick(kDeltaTime);
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
        d.mAlert.Tick(kDeltaTime);
        while (d.mAlert.Pop() >= 0)
        {
            DogEnter(self, DOG_ATTACK);
        }
        break;
    default:
        d.mAttack.Tick(kDeltaTime);
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
            Die(self, 1.0f);
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
void BurstBulletStart(Entity& b, const Entity& plant, float period)
{
    BurstBulletData& d = b.Data<BurstBulletData>();
    b.mExists = true;
    b.mT->mX = plant.mT->mX + Fx(5);
    b.mT->mY = plant.mT->mY + Fx(5);
    d.mHeight = Rng_Range(24, 34);
    d.mPeriod = fx(period >= 0 ? period : 0.7f + static_cast<float>(Rng_NextDouble()));
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
        d.mIdle.Add(0.4f + static_cast<float>(Rng_NextDouble()), 0);
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
    const float maxVel = 10 + 30 * static_cast<float>(Rng_NextDouble());
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
            BurstBulletStart(*b, self, 1.5f);
            Ecs_MoveTowards(*b, Ecs_CenterX(p), Ecs_CenterY(p), fx(maxVel * 2.5f));
        }
        else
        {
            BurstBulletStart(*b, self, -1);
            const float vx = -maxVel + 2 * maxVel * static_cast<float>(Rng_NextDouble());
            b->mT->mVelX = fx(vx);
            b->mT->mVelY = fx(sign * sqrtf(maxVel * maxVel - vx * vx));
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
        d.mInitial.Tick(kDeltaTime);
        while (d.mInitial.Pop() >= 0)
        {
            BurstEnter(self, BURST_IDLE);
        }
        break;
    case BURST_IDLE:
        d.mIdle.Tick(kDeltaTime);
        while (d.mIdle.Pop() >= 0)
        {
            BurstEnter(self, BURST_CHARGING);
        }
        break;
    case BURST_CHARGING:
        d.mCharging.Tick(kDeltaTime);
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
        Die(self, 0.7f);
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
        d.mInitial.Tick(kDeltaTime);
        while (d.mInitial.Pop() >= 0)
        {
            d.mState = FROG_IDLE;
            Ecs_Play(self, "idle");
        }
        break;
    case FROG_IDLE:
    {
        const Entity& p = *World_Player();
        const float dx = (Ecs_CenterX(self) - Ecs_CenterX(p)).mV;
        const float dy = (Ecs_CenterY(self) - Ecs_CenterY(p)).mV;
        if (dx * dx + dy * dy < 64 * 64)
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
                Die(self, 0.7f);
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
        const float dx = (d.mIdleX - t.mX).mV;
        const float dy = (d.mIdleY - t.mY).mV;
        if (dx * dx + dy * dy < 4)
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
constexpr float kFourShooterTime = 1.5f;

struct FourShooterData final
{
    float mTimer;
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
void FourShooterFire(FourShooterData& d, float cx, float cy, float dx, float dy)
{
    for (Entity* b : d.mBullets)
    {
        if (!b || b->mExists)
        {
            continue;
        }
        const float len = sqrtf(dx * dx + dy * dy);
        dx /= len;
        dy /= len;
        b->mExists = true;
        b->mT->mX = fx(cx + dx * 8 - 2);
        b->mT->mY = fx(cy + dy * 8 - 2);
        b->mT->mVelX = fx(dx * 50);
        b->mT->mVelY = fx(dy * 50);
        Ecs_Play(*b, "move");
        return;
    }
}

void FourShooterUpdate(Entity& self)
{
    FourShooterData& d = self.Data<FourShooterData>();
    Ecs_BaseUpdate(self);
    d.mTimer -= kDeltaTime;
    if (d.mTimer <= 0)
    {
        d.mTimer = kFourShooterTime;
        Game_PlaySfx("4sht_shoot");
        const float cx = Ecs_CenterX(self).mV;
        const float cy = Ecs_CenterY(self).mV;
        if (Ecs_Frame(self) == 0)
        {
            Ecs_Play(self, "axis_to_diag");
            FourShooterFire(d, cx, cy, 1, 0);
            FourShooterFire(d, cx, cy, -1, 0);
            FourShooterFire(d, cx, cy, 0, 1);
            FourShooterFire(d, cx, cy, 0, -1);
        }
        else
        {
            Ecs_Play(self, "diag_to_axis");
            FourShooterFire(d, cx, cy, 1, 1);
            FourShooterFire(d, cx, cy, -1, -1);
            FourShooterFire(d, cx, cy, -1, 1);
            FourShooterFire(d, cx, cy, 1, -1);
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
        d.mMove.Tick(kDeltaTime);
        while (d.mMove.Pop() >= 0)
        {
            Game_PlaySfx("mover_move");
            Ecs_Play(self, "Foom");
            const Entity& p = *World_Player();
            const fx dx = p.mT->mX - self.mT->mX;
            const fx dy = p.mT->mY - self.mT->mY;
            const float len = FxLength(dx, dy).mV;
            const float normal = fminf(3.0f, 100.0f / len);
            const float rx = static_cast<float>(Rng_Range(-5, 0));
            const float ry = static_cast<float>(Rng_Range(-5, 0));
            self.mT->mVelX = fx(dx.mV * normal + rx);
            self.mT->mVelY = fx(dy.mV * normal + ry);
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
        d.mIdle.Add(2.0f, 0);
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
        d.mHurting.Add(2.0f, 0);
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
        d.mIdle.Tick(kDeltaTime);
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
        d.mHurting.Tick(kDeltaTime);
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
        timer.Add(1.4f, 0);
        break;
    case SLASHER_WARNING:
        SlasherPlayFacing(self, "warning");
        timer.Add(0.5f, 0);
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
        timer.Add(0.4f, 0);
        break;
    case SLASHER_WIDE_ATTACK_DELAY:
        timer.Add(0.3f, 0);
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
        timer.Add(0.8f, 0);
        break;
    }
}

void SlasherUpdate(Entity& self)
{
    SlasherData& d = self.Data<SlasherData>();
    Ecs_BaseUpdate(self);
    const uint8_t state = d.mState;
    TimerState<1>& timer = d.mTimers[state];
    timer.Tick(kDeltaTime);
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
            const float dx = (Ecs_CenterX(self) - Ecs_VisualCenterX(p)).mV;
            const float dy = (Ecs_CenterY(self) - Ecs_VisualCenterY(p)).mV;
            const bool close = dx * dx + dy * dy < kSlasherWideAtkDistance * kSlasherWideAtkDistance;
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
        Die(self, 1, true);
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
constexpr float kRedBossPushTick = 0.15f;

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
    float mInvincible;
    float mAmp;
    float mPushdown;
    float mPushTimer;
    float mFinalTimer;
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
    float mDropTimer;
    float mEndTimer;
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
    return fx(v.mV - static_cast<float>(static_cast<int>(v.mV / 160) * 160));
}

void BubbleSfx()
{
    static const char* const kBubbles[] = { "bubble_1", "bubble_1", "bubble_2", "bubble_3" };
    Game_PlaySfxOneOf(kBubbles, 4);
}

// Shadow.UpdateShadow (ShadowType.Normal): jumpHeight 0..1.
void UpdateEntityShadow(Entity& shadow, fx jumpHeight)
{
    const int frame = static_cast<int>(ceilf((jumpHeight * 4).mV)) - 1;
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
        d.mDropTimer = 1.0f;
        d.mState = TENT_DROP;
        [[fallthrough]];
    case TENT_DROP:
        if (!MoveTo(flair.mV->mYPush, 0, Fx(120)))
        {
            d.mDropTimer -= kDeltaTime;
            break;
        }
        d.mState = TENT_HOLD;
        [[fallthrough]];
    case TENT_HOLD:
        if (d.mDropTimer > 0)
        {
            d.mDropTimer -= kDeltaTime;
            break;
        }
        d.mEndTimer = 0.3f;
        d.mState = TENT_RAISE;
        [[fallthrough]];
    case TENT_RAISE:
        if (!MoveTo(flair.mV->mYPush, Fx(64), Fx(180)))
        {
            d.mEndTimer -= kDeltaTime;
            break;
        }
        d.mState = TENT_END;
        [[fallthrough]];
    case TENT_END:
        if (d.mEndTimer > 0)
        {
            d.mEndTimer -= kDeltaTime;
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
        d.mAmp = 5;
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
        d.mAmp = 5;
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
        d.mAmp = 13;
        d.mStun = 3;
        [[fallthrough]];
    case 3:
        if (yPush < FxF(d.mAmp))
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
        d.mAmp = 20;
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
        d.mAmp = 5;
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
        d.mSplash.Tick(kDeltaTime);
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
        d.mDash.Tick(kDeltaTime);
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
    d.mPushTimer += kDeltaTime;
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
            d.mPushdown += kDeltaTime * 3;
            v.mYPush = Fx(static_cast<int>(d.mAmp + FxSin(fx(d.mPushdown)).mV * d.mAmp));
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
        // TODO: GlobalState.wave (the screen's wobble while it sinks)
        v.mYPush = 0;
        d.mState = RB_SINK;
        [[fallthrough]];
    case RB_SINK:
        if (v.mYPush < Fx(Ecs_SpriteHeight(self)))
        {
            MoveTo(d.mRipple->mV->mOpacity, 0, FxF(0.3f));
            d.mPushTimer += kDeltaTime;
            if (d.mPushTimer >= kRedBossPushTick)
            {
                d.mPushTimer = 0;
                v.mYPush += kFxOne;
            }
            break;
        }
        d.mFinalTimer = 2.0f;
        d.mState = RB_FINAL;
        [[fallthrough]];
    case RB_FINAL:
        if (d.mFinalTimer > 0)
        {
            d.mFinalTimer -= kDeltaTime;
            break;
        }
        if (self.mPreset)
        {
            Preset_SetAlive(*self.mPreset, false);
        }
        self.mExists = false;
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
        fx volume = FxF(Game_SongVolume());
        d.mVolume = !MoveTo(volume, 0, Fx(3));
        Game_SetSongVolume(volume.mV);
    }
    d.mInvincible -= kDeltaTime;
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
        d.mInvincible = 1.3f;
        d.mHealth--;
        Game_PlaySfx("redboss_moan");
    }
}

const EntityClass kRedBoss = {
    "Red_Boss", RedBossUpdate, nullptr, RedBossCollided, nullptr, nullptr, nullptr, nullptr,
    0,          GROUP_PLAYER | GROUP_BROOM, ECF_ENEMY,
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
    d.mMove.Add(0.5f, SLIME_MOVE_TIMER);
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
    d.mCharging.Add(0.5f, 0);
    d.mState = BURST_INITIAL;
    d.mInitial.Add(0.4f, 0);
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
        bd.mPeriod = fx(static_cast<float>(0.8 + Rng_NextDouble()));
    }
    d.mState = FROG_INITIAL;
    d.mInitial.Add(0.8f, 0);
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
    d.mActive.Add(2.3f, ANNOYER_FIRE);
    d.mCircle.Add(3.0f, ANNOYER_SWOOP_TIMER);
    AnnoyerEnterActive(*e);
    Ecs_Play(*e, preset.mFrame == kAnnoyerSuper ? "flap_super" : "flap");
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
    d.mAttack.Add(1.0f, 0);
    Ecs_Play(*e, "stop");
    Entity* region = Ecs_Create(kDogRegion, e->mT->mX, e->mT->mY, 0, nullptr, e);
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
    d.mMove.Add(1.0f, 0);
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
    d.mSplash.Add(3.0f, RBT_SPLASH);
    d.mSplash.Add(3.0f, RBT_TENTACLES);
    d.mDash.Add(1.5f, RBT_TENTACLES);
    d.mDash.Add(5.0f, RBT_END_DASH);

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

// ---- Person (Crowd/Person.cs): wanders and chatters, and the player wades through them at half speed -------------
namespace
{
constexpr int kPersonSpeed = 10;

struct PersonData final
{
    float mSwitchDirMax;
    float mSwitchDir;
    float mTalk;
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
    d.mSwitchDir -= kDeltaTime;
    if (d.mSwitchDir < 0)
    {
        d.mSwitchDir = d.mSwitchDirMax;
        PersonFace(self, static_cast<Facing>(Rng_Range(0, 4)));
    }
    d.mTalk -= kDeltaTime;
    if (d.mTalk < 0)
    {
        d.mTalk = static_cast<float>(0.5 + Rng_NextDouble());
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
    d.mSwitchDirMax = 1.3f;
    d.mSwitchDir = 2.0f;
    d.mTalk = static_cast<float>(0.5 + Rng_NextDouble());
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
        d.mSwitchDirMax = 0.5f;
        PersonSetFlip(*e);
        break;
    }
    default:
        Ecs_Play(*e, "walk_d");
        break;
    }
    return e;
}
