PROJECT(Anodyne LANGUAGES C CXX ASM)
cmake_minimum_required(VERSION 3.25)

# ANODYNE_SIM=ON builds the host-native SDL3 GBA simulator (anodyne/platform/sim) with the game compiled for the
# host, instead of the GBA ROM. Use a separate build directory under src/ (e.g. src/build-sim): the content
# compiler finds old/Content relative to it.
option(ANODYNE_SIM "Build the SDL3 GBA simulator instead of the GBA ROM" OFF)

# ANODYNE_WIDE_ROOMS=ON: 15x9-tile rooms (240x144 under a 16px HUD row) from old/ContentWide instead of the 10x10 ones
# (anodyne/room.h, tools/wide_rooms). Both the content and the code change, so it's its own build directory.
option(ANODYNE_WIDE_ROOMS "Build with the wide (15x9-tile) rooms" OFF)
if(ANODYNE_WIDE_ROOMS)
    add_compile_definitions(ANODYNE_WIDE_ROOMS)
    set(CONTENT_COMPILER_ARGS --wide-rooms)
endif()

# build with host compiler
add_subdirectory(tools)

if(ANODYNE_SIM)
    add_subdirectory(anodyne/platform/sim)
else()
    # built with GBA toolchain/arm
    add_subdirectory("3rdParty/libtonc")
    add_subdirectory(anodyne)
endif()
