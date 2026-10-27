#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content_paths.hpp"

struct SpriteAnimSpec final
{
    const char* mName;
    std::vector<int> mFrames;
    float mFps;
    bool mLoop;
};

struct SpriteSheetSpec final
{
    const char* mTexture; // PNG file stem under old/Content/textures
    int mFrameWidth;
    int mFrameHeight;
    std::vector<SpriteAnimSpec> mAnims;
};

// The sheets to compile (sprite_defs.cpp).
const std::vector<SpriteSheetSpec>& GetSpriteSheetSpecs();

// One hardware OBJ covering part of a frame. A frame of any size is split into these (see LayoutParts).
struct SpritePart final
{
    int mTileX = 0; // position inside the frame, in 8x8 tiles
    int mTileY = 0;
    int mTilesW = 0;
    int mTilesH = 0;
    int mShape = 0; // OBJ attr0 shape: 0 square, 1 wide, 2 tall
    int mSize = 0;  // OBJ attr1 size 0..3
    int mTileOffset = 0; // first tile of this part inside the frame's tile data
};

struct CompiledSpriteSheet final
{
    std::string mName;
    int mFrameWidth = 0;
    int mFrameHeight = 0;
    int mFramesPerRow = 0;    // frames per row in the source PNG; frame N sits at (N % perRow, N / perRow)
    int mFrameCount = 0;
    int mTilesW = 0;          // frame size in 8x8 tiles (rounded up)
    int mTilesH = 0;
    std::vector<SpritePart> mParts; // same for every frame (or each frame's own, see mFrameParts); tile data is stored part by part
    std::vector<uint16_t> mFrameParts; // empty, or a sparse sheet's first part of each frame in mParts, plus the end
    std::vector<uint16_t> mPalette; // 16 GBA RGB555 entries, index 0 = transparent
    std::vector<uint8_t> mTiles;    // 4bpp, 32 bytes per 8x8 tile; frames back to back; within a frame, part by part, tiles row-major per part
    std::vector<uint8_t> mPacked;   // each frame GBA-BIOS LZ77 compressed, 4-byte aligned, back to back
    std::vector<uint32_t> mFrameOffsets; // byte offset of each frame in mPacked, plus a final end offset
    std::vector<SpriteAnimSpec> mAnims;
};

class SpriteCompiler final
{
public:
    // Converts every sheet in GetSpriteSheetSpecs(). Failures are reported and skipped.
    std::vector<CompiledSpriteSheet> CompileAll(const ContentPaths& contentPaths) const;

    // Writes sprite_pixels.h (pixel/palette arrays) and sprite_registry.h (kSpriteSheets[]). Files are left
    // untouched when their content is unchanged so an up-to-date build doesn't recompile the ROM.
    void WriteHeaders(const std::vector<CompiledSpriteSheet>& sheets) const;
};
