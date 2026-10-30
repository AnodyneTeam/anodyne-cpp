#pragma once

#include <stdint.h>

#include "entity_types.h"

// Layout of the content-compiler generated map registry (map_registry.h).

struct MapPalData final
{
    uint32_t mPalSize = 0;
    const uint16_t* mPalData = nullptr;
};

struct MapTileSetData final
{
    uint32_t mTileSetSize = 0;
    const uint8_t* mTileSetData = nullptr;
};

// One map layer as 16x16 tile indices (0 = empty), little-endian u16, BIOS-LZ77 packed. mWidth == 0: no such layer.
struct MapLayerData final
{
    uint16_t mWidth = 0; // in 16x16 tiles
    uint16_t mHeight = 0;
    uint32_t mUnpackedSize = 0;
    const uint8_t* mPacked = nullptr;
};

// The drawn map: BG, BG2 and FG layers plus, for each 16x16 tile, its four 8x8 screen entries (TL, TR, BL, BR; GBA
// screen-entry format: 10 bit tile index + H/V flip) so BG screen entries can be built on the fly while scrolling.
struct MapScreenBlockData final
{
    uint32_t mTile16Count = 0;
    const uint16_t* mTile16Entries = nullptr;
    MapLayerData mLayers[3]; // 0 = BG, 1 = BG2, 2 = FG
};

// Collision for the map's 16x16 cells (see tools/content_compiler/collision_compiler.hpp): one byte per cell, an
// index into mClasses (3 bytes each: allowed Touching sides, CollisionEventType, direction), BIOS-LZ77 packed.
struct MapCollisionData final
{
    uint16_t mCellsW = 0;
    uint16_t mCellsH = 0;
    uint16_t mClassCount = 0;
    uint32_t mUnpackedSize = 0;
    const uint8_t* mClasses = nullptr;
    const uint8_t* mPacked = nullptr;
    uint16_t mTileCount = 0;          // every tile of the tileset:
    const uint8_t* mTiles = nullptr; // its own (allow, event, direction), for ChangeTile
};

// The BG layer's animated tiles (Map's AnimatedTile): mAnims holds 8 u16s each (the 16x16 tile; the four 8x8 VRAM tiles
// its frames go in and the BG layer draws it from, TL TR BL BR; the frame count; fps; the first frame), mFrames each
// frame's four 8bpp tiles.
struct MapTileAnimData final
{
    uint32_t mCount = 0;
    const uint16_t* mAnims = nullptr;
    const uint8_t* mFrames = nullptr;
};

struct MapEntry final
{
    MapPalData mPalData;
    MapTileSetData mTileSetData;
    MapScreenBlockData mScreenBlockData;
    MapCollisionData mCollision;
    MapTileAnimData mTileAnims;
};

// Map music/ambience (from the map's Settings.json). An area is a world-pixel rectangle that overrides the music
// while the player is inside it. Empty string = silence / no ambience.
struct MapAudioArea final
{
    int16_t mX;
    int16_t mY;
    int16_t mWidth;
    int16_t mHeight;
    const char* mMusic;
};

struct MapAudioData final
{
    const char* mMusic;
    const char* mAmbience;
    uint32_t mAreaCount;
    const MapAudioArea* mAreas;
};

// Map darkness (Settings.json DarknessAlpha / PlayerLight / Darkness / ReplaceTiles). Alphas are in 1/256ths and
// textures index kDarkTextures; -1 (or a null mReplace) means not set there, so the next settings' value applies: the
// areas the player is in first, then the events that are set, then the map's own.
struct MapDarkArea final
{
    int16_t mX;
    int16_t mY;
    int16_t mWidth;
    int16_t mHeight;
    int16_t mAlpha;
    int8_t mPlayerLight;
    int8_t mTexture;
    const char* mReplace; // "a-b;c-d": BG tile a draws as b... ("": none)
};

struct MapDarkEvent final
{
    const char* mEvent;
    int16_t mAlpha;
    int8_t mTexture;
    const char* mReplace;
};

struct MapDarkData final
{
    int16_t mAlpha;
    bool mPlayerLight;
    int8_t mTexture;
    uint32_t mAreaCount;
    const MapDarkArea* mAreas;
    uint32_t mEventCount;
    const MapDarkEvent* mEvents;
};

// A darkness texture (Darkness.SetTex): the 160x160 playfield's 8px bands, each one colour and alpha (RGBA8), blended
// over the screen in the overlay or (mHardLight) hard light way.
constexpr int kDarkBands = 20;
struct DarkTexture final
{
    uint8_t mHardLight;
    uint8_t mBands[kDarkBands][4];
};

// A background that scrolls at a fixed speed behind the map (or, with mOverlay, over everything: the windmill's
// rain). mPattern is a 20x20 tile (160x160 px) block of screen entries that repeats; velocities are px/s.
struct MapScrollData final
{
    const uint16_t* mPattern; // nullptr: this map has none
    int16_t mVelocityX;
    int16_t mVelocityY;
    uint8_t mOverlay;
};

// A door/teleporter trigger on a map (Entities.xml "Door"s, paired by link id). The hitbox is in map pixels;
// stepping on it (or pressing B on it, if mInteract) puts the player at (mDestX, mDestY) in map mDestMap.
// mFacing: 0 down, 1 right, 2 up, 3 left, 255 = keep. mPortal plays the teleport sound instead of the door's.
struct MapDoor final
{
    int16_t mX;
    int16_t mY;
    int16_t mW;
    int16_t mH;
    int16_t mDestX;
    int16_t mDestY;
    uint8_t mDestMap;
    uint8_t mFacing;
    uint8_t mInteract;
    uint8_t mPortal;
    int16_t mSrcX; // the Door preset's own position (its entity finds its trigger by it)
    int16_t mSrcY;
    uint8_t mType; // the Door "type" attribute (the C# class)
};

struct MapDoorList final
{
    uint32_t mCount;
    const MapDoor* mDoors;
};

// SwapperControl (the map's Swapper.dat): where the transformer may swap tiles. A region is a map-pixel rectangle; the
// first one holding a tile's top-left decides, none = default.
enum SwapperState : uint8_t
{
    SWAP_DEFAULT,
    SWAP_ALLOW,
    SWAP_DISALLOW,
    SWAP_DISALLOW_SILENTLY,
};

struct MapSwapRegion final
{
    uint8_t mState; // SwapperState
    int16_t mX;
    int16_t mY;
    int16_t mW;
    int16_t mH;
};

struct MapSwapperList final
{
    uint32_t mCount;
    const MapSwapRegion* mRegions;
};

// A map entity preset (Entities.xml, resolved to a class by the content compiler like the C# EntityManager).
// Positions are map pixels. mGlobalId indexes the game's per-preset state (EntityPreset.Alive / Activated) and is
// unique over all maps. A placeholder (an entity the port doesn't implement yet) draws static frame mLookFrame of
// kEntitySheets[mSheet].
struct MapEntity final
{
    int16_t mX;
    int16_t mY;
    int16_t mFrame;  // the XML "frame" (DungeonEntranceRef: its entrance's x)
    int16_t mLinkId; // -1: none (DungeonEntranceRef: its entrance's y; Treasure: the card of its screen)
    uint16_t mGlobalId;
    uint8_t mType;       // EntityPresetType
    uint8_t mPermanence; // EntityPermanence
    uint8_t mSheet;
    uint8_t mLookFrame;
    uint8_t mTypeValue; // kEntityTypeValues index: the XML "type" string
};

struct MapEntityList final
{
    uint32_t mCount;
    const MapEntity* mEntities;
};
