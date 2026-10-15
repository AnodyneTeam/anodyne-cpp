#pragma once

#include <stdint.h>

#include "obj_vram_allocator.hpp"
#include "sprite_data.h"

// One on-screen animated sprite. Only the current frame lives in OBJ VRAM (the whole sheet stays in ROM), so
// it is re-uploaded whenever the animation moves to a different frame.
struct Sprite final
{
    const SpriteSheetEntry* mSheet = nullptr;
    int mVramBlock = -1;
    int mPalBank = 0;
    int mOamSlot = 0;
    int mAnim = -1;
    int mAnimIndex = 0;
    int mTimer = 0;       // accumulates fps; one animation step per 60
    bool mFinished = false;
    int mLoadedFrame = -1;
    int mStaticFrame = 0; // used while no anim is playing
    int mPriority = 1;    // OBJ priority (0 = in front of every BG); same priority: lower OAM slot is in front
};

// Finds a compiled sheet by PNG stem. frameW/frameH disambiguate textures used at several sizes (0 = any).
const SpriteSheetEntry* Sprite_FindSheet(const char* name, int frameW = 0, int frameH = 0);

// Enumerates the compiled sheets (the registry lives in sprite.cpp only).
uint32_t Sprite_SheetCount();
const SpriteSheetEntry* Sprite_SheetAt(uint32_t index);
// True if the sheet can be passed to Sprite_Create (any frame size works; big frames use several OBJs).
bool Sprite_IsDrawable(const SpriteSheetEntry& sheet);

// Allocates OBJ VRAM for one frame, loads the sheet's palette into OBJ palette bank `palBank` and claims OAM
// slots oamSlot .. oamSlot + Sprite_OamCount() - 1 (one per hardware OBJ the frame needs). Returns false if the
// sheet is unknown, the slots don't fit in OAM, or VRAM is full.
bool Sprite_Create(Sprite& sprite, ObjVramAllocator& vram, const char* sheetName, int palBank, int oamSlot,
                   int frameW = 0, int frameH = 0);
// Number of OAM entries the sprite occupies.
inline int Sprite_OamCount(const Sprite& sprite)
{
    return static_cast<int>(sprite.mSheet->mPartCount);
}
void Sprite_Destroy(Sprite& sprite, ObjVramAllocator& vram);

// Starts the named anim. Does nothing if it's already playing, unless `restart`. Returns false if unknown.
bool Sprite_Play(Sprite& sprite, const char* animName, bool restart = false);
void Sprite_SetFrame(Sprite& sprite, int frame);
// The sheet frame being shown (the current anim frame, or the static frame).
int Sprite_CurrentFrame(const Sprite& sprite);

// Call once per 60Hz frame: advances the animation and uploads the frame to VRAM if it changed.
void Sprite_Update(Sprite& sprite);

// Writes the sprite's OAM entry (top-left in screen pixels).
void Sprite_Draw(const Sprite& sprite, int x, int y, bool hflip = false);
void Sprite_Hide(const Sprite& sprite);
