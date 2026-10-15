#pragma once

#include <stdint.h>

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
};

struct MapEntry final
{
    MapPalData mPalData;
    MapTileSetData mTileSetData;
    MapScreenBlockData mScreenBlockData;
    MapCollisionData mCollision;
};
