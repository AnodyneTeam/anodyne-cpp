#pragma once

#include "obj_vram_allocator.hpp"

// The play state's HUD (the C#'s 20px header strip), moved into the GBA screen's 40px side bars: the equipped broom
// icon in its frame with the dust icon, the key count and the mini minimap on the left, the health bar on the right.
// Black panels cover the bars behind it, so sprites poking out of the playfield stay hidden there.

// The wide view (world.h) has no side bars: the HUD keeps its place, in a black box in each top corner over the map.
constexpr int kHudBarW = 40;
constexpr int kHudWideLeftH = 64;  // the broom, the keys and the mini minimap
constexpr int kHudWideRightH = 36; // up to four rows of health

// The first OAM entries are the HUD's; the entities' come after them.
constexpr int kHudOamCount = 15;

void Hud_Init(ObjVramAllocator& vram);
// Once a frame while the play state draws (healthHidden: dying hides the health bar).
void Hud_Draw(bool healthHidden);
// Off the screen (the play state isn't shown).
void Hud_Hide();
