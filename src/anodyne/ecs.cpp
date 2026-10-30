#include "ecs.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tonc.h"

#include "fix16_sin.h"
#include "obj_vram_allocator.hpp"
#include "sprite.h"

// Pools and per-frame buffers live in EWRAM (".sbss" in gba_cart.ld: uninitialised, Ecs_Init resets them).
#define ECS_EWRAM __attribute__((section(".sbss")))
// The loops run every frame for every entity (drawing, collisions, physics): their code sits in IWRAM, which has no
// wait states. (ECS_IWRAM_LEAF: small helpers they call, still inlined where the compiler likes)
#ifdef ANODYNE_SIM
#define ECS_IWRAM_CODE
#define ECS_IWRAM_LEAF
#else
#define ECS_IWRAM_CODE __attribute__((section(".iwram.ecs_draw"), noinline))
#define ECS_IWRAM_LEAF __attribute__((section(".iwram.ecs_draw")))
#endif

#ifndef ANODYNE_SIM
// FxDivRaw's quotients over 32 bits (fx_div_arm.s).
extern "C" int32_t FxDivSlow(int32_t a, int32_t b)
{
    return static_cast<int32_t>((static_cast<int64_t>(a) * 65536) / b);
}
#endif

namespace
{
Pool<Entity, kMaxEntities> gEntities ECS_EWRAM;
Pool<Transform, kMaxEntities> gTransforms ECS_EWRAM;
Pool<Body, kMaxEntities> gBodies ECS_EWRAM;
Pool<Visual, kMaxVisuals> gVisuals ECS_EWRAM;
Pool<Brain, kMaxBrains> gBrains ECS_EWRAM;

Entity* gHead = nullptr;
Entity* gTail = nullptr;
int gBaseKilled = 0;
struct Rng
{
    int mSeedArray[56];
    int mNext;
    int mNextP;
    uint32_t mCalls;
};
Rng gRng;
// Ecs_UpdateAll's last entity: the ones created after it wait for the next frame.
Entity* gUpdateLast;

constexpr fx kOverlapBias = Fx(4); // GameObject.OVERLAP_BIAS

// Bumped by every change to the list, so a pass over a copy of it can tell when to go back to the list itself.
uint32_t gListGen = 0;

void Link(Entity* e)
{
    gListGen++;
    e->mPrev = gTail;
    e->mNext = nullptr;
    if (gTail)
    {
        gTail->mNext = e;
    }
    else
    {
        gHead = e;
    }
    gTail = e;
}

void Unlink(Entity* e)
{
    gListGen++;
    if (e->mPrev)
    {
        e->mPrev->mNext = e->mNext;
    }
    else
    {
        gHead = e->mNext;
    }
    if (e->mNext)
    {
        e->mNext->mPrev = e->mPrev;
    }
    else
    {
        gTail = e->mPrev;
    }
    e->mPrev = e->mNext = nullptr;
}

const SpriteAnimData* CurAnim(const Visual& v)
{
    return (v.mSheet && v.mAnim >= 0) ? &v.mSheet->mAnims[v.mAnim] : nullptr;
}

// Anim.Update: one step of the current animation.
// 1 / fps (FxDiv(1, fps): 65536 / fps raw) for the usual frame rates, so the frame doesn't pay for a divide.
constexpr int kDelayCount = 64;
struct Delays final
{
    fx mV[kDelayCount];
    constexpr Delays() : mV()
    {
        for (int i = 1; i < kDelayCount; i++)
        {
            mV[i] = FxDiv(kFxOne, Fx(i));
        }
    }
};
constexpr Delays kDelays;

void AnimUpdate(Visual& v)
{
    const SpriteAnimData* anim = CurAnim(v);
    if (!anim || v.mFinished)
    {
        return;
    }
    const int fps = v.mFpsOverride > 0 ? v.mFpsOverride : anim->mFps;
    if (fps == 0)
    {
        v.mFinished = true; // delay 0: always "finished"
        return;
    }
    // (strictly past it: 4 ticks (4368 raw) are not past 1/15 (4369), as 4 frames of 1/60 weren't in the C#'s floats)
    const fx delay = fps < kDelayCount ? kDelays.mV[fps] : FxDiv(kFxOne, Fx(fps));
    v.mTimer += kFxTick;
    while (v.mTimer > delay)
    {
        v.mTimer -= delay;
        if (v.mAnimIndex + 1 >= anim->mFrameCount)
        {
            if (anim->mLoop)
            {
                v.mAnimIndex = 0;
            }
            else
            {
                v.mFinished = true;
                break;
            }
        }
        else
        {
            v.mAnimIndex++;
        }
    }
}

// Entity.DoFlicker: visible toggles every 0.05s (3 frames) until the timer runs out.
void DoFlicker(Visual& v)
{
    v.mFlickerFreq -= kFxTick;
    if (v.mFlickerFreq <= 0)
    {
        v.mFlickerFreq = FxSec(0.05f);
        v.mVisible = !v.mVisible;
    }
    if (v.mFlickerTimer > 0)
    {
        v.mFlickerTimer -= kFxTick;
        if (v.mFlickerTimer <= 0)
        {
            v.mFlickering = false;
            v.mVisible = true;
        }
    }
}

// GameObject.CalculateDrag.
fx Drag(fx vel, fx drag)
{
    const fx d = FxStep(drag);
    if (vel < 0)
    {
        vel += d;
        return vel > 0 ? 0 : vel;
    }
    vel -= d;
    return vel < 0 ? 0 : vel;
}

// GameObject.SeparateX/Y for two objects given by position/last position/size (Fx). Object 2 may be a map tile
// (immovable2, bias = OVERLAP_BIAS) or an entity (bias / 2).
struct Obj final
{
    fx* mX;
    fx* mY;
    fx* mLastX;
    fx* mLastY;
    fx mW;
    fx mH;
    uint8_t mAllow;
    uint8_t* mTouching;
    bool mImmovable;
};

bool SeparateAxis(Obj& o1, Obj& o2, bool xAxis, bool o2IsEntity)
{
    if (o1.mImmovable && o2.mImmovable)
    {
        return false;
    }
    fx* p1 = xAxis ? o1.mX : o1.mY;
    fx* p2 = xAxis ? o2.mX : o2.mY;
    fx* l1 = xAxis ? o1.mLastX : o1.mLastY;
    fx* l2 = xAxis ? o2.mLastX : o2.mLastY;
    const fx d1 = *p1 - *l1;
    const fx d2 = *p2 - *l2;
    const fx d1a = d1 < 0 ? -d1 : d1;
    const fx d2a = d2 < 0 ? -d2 : d2;

    // The hulls: along this axis widened by the movement, across it at the last position.
    fx a1, b1, a2, b2, s1a, s1b, s2a, s2b;
    if (xAxis)
    {
        a1 = *p1 - (d1 > 0 ? d1 : 0);
        s1a = o1.mW + d1a;
        b1 = *o1.mLastY;
        s1b = o1.mH;
        a2 = *p2 - (d2 > 0 ? d2 : 0);
        s2a = o2.mW + d2a;
        b2 = *o2.mLastY;
        s2b = o2.mH;
    }
    else
    {
        a1 = *p1 - (d1 > 0 ? d1 : 0);
        s1a = o1.mH + d1a;
        b1 = *o1.mLastX;
        s1b = o1.mW;
        a2 = *p2 - (d2 > 0 ? d2 : 0);
        s2a = o2.mH + d2a;
        b2 = *o2.mLastX;
        s2b = o2.mW;
    }
    fx overlap = 0;
    if (a1 + s1a > a2 && a1 < a2 + s2a && b1 + s1b > b2 && b1 < b2 + s2b)
    {
        const fx maxOverlap = d1a + d2a + (o2IsEntity ? kOverlapBias / 2 : kOverlapBias);
        const fx size1 = xAxis ? o1.mW : o1.mH;
        const fx size2 = xAxis ? o2.mW : o2.mH;
        const fx overlapFwd = *p1 + size1 - *p2;  // right / down
        const fx overlapBack = *p1 - size2 - *p2; // left / up
        const uint8_t fwd1 = xAxis ? TOUCH_RIGHT : TOUCH_DOWN;
        const uint8_t back1 = xAxis ? TOUCH_LEFT : TOUCH_UP;
        if ((overlapFwd < 0 ? -overlapFwd : overlapFwd) < (overlapBack < 0 ? -overlapBack : overlapBack))
        {
            overlap = overlapFwd;
            if (overlap > maxOverlap || (o1.mAllow & fwd1) == 0 || (o2.mAllow & back1) == 0)
            {
                overlap = 0;
            }
            else
            {
                *o1.mTouching |= fwd1;
                if (o2.mTouching)
                {
                    *o2.mTouching |= back1;
                }
            }
        }
        else
        {
            overlap = overlapBack;
            if (-overlap > maxOverlap || (o1.mAllow & back1) == 0 || (o2.mAllow & fwd1) == 0)
            {
                overlap = 0;
            }
            else
            {
                *o1.mTouching |= back1;
                if (o2.mTouching)
                {
                    *o2.mTouching |= fwd1;
                }
            }
        }
    }
    if (overlap == 0)
    {
        return false;
    }
    if (!o1.mImmovable && !o2.mImmovable)
    {
        overlap /= 2;
        *p1 -= overlap;
        *p2 += overlap;
    }
    else if (!o1.mImmovable)
    {
        *p1 -= overlap;
        *l1 = *p1;
    }
    else
    {
        *p2 += overlap;
        *l2 = *p2;
    }
    return true;
}

Obj ObjOf(Entity& e)
{
    Obj o;
    o.mX = &e.mT->mX;
    o.mY = &e.mT->mY;
    o.mLastX = &e.mT->mLastX;
    o.mLastY = &e.mT->mLastY;
    o.mW = Fx(e.mB->mW);
    o.mH = Fx(e.mB->mH);
    o.mAllow = e.mB->mAllow;
    o.mTouching = &e.mB->mTouching;
    o.mImmovable = e.mB->mImmovable;
    return o;
}

// No velocity, acceleration or spin, and a drag >= 0 (which keeps a zero velocity zero): nothing would move.
bool AtRest(const Transform& t)
{
    return (t.mVelX.mRaw | t.mVelY.mRaw | t.mAccX.mRaw | t.mAccY.mRaw | t.mAngVel.mRaw | t.mAngAcc.mRaw) == 0 &&
           t.mDragX >= 0 && t.mDragY >= 0;
}

void Physics(Entity& e)
{
    Transform& t = *e.mT;
    t.mLastX = t.mX;
    t.mLastY = t.mY;
    if (AtRest(t))
    {
        // (the maths below would only add zeros: most entities stand still, so skip it)
        e.mB->mWasTouching = e.mB->mTouching;
        e.mB->mTouching = TOUCH_NONE;
        return;
    }
    const fx oldVx = t.mVelX;
    const fx oldVy = t.mVelY;
    t.mVelX += FxStep(t.mAccX);
    t.mVelY += FxStep(t.mAccY);
    t.mVelX = Drag(t.mVelX, t.mDragX);
    t.mVelY = Drag(t.mVelY, t.mDragY);
    // Average velocity: exact for constant acceleration.
    t.mX += FxStep(oldVx + (t.mVelX - oldVx) / 2);
    t.mY += FxStep(oldVy + (t.mVelY - oldVy) / 2);
    const fx oldAv = t.mAngVel;
    t.mAngVel += FxStep(t.mAngAcc);
    t.mRot += FxStep(oldAv + (t.mAngVel - oldAv) / 2);
    e.mB->mWasTouching = e.mB->mTouching;
    e.mB->mTouching = TOUCH_NONE;
}

bool Updatable(const Entity& e)
{
    return e.mExists && !e.mOld && !e.mFreed && !e.mSpawned;
}
} // namespace

// ---- Math ----------------------------------------------------------------------------------------------------
namespace
{
// floor(sqrt(v)) of a 64-bit value, bit by bit.
uint32_t ISqrt64(uint64_t v)
{
    uint64_t root = 0;
    uint64_t bit = static_cast<uint64_t>(1) << 62;
    while (bit > v)
    {
        bit >>= 2;
    }
    while (bit != 0)
    {
        if (v >= root + bit)
        {
            v -= root + bit;
            root = (root >> 1) + bit;
        }
        else
        {
            root >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<uint32_t>(root);
}

// The angle in 1/4096 turns, rounded: raw * 2^32 / (2 pi 65536), 2^32 / (32 pi) = 42722830.
int SinIndex(fx radians)
{
    return static_cast<int>((static_cast<int64_t>(radians.mRaw) * 42722830 + (static_cast<int64_t>(1) << 31)) >> 32);
}

fx SinAt(int i)
{
    i &= 4095;
    const int j = i & 1023;
    const int32_t v = (i & 1024) ? kFix16Sin[1024 - j] : kFix16Sin[j];
    return fx::Raw(i >= 2048 ? -v : v);
}
} // namespace

fx FxSqrt(fx v)
{
    return v.mRaw <= 0 ? kFxZero : fx::Raw(static_cast<int32_t>(ISqrt64(static_cast<uint64_t>(v.mRaw) << 16)));
}

fx FxLength(fx x, fx y)
{
    const uint64_t sq = static_cast<uint64_t>(static_cast<int64_t>(x.mRaw) * x.mRaw) +
                        static_cast<uint64_t>(static_cast<int64_t>(y.mRaw) * y.mRaw);
    return fx::Raw(static_cast<int32_t>(ISqrt64(sq)));
}

fx FxSin(fx radians)
{
    return SinAt(SinIndex(radians));
}

fx FxCos(fx radians)
{
    return SinAt(SinIndex(radians) + 1024);
}

bool MoveTo(fx& value, fx target, fx speedPerSecond)
{
    const fx step = FxStep(speedPerSecond);
    if (value > target)
    {
        value = value - step < target ? target : value - step;
    }
    else
    {
        value = value + step > target ? target : value + step;
    }
    return value == target;
}

// ---- Rng: .NET's legacy System.Random (Knuth's subtractive generator), as seeded with an int ----------------
namespace
{
bool gRngPinned = false; // a seed was chosen (the bot's, to match a C# run): the player's choice doesn't replace it
} // namespace

void Rng_SeedFromPlayer(uint32_t entropy)
{
    if (!gRngPinned)
    {
        Rng_Seed(static_cast<int>(entropy & 0x7FFFFFFF));
        gRngPinned = false;
    }
}

void Rng_Seed(int seed)
{
    gRngPinned = true;
    constexpr int kMbig = 0x7FFFFFFF;
    constexpr int kMseed = 161803398;
    const int subtraction = seed == INT32_MIN ? INT32_MAX : (seed < 0 ? -seed : seed);
    int mj = kMseed - subtraction;
    gRng.mSeedArray[55] = mj;
    int mk = 1;
    int ii = 0;
    for (int i = 1; i < 55; i++)
    {
        if ((ii += 21) >= 55)
        {
            ii -= 55;
        }
        gRng.mSeedArray[ii] = mk;
        mk = mj - mk;
        if (mk < 0)
        {
            mk += kMbig;
        }
        mj = gRng.mSeedArray[ii];
    }
    for (int k = 1; k < 5; k++)
    {
        for (int i = 1; i < 56; i++)
        {
            int n = i + 30;
            if (n >= 55)
            {
                n -= 55;
            }
            gRng.mSeedArray[i] -= gRng.mSeedArray[1 + n];
            if (gRng.mSeedArray[i] < 0)
            {
                gRng.mSeedArray[i] += kMbig;
            }
        }
    }
    gRng.mNext = 0;
    gRng.mNextP = 21;
    gRng.mCalls = 0;
}

uint32_t Rng_Calls()
{
    return gRng.mCalls;
}

namespace
{
uint32_t gGameFrame;

// RecordingManager.Now: frame * TicksPerSecond / 60, truncated frame by frame, so what a difference comes to depends
// on the frame numbers themselves. A parity replay counts them the C# game's way (it is that many frames behind).
int64_t FrameTicks(uint32_t frame)
{
#ifdef ANODYNE_SIM
    static const char* const sOffset = getenv("ANODYNE_SIM_NOW_OFFSET");
    static const uint32_t sBehind = sOffset ? static_cast<uint32_t>(atoi(sOffset)) : 0;
    frame -= sBehind;
#elif defined(ANODYNE_LINK)
    frame -= 340; // (what SimHost tells the simulator: the C# game's frames start 340 after the port's)
#endif
    return static_cast<int64_t>(frame) * 10000000 / 60;
}
} // namespace

void Game_CountFrame()
{
    gGameFrame++;
}

uint32_t Game_Frame()
{
    return gGameFrame;
}

void Game_SetFrame(uint32_t frame)
{
    gGameFrame = frame;
}

fx Game_SecondsSince(uint32_t thenFrame)
{
    return fx::Raw(static_cast<int32_t>((FrameTicks(gGameFrame) - FrameTicks(thenFrame)) * 65536 / 10000000));
}

int Rng_Next()
{
    constexpr int kMbig = 0x7FFFFFFF;
    gRng.mCalls++;
    if (++gRng.mNext >= 56)
    {
        gRng.mNext = 1;
    }
    if (++gRng.mNextP >= 56)
    {
        gRng.mNextP = 1;
    }
    int ret = gRng.mSeedArray[gRng.mNext] - gRng.mSeedArray[gRng.mNextP];
    if (ret == kMbig)
    {
        ret--;
    }
    if (ret < 0)
    {
        ret += kMbig;
    }
    gRng.mSeedArray[gRng.mNext] = ret;
    return ret;
}

double Rng_NextDouble()
{
    return Rng_Next() * (1.0 / 0x7FFFFFFF);
}

fx Rng_Unit()
{
    return fx::Raw(static_cast<int32_t>((static_cast<int64_t>(Rng_Next()) << 16) / 0x7FFFFFFF));
}

int Rng_Range(int lo, int hi)
{
    // (int)(NextDouble() * range): next * range / (2^31 - 1) in integers (no soft doubles), unless it lands too near a
    // whole number for the doubles' rounding to be sure of; then the doubles themselves.
    constexpr uint32_t kMbig = 0x7FFFFFFF;
    constexpr uint32_t kSlack = 1u << 16;
    const int64_t range = static_cast<int64_t>(hi) - lo;
    const int next = Rng_Next();
    if (range > 0 && range <= INT32_MAX)
    {
        const uint64_t p = static_cast<uint64_t>(next) * static_cast<uint64_t>(range);
        uint32_t q = static_cast<uint32_t>(p >> 31); // p = q * 2^31 + low = q * kMbig + (low + q)
        uint32_t r = static_cast<uint32_t>(p & kMbig) + q;
        while (r >= kMbig)
        {
            r -= kMbig;
            q++;
        }
        if (r > kSlack && r < kMbig - kSlack)
        {
            return static_cast<int>(q) + lo;
        }
    }
    return static_cast<int>(next * (1.0 / 0x7FFFFFFF) * range) + lo;
}

int FacingX(Facing f)
{
    return f == FACE_RIGHT ? 1 : (f == FACE_LEFT ? -1 : 0);
}

int FacingY(Facing f)
{
    return f == FACE_DOWN ? 1 : (f == FACE_UP ? -1 : 0);
}

Facing FlipFacing(Facing f)
{
    switch (f)
    {
    case FACE_LEFT:
        return FACE_RIGHT;
    case FACE_RIGHT:
        return FACE_LEFT;
    case FACE_UP:
        return FACE_DOWN;
    default:
        return FACE_UP;
    }
}

// ---- Lifetime ------------------------------------------------------------------------------------------------
void Ecs_Init()
{
    gEntities.Reset();
    gTransforms.Reset();
    gBodies.Reset();
    gVisuals.Reset();
    gBrains.Reset();
    gHead = gTail = nullptr;
    gBaseKilled = 0;
}

Entity* Ecs_Create(const EntityClass& cls, fx x, fx y, uint8_t parts, const MapEntity* preset, Entity* parent)
{
    if (gEntities.mFreeCount == 0 || gTransforms.mFreeCount == 0 || gBodies.mFreeCount == 0 ||
        ((parts & PART_VISUAL) && gVisuals.mFreeCount == 0) || ((parts & PART_BRAIN) && gBrains.mFreeCount == 0))
    {
        return nullptr;
    }
    Entity* e = gEntities.Alloc();
    e->mIndex = static_cast<uint16_t>(gEntities.IndexOf(e));
    e->mClass = &cls;
    e->mT = gTransforms.Alloc();
    e->mB = gBodies.Alloc();
    e->mV = (parts & PART_VISUAL) ? gVisuals.Alloc() : nullptr;
    e->mBrain = (parts & PART_BRAIN) ? gBrains.Alloc() : nullptr;
    e->mPreset = preset;
    e->mParent = parent;
    e->mExists = true;
    e->mSpawned = false;
    e->mKeep = false;
    e->mCutsceneOwned = false;
    e->mFacing = FACE_LEFT; // (the C# enum default)
    e->mT->mX = e->mT->mLastX = x;
    e->mT->mY = e->mT->mLastY = y;
    e->mB->mAllow = TOUCH_ANY;
    e->mB->mMapInteraction = true;
    if (e->mV)
    {
        e->mV->mAnim = -1;
        e->mV->mVisible = true;
        e->mV->mLayer = LAYER_ENTITIES;
        e->mV->mOpacity = kFxOne;
        e->mV->mScale = kFxOne;
    }
    Link(e);
    return e;
}

void Ecs_Free(Entity& e)
{
    if (e.mFreed)
    {
        return;
    }
    e.mFreed = true;
    if (e.mClass->mDestroy)
    {
        e.mClass->mDestroy(e);
    }
    Unlink(&e);
    for (Entity* c = gHead; c; c = c->mNext)
    {
        if (c->mParent == &e)
        {
            c->mParent = nullptr;
        }
    }
    gTransforms.Free(e.mT);
    gBodies.Free(e.mB);
    gVisuals.Free(e.mV);
    gBrains.Free(e.mBrain);
    gEntities.Free(&e);
}

Entity* Ecs_First()
{
    return gHead;
}

void Ecs_MarkScreenOld()
{
    for (Entity* e = gHead; e; e = e->mNext)
    {
        if (!e->mPlayerOwned)
        {
            e->mOld = true;
        }
    }
}

void Ecs_FreeOld()
{
    for (Entity* e = gHead; e;)
    {
        Entity* next = e->mNext;
        if (e->mOld)
        {
            Ecs_Free(*e);
        }
        e = next;
    }
}

void Ecs_FreeAllButPlayer()
{
    for (Entity* e = gHead; e;)
    {
        Entity* next = e->mNext;
        if (!e->mPlayerOwned)
        {
            Ecs_Free(*e);
        }
        e = next;
    }
}

void Ecs_FreeDead()
{
    for (Entity* e = gHead; e;)
    {
        Entity* next = e->mNext;
        if (!e->mExists && !e->mKeep && !e->mPreset && !e->mPlayerOwned && !(e->mClass->mFlags & ECF_ENEMY))
        {
            Ecs_Free(*e);
        }
        e = next;
    }
}

void Ecs_MoveLast(Entity& e)
{
    Unlink(&e);
    Link(&e);
}

void Ecs_MovePlayerOwnedLast()
{
    Entity* owned[kMaxEntities];
    int n = 0;
    for (Entity* e = gHead; e; e = e->mNext)
    {
        if (e->mPlayerOwned)
        {
            owned[n++] = e;
        }
    }
    for (int i = 0; i < n; i++)
    {
        Unlink(owned[i]);
        Link(owned[i]);
    }
}

void Ecs_MoveAfter(Entity& e, Entity& after)
{
    if (&e == &after)
    {
        return;
    }
    if (gUpdateLast == &after)
    {
        gUpdateLast = &e; // (Ecs_UpdateAll: still updated this frame)
    }
    Unlink(&e);
    e.mPrev = &after;
    e.mNext = after.mNext;
    if (after.mNext)
    {
        after.mNext->mPrev = &e;
    }
    else
    {
        gTail = &e;
    }
    after.mNext = &e;
}

int Ecs_CountChildren(const Entity& parent, const EntityClass& cls)
{
    int n = 0;
    for (const Entity* e = gHead; e; e = e->mNext)
    {
        n += (e->mParent == &parent && e->mClass == &cls && e->mExists && !e->mFreed) ? 1 : 0;
    }
    return n;
}

int Ecs_Count()
{
    return gEntities.mUsed;
}

void Ecs_Stats(int* entities, int* visuals, int* brains)
{
    *entities = gEntities.mPeak;
    *visuals = gVisuals.mPeak;
    *brains = gBrains.mPeak;
}

// ---- Animation -----------------------------------------------------------------------------------------------
bool Ecs_SetSheet(Entity& e, const char* sheet, int frameW, int frameH)
{
    if (!e.mV)
    {
        return false;
    }
    const SpriteSheetEntry* s = Sprite_FindSheet(sheet, frameW, frameH);
    if (!s)
    {
        return false;
    }
    if (s != e.mV->mSheet)
    {
        e.mV->mSheet = s;
        e.mV->mAnim = s->mAnimCount > 0 ? 0 : -1; // AnimatedSpriteRenderer starts on its first anim
        e.mV->mAnimIndex = 0;
        e.mV->mTimer = 0;
        e.mV->mFinished = false;
        e.mV->mStaticFrame = 0;
    }
    return true;
}

bool Ecs_SetTexture(Entity& e, const char* sheet, int frameW, int frameH)
{
    const SpriteSheetEntry* s = e.mV ? Sprite_FindSheet(sheet, frameW, frameH) : nullptr;
    if (!s)
    {
        return false;
    }
    if (!e.mV->mSheet || s->mAnimCount != e.mV->mSheet->mAnimCount)
    {
        return Ecs_SetSheet(e, sheet, frameW, frameH);
    }
    e.mV->mSheet = s; // (the same anims, so the one playing carries on)
    return true;
}

bool Ecs_Play(Entity& e, const char* anim, bool force, int fps)
{
    Visual* v = e.mV;
    if (!v || !v->mSheet)
    {
        return false;
    }
    for (uint32_t i = 0; i < v->mSheet->mAnimCount; i++)
    {
        const char* name = v->mSheet->mAnims[i].mName;
        // "name@N" is a second anim of the same name for another entity sharing the sheet: match the bare name too.
        size_t n = strlen(anim);
        if (strncmp(name, anim, n) != 0 || (name[n] != '\0' && name[n] != '@'))
        {
            continue;
        }
        // AnimatedSpriteRenderer.PlayAnim: the same unfinished anim keeps playing.
        if (!force && v->mAnim == static_cast<int>(i) && !v->mFinished)
        {
            return false;
        }
        v->mAnim = static_cast<int16_t>(i);
        v->mAnimIndex = 0;
        v->mTimer = 0;
        v->mFinished = false;
        if (fps > 0)
        {
            v->mFpsOverride = static_cast<int16_t>(fps); // sticks, like changing the Anim's frame rate
        }
        return true;
    }
    return false;
}

void Ecs_PlayFacing(Entity& e, const char* prefix, const char* variant)
{
    static const char kSuffix[4] = { 'l', 'r', 'u', 'd' };
    char name[24];
    const size_t n = strlen(prefix);
    const size_t v = variant ? strlen(variant) : 0;
    if (n + v + 3 > sizeof(name))
    {
        return;
    }
    memcpy(name, prefix, n);
    name[n] = '_';
    name[n + 1] = kSuffix[e.mFacing & 3];
    memcpy(name + n + 2, variant ? variant : "", v);
    name[n + 2 + v] = '\0';
    Ecs_Play(e, name);
}

void Ecs_SetFrame(Entity& e, int frame)
{
    if (e.mV)
    {
        e.mV->mAnim = -1;
        e.mV->mStaticFrame = static_cast<uint16_t>(frame);
        e.mV->mFinished = true;
    }
}

int Ecs_Frame(const Entity& e)
{
    if (!e.mV)
    {
        return 0;
    }
    const SpriteAnimData* anim = CurAnim(*e.mV);
    if (!anim)
    {
        return e.mV->mStaticFrame;
    }
    return anim->mFrameCount ? anim->mFrames[e.mV->mAnimIndex] + e.mV->mFrameBase : 0;
}

int Ecs_AnimIndex(const Entity& e)
{
    return e.mV ? e.mV->mAnimIndex : 0;
}

bool Ecs_AnimFinished(const Entity& e)
{
    return !e.mV || e.mV->mAnim < 0 || e.mV->mFinished;
}

const char* Ecs_AnimName(const Entity& e)
{
    const SpriteAnimData* anim = e.mV ? CurAnim(*e.mV) : nullptr;
    return anim ? anim->mName : "";
}

bool Ecs_AnimIs(const Entity& e, const char* name)
{
    const char* cur = Ecs_AnimName(e);
    const size_t n = strlen(name);
    return strncmp(cur, name, n) == 0 && (cur[n] == '\0' || cur[n] == '@');
}

void Ecs_Flicker(Entity& e, fx frames)
{
    if (!e.mV)
    {
        return;
    }
    if (frames == 0)
    {
        e.mV->mFlickering = false;
        e.mV->mVisible = true;
        return;
    }
    e.mV->mFlickering = true;
    e.mV->mFlickerTimer = frames;
}

int Ecs_SpriteWidth(const Entity& e)
{
    return (e.mV && e.mV->mSheet) ? e.mV->mSheet->mFrameWidth : 16;
}

int Ecs_SpriteHeight(const Entity& e)
{
    return (e.mV && e.mV->mSheet) ? e.mV->mSheet->mFrameHeight : 16;
}

// ---- GameObject helpers --------------------------------------------------------------------------------------
ECS_IWRAM_LEAF Rect Ecs_Hitbox(const Entity& e)
{
    return { FxTrunc(e.mT->mX), FxTrunc(e.mT->mY), e.mB->mW, e.mB->mH };
}

ECS_IWRAM_LEAF bool Ecs_Intersects(const Rect& a, const Rect& b)
{
    return a.mX < b.mX + b.mW && a.mX + a.mW > b.mX && a.mY < b.mY + b.mH && a.mY + a.mH > b.mY;
}

bool Ecs_Contains(const Rect& r, fx x, fx y)
{
    // Rectangle.Contains(Vector2): float compare against the integer edges.
    return x >= Fx(r.mX) && x < Fx(r.mX + r.mW) && y >= Fx(r.mY) && y < Fx(r.mY + r.mH);
}

fx Ecs_CenterX(const Entity& e)
{
    return e.mT->mX + Fx(e.mB->mW) / 2;
}

fx Ecs_CenterY(const Entity& e)
{
    return e.mT->mY + Fx(e.mB->mH) / 2;
}

fx Ecs_VisualCenterX(const Entity& e)
{
    return e.mT->mX - (e.mV ? e.mV->mOffX : 0) + Fx(Ecs_SpriteWidth(e) / 2);
}

fx Ecs_VisualCenterY(const Entity& e)
{
    return e.mT->mY - (e.mV ? e.mV->mOffY : 0) + Fx(Ecs_SpriteHeight(e) / 2);
}

void Ecs_MoveTowards(Entity& e, fx tx, fx ty, fx speed)
{
    const fx dx = tx - e.mT->mX;
    const fx dy = ty - e.mT->mY;
    const fx len = FxLength(dx, dy);
    if (len == 0)
    {
        // Vector2.Normalize of zero is NaN in the C#; standing still is the sane equivalent.
        e.mT->mVelX = e.mT->mVelY = 0;
        return;
    }
    // Vector2.Normalize (x and y times 1 / length), then * speed
    const fx inv = FxDiv(kFxOne, len);
    e.mT->mVelX = FxMul(FxMul(dx, inv), speed);
    e.mT->mVelY = FxMul(FxMul(dy, inv), speed);
}

void Ecs_FaceTowards(Entity& e, fx tx, fx ty)
{
    const fx dx = tx - e.mT->mX;
    const fx dy = ty - e.mT->mY;
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy))
    {
        e.mFacing = dx > 0 ? FACE_RIGHT : FACE_LEFT;
    }
    else
    {
        e.mFacing = dy > 0 ? FACE_DOWN : FACE_UP;
    }
}

void Ecs_CenterOffset(Entity& e, bool updatePos)
{
    if (!e.mV)
    {
        return;
    }
    e.mV->mOffX = Fx(Ecs_SpriteWidth(e) - e.mB->mW) / 2;
    e.mV->mOffY = Fx(Ecs_SpriteHeight(e) - e.mB->mH) / 2;
    if (updatePos)
    {
        e.mT->mX += e.mV->mOffX;
        e.mT->mY += e.mV->mOffY;
    }
}

bool Ecs_Separate(Entity& a, Entity& b)
{
    Obj o1 = ObjOf(a);
    Obj o2 = ObjOf(b);
    const bool x = SeparateAxis(o1, o2, true, true);
    o1 = ObjOf(a);
    o2 = ObjOf(b);
    const bool y = SeparateAxis(o1, o2, false, true);
    return x || y;
}

bool Ecs_SeparateRect(Entity& a, fx rx, fx ry, fx rw, fx rh, uint8_t allow)
{
    fx x = rx;
    fx y = ry;
    fx lx = rx;
    fx ly = ry;
    Obj o1 = ObjOf(a);
    Obj o2;
    o2.mX = &x;
    o2.mY = &y;
    o2.mLastX = &lx;
    o2.mLastY = &ly;
    o2.mW = rw;
    o2.mH = rh;
    o2.mAllow = allow;
    o2.mTouching = nullptr;
    o2.mImmovable = true;
    const bool sx = SeparateAxis(o1, o2, true, false);
    o1 = ObjOf(a);
    const bool sy = SeparateAxis(o1, o2, false, false);
    return sx || sy;
}

// ---- Systems -------------------------------------------------------------------------------------------------
namespace
{
struct DrawItem final
{
    Entity* mEntity;
    int32_t mKey;
};
// Working space for one call of Ecs_DoCollisions or of Ecs_Draw, which never run at once: they share it (EWRAM is
// nearly full).
union FrameScratch
{
    struct
    {
        Entity* mPicked[kMaxEntities];
        Entity* mAll[kMaxEntities];
        uint32_t mGroupsOf[kMaxEntities];
        uint32_t mWithOf[kMaxEntities];
        Entity* mTargets[kMaxEntities];
        Rect mBoxes[kMaxEntities];
        bool mUpdatable[kMaxEntities];
    } mCollisions;
    struct
    {
        DrawItem mSorted[kMaxVisuals];
    } mDraw;
};
FrameScratch gScratch ECS_EWRAM;
} // namespace

ECS_IWRAM_CODE void Ecs_DoCollisions(bool ignorePlayerMap, int screenX, int screenY)
{
    Entity* player = World_Player();

    // The entities a pass could pick by their class (which never changes), listed once in list order: while nothing
    // joins or leaves the list (gListGen), walking these is walking the list and skipping the rest.
    Entity** const sPicked = gScratch.mCollisions.mPicked;

    // Map colliders: the map's tiles, then every solid PartOfMap entity they overlap.
    int partsOfMap = 0;
    for (Entity* m = gHead; m; m = m->mNext)
    {
        if ((m->mClass->mFlags & ECF_PART_OF_MAP) && m->mClass->mCollided)
        {
            sPicked[partsOfMap++] = m;
        }
    }
    const uint32_t mapGen = gListGen;
    for (Entity* e = gHead; e; e = e->mNext)
    {
        if (!Updatable(*e) || !(e->mClass->mFlags & ECF_MAP_COLLISION))
        {
            continue;
        }
        const uint8_t allow = e->mB->mAllow;
        if (ignorePlayerMap && e == player)
        {
            e->mB->mAllow = TOUCH_NONE; // tile events still apply
        }
        Map_Collide(*e);
        Entity* m = nullptr;
        if (gListGen == mapGen)
        {
            int i = 0;
            for (; i < partsOfMap; i++)
            {
                Entity* mm = sPicked[i];
                if (mm == e || !Updatable(*mm) || !Ecs_Solid(*mm))
                {
                    continue;
                }
                if (Ecs_Intersects(Ecs_Hitbox(*mm), Ecs_Hitbox(*e)))
                {
                    mm->mClass->mCollided(*mm, *e);
                    if (gListGen != mapGen)
                    {
                        m = mm->mNext;
                        break;
                    }
                }
            }
        }
        else
        {
            m = gHead;
        }
        for (; m; m = m->mNext)
        {
            if (m == e || !Updatable(*m) || !(m->mClass->mFlags & ECF_PART_OF_MAP) || !Ecs_Solid(*m) ||
                !m->mClass->mCollided)
            {
                continue;
            }
            if (Ecs_Intersects(Ecs_Hitbox(*m), Ecs_Hitbox(*e)))
            {
                m->mClass->mCollided(*m, *e);
            }
        }
        e->mB->mAllow = allow;
    }

    // Collider -> target groups, a group at a time (CollisionGroups' dictionary, one group per target type): in the
    // order the screen's registration first named them - the player's, then each entity's own and the ones it collides
    // with, in list order.
    uint32_t order[32];
    int groups = 0;
    uint32_t seen = 0;
    auto note = [&](uint32_t bits)
    {
        // (the new bits, lowest first)
        for (bits &= ~seen; bits != 0; bits &= bits - 1)
        {
            const uint32_t b = bits & (0u - bits);
            seen |= b;
            order[groups++] = b;
        }
    };
    if (player)
    {
        note(player->mClass->mGroups);
        note(player->mClass->mCollidesWith);
    }
    // The list with each entity's groups and the groups it collides with (none without a callback), so the passes
    // below needn't go through every entity's class: taken again if the list changes.
    Entity** const sAll = gScratch.mCollisions.mAll;
    uint32_t* const sGroupsOf = gScratch.mCollisions.mGroupsOf;
    uint32_t* const sWithOf = gScratch.mCollisions.mWithOf;
    int all = 0;
    auto snapshot = [&](bool noting)
    {
        all = 0;
        for (Entity* e = gHead; e; e = e->mNext)
        {
            const EntityClass& k = *e->mClass;
            if (noting)
            {
                note(k.mGroups);
                note(k.mCollidesWith);
            }
            sAll[all] = e;
            sGroupsOf[all] = k.mGroups;
            sWithOf[all] = k.mCollided ? k.mCollidesWith : 0;
            all++;
        }
        return gListGen;
    };
    uint32_t snapGen = snapshot(true);
    // The targets in a group are listed once, rather than walking the whole entity list again for every collider:
    // while nothing joins or leaves the list (gListGen), the list's targets in order are exactly these.
    Entity** const sTargets = gScratch.mCollisions.mTargets;
    // The targets' hitboxes and whether they update: only a callback changes those, so they're taken again after
    // one rather than for every collider.
    Rect* const sBoxes = gScratch.mCollisions.mBoxes;
    bool* const sUpdatable = gScratch.mCollisions.mUpdatable;
    for (int g = 0; g < groups; g++)
    {
        const uint32_t group = order[g];
        if (gListGen != snapGen)
        {
            snapGen = snapshot(false);
        }
        int targets = 0;
        int colliders = 0;
        for (int k = 0; k < all; k++)
        {
            if (sGroupsOf[k] & group)
            {
                sTargets[targets++] = sAll[k];
            }
            if (sWithOf[k] & group)
            {
                sPicked[colliders++] = sAll[k];
            }
        }
        const uint32_t gen = gListGen;
        bool boxesTaken = false;
        int ci = 0;
        for (Entity* c = colliders ? sPicked[0] : nullptr; c;
             c = gListGen == gen ? (++ci < colliders ? sPicked[ci] : nullptr) : c->mNext)
        {
            if ((c->mClass->mCollidesWith & group) == 0 || !c->mClass->mCollided || !Updatable(*c))
            {
                continue;
            }
            // From the copy while the list is as it was, else (from where the copy got to) from the list itself.
            Entity* t = nullptr;
            bool done = false;
            if (gListGen == gen)
            {
                Rect hc = Ecs_Hitbox(*c);
                int i = 0;
                for (; i < targets; i++)
                {
                    if (!boxesTaken)
                    {
                        for (int k = 0; k < targets; k++)
                        {
                            sUpdatable[k] = Updatable(*sTargets[k]);
                            sBoxes[k] = Ecs_Hitbox(*sTargets[k]);
                        }
                        boxesTaken = true;
                    }
                    Entity* tt = sTargets[i];
                    if (tt == c || !sUpdatable[i] || !Ecs_Intersects(hc, sBoxes[i]))
                    {
                        continue;
                    }
                    c->mClass->mCollided(*c, *tt);
                    boxesTaken = false;
                    if (!Updatable(*c))
                    {
                        done = true;
                        break;
                    }
                    if (gListGen != gen)
                    {
                        t = tt->mNext;
                        break;
                    }
                    hc = Ecs_Hitbox(*c); // (the callback may have moved it)
                }
                if (i == targets)
                {
                    done = true;
                }
            }
            else
            {
                t = gHead;
            }
            for (; !done && t; t = t->mNext)
            {
                if ((t->mClass->mGroups & group) == 0 || t == c || !Updatable(*t))
                {
                    continue;
                }
                if (Ecs_Intersects(Ecs_Hitbox(*c), Ecs_Hitbox(*t)))
                {
                    c->mClass->mCollided(*c, *t);
                    boxesTaken = false;
                    if (!Updatable(*c))
                    {
                        break;
                    }
                }
            }
        }
    }

    // KeepOnScreen.
    const fx left = Fx(screenX);
    const fx top = Fx(screenY);
    const fx right = Fx(screenX + 160);
    const fx bottom = Fx(screenY + 160);
    for (Entity* e = gHead; e; e = e->mNext)
    {
        if (!Updatable(*e) || !(e->mClass->mFlags & ECF_KEEP_ON_SCREEN))
        {
            continue;
        }
        const Rect h = Ecs_Hitbox(*e);
        if (Fx(h.mX) < left)
        {
            e->mT->mX = left;
            e->mB->mTouching |= TOUCH_LEFT;
        }
        else if (Fx(h.mX + h.mW) > right)
        {
            e->mT->mX = right - Fx(e->mB->mW);
            e->mB->mTouching |= TOUCH_RIGHT;
        }
        if (Fx(h.mY) < top)
        {
            e->mT->mY = top;
            e->mB->mTouching |= TOUCH_UP;
        }
        else if (Fx(h.mY + h.mH) > bottom)
        {
            e->mT->mY = bottom - Fx(e->mB->mH);
            e->mB->mTouching |= TOUCH_DOWN;
        }
    }
}

void Ecs_BaseUpdate(Entity& e)
{
    if (e.mV && e.mV->mFlickering)
    {
        DoFlicker(*e.mV);
    }
}

ECS_IWRAM_CODE void Ecs_BasePostUpdate(Entity& e)
{
    Physics(e);
    if (e.mV)
    {
        AnimUpdate(*e.mV);
    }
}

void Ecs_UpdateOne(Entity& e)
{
    if (e.mClass->mUpdate)
    {
        e.mClass->mUpdate(e);
    }
    else
    {
        Ecs_BaseUpdate(e);
    }
    if (!e.mFreed)
    {
        Ecs_BasePostUpdate(e);
        if (e.mClass->mPostUpdate)
        {
            e.mClass->mPostUpdate(e);
        }
    }
}

void Ecs_ReleaseSpawned()
{
    for (Entity* e = gHead; e; e = e->mNext)
    {
        e->mSpawned = false;
    }
}

void Ecs_UpdateAll(Entity* skip)
{
    // Entities created during the loop are appended and so also update this frame, unlike the C#'s _newlySpawned;
    // stop at the entity that was last when the loop started.
    gUpdateLast = gTail;
    for (Entity* e = gHead; e;)
    {
        Entity* next = e->mNext;
        if (e != skip && Updatable(*e))
        {
            Ecs_UpdateOne(*e);
            // (a sub-entity it moved in just after itself updates this frame too, as the C#'s pool entities do)
            if (!e->mFreed)
            {
                next = e->mNext;
            }
        }
        if (e == gUpdateLast)
        {
            break;
        }
        e = next;
    }
    gUpdateLast = nullptr;
}

bool Ecs_CheckInteraction(Entity& player)
{
    // PlayState.CheckInteraction: the entity's box grows by one pixel on each side of the player's facing axis and
    // shifts one pixel towards the player.
    const int fx_ = -FacingX(static_cast<Facing>(player.mFacing));
    const int fy_ = -FacingY(static_cast<Facing>(player.mFacing));
    const int ax = fx_ < 0 ? -fx_ : fx_;
    const int ay = fy_ < 0 ? -fy_ : fy_;
    const Rect p = Ecs_Hitbox(player);
    for (Entity* e = gHead; e; e = e->mNext)
    {
        if (!Updatable(*e) || !e->mClass->mInteract)
        {
            continue;
        }
        Rect r = Ecs_Hitbox(*e);
        r.mX -= ax;
        r.mY -= ay;
        r.mW += 2 * ax;
        r.mH += 2 * ay;
        r.mX += fx_;
        r.mY += fy_;
        if (Ecs_Intersects(r, p) && e->mClass->mInteract(*e, static_cast<Facing>(player.mFacing)))
        {
            return true;
        }
    }
    return false;
}

void Ecs_FireEvent(int gameEvent)
{
    for (Entity* e = gHead; e; e = e->mNext)
    {
        if (!e->mFreed && !e->mOld && e->mClass->mOnEvent)
        {
            e->mClass->mOnEvent(*e, gameEvent);
        }
    }
}

int Ecs_KilledEnemies()
{
    int n = gBaseKilled;
    for (Entity* e = gHead; e; e = e->mNext)
    {
        if (!e->mOld && (e->mClass->mFlags & ECF_ENEMY) && !e->mExists)
        {
            n++;
        }
    }
    return n;
}

void Ecs_SetBaseKilledEnemies(int count)
{
    gBaseKilled = count;
}

// ---- Rendering -----------------------------------------------------------------------------------------------
namespace
{
// OBJ VRAM: one cached copy of each (sheet, frame) shown recently, shared by every sprite showing it.
constexpr int kFrameCacheSize = 112;
constexpr int kFrameCacheTiles = 760; // leave the rest of the 1024 tiles to the HUD and the dialogue box
struct FrameSlot final
{
    const SpriteSheetEntry* mSheet;
    uint16_t mFrame;
    int16_t mBlock;
    uint16_t mTiles;
    uint32_t mLastUsed;
};
FrameSlot gFrames[kFrameCacheSize] ECS_EWRAM;
// Where a sheet's frame was last found in gFrames, by a hash of the two (checked: a stale hint just means a search).
constexpr int kFrameHints = 1024;
uint8_t gFrameHint[kFrameHints] ECS_EWRAM;
int gFrameTiles = 0;
uint32_t gDrawTick = 1;

// OBJ palette banks gFirstBank .. + gBankCount - 1, by sheet palette.
constexpr int kPalBanks = 16;
const uint16_t* gBankPal[kPalBanks];
uint32_t gBankUsed[kPalBanks];
int gFirstBank = 0;
int gBankCount = 13;

ObjVramAllocator* gVram = nullptr;

int AcquireFrame(const SpriteSheetEntry* sheet, int frame)
{
    if (frame < 0 || frame >= sheet->mFrameCount)
    {
        frame = 0;
    }
    const uint32_t hash =
        ((static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sheet)) + static_cast<uint32_t>(frame)) * 2654435761u) >>
        (32 - 10); // (kFrameHints = 2^10)
    {
        const int hint = gFrameHint[hash];
        FrameSlot& s = gFrames[hint < kFrameCacheSize ? hint : 0];
        if (s.mSheet == sheet && s.mFrame == frame)
        {
            s.mLastUsed = gDrawTick;
            return s.mBlock;
        }
    }
    int freeSlot = -1;
    for (int i = 0; i < kFrameCacheSize; i++)
    {
        FrameSlot& s = gFrames[i];
        if (s.mSheet == sheet && s.mFrame == frame)
        {
            s.mLastUsed = gDrawTick;
            gFrameHint[hash] = static_cast<uint8_t>(i);
            return s.mBlock;
        }
        if (!s.mSheet && freeSlot < 0)
        {
            freeSlot = i;
        }
    }
    const int tiles = Sprite_FrameTiles(*sheet, frame);
    int block = -1;
    for (;;)
    {
        if (freeSlot >= 0 && gFrameTiles + tiles <= kFrameCacheTiles)
        {
            block = gVram->alloc(tiles * 32);
            if (block >= 0)
            {
                break;
            }
        }
        // Evict the least recently used frame not shown this frame.
        int lru = -1;
        for (int i = 0; i < kFrameCacheSize; i++)
        {
            if (gFrames[i].mSheet && gFrames[i].mLastUsed != gDrawTick &&
                (lru < 0 || gFrames[i].mLastUsed < gFrames[lru].mLastUsed))
            {
                lru = i;
            }
        }
        if (lru < 0)
        {
            return -1; // everything is on screen: this sprite doesn't fit
        }
        gVram->free(gFrames[lru].mBlock);
        gFrameTiles -= gFrames[lru].mTiles;
        gFrames[lru].mSheet = nullptr;
        if (freeSlot < 0)
        {
            freeSlot = lru;
        }
    }
    gFrameHint[hash] = static_cast<uint8_t>(freeSlot);
    FrameSlot& s = gFrames[freeSlot];
    s.mSheet = sheet;
    s.mFrame = static_cast<uint16_t>(frame);
    s.mBlock = static_cast<int16_t>(block);
    s.mTiles = static_cast<uint16_t>(tiles);
    s.mLastUsed = gDrawTick;
    gFrameTiles += tiles;
    LZ77UnCompVram(sheet->mFrameData + sheet->mFrameOffsets[frame], &tile_mem[4][block]);
    return block;
}

int AcquireBank(const uint16_t* pal)
{
    int freeBank = -1;
    for (int b = gFirstBank; b < gFirstBank + gBankCount; b++)
    {
        if (gBankPal[b] == pal)
        {
            gBankUsed[b] = gDrawTick;
            return b;
        }
        if (freeBank < 0 && (!gBankPal[b] || gBankUsed[b] != gDrawTick))
        {
            freeBank = b;
        }
    }
    if (freeBank < 0)
    {
        return -1;
    }
    gBankPal[freeBank] = pal;
    gBankUsed[freeBank] = gDrawTick;
    memcpy16(&pal_obj_bank[freeBank], pal, 16);
    return freeBank;
}

} // namespace

int Ecs_ObjBanksInUse()
{
    int end = gFirstBank;
    for (int b = gFirstBank; b < gFirstBank + gBankCount; b++)
    {
        end = gBankPal[b] && gBankUsed[b] == gDrawTick ? b + 1 : end;
    }
    return end;
}

namespace
{
int ObjPriority(int layer)
{
    if (layer < LAYER_MAP_BG2)
    {
        return 3; // between BG and BG2
    }
    if (layer == LAYER_MAP_BG2)
    {
        return 2; // on BG2
    }
    if (layer < LAYER_MAP_FG)
    {
        return 1; // between BG2 and FG
    }
    return 0;
}

// Front-to-back key: layer, then (ENTITIES only) the bottom of the hitbox within the screen, then RefLayer offsets.
int32_t DepthKey(const Entity& e, int depth)
{
    const Visual& v = *e.mV;
    if (v.mLayerRef && v.mLayerRef->mV && depth < 4)
    {
        return DepthKey(*v.mLayerRef, depth + 1) + v.mLayerOffset;
    }
    int32_t key = static_cast<int32_t>(v.mLayer) << 20;
    if (v.mLayer == LAYER_ENTITIES)
    {
        const int y = FxTrunc(e.mT->mY);
        // (y mod 160 by a multiply where it's exact, as Thumb has no divide)
        const uint32_t biased = static_cast<uint32_t>(y + 160 * 64);
        const int inGrid = biased < 81920 ? static_cast<int>(biased - ((biased * 52429u) >> 23) * 160)
                                          : ((y % 160) + 160) % 160;
        key += (inGrid + e.mB->mH + 256) << 6;
    }
    return key + 32;
}

int gLastOamEnd = 0;
int gLastOamFirst = 0;
} // namespace

void Ecs_SetRenderer(ObjVramAllocator& vram, int firstPalBank, int palBankCount)
{
    gVram = &vram;
    gFirstBank = firstPalBank;
    gBankCount = palBankCount;
    Ecs_FlushRenderCaches();
}

void Ecs_FlushFrameCache()
{
    for (FrameSlot& s : gFrames)
    {
        if (s.mSheet && gVram)
        {
            gVram->free(s.mBlock);
        }
        s.mSheet = nullptr;
    }
    gFrameTiles = 0;
}

void Ecs_FlushRenderCaches()
{
    Ecs_FlushFrameCache();
    for (int b = 0; b < kPalBanks; b++)
    {
        gBankPal[b] = nullptr;
        gBankUsed[b] = 0;
    }
}

static bool gCutsceneView = false;

void Ecs_SetCutsceneView(bool on)
{
    gCutsceneView = on;
}

ECS_IWRAM_CODE void Ecs_Draw(int camX, int camY, int viewX, int firstOam, int endOam, int shakeX, int shakeY,
                             bool wide)
{
    if (!gVram)
    {
        return;
    }
    gDrawTick++;
    DrawItem* const gDraw = gScratch.mDraw.mSorted;
    const int clipL = wide ? 0 : viewX;
    const int clipR = wide ? 240 : viewX + 160;
    int count = 0;
    for (Entity* e = gHead; e && count < kMaxVisuals; e = e->mNext)
    {
        if (e->mFreed || !e->mExists || !e->mV || !e->mV->mVisible || !e->mV->mSheet ||
            (gCutsceneView && !e->mCutsceneOwned))
        {
            continue;
        }
        if (e->mV->mOpacity <= FxF(0.05f) || e->mV->mScale <= 0)
        {
            continue;
        }
        gDraw[count].mEntity = e;
        gDraw[count].mKey = DepthKey(*e, 0);
        count++;
    }
    // Insertion sort, front (highest key) first; stable so list order breaks ties like the C#'s SpriteBatch.
    for (int i = 1; i < count; i++)
    {
        DrawItem item = gDraw[i];
        int j = i - 1;
        while (j >= 0 && gDraw[j].mKey < item.mKey)
        {
            gDraw[j + 1] = gDraw[j];
            j--;
        }
        gDraw[j + 1] = item;
    }

    int oam = firstOam;
    int affine = 0;
    for (int i = 0; i < count; i++)
    {
        Entity& e = *gDraw[i].mEntity;
        Visual& v = *e.mV;
        // (UIEntity ignores chaos's texture swaps)
        const SpriteSheetEntry& sheet = v.mLayer == LAYER_UI ? *v.mSheet : Sprite_ChaosSheet(*v.mSheet);
        int frame = Ecs_Frame(e);
        if (frame < 0)
        {
            frame = 0;
        }
        else if (frame >= sheet.mFrameCount)
        {
            frame = &sheet == v.mSheet ? 0 : frame % sheet.mFrameCount; // (a swapped sheet: wrapped round)
        }
        uint32_t firstPart;
        const int parts = static_cast<int>(Sprite_FrameParts(sheet, frame, &firstPart));
        if (parts == 0)
        {
            continue;
        }
        // Entity.Draw: Position - offset * scale; the C# scales about the frame centre.
        const int fw = sheet.mFrameWidth;
        const int fh = sheet.mFrameHeight;
        // (x * 1 is x: most sprites aren't scaled, so skip those soft float multiplies)
        const bool unscaled = v.mScale == kFxOne;
        const fx px = e.mT->mX - (unscaled ? v.mOffX : FxMul(v.mOffX, v.mScale));
        const fx py = e.mT->mY - (unscaled ? v.mOffY : FxMul(v.mOffY, v.mScale)) + v.mYPush;
        const int sx = FxTrunc(px) - camX + viewX + shakeX;
        const int sy = FxTrunc(py) - camY + shakeY;
        const int sw = unscaled ? fw : FxTrunc(FxMul(Fx(fw), v.mScale));
        const int sh = unscaled ? fh : FxTrunc(FxMul(Fx(fh), v.mScale));
        // Off the playfield entirely: skip.
        if (v.mLayer != LAYER_UI && (sx + sw <= clipL || sx >= clipR || sy + sh <= 0 || sy >= 160))
        {
            continue;
        }
        const bool rotated = e.mT->mRot != 0 && parts == 1;
        const bool scaled = v.mScale != kFxOne && parts == 1;
        const bool useAffine = (rotated || scaled) && affine < 32;
        if (oam + parts > endOam)
        {
            break;
        }
        const int bank = AcquireBank(sheet.mPalette);
        const int block = bank >= 0 ? AcquireFrame(&sheet, frame) : -1;
        if (block < 0)
        {
            continue;
        }
        const uint16_t mode = v.mWindow ? ATTR0_WINDOW : (v.mOpacity < FxF(0.95f) ? ATTR0_BLEND : 0);
        const int prio = ObjPriority(v.mLayer);
        if (useAffine)
        {
            const SpritePartData& part = sheet.mParts[firstPart];
            // texture = P * screen, P = R(-rot) / scale (rot clockwise on screen).
            const fx c = FxCos(e.mT->mRot);
            const fx s = FxSin(e.mT->mRot);
            // 1 / scale, remembered for the last few scales (Thumb has no divide: FxDiv's is a slow library call, and
            // a screen's scaled sprites mostly share a few scales)
            static fx sScales[4] = { kFxOne, kFxOne, kFxOne, kFxOne };
            static fx sInverses[4] = { kFxOne, kFxOne, kFxOne, kFxOne };
            static int sNextScale = 0;
            int si = 0;
            while (si < 4 && sScales[si] != v.mScale)
            {
                si++;
            }
            if (si == 4)
            {
                si = sNextScale;
                sNextScale = (sNextScale + 1) & 3;
                sScales[si] = v.mScale;
                sInverses[si] = FxDiv(kFxOne, v.mScale);
            }
            const fx inv = sInverses[si];
            const int cs = FxMul(c, inv).mRaw / 256; // 8.8
            const int sn = FxMul(s, inv).mRaw / 256;
            int pa = cs;
            int pb = sn;
            int pc = -sn;
            int pd = cs;
            if (v.mHFlip)
            {
                pa = -pa;
                pb = -pb;
            }
            if (v.mVFlip)
            {
                pc = -pc;
                pd = -pd;
            }
            OBJ_AFFINE* m = &obj_aff_mem[affine];
            m->pa = static_cast<s16>(pa);
            m->pb = static_cast<s16>(pb);
            m->pc = static_cast<s16>(pc);
            m->pd = static_cast<s16>(pd);
            // Double size when it grows or turns, so the corners aren't cut.
            const bool dbl = v.mScale > kFxOne || rotated;
            const int cx = sx + sw / 2;
            const int cy = sy + sh / 2;
            const int boxW = part.mWidth * (dbl ? 2 : 1);
            const int boxH = part.mHeight * (dbl ? 2 : 1);
            const int ox = cx - boxW / 2;
            const int oy = cy - boxH / 2;
            obj_set_attr(&oam_mem[oam],
                         (dbl ? ATTR0_AFF_DBL : ATTR0_AFF) | mode | ATTR0_4BPP |
                             (part.mShape << 14) | ATTR0_Y(oy & 0xFF),
                         (part.mSize << 14) | ATTR1_AFF_ID(affine) | ATTR1_X(ox & 0x1FF),
                         ATTR2_ID(block + part.mTileOffset) | ATTR2_PRIO(prio) | ATTR2_PALBANK(bank));
            affine++;
            oam++;
            continue;
        }
        const int frameWidth = sheet.mTilesW * 8;
        const int frameHeight = sheet.mTilesH * 8;
        for (int p = 0; p < parts; p++)
        {
            const SpritePartData& part = sheet.mParts[firstPart + p];
            const int x = sx + (v.mHFlip ? frameWidth - part.mX - part.mWidth : part.mX);
            const int y = sy + (v.mVFlip ? frameHeight - part.mY - part.mHeight : part.mY);
            obj_set_attr(&oam_mem[oam + p],
                         ATTR0_REG | mode | ATTR0_4BPP | (part.mShape << 14) | ATTR0_Y(y & 0xFF),
                         (part.mSize << 14) | ATTR1_X(x & 0x1FF) | (v.mHFlip ? ATTR1_HFLIP : 0) |
                             (v.mVFlip ? ATTR1_VFLIP : 0),
                         ATTR2_ID(block + part.mTileOffset) | ATTR2_PRIO(prio) | ATTR2_PALBANK(bank));
        }
        oam += parts;
    }
    // Hide what was drawn last frame and isn't now.
    const int hideEnd = gLastOamFirst == firstOam && gLastOamEnd > oam ? gLastOamEnd : oam;
    for (int i = oam; i < hideEnd; i++)
    {
        obj_hide(&oam_mem[i]);
    }
    gLastOamFirst = firstOam;
    gLastOamEnd = oam;
}

void Ecs_HideAll(int firstOam, int endOam)
{
    for (int i = firstOam; i < endOam; i++)
    {
        obj_hide(&oam_mem[i]);
    }
    gLastOamEnd = firstOam;
}
