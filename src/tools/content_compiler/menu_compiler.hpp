#pragma once

#include "content_paths.hpp"

// Compiles the pause menu's art into menu_data.h: its images as plain 8bpp bitmaps (the menu draws them into a
// bitmap BG), each in a palette group (the colours every page shares, plus one page's own: the BG palette only
// holds 255), and the minimaps (MiniMaps/Minimap_<MAP>.csv) with where each map's cells sit in the visited table.
bool CompileMenu(const ContentPaths& contentPaths);
