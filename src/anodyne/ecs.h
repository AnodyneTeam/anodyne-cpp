#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "map_data.h"
#include "sprite_data.h"

// The entity system, ported from AnodyneSharp's Entity/GameObject/CollisionGroups.
//
// Components live in global fixed-size pools (an array plus a stack of free slots each), so nothing is allocated at
// run time and the GBA's 256K of EWRAM holds everything. An entity is a node of an intrusive doubly linked list (the
// screen's entities, in update order) that points at its components:
//   Transform - position, last position, velocity, acceleration, drag (GameObject)
//   Body      - hitbox size, collision sides, touching flags, immovable (GameObject)
//   Visual    - sprite sheet, animation, offset, draw layer, flicker, opacity/scale/rotation (Entity + renderer)
//   Brain     - the class's own state (a fixed-size blob the class casts to its struct)
// and at its class (EntityClass): the C#'s virtual methods (Update, PostUpdate, Collided, PlayerInteraction, the
// map tile callbacks...) as a table of function pointers, plus its [Collision] attribute.
//
// Systems run over the list each frame in PlayState's order: collisions (map, PartOfMap entities, collider ->
// target groups, KeepOnScreen), then Update + PostUpdate (physics) for each entity, then drawing (sorted by the C#
// draw order and depth into OAM, with OBJ VRAM and palette banks shared between sprites through caches).
//
// Units: positions are map pixels; velocities, accelerations and drag are px per second (the C#'s numbers); one
// update is 1/60 s. Timers count seconds, ticking kFxTick (GameTimes.DeltaTime) per update like the C#'s.
//
// fx is Fix16: a 16.16 fixed point number (int32 raw, 1.0 = 65536), the same type and rounding rules as the C#'s Fix
// (AnodyneSharp's Fix.cs), so the two games still match bit for bit while the GBA, which has no FPU, does integer
// maths instead of soft float calls. The rules, which both sides follow exactly:
//   - whole numbers: raw = n * 65536; float constants (FxF, the C#'s implicit float -> Fix): the float's value * 65536
//     rounded to nearest, halves away from zero (in double, at compile time)
//   - + and - wrap; fx * int, fx / int are raw * n, raw / n (truncating)
//   - FxMul: (int64)a * b / 65536, truncating (so x * 0.5f is x / 2 and -a * b is -(a * b), as in float);
//     FxDiv: ((int64)a << 16) / b, truncating, and dividing by zero gives INT32_MAX (a >= 0) or INT32_MIN
//   - FxMod (the C#'s %): raw % raw, the sign of a
//   - FxFloor: raw >> 16; FxTrunc (the C#'s (int) cast): raw / 65536
//   - FxSqrt: floor(sqrt(raw << 16)), 0 for v <= 0; FxLength(x, y): floor(sqrt(x.raw^2 + y.raw^2)), in 64 bits so
//     long vectors don't overflow
//   - FxLengthSq(x, y): (x.raw^2 + y.raw^2) >> 16 in 64 bits, saturating at INT32_MAX
//   - FxNormalize: x, y times FxDiv(1, FxLength(x, y)) (MonoGame's Vector2.Normalize; zero stays zero)
//   - FxSin/FxCos: the shared table (fix16_sin.h, 1025 entries: sin of a quarter turn in 1024 steps, rounded to
//     16.16), indexed by the angle in 1/4096 turns: (raw * 2^32 / (2 pi 65536) rounded), wrapped
//   - Rng_Unit: ((int64)Random.Next() << 16) / int.MaxValue
// It is a struct rather than a bare int so nothing converts to it by accident: whole numbers go through Fx(), C# float
// constants through FxF().

struct fx
{
    int32_t mRaw;

    fx() = default;
    // Only a literal 0 converts implicitly (it is a null pointer constant); other whole numbers go through Fx().
    constexpr fx(decltype(nullptr)) : mRaw(0)
    {
    }
    static constexpr fx Raw(int32_t raw)
    {
        fx v{};
        v.mRaw = raw;
        return v;
    }

    constexpr fx operator-() const
    {
        return Raw(static_cast<int32_t>(0u - static_cast<uint32_t>(mRaw)));
    }
    constexpr fx operator+(fx o) const
    {
        return Raw(static_cast<int32_t>(static_cast<uint32_t>(mRaw) + static_cast<uint32_t>(o.mRaw)));
    }
    constexpr fx operator-(fx o) const
    {
        return Raw(static_cast<int32_t>(static_cast<uint32_t>(mRaw) - static_cast<uint32_t>(o.mRaw)));
    }
    // Scaling by a whole number (the C#'s x * 2, x / 2f).
    constexpr fx operator*(int n) const
    {
        return Raw(static_cast<int32_t>(static_cast<uint32_t>(mRaw) * static_cast<uint32_t>(n)));
    }
    constexpr fx operator/(int n) const
    {
        return Raw(mRaw / n);
    }
    // A float would quietly narrow to a whole number here (0.12f * kFxTick is 0): FxMul(FxF(...), ...) instead.
    fx operator*(float) const = delete;
    fx operator*(double) const = delete;
    fx operator/(float) const = delete;
    fx operator/(double) const = delete;
    fx& operator*=(float) = delete;
    fx& operator*=(double) = delete;
    fx& operator/=(float) = delete;
    fx& operator/=(double) = delete;
    fx& operator+=(fx o)
    {
        return *this = *this + o;
    }
    fx& operator-=(fx o)
    {
        return *this = *this - o;
    }
    fx& operator*=(int n)
    {
        return *this = *this * n;
    }
    fx& operator/=(int n)
    {
        return *this = *this / n;
    }
    friend constexpr bool operator<(fx a, fx b)
    {
        return a.mRaw < b.mRaw;
    }
    friend constexpr bool operator>(fx a, fx b)
    {
        return a.mRaw > b.mRaw;
    }
    friend constexpr bool operator<=(fx a, fx b)
    {
        return a.mRaw <= b.mRaw;
    }
    friend constexpr bool operator>=(fx a, fx b)
    {
        return a.mRaw >= b.mRaw;
    }
    friend constexpr bool operator==(fx a, fx b)
    {
        return a.mRaw == b.mRaw;
    }
    friend constexpr bool operator!=(fx a, fx b)
    {
        return a.mRaw != b.mRaw;
    }
};
inline constexpr fx operator*(int n, fx v)
{
    return v * n;
}
fx operator*(float, fx) = delete;
fx operator*(double, fx) = delete;

constexpr fx kFxZero = fx::Raw(0);
constexpr fx kFxOne = fx::Raw(65536);
constexpr int kFps = 60;

constexpr fx Fx(int v)
{
    return fx::Raw(static_cast<int32_t>(static_cast<uint32_t>(v) << 16));
}
// Fx takes whole numbers only (a float or double would truncate): FxF for a constant, Rng_Unit for a draw.
fx Fx(float) = delete;
fx Fx(double) = delete;
// A C# float constant (e.g. 0.7f).
constexpr fx FxF(float v)
{
    const double d = static_cast<double>(v) * 65536.0;
    return fx::Raw(d >= 0 ? static_cast<int32_t>(d + 0.5) : -static_cast<int32_t>(-d + 0.5));
}
// A time in seconds (a C# float constant).
constexpr fx FxSec(float seconds)
{
    return FxF(seconds);
}
// GameTimes.DeltaTime for what stays float on both sides: the menus, dialog, HUD, credits and PlayState's own overlay
// timers (a few adds a frame, not the entities' maths).
constexpr float kUiDeltaTime = 1.0f / 60.0f;
// One update: GameTimes.DeltaTime, 1/60 s (1092 raw).
constexpr fx kFxTick = FxF(1.0f / 60.0f);
// Floor to whole pixels.
constexpr int FxFloor(fx v)
{
    return v.mRaw >> 16;
}
// C#'s (int) cast: towards zero.
constexpr int FxTrunc(fx v)
{
    return v.mRaw / 65536;
}
// Math.Ceiling.
constexpr int FxCeil(fx v)
{
    return -((-v.mRaw) >> 16);
}
constexpr fx FxMul(fx a, fx b)
{
    return fx::Raw(static_cast<int32_t>((static_cast<int64_t>(a.mRaw) * b.mRaw) / 65536));
}
#if defined(__arm__) && !defined(ANODYNE_SIM)
// FxDiv's division on the GBA, b != 0 (platform/gba/sys/fx_div_arm.s: ARM code in IWRAM, not libgcc's 64-bit one).
extern "C" int32_t FxDivRaw(int32_t a, int32_t b);
#endif
constexpr fx FxDiv(fx a, fx b)
{
#if defined(__arm__) && !defined(ANODYNE_SIM)
    if (!__builtin_is_constant_evaluated() && b.mRaw != 0)
    {
        return fx::Raw(FxDivRaw(a.mRaw, b.mRaw));
    }
#endif
    return b.mRaw == 0 ? fx::Raw(a.mRaw >= 0 ? INT32_MAX : INT32_MIN)
                       : fx::Raw(static_cast<int32_t>((static_cast<int64_t>(a.mRaw) * 65536) / b.mRaw));
}
// The C#'s % on floats (fmod): raw % raw, the sign of a.
constexpr fx FxMod(fx a, fx b)
{
    return fx::Raw(a.mRaw % b.mRaw);
}
// How far something moving at `perSecond` (px/s) goes in one update (GameTimes.DeltaTime * v).
// (FxMul's result, but in 32-bit pieces: the tick is under 1.0, so no 64-bit multiply is needed)
constexpr fx FxStep(fx perSecond)
{
    static_assert(kFxTick.mRaw > 0 && kFxTick.mRaw < 65536, "FxStep's split needs a tick under 1.0");
    const int32_t v = perSecond.mRaw;
    const uint32_t low = static_cast<uint32_t>(v & 0xFFFF) * static_cast<uint32_t>(kFxTick.mRaw);
    const int32_t floor = (v >> 16) * kFxTick.mRaw + static_cast<int32_t>(low >> 16);
    return fx::Raw(v < 0 && (low & 0xFFFF) != 0 ? floor + 1 : floor); // (FxMul rounds towards zero)
}
// As raw 16.16, for the bot link's JSON.
constexpr int FxRaw16(fx v)
{
    return v.mRaw;
}
// For what stays float on both sides (volumes, menus).
constexpr float FxToFloat(fx v)
{
    return static_cast<float>(v.mRaw) * (1.0f / 65536.0f);
}
constexpr fx FxAbs(fx v)
{
    return v.mRaw < 0 ? -v : v;
}
constexpr fx FxMin(fx a, fx b)
{
    return a < b ? a : b;
}
constexpr fx FxMax(fx a, fx b)
{
    return a > b ? a : b;
}
fx FxSqrt(fx v);
// Vector2.LengthSquared: (x.raw^2 + y.raw^2) >> 16 in 64 bits, saturating at INT32_MAX.
constexpr fx FxLengthSq(fx x, fx y)
{
    const int64_t sq = (static_cast<int64_t>(x.mRaw) * x.mRaw + static_cast<int64_t>(y.mRaw) * y.mRaw) >> 16;
    return fx::Raw(sq > INT32_MAX ? INT32_MAX : static_cast<int32_t>(sq));
}
// Vector2.Length.
fx FxLength(fx x, fx y);
// Vector2.Normalize (MonoGame's: x and y times 1 / Length; a zero vector stays zero).
inline void FxNormalize(fx& x, fx& y)
{
    const fx inv = FxDiv(kFxOne, FxLength(x, y));
    x = FxMul(x, inv);
    y = FxMul(y, inv);
}
// MathF.Sin/Cos of an angle in radians.
fx FxSin(fx radians);
fx FxCos(fx radians);
constexpr fx kFxPi = FxF(3.14159265358979f); // MathHelper.Pi
constexpr fx kFxTau = FxF(6.28318530717959f); // MathHelper.TwoPi

// Parabola_Thing's offset at time t: t * (period - t) / (half_period * half_period) * height.
inline fx ParabolaOffset(fx t, fx period, int height)
{
    const fx half = period / 2;
    return FxDiv(FxMul(t, period - t), FxMul(half, half)) * height;
}

// MathUtilities.MoveTo: moves value towards target by speed (per second) * dt; true once it is there.
bool MoveTo(fx& value, fx target, fx speedPerSecond);

// Rng: the game's random numbers (GlobalState.RNG), .NET's System.Random exactly, so a run seeded like the C#'s
// (RecordingManager.DeterministicSeed) rolls the same numbers.
void Rng_Seed(int seed);
// The C#'s Random is seeded from the clock; the GBA has none, so the title seeds it from how long the player took
// to pick a file (unless Rng_Seed already chose a seed, as the bot does to match a C# run).
void Rng_SeedFromPlayer(uint32_t entropy);
// How many numbers have been drawn since Rng_Seed (the parity traces compare it with the C#'s).
uint32_t Rng_Calls();
// Random.Next(): [0, int.MaxValue).
int Rng_Next();
// Random.NextDouble().
double Rng_NextDouble();
// (float)Random.NextDouble().
fx Rng_Unit();
// Random.Next(lo, hi).
int Rng_Range(int lo, int hi);

// RecordingManager's frame count: one a game frame (the main loop's), whatever is showing.
void Game_CountFrame();
uint32_t Game_Frame();
// Parity replays: the frame count picks up from `frame` (after the title, whose frames the two games count differently).
void Game_SetFrame(uint32_t frame);
// RecordingManager.Now - then, in seconds, for `then` a Game_Frame() (the C#'s TimeSpan of frame ticks at 60 a second).
// In 16.16 from the ticks: (now - then) * 65536 / TimeSpan.TicksPerSecond, truncated.
fx Game_SecondsSince(uint32_t thenFrame);

// FSM TimerState: named timers on one clock (room for N). A timer fires once the clock reaches it and is re-armed its
// period on from the clock's time then; Advance moves the clock on without firing anything. Reset drops the timers but
// not the clock.
template <int N>
struct TimerState final
{
    struct Timer
    {
        fx mAt;
        fx mMax;
        uint8_t mId;
    };
    fx mCurrent;
    uint8_t mCount;
    Timer mTimers[N];

    // AddTimer: due `time` from now, after the ones due at the same time.
    void Add(fx time, uint8_t id)
    {
        if (mCount >= N)
        {
            return;
        }
        const fx at = mCurrent + time;
        int i = mCount++;
        for (; i > 0 && mTimers[i - 1].mAt > at; i--)
        {
            mTimers[i] = mTimers[i - 1];
        }
        mTimers[i] = { at, time, id };
    }
    void Reset()
    {
        mCount = 0;
    }
    void Tick()
    {
        mCurrent += kFxTick;
    }
    void Advance(fx time)
    {
        mCurrent += time;
    }
    // The next timer due (re-armed) or -1: Update's loop, one event at a time.
    int Pop()
    {
        if (mCount == 0 || mTimers[0].mAt > mCurrent)
        {
            return -1;
        }
        const Timer t = mTimers[0];
        mCount--;
        for (int i = 0; i < mCount; i++)
        {
            mTimers[i] = mTimers[i + 1];
        }
        Add(t.mMax, t.mId);
        return t.mId;
    }
};

enum Touching : uint8_t
{
    TOUCH_NONE = 0,
    TOUCH_LEFT = 1,
    TOUCH_RIGHT = 2,
    TOUCH_UP = 4,
    TOUCH_DOWN = 8,
    TOUCH_ANY = 15,
};

// Entity.Facing, in the C#'s order.
enum Facing : uint8_t
{
    FACE_LEFT = 0,
    FACE_RIGHT = 1,
    FACE_UP = 2,
    FACE_DOWN = 3,
};
// Entity.FacingDirection as unit offsets.
int FacingX(Facing f);
int FacingY(Facing f);
Facing FlipFacing(Facing f);

// DrawOrder (DrawingUtilities.cs), the layers entities use.
enum DrawLayer : uint8_t
{
    LAYER_BACKGROUND = 0,
    LAYER_MAP_BG,
    LAYER_PLAYER_REFLECTION,
    LAYER_FOOT_OVERLAY,
    LAYER_MAP_BG2,
    LAYER_VERY_BG_ENTITIES,
    LAYER_BG_ENTITIES,
    LAYER_SHADOWS,
    LAYER_PARTICLES,
    LAYER_ROLLERS,
    LAYER_ENTITIES,
    LAYER_FG_SPRITES,
    LAYER_MAP_FG,
    LAYER_DEC_OVER,
    LAYER_UI, // anything drawn as UI (UIEntity / the header in the C#): screen coordinates, in front of everything
};

// Collision groups: the C# collides by type ([Collision(typeof(Player), typeof(Broom))] = "call my Collided for every
// Player and Broom I overlap"). Each class is a member of some groups and collides with some groups.
enum CollisionGroup : uint32_t
{
    GROUP_PLAYER = 1u << 0,
    GROUP_BROOM = 1u << 1,
    GROUP_DUST = 1u << 2,
    GROUP_SHIELDY = 1u << 3,
    GROUP_DASH_TRAP = 1u << 4,
    GROUP_WHIRLPOOL = 1u << 5,
    GROUP_DOOR = 1u << 6,
    GROUP_MOVER = 1u << 7,
    GROUP_STOP_MARKER = 1u << 8,
    GROUP_SILVERFISH = 1u << 9,
    GROUP_RAT = 1u << 10,
    GROUP_SPIKE_ROLLER = 1u << 11, // BaseSpikeRoller
    GROUP_CIRCUS_ARTHUR = 1u << 12, // CircusFolks.Arthur
    GROUP_BLUE_MITRA = 1u << 13,    // BlueBriar.BlueMitra
    GROUP_MIST = 1u << 14,          // BlueThorn.Mist
    GROUP_BLUE_THORN = 1u << 15,    // BlueThorn
};

// Map tile events (Map.CollideTile -> the Entity virtuals).
enum MapEvent : uint8_t
{
    MAPEV_FALL,       // Fall(fallPoint): a hole's middle
    MAPEV_SLOW,       // SlowTile
    MAPEV_PUDDLE,     // Puddle
    MAPEV_REFLECTION, // Reflection
    MAPEV_LADDER,     // Ladder
    MAPEV_CONVEYOR,   // Conveyor(direction)
    MAPEV_GRASS,      // Grass
    MAPEV_SPIKE,      // a spike's middle (the C# only hurts the player)
};

struct Entity;

struct Transform final
{
    fx mX;
    fx mY;
    fx mLastX;
    fx mLastY;
    fx mVelX;
    fx mVelY;
    fx mAccX;
    fx mAccY;
    fx mDragX;
    fx mDragY;
    fx mRot; // radians, 16.16
    fx mAngVel;
    fx mAngAcc;
};

struct Body final
{
    int16_t mW;
    int16_t mH;
    uint8_t mAllow; // allowCollisions (Solid = ANY)
    uint8_t mTouching;
    uint8_t mWasTouching;
    bool mImmovable;
    bool mMapInteraction; // tile events apply (Entity.MapInteraction)
    bool mHoleAsWall;
};

// The renderer's per-frame state of a sprite.
struct Visual final
{
    const SpriteSheetEntry* mSheet;
    int16_t mAnim; // -1: static frame
    uint16_t mAnimIndex;
    fx mTimer;
    uint16_t mStaticFrame;
    uint16_t mFrameBase;  // added to "variant-relative" anim frames (the C#'s per-instance frame offset)
    int16_t mFpsOverride; // Play(name, force, newFramerate); 0 = the anim's own
    bool mFinished;
    bool mVisible;
    bool mFlickering;
    bool mHFlip;
    bool mVFlip;
    bool mWindow;        // a Light: drawn as an OBJ-window shape that cuts the darkness, not as a sprite
    uint8_t mLayer;      // DrawLayer
    int8_t mLayerOffset; // RefLayer: in front (+) / behind (-) mLayerRef by this much
    Entity* mLayerRef;   // RefLayer parent (nullptr: own layer)
    fx mOffX;            // Entity.offset: sprite top-left = position - offset
    fx mOffY;
    fx mYPush;        // sinking into the ground
    fx mFlickerTimer; // frames left (16.16)
    fx mFlickerFreq;
    fx mOpacity; // 1.0 = opaque
    fx mScale;   // 1.0 = normal
};

constexpr int kBrainBytes = 160;

struct Brain final
{
    alignas(8) uint8_t mData[kBrainBytes];
};

// An entity class (the C# class and its virtual methods). Missing functions do nothing.
struct EntityClass final
{
    const char* mName;
    void (*mUpdate)(Entity& e);
    void (*mPostUpdate)(Entity& e); // runs after the physics step (the C# overrides that call base.PostUpdate first)
    void (*mCollided)(Entity& self, Entity& other);
    bool (*mInteract)(Entity& self, Facing playerFacing); // Interactable.PlayerInteraction
    void (*mMapEvent)(Entity& self, MapEvent ev, uint8_t dir, fx tileX, fx tileY);
    void (*mOnEvent)(Entity& self, int gameEvent); // GameEvent (see game_state.h)
    void (*mDestroy)(Entity& self);                // the entity is being freed
    uint32_t mGroups;       // groups it is a target in
    uint32_t mCollidesWith; // groups whose members it collides with
    uint8_t mFlags;         // ECF_*
    // Fall(): what the base class does when a hole is under the middle: nothing special = exists = false.
};

enum EntityClassFlags : uint8_t
{
    ECF_MAP_COLLISION = 1 << 0,  // collides with the map
    ECF_PART_OF_MAP = 1 << 1,    // map colliders collide with it (gates, chests...)
    ECF_KEEP_ON_SCREEN = 1 << 2, // can't leave the current screen
    ECF_ENEMY = 1 << 3,          // [Enemy]: counts for ENEMIES_KILLED once it stops existing
    ECF_ALWAYS_SPAWN = 1 << 4,   // ignore EntityPreset.Alive when loading the screen
    ECF_INTERACTABLE = 1 << 5,   // Accept checks it
    ECF_PROBE_OWNED = 1 << 6,    // the bot link lists it even while the player owns it (Player.follower)
};

struct Entity final
{
    Entity* mPrev;
    Entity* mNext;
    const EntityClass* mClass;
    Transform* mT;
    Body* mB;
    Visual* mV;
    Brain* mBrain;
    const MapEntity* mPreset; // nullptr: created at run time
    Entity* mParent;          // the entity this one belongs to (bullets, particles, the broom...)
    uint8_t mFacing;
    bool mExists;
    bool mPlayerOwned; // the player's sub-entities: kept across screens and maps
    bool mOld;         // left over from the previous screen: drawn during the transition, not updated
    bool mFreed;
    bool mSpawned;      // GlobalState.SpawnEntity: in the play state's lists (updates, collisions) from the next update
    bool mKeep;         // a pooled sub-entity (EntityPool): kept while it doesn't exist, for its owner to reuse
    bool mCutsceneOwned; // a CutsceneState's own entity: the only kind drawn while the cutscene shows another map
    uint8_t mDepthRank; // set by the renderer
    uint16_t mIndex;    // pool index

    template <typename T>
    T& Data()
    {
        static_assert(sizeof(T) <= kBrainBytes, "brain data too big");
        return *reinterpret_cast<T*>(mBrain->mData);
    }
};

// ---- Pools ---------------------------------------------------------------------------------------------------
// Fixed-capacity pool: Alloc pops a free slot (zeroed), Free pushes it back.
template <typename T, int N>
struct Pool final
{
    T mItems[N];
    uint16_t mFree[N];
    int mFreeCount; // Pool lives in uninitialised EWRAM: Reset before use
    int mUsed;
    int mPeak;

    void Reset()
    {
        mFreeCount = N;
        for (int i = 0; i < N; i++)
        {
            mFree[i] = static_cast<uint16_t>(N - 1 - i);
        }
        mUsed = 0;
        mPeak = 0;
    }
    T* Alloc()
    {
        if (mFreeCount == 0)
        {
            return nullptr;
        }
        T* item = &mItems[mFree[--mFreeCount]];
        *item = T();
        mUsed++;
        mPeak = mUsed > mPeak ? mUsed : mPeak;
        return item;
    }
    void Free(T* item)
    {
        if (item)
        {
            mFree[mFreeCount++] = static_cast<uint16_t>(item - mItems);
            mUsed--;
        }
    }
    int IndexOf(const T* item) const
    {
        return static_cast<int>(item - mItems);
    }
};

constexpr int kMaxEntities = 160;
constexpr int kMaxVisuals = 140;
constexpr int kMaxBrains = 120;

// ---- Entity lifetime -----------------------------------------------------------------------------------------
void Ecs_Init();

// Creates an entity of `cls` at map pixel (x, y) with the components it asks for, linked at the end of the list (the
// C#'s _newlySpawned: it starts updating next frame). Returns nullptr when a pool is exhausted.
enum EntityParts : uint8_t
{
    PART_VISUAL = 1 << 0,
    PART_BRAIN = 1 << 1,
};
Entity* Ecs_Create(const EntityClass& cls, fx x, fx y, uint8_t parts, const MapEntity* preset = nullptr,
                   Entity* parent = nullptr);
// Frees an entity now (and nothing else: sub-entities are freed by their owner's mDestroy or by Ecs_ClearScreen).
void Ecs_Free(Entity& e);
// GlobalState.SpawnEntity (_newlySpawned): left out of everything until the next Ecs_UpdateAll.
// Pre-made, not existing until its owner sets mExists: Ecs_FreeDead leaves it be.
inline void Ecs_KeepPooled(Entity& e)
{
    e.mKeep = true;
    e.mExists = false;
}

inline void Ecs_MarkSpawned(Entity& e)
{
    e.mSpawned = true;
}
// The top of PlayState.Update: _newlySpawned joins the lists, so a spawn from a collision waits a whole frame too.
void Ecs_ReleaseSpawned();
// All entities, in update order.
Entity* Ecs_First();
// PlayState.LoadGridEntities' order: the screen's presets, then the player's sub-entities.
void Ecs_MovePlayerOwnedLast();
// Moves `e` to just after `after` in the update order (a sub-entity made late that the C# has after its owner).
void Ecs_MoveAfter(Entity& e, Entity& after);
// Moves `e` to the end of the update order.
void Ecs_MoveLast(Entity& e);
// Frees every entity of the previous screen (mOld) / every entity not owned by the player.
void Ecs_FreeOld();
void Ecs_MarkScreenOld();
void Ecs_FreeAllButPlayer();
// Frees the run-time entities (no preset, not an enemy, not the player's) that stopped existing.
void Ecs_FreeDead();
int Ecs_Count();
// Existing entities of class `cls` whose parent is `parent` (an EntityPool's Alive count).
int Ecs_CountChildren(const Entity& parent, const EntityClass& cls);

// Entity.Play / SetFrame / PlayFacing on the visual. Play returns true when the anim changed (AnimationChanged).
bool Ecs_SetSheet(Entity& e, const char* sheet, int frameW = 0, int frameH = 0);
// AnimatedSpriteRenderer.SetTexture: another sheet with the same anims (the one playing carries on).
bool Ecs_SetTexture(Entity& e, const char* sheet, int frameW = 0, int frameH = 0);
bool Ecs_Play(Entity& e, const char* anim, bool force = false, int fps = 0);
// `variant`: "@N" for a sheet's Nth anim set of that name.
void Ecs_PlayFacing(Entity& e, const char* prefix, const char* variant = nullptr);
void Ecs_SetFrame(Entity& e, int frame);
int Ecs_Frame(const Entity& e);        // the sheet frame shown
inline void Ecs_SetFrameBase(Entity& e, int base)
{
    if (e.mV)
    {
        e.mV->mFrameBase = static_cast<uint16_t>(base);
    }
}
int Ecs_AnimIndex(const Entity& e);    // FrameIndex
bool Ecs_AnimFinished(const Entity& e);
const char* Ecs_AnimName(const Entity& e);
bool Ecs_AnimIs(const Entity& e, const char* name);
void Ecs_Flicker(Entity& e, fx frames);
int Ecs_SpriteWidth(const Entity& e);
int Ecs_SpriteHeight(const Entity& e);

// ---- GameObject helpers --------------------------------------------------------------------------------------
inline bool Ecs_Solid(const Entity& e)
{
    return e.mB->mAllow != TOUCH_NONE;
}
inline void Ecs_SetSolid(Entity& e, bool solid)
{
    e.mB->mAllow = solid ? TOUCH_ANY : TOUCH_NONE;
}
struct Rect final
{
    int mX;
    int mY;
    int mW;
    int mH;
};
// GameObject.Hitbox: position truncated to whole pixels.
Rect Ecs_Hitbox(const Entity& e);
bool Ecs_Intersects(const Rect& a, const Rect& b);
bool Ecs_Contains(const Rect& r, fx x, fx y);
fx Ecs_CenterX(const Entity& e);
fx Ecs_CenterY(const Entity& e);
// Entity.VisualCenter.
fx Ecs_VisualCenterX(const Entity& e);
fx Ecs_VisualCenterY(const Entity& e);
// GameObject.MoveTowards: velocity = normalize(target - position) * speed.
void Ecs_MoveTowards(Entity& e, fx tx, fx ty, fx speed);
// Entity.FaceTowards.
void Ecs_FaceTowards(Entity& e, fx tx, fx ty);
// Entity.CenterOffset.
void Ecs_CenterOffset(Entity& e, bool updatePos = true);

// GameObject.Separate(a, b): pushes them apart (immovable ones stay). Object2 being an entity uses half the overlap
// bias of a map tile.
bool Ecs_Separate(Entity& a, Entity& b);
// Against an immovable rectangle (map tiles, thin walls): `bias` = OVERLAP_BIAS for tiles.
bool Ecs_SeparateRect(Entity& a, fx rx, fx ry, fx rw, fx rh, uint8_t allow);

// ---- Systems -------------------------------------------------------------------------------------------------
// CollisionGroups.DoCollision. ignorePlayerMap: the player doesn't collide with the map tiles (screen transitions).
void Ecs_DoCollisions(bool ignorePlayerMap, int screenX, int screenY);
// Update + PostUpdate of every existing, not-old entity, in list order (the player is updated by the caller).
void Ecs_UpdateAll(Entity* skip);
void Ecs_UpdateOne(Entity& e);
// Entity.Update / PostUpdate base parts (flicker; physics + animation).
void Ecs_BaseUpdate(Entity& e);
void Ecs_BasePostUpdate(Entity& e);
// Accept pressed: the first Interactable in reach of the player's facing side whose PlayerInteraction returns true.
bool Ecs_CheckInteraction(Entity& player);
// Fires a game event at every entity that listens.
void Ecs_FireEvent(int gameEvent);
// ENEMIES_KILLED: enemies of this screen that stopped existing (plus the ones already dead when it loaded).
int Ecs_KilledEnemies();
void Ecs_SetBaseKilledEnemies(int count);

// Draws every visible entity: camX/camY = camera top-left in map pixels, viewX = playfield left edge on screen.
// OAM entries firstOam .. endOam-1 are the renderer's (sorted by layer and depth, front first).
// CutsceneState.Warp: only the cutscene's own entities are drawn (the play state isn't).
void Ecs_SetCutsceneView(bool on);
// The OBJ palette banks Ecs_Draw used this frame end before this one.
int Ecs_ObjBanksInUse();
void Ecs_Draw(int camX, int camY, int viewX, int firstOam, int endOam, int shakeX, int shakeY);
// The OBJ VRAM the renderer caches frames in and the OBJ palette banks it may use.
class ObjVramAllocator;
void Ecs_SetRenderer(ObjVramAllocator& vram, int firstPalBank, int palBankCount);
// Hides OAM entries firstOam .. endOam-1 (the world isn't shown).
void Ecs_HideAll(int firstOam, int endOam);
// Forget cached frames/palettes (map change).
// Frees the sprite frames cached in OBJ VRAM (they reload as they are drawn), e.g. when the cache's scattered
// blocks leave no room for something bigger.
void Ecs_FlushFrameCache();
void Ecs_FlushRenderCaches();
// Peak pool usage, for the debug log.
void Ecs_Stats(int* entities, int* visuals, int* brains);

// Callbacks into the world (world.cpp): the map's collision and the player.
void Map_Collide(Entity& e);
uint8_t Map_CollisionAllowAt(fx x, fx y); // Map.GetCollisionData
Entity* World_Player();
// The top-left of the screen being shown (UI coordinates' origin in the world).
fx World_CamX();
fx World_CamY();
