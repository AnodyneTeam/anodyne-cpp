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
    ${SOURCES}platform/gba/sys/syscalls.c
    ${SOURCES}platform/gba/sys/gba_crt0.s
    anodyne.c
    dialog_manager.c
)

# ------------------------------------------------------------
# STATIC library (bulletproof)
# ------------------------------------------------------------
add_library(first_lib STATIC ${FIRST_SOURCES})

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
)

add_dependencies(first_lib gbafix tonc)

# ------------------------------------------------------------
# Manual link step (STATIC LIB = no OBJECT issues)
# ------------------------------------------------------------
add_custom_command(
    OUTPUT first.elf
    COMMAND arm-none-eabi-gcc
        -mcpu=arm7tdmi -mthumb -mthumb-interwork
        -Wl,-Map,first.map -Wl,--gc-sections
        -specs=nano.specs
        -T${SOURCEDIR}/anodyne/platform/gba/sys/gba_cart.ld
        "$<TARGET_FILE:first_lib>"
        "$<TARGET_FILE:tonc>"
        -lc
        -o first.elf
    DEPENDS first_lib tonc
)

add_custom_target(elf DEPENDS first.elf)

# ------------------------------------------------------------
# Build .gba ROM
# ------------------------------------------------------------
add_custom_command(
    OUTPUT first.gba
    COMMAND ${OBJCOPY} -O binary first.elf first.gba
    COMMAND $<TARGET_FILE:gbafix> first.gba -tFIRST -c00
    DEPENDS first.elf
)

add_custom_target(rom ALL DEPENDS first.gba)

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
