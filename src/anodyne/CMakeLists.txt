

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
# Executable target
# ------------------------------------------------------------
add_executable(first.elf
    ${SOURCES}platform/gba/sys/syscalls.c
    ${SOURCES}platform/gba/sys/gba_crt0.s
    anodyne.c
    dialog_manager.c
)


# ------------------------------------------------------------
# Compile options
# ------------------------------------------------------------
target_compile_options(first.elf PRIVATE
    -mcpu=arm7tdmi
    -mtune=arm7tdmi
    -mthumb
    -mthumb-interwork
    -ffunction-sections
    -fdata-sections
    -D__GBA__
)

target_compile_options(first.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=gnu11 -O3 -Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++14 -O3 -Wall -fno-exceptions -fno-rtti>
)

target_include_directories(first.elf PRIVATE
    "${LIBTONC_DIR}/include"
)

# ------------------------------------------------------------
# Link options
# ------------------------------------------------------------
set_target_properties(first.elf PROPERTIES
    OUTPUT_NAME "first"
    SUFFIX ".elf"
)
set_property(TARGET first.elf PROPERTY
    WIN32_EXECUTABLE FALSE
)

target_link_options(first.elf PRIVATE "")
target_link_options(first.elf PRIVATE
    -mcpu=arm7tdmi
    -mtune=arm7tdmi
    -mthumb
    -mthumb-interwork
    -Wl,-Map,first.map
    -Wl,--gc-sections
    -specs=nano.specs
    -T${SOURCEDIR}/anodyne/platform/gba/sys/gba_cart.ld
)

# ------------------------------------------------------------
# Link libraries (IMPORTANT: libtonc must be here, not in link options)
# ------------------------------------------------------------
target_link_libraries(first.elf PRIVATE
    -Wl,--start-group
        "$<TARGET_FILE:tonc>"
        c
    -Wl,--end-group
)
add_dependencies(first.elf gbafix)
add_dependencies(first.elf tonc)

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
