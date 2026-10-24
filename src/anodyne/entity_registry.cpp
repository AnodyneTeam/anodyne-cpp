// EntityPresetType -> the class's spawn function (EntityPreset.Create).
#include "entity_classes.h"

SpawnFn Entities_SpawnFn(uint8_t type)
{
    switch (type)
    {
    case EPT_Console:
        return Spawn_Console;
    case EPT_Gate:
        return Spawn_Gate;
    case EPT_SageBlank:
        return Spawn_SageBlank;
    default:
        return nullptr;
    }
}
