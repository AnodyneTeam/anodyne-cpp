# ------------------------------------------------------------
# Toolchain
# ------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Generic)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

set(OBJCOPY arm-none-eabi-objcopy)
set(OBJDUMP arm-none-eabi-objdump)

# ------------------------------------------------------------
# Paths
# ------------------------------------------------------------
set(SOURCEDIR "${CMAKE_SOURCE_DIR}")
set(LIBTONC_DIR "${SOURCEDIR}/3rdParty/libtonc")

# ------------------------------------------------------------
# Sources
# ------------------------------------------------------------
set(FIRST_SOURCES
    ${SOURCEDIR}/anodyne/platform/gba/sys/syscalls.c
    ${SOURCEDIR}/anodyne/platform/gba/sys/gba_crt0.s
    anodyne.cpp
    dialog_manager.c
    audio_manager.cpp
    audio_decode_core.cpp
    sprite.cpp
    entities.cpp
    world.cpp
)

# ------------------------------------------------------------
# STATIC library
# ------------------------------------------------------------
add_library(first_lib STATIC ${FIRST_SOURCES})
add_dependencies(first_lib RunContentCompiler)

target_compile_options(first_lib PRIVATE
    -mcpu=arm7tdmi
    -mtune=arm7tdmi
    -mthumb
    -mthumb-interwork
    -ffunction-sections
    -fdata-sections
    -D__GBA__
    $<$<COMPILE_LANGUAGE:C>:-std=gnu11 -O3 -Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++14 -O3 -Wall -fno-exceptions -fno-rtti>
)

target_include_directories(first_lib PRIVATE
    "${LIBTONC_DIR}/include"
    "${SOURCEDIR}/3rdParty/qoa"
)

add_dependencies(first_lib gbafix tonc)

# ------------------------------------------------------------
# Audio blob -> linkable object
# ------------------------------------------------------------
# audio_blob.bin (the QOA-encoded BGM/ambience/SFX pool, tens of MB) is far
# too big to emit as a C array header the way map data is (SaveTileSetGBA
# etc.) - it ships as a raw binary file instead, turned into a linkable
# object here.
#
# `ld -r -b binary` names its section `.data`, which gba_cart.ld's `.data`
# rule places in IWRAM (32K) - nowhere near big enough for this blob, and it
# would trip the linker script's own IWRAM-overflow ASSERT. The
# --rename-section step is required, not optional: it routes the blob to
# .rodata -> ROM (32M) instead.
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/tools/audio_blob.bin.o
    COMMAND arm-none-eabi-ld -r -b binary -o audio_blob.bin.o audio_blob.bin
    COMMAND arm-none-eabi-objcopy
        --rename-section .data=.rodata,alloc,load,readonly,data,contents
        audio_blob.bin.o audio_blob.bin.o
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tools
    # The blob FILE, not just the RunContentCompiler target: a target
    # dependency only orders the build, it never makes this output stale, so
    # re-encoded audio used to leave an old audio_blob.bin.o linked against a
    # new audio_registry.h (wrong frame offsets -> full-scale garbage audio).
    DEPENDS ${CMAKE_BINARY_DIR}/tools/audio_blob.bin RunContentCompiler
)
add_custom_target(AudioBlobObj DEPENDS ${CMAKE_BINARY_DIR}/tools/audio_blob.bin.o)
add_dependencies(first_lib AudioBlobObj)

# ------------------------------------------------------------
# Manual link step
# ------------------------------------------------------------
add_custom_command(
    OUTPUT first.elf
    COMMAND arm-none-eabi-gcc
        -mcpu=arm7tdmi -mthumb -mthumb-interwork
        -Wl,-Map,first.map -Wl,--gc-sections
        -specs=nano.specs
        -T${SOURCEDIR}/anodyne/platform/gba/sys/gba_cart.ld
        # --start-group/--end-group: first_lib's syscalls.c.o (which defines
        # _write/_sbrk/etc.) is only demanded once -lc's newlib reentrant
        # wrappers (_write_r/_sbrk_r/etc., pulled in by the tte/iprintf HUD
        # code) are scanned - but -lc comes AFTER first_lib on the command
        # line, so a plain single pass never links syscalls.c.o in. The group
        # makes the linker iterate first_lib/tonc/-lc until everything
        # resolves, instead of just once left-to-right.
        -Wl,--start-group
        "$<TARGET_FILE:first_lib>"
        "$<TARGET_FILE:tonc>"
        -lc
        -Wl,--end-group
        "${CMAKE_BINARY_DIR}/tools/audio_blob.bin.o"
        -o first.elf
    DEPENDS first_lib tonc AudioBlobObj
)

add_custom_target(elf DEPENDS first.elf)

# ------------------------------------------------------------
# CMake launcher script for gbafix (cross‑platform)
# ------------------------------------------------------------
file(WRITE ${CMAKE_BINARY_DIR}/run_gbafix.cmake
"
execute_process(
    COMMAND \"${CMAKE_BINARY_DIR}/gbafix\" first.gba -tFIRST -c00
)
")

# ------------------------------------------------------------
# Build .gba ROM
# ------------------------------------------------------------
add_custom_command(
    OUTPUT first.gba
    COMMAND ${OBJCOPY} -O binary first.elf first.gba
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/run_gbafix.cmake
    DEPENDS first.elf gbafix
)

add_custom_target(rom ALL DEPENDS first.gba)

# ------------------------------------------------------------
# ROM size guard (soft warning - the GBA cart ROM ceiling is a hard 32MB,
# per gba_cart.ld's MEMORY{ROM: LENGTH = 32M}, but nothing else in the build
# checks the final .gba against it)
# ------------------------------------------------------------
add_custom_command(TARGET rom POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DROM_FILE=${CMAKE_BINARY_DIR}/anodyne/first.gba
        -P ${SOURCEDIR}/anodyne/check_rom_size.cmake
)

# ------------------------------------------------------------
# Build dump
# ------------------------------------------------------------
add_custom_command(
    OUTPUT first.dump
    COMMAND ${OBJDUMP} -h -C -S first.elf > first.dump
    DEPENDS first.elf
)

add_custom_target(dump DEPENDS first.dump)

# ------------------------------------------------------------
# Clean extras
# ------------------------------------------------------------
set_property(DIRECTORY PROPERTY ADDITIONAL_CLEAN_FILES
    "first.gba" "first.dump" "first.map"
)
