# Host-native GBA simulator: the game compiled for the host, running against a simulated GBA (see README.md).
find_package(SDL3 REQUIRED CONFIG)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

option(ANODYNE_SIM_SANITIZE "Build the simulator with AddressSanitizer + UBSan" ON)

set(SOURCEDIR "${CMAKE_SOURCE_DIR}")
set(GAMEDIR "${SOURCEDIR}/anodyne")
set(SIMDIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(TONCDIR "${SOURCEDIR}/3rdParty/libtonc")
set(GENDIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENDIR}")

# ---- GBA memory sizes from the linker script ------------------------------------------------------------------
set(LD_SCRIPT "${GAMEDIR}/platform/gba/sys/gba_cart.ld")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LD_SCRIPT}")
file(READ "${LD_SCRIPT}" LD_TEXT)
set(LAYOUT "// Generated from gba_cart.ld's MEMORY block by platform/sim/CMakeLists.txt. Do not edit.\n#pragma once\n")
foreach(REGION ROM IWRAM EWRAM)
    string(REGEX MATCH "${REGION}[ \t]*:[ \t]*ORIGIN[ \t]*=[ \t]*(0x[0-9A-Fa-f]+)[ \t]*,[ \t]*LENGTH[ \t]*=[ \t]*([0-9]+)([KM]?)"
        MATCHED "${LD_TEXT}")
    if(NOT MATCHED)
        message(FATAL_ERROR "can't find ${REGION} in ${LD_SCRIPT}'s MEMORY block")
    endif()
    set(ORIGIN ${CMAKE_MATCH_1})
    set(LENGTH ${CMAKE_MATCH_2})
    if(CMAKE_MATCH_3 STREQUAL "K")
        math(EXPR LENGTH "${LENGTH} * 1024")
    elseif(CMAKE_MATCH_3 STREQUAL "M")
        math(EXPR LENGTH "${LENGTH} * 1024 * 1024")
    endif()
    string(APPEND LAYOUT "#define SIM_${REGION}_ORIGIN ${ORIGIN}u\n#define SIM_${REGION}_LENGTH ${LENGTH}u\n")
endforeach()
file(CONFIGURE OUTPUT "${GENDIR}/sim_memory_layout.h" CONTENT "${LAYOUT}")

# ---- libtonc for the host ---------------------------------------------------------------------------------------
# Its C sources compile as they are; the assembly is replaced: data tables/fonts are converted to C (s2c.py), the
# routines are in tonc_host.c / sim_bios.cpp, and tonc_irq.c is wrapped (tonc_irq_host.c).
set(TONC_DATA_S
    "${TONCDIR}/asm/sin_lut.s"
    "${TONCDIR}/asm/div_lut.s"
    "${TONCDIR}/src/pre1.3/toncfont.s")
file(GLOB TONC_FONTS_S "${TONCDIR}/src/font/*.s")
list(APPEND TONC_DATA_S ${TONC_FONTS_S})
add_custom_command(
    OUTPUT "${GENDIR}/tonc_data.c"
    COMMAND Python3::Interpreter "${SIMDIR}/tools/s2c.py" "${GENDIR}/tonc_data.c" ${TONC_DATA_S}
    DEPENDS "${SIMDIR}/tools/s2c.py" ${TONC_DATA_S})

file(GLOB TONC_C "${TONCDIR}/src/*.c" "${TONCDIR}/src/tte/*.c" "${TONCDIR}/src/pre1.3/*.c")
list(REMOVE_ITEM TONC_C "${TONCDIR}/src/tonc_irq.c")
add_library(tonc_host STATIC ${TONC_C} "${SIMDIR}/tonc_irq_host.c" "${SIMDIR}/tonc_host.c" "${SIMDIR}/tonc_nocash_buffer.c"
    "${GENDIR}/tonc_data.c")
target_include_directories(tonc_host PUBLIC "${TONCDIR}/include")
# Upstream libtonc: leave its warnings alone.
target_compile_options(tonc_host PRIVATE -w -fno-strict-aliasing)

# ---- the audio pool, linked in like the GBA build does ----------------------------------------------------------
set(AUDIO_BLOB "${CMAKE_BINARY_DIR}/tools/audio_blob.bin")
file(WRITE "${GENDIR}/audio_blob.S"
"    .section .rodata
    .global _binary_audio_blob_bin_start
    .global _binary_audio_blob_bin_end
    .balign 4
_binary_audio_blob_bin_start:
    .incbin \"${AUDIO_BLOB}\"
_binary_audio_blob_bin_end:
    .section .note.GNU-stack,\"\",@progbits
")
set_source_files_properties("${GENDIR}/audio_blob.S" PROPERTIES OBJECT_DEPENDS "${AUDIO_BLOB}")
add_custom_command(OUTPUT "${AUDIO_BLOB}" DEPENDS RunContentCompiler COMMAND "${CMAKE_COMMAND}" -E true)

# ---- the simulator + game -----------------------------------------------------------------------------------------
include("${GAMEDIR}/sources.cmake")
list(TRANSFORM ANODYNE_GAME_SOURCES PREPEND "${GAMEDIR}/")

add_executable(anodyne_sim
    sim_main.cpp
    sim_frontend.cpp
    sim_ipc.cpp
    sim_memory.cpp
    sim_io.cpp
    sim_irq.cpp
    sim_dma_timer.cpp
    sim_apu.cpp
    sim_ppu.cpp
    sim_bios.cpp
    "${GENDIR}/audio_blob.S"
    ${ANODYNE_GAME_SOURCES})
add_dependencies(anodyne_sim RunContentCompiler)
set_property(TARGET anodyne_sim PROPERTY CXX_STANDARD 17)

target_include_directories(anodyne_sim PRIVATE
    "${GENDIR}"
    "${SIMDIR}"
    "${GAMEDIR}"
    "${CMAKE_BINARY_DIR}/tools"
    "${SOURCEDIR}/3rdParty/qoa"
    "${SOURCEDIR}/3rdParty/lodepng")

# The game's main() becomes GameMain(); sim_main.cpp owns the real one.
set_source_files_properties(${ANODYNE_GAME_SOURCES} PROPERTIES COMPILE_DEFINITIONS "main=GameMain;ANODYNE_SIM=1")

# GBA memory is mapped at its real addresses (0x02000000-0x07FFFFFF) and the game casts pointers to u32 (DMA
# source registers), so the binary is linked without PIE and above the GBA's address space, below 4GB.
target_compile_options(anodyne_sim PRIVATE -fno-pie -g -O1 -Wall)
target_link_options(anodyne_sim PRIVATE -no-pie -Wl,-Ttext-segment=0x10000000)
target_compile_options(tonc_host PRIVATE -fno-pie -g -O1)

if(ANODYNE_SIM_SANITIZE)
    # -fsanitize-sections: also instrument globals the game puts in EWRAM/IWRAM sections.
    set(SANITIZE -fsanitize=address,undefined -fno-omit-frame-pointer "-fsanitize-sections=.sbss*,.ewram*,.iwram*")
    target_compile_options(anodyne_sim PRIVATE ${SANITIZE})
    target_compile_options(tonc_host PRIVATE ${SANITIZE})
    target_link_options(anodyne_sim PRIVATE -fsanitize=address,undefined)
endif()

target_link_libraries(anodyne_sim PRIVATE tonc_host lodepng SDL3::SDL3)
