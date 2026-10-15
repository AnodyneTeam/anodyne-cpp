#pragma once

#include <string>
#include <vector>

#include "content_paths.hpp"

// Reads the doors/teleporters out of Entities.xml and pairs them up by their link id (the "frame" attribute), like
// AnodyneSharp's EntityManager. Returns C++ source defining `kMapDoors[]` (one MapDoorList per map in `mapNames`);
// it must be pasted after the map registry's per-map includes and map_data.h's MapDoor.
std::string BuildDoorTables(const ContentPaths& contentPaths, const std::vector<std::string>& mapNames);
