#include "sprite.h"

#include <string.h>

#include "tonc.h"

#include "../build/tools/sprite_registry.h"

namespace
{
uint32_t FrameBytes(const SpriteSheetEntry& sheet)
{
    return static_cast<uint32_t>(sheet.mTilesW) * sheet.mTilesH * 32;
}

void Upload(Sprite& sprite, int frame)
{
    if (frame == sprite.mLoadedFrame || frame < 0 || frame >= sprite.mSheet->mFrameCount)
    {
        return; // out-of-range frames (blank in the original too) keep the last good image
    }

    // Streams are word aligned (alignas(4) array, offsets padded to 4); VRAM-safe BIOS decompress (16-bit writes).
    LZ77UnCompVram(sprite.mSheet->mFrameData + sprite.mSheet->mFrameOffsets[frame], &tile_mem[4][sprite.mVramBlock]);
    sprite.mLoadedFrame = frame;
}

int CurrentFrame(const Sprite& sprite)
{
    if (sprite.mAnim < 0)
    {
        return sprite.mStaticFrame;
    }
    const SpriteAnimData& anim = sprite.mSheet->mAnims[sprite.mAnim];
    return anim.mFrameCount ? anim.mFrames[sprite.mAnimIndex] : 0;
}
} // namespace

uint32_t Sprite_SheetCount()
{
    return kSpriteSheetCount;
}

const SpriteSheetEntry* Sprite_SheetAt(uint32_t index)
{
    return index < kSpriteSheetCount ? &kSpriteSheets[index] : nullptr;
}

bool Sprite_IsDrawable(const SpriteSheetEntry& sheet)
{
    // Any frame size works (big frames use several OBJs); only the 128 OAM entries and 32KB of OBJ VRAM limit it.
    return sheet.mPartCount > 0 && sheet.mPartCount <= 128 && FrameBytes(sheet) <= 16 * 1024;
}

const SpriteSheetEntry* Sprite_FindSheet(const char* name, int frameW, int frameH)
{
    for (uint32_t i = 0; i < kSpriteSheetCount; i++)
    {
        const SpriteSheetEntry& s = kSpriteSheets[i];
        if (strcmp(s.mName, name) == 0 && (frameW == 0 || s.mFrameWidth == frameW) &&
            (frameH == 0 || s.mFrameHeight == frameH))
        {
            return &s;
        }
    }
    return nullptr;
}

bool Sprite_Create(Sprite& sprite, ObjVramAllocator& vram, const char* sheetName, int palBank, int oamSlot,
                   int frameW, int frameH)
{
    const SpriteSheetEntry* sheet = Sprite_FindSheet(sheetName, frameW, frameH);
    if (!sheet || !Sprite_IsDrawable(*sheet) || palBank < 0 || palBank > 15 || oamSlot < 0 ||
        oamSlot + static_cast<int>(sheet->mPartCount) > 128)
    {
        return false;
    }

    const int block = vram.alloc(static_cast<int>(FrameBytes(*sheet)));
    if (block < 0)
    {
        return false;
    }

    sprite = Sprite();
    sprite.mSheet = sheet;
    sprite.mVramBlock = block;
    sprite.mPalBank = palBank;
    sprite.mOamSlot = oamSlot;

    memcpy16(&pal_obj_bank[palBank], sheet->mPalette, 16);
    Upload(sprite, 0);
    return true;
}

void Sprite_Destroy(Sprite& sprite, ObjVramAllocator& vram)
{
    if (sprite.mSheet)
    {
        Sprite_Hide(sprite);
        vram.free(sprite.mVramBlock);
    }
    sprite = Sprite();
}

bool Sprite_Play(Sprite& sprite, const char* animName, bool restart)
{
    for (uint32_t i = 0; i < sprite.mSheet->mAnimCount; i++)
    {
        if (strcmp(sprite.mSheet->mAnims[i].mName, animName) == 0)
        {
            if (restart || sprite.mAnim != static_cast<int>(i))
            {
                sprite.mAnim = static_cast<int>(i);
                sprite.mAnimIndex = 0;
                sprite.mTimer = 0;
                sprite.mFinished = false;
            }
            return true;
        }
    }
    return false;
}

void Sprite_SetFrame(Sprite& sprite, int frame)
{
    sprite.mAnim = -1;
    sprite.mStaticFrame = frame;
}

int Sprite_CurrentFrame(const Sprite& sprite)
{
    return CurrentFrame(sprite);
}

void Sprite_Update(Sprite& sprite)
{
    if (sprite.mAnim >= 0 && !sprite.mFinished)
    {
        const SpriteAnimData& anim = sprite.mSheet->mAnims[sprite.mAnim];
        if (anim.mFps > 0 && anim.mFrameCount > 1)
        {
            sprite.mTimer += anim.mFps;
            while (sprite.mTimer >= 60)
            {
                sprite.mTimer -= 60;
                if (sprite.mAnimIndex + 1 < anim.mFrameCount)
                {
                    sprite.mAnimIndex++;
                }
                else if (anim.mLoop)
                {
                    sprite.mAnimIndex = 0;
                }
                else
                {
                    sprite.mFinished = true;
                    break;
                }
            }
        }
    }

    Upload(sprite, CurrentFrame(sprite));
}

void Sprite_Draw(const Sprite& sprite, int x, int y, bool hflip)
{
    const SpriteSheetEntry& sheet = *sprite.mSheet;
    const int frameWidth = sheet.mTilesW * 8;

    for (uint32_t i = 0; i < sheet.mPartCount; i++)
    {
        const SpritePartData& part = sheet.mParts[i];
        // Flipping the whole sprite mirrors each OBJ's pixels (HFLIP) and its position inside the frame.
        const int px = x + (hflip ? frameWidth - part.mX - part.mWidth : part.mX);
        const int py = y + part.mY;
        obj_set_attr(&oam_mem[sprite.mOamSlot + i],
                     ATTR0_REG | ATTR0_4BPP | (part.mShape << 14) | ATTR0_Y(py & 0xFF),
                     (part.mSize << 14) | ATTR1_X(px & 0x1FF) | (hflip ? ATTR1_HFLIP : 0),
                     ATTR2_ID(sprite.mVramBlock + part.mTileOffset) | ATTR2_PRIO(sprite.mPriority) | ATTR2_PALBANK(sprite.mPalBank));
    }
}

void Sprite_Hide(const Sprite& sprite)
{
    for (uint32_t i = 0; i < sprite.mSheet->mPartCount; i++)
    {
        obj_hide(&oam_mem[sprite.mOamSlot + i]);
    }
}

void Sprite_DrawAffine(const Sprite& sprite, int cx, int cy, int affineIndex, bool blend)
{
    const SpritePartData& part = sprite.mSheet->mParts[0];
    // Not double-size: an OBJ costs 10 + 2 * box width cycles on every scanline it covers and the budget is 1210, so
    // four 128px double-size boxes starve the sprites after them (they get clipped). Rotated corners of the
    // original-size box are cut instead, which only matters for sprites that aren't round.
    const int x = cx - part.mWidth / 2;
    const int y = cy - part.mHeight / 2;
    obj_set_attr(&oam_mem[sprite.mOamSlot],
                 ATTR0_AFF | (blend ? ATTR0_BLEND : 0) | ATTR0_4BPP | (part.mShape << 14) | ATTR0_Y(y & 0xFF),
                 (part.mSize << 14) | ATTR1_AFF_ID(affineIndex) | ATTR1_X(x & 0x1FF),
                 ATTR2_ID(sprite.mVramBlock + part.mTileOffset) | ATTR2_PRIO(sprite.mPriority) | ATTR2_PALBANK(sprite.mPalBank));
}
