#pragma once

#include "content_paths.hpp"

// Compiles the play state's HUD (the C#'s header strip: broom icon and frame, dust icon, key, health, mini minimap)
// into hud_data.h: 4bpp-ready pixel indices into two OBJ palette banks. Bank 14 holds the frame, dust, key, label,
// health and black; bank 13 the mini minimap's colours (1-4) and the equipped broom icon's own (5-15, loaded with it).
bool CompileHud(const ContentPaths& contentPaths);
