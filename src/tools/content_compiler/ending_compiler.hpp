#pragma once

#include "content_paths.hpp"

// Compiles the credits' art into ending_data.h: the "go" backdrop (160x480) as 8bpp tiles (de-duplicated with
// flips) and a 20x60 map, its colours at BG palette entries 16 and up (0-15 are the credits' text bank); and the
// screenies, each its own tiles, map and cut-down palette.
bool CompileEnding(const ContentPaths& contentPaths);
