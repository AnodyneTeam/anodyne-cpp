// Enemies (AnodyneSharp Entities/Enemy): the ones met before the first boss.
#include "entity_classes.h"

#include "player.h"

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
void Die(Entity& self, fx dropChance, bool big = false)
{
    if (self.mPreset)
    {
        Preset_SetAlive(*self.mPreset, false);
    }
    self.mExists = false;
    if (Rng_Unit() < dropChance)
    {
        Spawn_HealthDrop(self.mT->mX, self.mT->mY, big);
    }
}

// Parabola_Thing: the offset that lifts a sprite `height` px at the middle of `period` frames; true when done.
bool ParabolaTick(fx& t, fx period, int height, fx& offY)
{
    t += kFxOne;
    if (t >= period)
    {
        offY = 0;
        return true;
    }
    const int64_t half = period / 2;
    offY = static_cast<fx>(static_cast<int64_t>(t) * (period - t) * height / (half * half / kFxOne));
    return false;
}

// ---- Slime (Slime.cs) -------------------------------------------------------------------------------------------
enum SlimeState : uint8_t
{
    SLIME_MOVE,
    SLIME_HURT,
    SLIME_DYING,
};

struct SlimeData final
{
    uint8_t mState;
    bool mBullet;
    bool mSoundSync;
    int mHealth;
    fx mSpeed;
    fx mMoveTimer;
    fx mShootTimer;
};

struct GooData final
{
    bool mSplash;
    fx mT;
    fx mPeriod;
};

void GooUpdate(Entity& self)
{
    GooData& d = self.Data<GooData>();
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

void SpawnGoo(const Entity& slime)
{
    Entity* g = Ecs_Create(kGoo, slime.mT->mX, slime.mT->mY, PART_VISUAL | PART_BRAIN, nullptr,
                           const_cast<Entity*>(&slime));
    if (!g)
    {
        return;
    }
    g->mB->mW = g->mB->mH = 6;
    Ecs_SetSheet(*g, "slime_goo");
    g->mV->mLayer = LAYER_PARTICLES;
    Ecs_Play(*g, "move", true, Rng_Range(5, 10));
    GooData& d = g->Data<GooData>();
    d.mPeriod = FxMul(Fx(kFps), FxF(0.8f) + FxMul(FxF(0.3f), Rng_Unit()));
    const fx vx = Fx(10) + FxMul(Fx(5), Rng_Unit());
    const fx vy = Fx(10) + FxMul(Fx(5), Rng_Unit());
    g->mT->mVelX = Rng_Range(0, 2) ? vx : -vx;
    g->mT->mVelY = Rng_Range(0, 2) ? vy : -vy;
}

void SlimeBulletUpdate(Entity& self)
{
    Ecs_BaseUpdate(self);
    self.mV->mOpacity -= FxF(0.39f) / kFps;
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

void SlimeShoot(Entity& slime)
{
    if (Ecs_CountChildren(slime, kSlimeBullet) >= 4)
    {
        return;
    }
    Entity* b = Ecs_Create(kSlimeBullet, slime.mT->mX, slime.mT->mY, PART_VISUAL, nullptr, &slime);
    if (!b)
    {
        return;
    }
    Game_PlaySfx("slime_shoot");
    b->mB->mW = b->mB->mH = 8;
    Ecs_SetSheet(*b, "slime_bullet");
    b->mV->mLayer = LAYER_PARTICLES;
    Ecs_Play(*b, "move", true, Rng_Range(5, 10));
    const Entity& p = *World_Player();
    Ecs_MoveTowards(*b, p.mT->mX, p.mT->mY, Fx(40));
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

void SlimeUpdate(Entity& self)
{
    SlimeData& d = self.Data<SlimeData>();
    switch (d.mState)
    {
    case SLIME_MOVE:
        if (Ecs_Frame(self) == 1 && !d.mSoundSync)
        {
            Game_PlaySfx("slime_walk");
            d.mSoundSync = true;
        }
        else if (Ecs_Frame(self) == 0)
        {
            d.mSoundSync = false;
        }
        d.mMoveTimer -= kFxOne;
        if (d.mMoveTimer <= 0)
        {
            d.mMoveTimer += FxSec(0.5f);
            SlimeChangeDir(self);
        }
        if (d.mBullet)
        {
            d.mShootTimer -= kFxOne;
            if (d.mShootTimer <= 0)
            {
                d.mShootTimer += FxSec(1.8f);
                SlimeShoot(self);
            }
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
            d.mState = SLIME_MOVE;
            Ecs_Play(self, "Move");
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

struct AnnoyerData final
{
    uint8_t mState;
    bool mDying;
    int mHealth;
    fx mTimer;     // Wait's ApproachCheck, Circle's Swoop, Hit's end
    fx mFireTimer; // Active's Fire
    fx mAngle;
    fx mTargetX;
    fx mTargetY;
};

constexpr int kAnnoyerSuper = 2;

struct FireballData final
{
    bool mPoof;
};

void FireballUpdate(Entity& self)
{
    FireballData& d = self.Data<FireballData>();
    if (!d.mPoof)
    {
        self.mV->mOpacity -= FxF(0.06f) / kFps;
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

void AnnoyerEnterActive(Entity& self, uint8_t state)
{
    AnnoyerData& d = self.Data<AnnoyerData>();
    d.mState = state;
    d.mTimer = 0;
    if (state == ANNOYER_WAIT)
    {
        d.mTimer = FxSec(0.25f);
    }
    else if (state == ANNOYER_CIRCLE)
    {
        d.mAngle = 0;
        d.mTimer = FxSec(3.0f);
    }
    else if (state == ANNOYER_SWOOP)
    {
        const Entity& p = *World_Player();
        d.mTargetX = self.mT->mX + 3 * (p.mT->mX - self.mT->mX);
        d.mTargetY = self.mT->mY + 3 * (p.mT->mY - self.mT->mY);
    }
}

void AnnoyerUpdate(Entity& self)
{
    AnnoyerData& d = self.Data<AnnoyerData>();
    Transform& t = *self.mT;
    if (d.mState != ANNOYER_HIT)
    {
        d.mFireTimer -= kFxOne;
        if (d.mFireTimer <= 0)
        {
            d.mFireTimer += FxSec(2.3f);
            if (self.mPreset && self.mPreset->mFrame == kAnnoyerSuper && Ecs_CountChildren(self, kFireball) < 4)
            {
                Entity* f = Ecs_Create(kFireball, t.mX, t.mY, PART_VISUAL | PART_BRAIN, nullptr, &self);
                if (f)
                {
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
        }
    }
    switch (d.mState)
    {
    case ANNOYER_WAIT:
    {
        d.mTimer -= kFxOne;
        if (d.mTimer <= 0)
        {
            d.mTimer += FxSec(0.25f);
            const Entity& p = *World_Player();
            if (FxLength(t.mX - p.mT->mX, t.mY - p.mT->mY) < Fx(64))
            {
                AnnoyerEnterActive(self, ANNOYER_APPROACH);
            }
        }
        break;
    }
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
            t.mVelX = t.mVelY = 0;
            AnnoyerEnterActive(self, ANNOYER_CIRCLE);
        }
        break;
    }
    case ANNOYER_CIRCLE:
    {
        d.mAngle += FxF(8.4f) / kFps;
        const Entity& p = *World_Player();
        t.mX = Ecs_VisualCenterX(p) + FxMul(FxCos(d.mAngle), Fx(20));
        t.mY = Ecs_VisualCenterY(p) + FxMul(FxSin(d.mAngle), Fx(20));
        d.mTimer -= kFxOne;
        if (d.mTimer <= 0)
        {
            AnnoyerEnterActive(self, ANNOYER_SWOOP);
        }
        break;
    }
    case ANNOYER_SWOOP:
    {
        const bool x = MoveTo(t.mX, d.mTargetX, Fx(150));
        const bool y = MoveTo(t.mY, d.mTargetY, Fx(150));
        if (x && y)
        {
            AnnoyerEnterActive(self, ANNOYER_APPROACH);
        }
        break;
    }
    default:
        d.mTimer -= kFxOne;
        if (d.mTimer <= 0)
        {
            Ecs_SetSolid(self, false);
            if (d.mDying)
            {
                self.mExists = false;
                Preset_SetAlive(*self.mPreset, false);
                Entity* ex = Ecs_Create(kAnnoyerExplosion, t.mX, t.mY, PART_VISUAL);
                if (ex)
                {
                    Ecs_SetSolid(*ex, false);
                    Ecs_SetSheet(*ex, "enemy_explode_2");
                    Ecs_Play(*ex, "explode");
                }
                Game_PlaySfx("hit_wall");
            }
            else
            {
                t.mVelX = t.mVelY = 0;
                AnnoyerEnterActive(self, ANNOYER_APPROACH);
            }
        }
        break;
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
        d.mState = ANNOYER_HIT;
        Game_PlaySfx("player_hit_1");
        Ecs_Flicker(self, FxSec(0.2f));
        if (--d.mHealth <= 0)
        {
            Ecs_SetSolid(self, true);
            d.mDying = true;
            d.mTimer = FxSec(0.25f);
        }
        else
        {
            d.mTimer = FxSec(0.4f);
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
        self.mV->mOpacity -= FxF(0.06f) / kFps;
        if (self.mB->mTouching != TOUCH_NONE)
        {
            LaserPoof(self);
        }
    }
    else if (Ecs_AnimFinished(self))
    {
        self.mExists = false;
    }
    d.mSolidTimer -= kFxOne;
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
    d.mTimer -= kFxOne;
    if (d.mTimer < 0)
    {
        d.mTimer = FxSec(0.5f);
        if (Ecs_CountChildren(self, kLaser) < 5)
        {
            const Facing f = static_cast<Facing>(self.mFacing);
            Entity* l = Ecs_Create(kLaser, self.mT->mX, self.mT->mY, PART_VISUAL | PART_BRAIN, nullptr, &self);
            if (l)
            {
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
        d.mHitTimer -= kFxOne;
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
    bool mPlayerInArea;
    int mHealth;
    fx mTimer;
    fx mHitTimer;
    fx mAttackX[2];
    fx mAttackY[2];
};

const fx kDogAttackSpeeds[5] = { FxF(66.0f), FxF(84.0f), FxF(84.0f), FxF(96.0f), FxF(102.0f) };

void DogSetFlip(Entity& self)
{
    self.mV->mHFlip = self.mT->mVelX <= 0;
}

void DogEnter(Entity& self, uint8_t state)
{
    DogData& d = self.Data<DogData>();
    d.mState = state;
    d.mTimer = FxSec(1.0f);
    Transform& t = *self.mT;
    if (state == DOG_PACE)
    {
        t.mVelX = t.mVelY = 0;
    }
    else if (state == DOG_ALERT)
    {
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
        if (!d.mSuper)
        {
            d.mAttackX[0] = px + Fx(Rng_Range(30, 43));
            d.mAttackY[0] = py - Fx(8);
            d.mAttackX[1] = px - Fx(Rng_Range(38, 51));
            d.mAttackY[1] = py - Fx(8);
        }
        Ecs_MoveTowards(self, px - Fx(Rng_Range(38, 51)), py - Fx(8), kDogAttackSpeeds[0]);
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
        if (d.mPlayerInArea)
        {
            DogEnter(self, DOG_ALERT);
            break;
        }
        d.mTimer -= kFxOne;
        if (d.mTimer <= 0)
        {
            d.mTimer += FxSec(1.0f);
            if (Rng_Unit() > FxF(0.33f))
            {
                Ecs_Play(self, "walk");
                t.mVelX = Fx(20);
                DogSetFlip(self);
            }
            else if (Rng_Unit() > FxF(0.5f))
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
        d.mTimer -= kFxOne;
        if (d.mTimer <= 0)
        {
            DogEnter(self, DOG_ATTACK);
        }
        break;
    default:
        d.mTimer -= kFxOne;
        if (d.mTimer <= 0)
        {
            d.mTimer += FxSec(1.0f);
            d.mPhase++;
            if (d.mPhase > (d.mSuper ? 4 : 2))
            {
                DogEnter(self, DOG_PACE);
                break;
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
    d.mPlayerInArea = false;
    if (d.mHitTimer > 0)
    {
        d.mHitTimer -= kFxOne;
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
            Die(self, kFxOne);
            Spawn_Explosion(self);
        }
    }
}

const EntityClass kDog = {
    "Dog",        DogUpdate, nullptr, DogCollided, nullptr, nullptr, nullptr, nullptr,
    0,            GROUP_PLAYER | GROUP_BROOM,
    ECF_MAP_COLLISION | ECF_KEEP_ON_SCREEN | ECF_ENEMY,
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
    if (dog && dog->mExists && dog->Data<DogData>().mState == DOG_PACE)
    {
        dog->Data<DogData>().mPlayerInArea = true;
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
    d.mAngle += FxF(1.2f) / kFps;
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
};

constexpr fx kTwoPi = FxF(6.2831853f);

Entity* SeerChild(Entity& self, const EntityClass& cls, int index = 0)
{
    for (Entity* e = Ecs_First(); e; e = e->mNext)
    {
        if (e->mParent == &self && e->mClass == &cls && !e->mFreed && index-- == 0)
        {
            return e;
        }
    }
    return nullptr;
}

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
    Ecs_BaseUpdate(self);
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
// a dust (the first three). False once done.
bool SeerVertical(Entity& self)
{
    SeerData& d = self.Data<SeerData>();
    if (!d.mDown)
    {
        if (MoveTo(self.mT->mY, d.mPresetY - Fx(32), Fx(120)))
        {
            d.mDown = true;
        }
        return true;
    }
    if (!MoveTo(self.mT->mY, d.mPresetY + Fx(80), Fx(90)))
    {
        return true;
    }
    Screen_Shake(FxF(0.05f), FxF(0.3f));
    if (d.mBounce < 3)
    {
        Entity* dust = Spawn_DustAt(self.mT->mX, self.mT->mY);
        if (dust)
        {
            Ecs_Play(*dust, "unpoof", true);
        }
    }
    Game_PlaySfx("hit_wall");
    d.mDown = false;
    return ++d.mBounce <= 3;
}

void SeerRotateOrbs(Entity& self)
{
    for (int i = 0; i < 2; i++)
    {
        Entity* orb = SeerChild(self, kSeerOrb, i);
        if (!orb)
        {
            continue;
        }
        SeerOrbData& o = orb->Data<SeerOrbData>();
        o.mAngle += o.mSpeed / kFps;
        while (o.mAngle >= kTwoPi)
        {
            o.mAngle -= kTwoPi;
        }
        orb->mT->mX = Ecs_CenterX(self) + FxMul(o.mRadius, FxCos(o.mAngle));
        orb->mT->mY = Ecs_CenterY(self) + FxMul(o.mRadius, FxSin(o.mAngle));
    }
}

void SeerFadeEverything(Entity& self)
{
    MoveTo(self.mV->mOpacity, 0, Fx(3));
    for (int i = 0; i < 2; i++)
    {
        Entity* orb = SeerChild(self, kSeerOrb, i);
        if (orb)
        {
            MoveTo(orb->mV->mOpacity, 0, Fx(2));
        }
    }
    Entity* wave = SeerChild(self, kSeerWave);
    if (wave)
    {
        MoveTo(wave->mV->mOpacity, 0, Fx(4));
    }
}

void SeerSpawnOrbs(Entity& self)
{
    static const fx kRadius[2] = { Fx(16), Fx(32) };
    static const fx kSpeed[2] = { FxF(3.0f), FxF(2.4f) };
    for (int i = 0; i < 2; i++)
    {
        Entity* orb = Ecs_Create(kSeerOrb, self.mT->mX + kRadius[i], self.mT->mY, PART_VISUAL | PART_BRAIN, nullptr,
                                 &self);
        if (!orb)
        {
            continue;
        }
        Ecs_SetSheet(*orb, "light_orb");
        orb->mV->mLayer = LAYER_FG_SPRITES;
        Ecs_Play(*orb, "glow");
        Ecs_SetSolid(*orb, false);
        SeerOrbData& o = orb->Data<SeerOrbData>();
        o.mIndex = static_cast<uint8_t>(i);
        o.mRadius = kRadius[i];
        o.mSpeed = kSpeed[i];
    }
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

void SeerUpdate(Entity& self)
{
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
            SeerEnter(self, SEER_SHAKE);
            Game_PlaySong("bedroom-boss");
            Darkness_TargetAlpha(FxF(0.1f), FxF(1.2f));
            Screen_Shake(FxF(0.01f), Fx(3));
            break;
        }
        Game_ShowDialogue("sun_guy", "before_fight");
        SeerEnter(self, SEER_DIALOGUE);
        break;
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
        break;
    case SEER_SHAKE:
        if (Screen_ShakeActive())
        {
            SeerLightScale(kFxOne, FxF(1.2f));
            break;
        }
        SeerSpawnOrbs(self);
        d.mHorizontalBack = false;
        d.mYTimer = 0;
        SeerEnter(self, SEER_STAGE1);
        [[fallthrough]];
    case SEER_STAGE1:
        if (d.mHealth > 6)
        {
            d.mYTimer += kFxOne / kFps;
            t.mY = d.mPresetY + 10 * FxSin(d.mYTimer);
            SeerHorizontal(self);
            SeerRotateOrbs(self);
            break;
        }
        SeerEnter(self, SEER_POSITION);
        [[fallthrough]];
    case SEER_POSITION:
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
            for (int i = 0; i < 2; i++)
            {
                Entity* orb = SeerChild(self, kSeerOrb, i);
                if (orb)
                {
                    SeerOrbData& o = orb->Data<SeerOrbData>();
                    MoveTo(o.mRadius, Fx(8), Fx(30));
                    MoveTo(o.mSpeed, Fx(240), FxF(0.6f));
                }
            }
            SeerRotateOrbs(self);
            SeerHorizontal(self);
            break;
        }
        SeerEnter(self, SEER_CHARGE);
        [[fallthrough]];
    case SEER_CHARGE:
    {
        const bool x = MoveTo(t.mX, d.mPresetX + Fx(30), Fx(60));
        const bool y = MoveTo(t.mY, d.mPresetY - Fx(20), Fx(60));
        if (!(x && y))
        {
            Entity* orb0 = SeerChild(self, kSeerOrb, 0);
            Entity* orb1 = SeerChild(self, kSeerOrb, 1);
            if (orb0)
            {
                MoveTo(orb0->mT->mX, t.mX + Fx(4), Fx(120));
                MoveTo(orb0->mT->mY, t.mY + Fx(10), Fx(120));
            }
            if (orb1)
            {
                MoveTo(orb1->mT->mX, t.mX - Fx(4), Fx(120));
                MoveTo(orb1->mT->mY, t.mY + Fx(10), Fx(120));
            }
            break;
        }
        Game_PlaySfx("sun_guy_charge");
        Entity* wave = Ecs_Create(kSeerWave, d.mPresetX - Fx(50), d.mPresetY - Fx(10), PART_VISUAL | PART_BRAIN,
                                  nullptr, &self);
        if (wave)
        {
            Ecs_SetSheet(*wave, "sun_guy_wave");
            wave->mB->mW = 128;
            wave->mB->mH = 8;
            wave->mV->mLayer = LAYER_FG_SPRITES;
            Ecs_SetSolid(*wave, false);
            Ecs_Play(*wave, "play");
            Ecs_Flicker(*wave, FxSec(3.0f));
        }
        SeerEnter(self, SEER_ORBS_OUT);
        break;
    }
    case SEER_ORBS_OUT:
    {
        Entity* wave = SeerChild(self, kSeerWave);
        const bool ended = !wave || wave->Data<SeerWaveData>().mEnd;
        bool done = ended;
        if (!ended)
        {
            Entity* orb0 = SeerChild(self, kSeerOrb, 0);
            Entity* orb1 = SeerChild(self, kSeerOrb, 1);
            const bool a = !orb0 || MoveTo(orb0->mT->mX, wave->mT->mX, Fx(18));
            const bool b = !orb1 || MoveTo(orb1->mT->mX, d.mPresetX + Fx(75), Fx(18));
            Screen_Shake(FxF(0.02f), FxF(0.1f));
            done = a && b;
        }
        if (!done)
        {
            break;
        }
        if (!ended)
        {
            Game_PlaySfx("sun_guy_death_short");
        }
        if (wave)
        {
            wave->mT->mVelY = Fx(30);
        }
        d.mHorizontalBack = false;
        SeerEnter(self, SEER_WAVE);
        break;
    }
    case SEER_WAVE:
    {
        Entity* wave = SeerChild(self, kSeerWave);
        if (wave && !wave->Data<SeerWaveData>().mEnd)
        {
            SeerHorizontal(self);
            break;
        }
        if (wave && wave->mExists)
        {
            break;
        }
        for (Entity* e = Ecs_First(); e; e = e->mNext)
        {
            if ((e->mClass->mGroups & GROUP_DUST) && e->mExists && !e->mFreed)
            {
                Ecs_Play(*e, "poof");
            }
        }
        Player_DropBroomDust();
        SeerEnter(self, SEER_WAVE_GONE);
        break;
    }
    case SEER_WAVE_GONE:
        for (Entity* e = Ecs_First(); e; e = e->mNext)
        {
            if ((e->mClass->mGroups & GROUP_DUST) && e->mExists && !e->mFreed)
            {
                return Ecs_BaseUpdate(self);
            }
        }
        SeerEnter(self, SEER_POSITION);
        break;
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
        break;
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
        }
        else if (d.mFlashes == 3)
        {
            Screen_Flash(FxF(3.3f));
            Screen_Shake(FxF(0.02f), Fx(2));
            Game_PlaySfx("sun_guy_death_long");
        }
        else
        {
            Darkness_TargetAlpha(FxF(0.75f), FxF(1.25f));
            SeerEnter(self, SEER_LIGHT);
        }
        d.mFlashes++;
        break;
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
        for (Entity* e = Ecs_First(); e; e = e->mNext)
        {
            if (e->mParent == &self)
            {
                e->mExists = false;
            }
        }
        Game_PlaySong("bedroom");
        break;
    default:
        break;
    }
    Ecs_BaseUpdate(self);
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
} // namespace

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
    d.mMoveTimer = FxSec(0.5f);
    d.mShootTimer = FxSec(1.8f);
    Ecs_Play(*e, "Move");
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
    d.mFireTimer = FxSec(2.3f);
    AnnoyerEnterActive(*e, ANNOYER_WAIT);
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
    Ecs_Play(*e, "stop");
    DogEnter(*e, DOG_PACE);
    Entity* region = Ecs_Create(kDogRegion, e->mT->mX, e->mT->mY, 0, nullptr, e);
    if (region)
    {
        region->mB->mW = region->mB->mH = 96;
        Ecs_SetSolid(*region, false);
    }
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
    return e;
}
