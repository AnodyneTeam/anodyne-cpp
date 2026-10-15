#pragma once

#include <string>
#include <vector>

#include "content_paths.hpp"

// Reads every entity out of Entities.xml and maps it to a placeholder sprite (a static frame of the entity's own
// sheet where we know it, a generic "placeholder" box otherwise). Returns C++ source defining `kEntitySheets[]`
// and `kMapEntities[]` (one MapEntityList per map in `mapNames`); needs map_data.h's MapEntity.
std::string BuildEntityTables(const ContentPaths& contentPaths, const std::vector<std::string>& mapNames);
