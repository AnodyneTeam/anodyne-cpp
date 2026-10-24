#pragma once

#include <stdint.h>

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
// Units: positions are 16.16 fixed point map pixels; velocities, accelerations and drag are 16.16 px per second
// (the C#'s numbers, scaled); one update is 1/60 s. Timers count 16.16 frames (Fx(30) = half a second).

using fx = int32_t;

constexpr int kFxShift = 16;
constexpr fx kFxOne = 1 << kFxShift;
constexpr int kFps = 60;

constexpr fx Fx(int v)
{
    return v * kFxOne;
}
// A C# float constant (e.g. 0.7f) as fixed point.
constexpr fx FxF(float v)
{
    return static_cast<fx>(v * 65536.0f + (v < 0 ? -0.5f : 0.5f));
}
// Seconds -> 16.16 frames.
constexpr fx FxSec(float seconds)
{
    return FxF(seconds * kFps);
}
// Floor to whole pixels.
constexpr int FxFloor(fx v)
{
    return v >> kFxShift;
}
// C#'s (int) cast: towards zero.
constexpr int FxTrunc(fx v)
{
    return v < 0 ? -((-v) >> kFxShift) : (v >> kFxShift);
}
inline fx FxMul(fx a, fx b)
{
    return static_cast<fx>((static_cast<int64_t>(a) * b) >> kFxShift);
}
inline fx FxDiv(fx a, fx b)
{
    return static_cast<fx>((static_cast<int64_t>(a) << kFxShift) / b);
}
// How far something moving at `perSecond` (Fx px/s) goes in one update.
inline fx FxStep(fx perSecond)
{
    return perSecond / kFps;
}
fx FxSqrt(fx v);
fx FxLength(fx x, fx y);
// sin/cos of an angle in 16.16 radians, as 16.16.
fx FxSin(fx radians);
fx FxCos(fx radians);
constexpr fx kFxPi = 205887;  // pi in 16.16
constexpr fx kFxTau = 411775; // 2 pi

// MathUtilities.MoveTo: moves value towards target by speed (per second) * dt; true once it is there.
bool MoveTo(fx& value, fx target, fx speedPerSecond);

// Rng: the game's random numbers (GlobalState.RNG). Not the .NET generator, so runs don't repeat the C#'s rolls.
uint32_t Rng_Next();
// [0, 1) as 16.16.
fx Rng_Unit();
// [lo, hi) like System.Random.Next(lo, hi).
int Rng_Range(int lo, int hi);

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
    uint16_t mTimer;
    uint16_t mStaticFrame;
    uint16_t mFrameBase;  // added to "variant-relative" anim frames (the C#'s per-instance frame offset)
    int16_t mFpsOverride; // Play(name, force, newFramerate); 0 = the anim's own
    bool mFinished;
    bool mVisible;
    bool mFlickering;
    bool mHFlip;
    bool mVFlip;
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
// All entities, in update order.
Entity* Ecs_First();
// Frees every entity of the previous screen (mOld) / every entity not owned by the player.
void Ecs_FreeOld();
void Ecs_MarkScreenOld();
void Ecs_FreeAllButPlayer();
// Frees the run-time entities (no preset, not an enemy, not the player's) that stopped existing.
void Ecs_FreeDead();
int Ecs_Count();

// Entity.Play / SetFrame / PlayFacing on the visual. Play returns true when the anim changed (AnimationChanged).
bool Ecs_SetSheet(Entity& e, const char* sheet, int frameW = 0, int frameH = 0);
bool Ecs_Play(Entity& e, const char* anim, bool force = false, int fps = 0);
void Ecs_PlayFacing(Entity& e, const char* prefix);
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
void Ecs_Draw(int camX, int camY, int viewX, int firstOam, int endOam, int shakeX, int shakeY);
// The OBJ VRAM the renderer caches frames in and the OBJ palette banks it may use.
class ObjVramAllocator;
void Ecs_SetRenderer(ObjVramAllocator& vram, int firstPalBank, int palBankCount);
// Hides OAM entries firstOam .. endOam-1 (the world isn't shown).
void Ecs_HideAll(int firstOam, int endOam);
// Forget cached frames/palettes (map change).
void Ecs_FlushRenderCaches();
// Peak pool usage, for the debug log.
void Ecs_Stats(int* entities, int* visuals, int* brains);

// Callbacks into the world (world.cpp): the map's collision and the player.
void Map_Collide(Entity& e);
uint8_t Map_CollisionAllowAt(fx x, fx y); // Map.GetCollisionData
Entity* World_Player();
