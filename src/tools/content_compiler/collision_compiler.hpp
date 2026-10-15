#pragma once

#include <string>

#include "content_paths.hpp"

// Compiles a map's collision data from the AnodyneSharp content:
//   - BG.csv / BG2.csv: 16x16 tile indices per layer
//   - TileData.col: per tile index (ranges, later lines override earlier ones) the sides it blocks
//     (Touching flags), its CollisionEventType and its direction
// Each 16x16 cell becomes one byte: an index into a small per-map table of unique
// (allow, event, direction) classes, merged from the BG and BG2 tile like the C# Map.GetCollisionData (allow flags
// are OR-ed; BG2's event/direction wins when it has one). The byte grid is BIOS-LZ77 compressed.
//
// Writes <MAP>_col.h (namespace <MAP>: gColWidth/gColHeight in cells, gColClassCount, gColClasses[3 * count],
// gColUnpackedSize, gColPacked[]). Returns false if the map's BG.csv can't be read.
bool CompileMapCollision(const ContentPaths& contentPaths, const std::string& mapName);
