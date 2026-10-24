# The game's sources, shared by the GBA ROM (anodyne/CMakeLists.txt) and the simulator (platform/sim).
set(ANODYNE_GAME_SOURCES
    anodyne.cpp
    dialog.cpp
    audio_manager.cpp
    audio_decode_core.cpp
    sprite.cpp
    entities.cpp
    entity_registry.cpp
    ent_gadgets.cpp
    ecs.cpp
    game_state.cpp
    game_probe.cpp
    title.cpp
    soft_mosaic.cpp
    pixelate_test.cpp
    world.cpp
)
