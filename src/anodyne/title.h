#pragma once

#include "obj_vram_allocator.hpp"

// The title screen and main menu (ported from AnodyneSharp's TitleState / MainMenuState): scrolling background,
// the Nexus door rising into view, the title text, then the file menu. There are no save files yet, so every
// slot starts a new game.

// Takes over the display (BG1-3 + OBJ) and starts the title music. Needs OAM slots 0-15 and OBJ palette banks 0-5.
void Title_Enter(ObjVramAllocator& vram);

// Per frame. Returns true once a game was started from the menu (slot 0-2 in *slot); then call Title_Exit().
bool Title_Update(int* slot);

// Frees the title's sprites and clears the display registers it changed.
void Title_Exit(ObjVramAllocator& vram);
