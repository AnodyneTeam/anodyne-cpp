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
    X(MiaoXiao)                 \
    X(OutsideMonster)           \
    X(SwitchPillar)             \
    X(PillarSwitch)             \
    X(InsideMonster)            \
    X(ShopKeep)                 \
    X(HealthCicada)             \
    X(Sadbro)                   \
    X(FollowerBro)              \
    X(Slime)                    \
    X(Shieldy)                  \
    X(PewLaser)                 \
    X(Annoyer)                  \
    X(Seer)                     \
    X(Dog)                      \
    X(BurstPlant)               \
    X(Frog)                     \
    X(DashTrap)                 \
    X(FourShooter)              \
    X(Slasher)                  \
    X(Fisherman)                \
    X(RedWalker)                \
    X(RedseaNpc)                \
    X(Sign)                     \
    X(RedCaveEntrance)          \
    X(RedPillar)                \
    X(Mover)                    \
    X(OnOffLaser)               \
    X(Rotator)                  \
    X(RedBoss)                  \
    X(WallBoss)                 \
    X(SageRedCave)              \
    X(SageCrowd)                \
    X(SageTerminal)             \
    X(BioFilm)                  \
    X(WindmillConsole)          \
    X(SuburbWalker)            \
    X(SuburbKiller)            \
    X(SuburbBlocker)           \
    X(SuburbIndoors)           \
    X(SuburbQuest)             \
    X(Rat)                      \
    X(Silverfish)               \
    X(GasGuy)                   \
    X(TeleGuy)                  \
    X(Rail)                     \
    X(EasterEgg)                \
    X(SplitBoss)                \
    X(Dustmaid)                \
    X(SteamPipe)               \
    X(EyebossPreview)          \
    X(HotelGuy)                \
    X(Elevator)                \
    X(EyeBossWater)            \
    X(EyeBossLand)             \
    X(SpaceFace)               \
    X(SpaceNpc)                \
    X(Chaser)                  \
    X(CliffDog)                \
    X(CellBody)                \
    X(FirePillar)              \
    X(Contort)                 \
    X(Lion)                    \
    X(ArthurDanger)            \
    X(JavieraDanger)           \
    X(ArthurDead)              \
    X(JavieraDead)             \
    X(CircusFolks)             \
    X(SageBoss)                \
    X(DashPad)                 \
    X(GoDetector)              \
    X(Snowman)                 \
    X(Dam)                     \
    X(WaterAnim)               \
    X(BlueBriar)               \
    X(PostBlue)                \
    X(GoHappyBlocker)          \
    X(HappyNpc)                \
    X(HappyEventTrigger)       \
    X(BriarBoss)               \
    X(SageSentinel)            \
    X(Bunny)                    \
    X(BigDoor)                  \
    X(BigTree)                  \
    X(TestNpc)                  \
    X(DevEaster)                \
    X(SoundTestConsole)         \
    X(BlackThing)               \
    X(CliffQuest)               \
    X(BeachQuest)               \
    X(ForestQuest)              \
    X(FieldsQuest)              \
    X(CellQuest)                \
    X(SpaceQuest)               \
    X(GoQuest)                  \
    X(CubeKing)                 \
    X(FieldsEaster)             \
    X(Thorax)                   \
    X(Mushroom)                 \
    X(Olive)                    \
    X(ForestBriar)              \
    X(BeachBriar)               \
    X(CrowdRail)                \
    X(MitraCliff)               \
    X(BunnyRun)                 \
    X(Person)                   \
    X(SpikeRoller)              \
    X(Propelled)                \
    X(StopMarker)               \
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
