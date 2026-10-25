#pragma once

// The entity classes the port implements, shared by the content compiler (which resolves every Entities.xml element
// to one of these like the C# EntityManager does with its [NamedEntity] attributes) and the game (which keeps a
// behaviour table indexed by them, see entity_registry.cpp). Placeholder: an element the port doesn't implement
// yet, drawn as a static frame of its sheet.
#define ANODYNE_ENTITY_TYPES(X) \
    X(Placeholder)              \
    X(Console)                  \
    X(BlankConsole)             \
    X(Gate)                     \
    X(Button)                   \
    X(Treasure)                 \
    X(SolidTile)                \
    X(Blocker)                  \
    X(VBlocker)                 \
    X(Dust)                     \
    X(JumpTrigger)              \
    X(SpringPad)                \
    X(CrackedTile)              \
    X(Hole)                     \
    X(SmallKeyGate)             \
    X(BigKeyGate)               \
    X(CardGate)                 \
    X(Key)                      \
    X(HealthEntity)             \
    X(Checkpoint)               \
    X(DungeonEntrance)          \
    X(DungeonEntranceRef)       \
    X(VolumeEvent)              \
    X(BossKeyReset)             \
    X(BedroomBounceLight)       \
    X(EyeLight)                 \
    X(DungeonStatue)            \
    X(Rock)                     \
    X(Statue)                   \
    X(BigKey)                   \
    X(SageBlank)                \
    X(SageNexus)                \
    X(SageOverworld)            \
    X(SageBedroom)              \
    X(MitraOverworld)           \
    X(MitraFields)              \
    X(HealthCicada)             \
    X(Sadbro)                   \
    X(FollowerBro)              \
    X(Slime)                    \
    X(Shieldy)                  \
    X(PewLaser)                 \
    X(Annoyer)                  \
    X(Seer)                     \
    X(Dog)                      \
    X(Rotator)                  \
    X(Door)

enum EntityPresetType : unsigned char
{
#define ANODYNE_ENTITY_ENUM(name) EPT_##name,
    ANODYNE_ENTITY_TYPES(ANODYNE_ENTITY_ENUM)
#undef ANODYNE_ENTITY_ENUM
    EPT_Count
};

// EntityPreset.Permanence: whether a dead/used entity comes back on leaving the screen, the map, or never.
enum EntityPermanence : unsigned char
{
    PERM_GRID_LOCAL = 0,
    PERM_MAP_LOCAL = 1,
    PERM_GLOBAL = 2,
};
