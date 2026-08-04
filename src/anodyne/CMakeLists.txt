# ------------------------------------------------------------
# Toolchain
# ------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Generic)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

set(OBJCOPY arm-none-eabi-objcopy)
set(OBJDUMP arm-none-eabi-objdump)

set(SOURCEDIR "${CMAKE_SOURCE_DIR}")
set(LIBTONC_DIR "${SOURCEDIR}/3rdParty/libtonc")

# ------------------------------------------------------------
# Common sources
# ------------------------------------------------------------
set(FIRST_SOURCES
    ${SOURCEDIR}/anodyne/platform/gba/sys/syscalls.c
    ${SOURCEDIR}/anodyne/platform/gba/sys/gba_crt0.s
    anodyne.c
    dialog_manager.c
)

# ------------------------------------------------------------
# Linux / UNIX: normal executable, let CMake drive GCC
# ------------------------------------------------------------
if(UNIX AND NOT WIN32)
    add_executable(first.elf ${FIRST_SOURCES})

    target_compile_options(first.elf PRIVATE
        -mcpu=arm7tdmi -mtune=arm7tdmi
        -mthumb -mthumb-interwork
        -ffunction-sections -fdata-sections
        -D__GBA__
        $<$<COMPILE_LANGUAGE:C>:-std=gnu11 -O3 -Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++14 -O3 -Wall -fno-exceptions -fno-rtti>
    )

    target_include_directories(first.elf PRIVATE
        "${LIBTONC_DIR}/include"
    )

    target_link_options(first.elf PRIVATE
        -mcpu=arm7tdmi -mtune=arm7tdmi
        -mthumb -mthumb-interwork
        -Wl,-Map,first.map
        -Wl,--gc-sections
        -specs=nano.specs
        -T${SOURCEDIR}/anodyne/platform/gba/sys/gba_cart.ld
    )

    target_link_libraries(first.elf PRIVATE
        "$<TARGET_FILE:tonc>"
        c
    )

    add_dependencies(first.elf gbafix tonc)

# ------------------------------------------------------------
# Windows: OBJECT library + manual link to avoid PE flags
# ------------------------------------------------------------
else() # WIN32
    add_library(first_obj OBJECT ${FIRST_SOURCES})

    target_compile_options(first_obj PRIVATE
        -mcpu=arm7tdmi -mtune=arm7tdmi
        -mthumb -mthumb-interwork
        -ffunction-sections -fdata-sections
        -D__GBA__
        $<$<COMPILE_LANGUAGE:C>:-std=gnu11 -O3 -Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++14 -O3 -Wall -fno-exceptions -fno-rtti>
    )

    target_include_directories(first_obj PRIVATE
        "${LIBTONC_DIR}/include"
    )

    add_dependencies(first_obj gbafix tonc)

    add_custom_command(
        OUTPUT first.elf
        COMMAND arm-none-eabi-gcc
            -mcpu=arm7tdmi -mthumb -mthumb-interwork
            -Wl,-Map,first.map -Wl,--gc-sections
            -specs=nano.specs
            -T${SOURCEDIR}/anodyne/platform/gba/sys/gba_cart.ld
            $<TARGET_OBJECTS:first_obj>
            "$<TARGET_FILE:tonc>" -lc -o first.elf
        DEPENDS first_obj tonc
    )
endif()

# ------------------------------------------------------------
# .gba and dump (shared)
# ------------------------------------------------------------
add_custom_command(
    OUTPUT first.gba
    COMMAND ${OBJCOPY} -O binary first.elf first.gba
    COMMAND $<TARGET_FILE:gbafix> first.gba -tFIRST -c00
    DEPENDS first.elf
)

add_custom_target(rom ALL DEPENDS first.gba)

add_custom_command(
    OUTPUT first.dump
    COMMAND ${OBJDUMP} -h -C -S first.elf > first.dump
    DEPENDS first.elf
)

add_custom_target(dump DEPENDS first.dump)

set_property(DIRECTORY PROPERTY ADDITIONAL_CLEAN_FILES
    "first.gba" "first.dump" "first.map"
)
