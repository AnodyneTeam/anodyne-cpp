#pragma once

#include <stdint.h>

// Layout of the content-compiler generated sprite registry (sprite_registry.h).
// Pixel data is 4bpp OBJ tiles (32 bytes per 8x8 tile). A frame covers mTilesW x mTilesH tiles
// (tilesW * tilesH * 32 bytes uncompressed), stored part by part (see SpritePartData) so each OBJ's tiles are
// consecutive, as 1D OBJ mapping needs. Each frame is separately BIOS-LZ77 compressed: frame N's stream
// is at mFrameData + mFrameOffsets[N] (word aligned) and unpacks straight into OBJ VRAM with LZ77UnCompVram.
// Source-sheet frame N sat at (N % framesPerRow, N / framesPerRow).

struct SpriteAnimData final
{
    const char* mName; // as in AnodyneSharp; "name@N" = a second, different anim of the same name
    uint16_t mFrameCount;
    uint8_t mFps;
    uint8_t mLoop;
    const uint16_t* mFrames;
};

// One hardware OBJ of a frame. A frame is covered by mPartCount of these (a big sprite uses several OBJs).
struct SpritePartData final
{
    uint16_t mX; // position inside the frame in pixels
    uint16_t mY;
    uint16_t mWidth; // pixel size of this OBJ
    uint16_t mHeight;
    uint8_t mShape;  // OBJ attr0 shape: 0 square, 1 wide, 2 tall
    uint8_t mSize;   // OBJ attr1 size 0..3
    uint16_t mTileOffset; // first tile of this OBJ within the frame's tile data
};

struct SpriteSheetEntry final
{
    const char* mName; // PNG file stem
    uint8_t mFrameWidth;
    uint8_t mFrameHeight;
    uint8_t mTilesW;
    uint8_t mTilesH;
    uint8_t mFramesPerRow;
    uint16_t mFrameCount;
    const uint16_t* mPalette; // 16 x RGB555, index 0 transparent
    const uint8_t* mFrameData;
    const uint32_t* mFrameOffsets; // mFrameCount + 1 entries
    const SpritePartData* mParts;
    uint32_t mPartCount;
    const SpriteAnimData* mAnims;
    uint32_t mAnimCount;
};
