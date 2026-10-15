#pragma once

#include "content_paths.hpp"

// Compiles the title screen / main menu art into title_data.h:
//  - the scrolling title background and the Nexus door as 8bpp tilemaps sharing one palette (entries 0-223;
//    224-255 are left for the 4bpp text banks 14 and 15),
//  - the UI font (font-white-apple-7x8) as 4bpp glyph tiles (ink = colour index 1) plus an ASCII -> tile table.
// The title's sprites (title_text, press_enter, ...) go through the normal sprite compiler (sprite_defs.cpp).
bool CompileTitle(const ContentPaths& contentPaths);
