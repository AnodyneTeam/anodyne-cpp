
add_executable(gbafix
    ${SOURCES}gbafix/gbafix.c
)

add_library(lodepng
    ../3rdParty/lodepng/lodepng.cpp)

add_executable(CompileContent
    content_compiler/map_compiler.cpp
    content_compiler/dialog_compiler.cpp
    content_compiler/audio_compiler.cpp
    content_compiler/sprite_compiler.cpp
    content_compiler/door_compiler.cpp
    content_compiler/entity_compiler.cpp
    content_compiler/title_compiler.cpp
    content_compiler/ending_compiler.cpp
    content_compiler/menu_compiler.cpp
    content_compiler/hud_compiler.cpp
    content_compiler/collision_compiler.cpp
    content_compiler/sprite_defs.cpp
    content_compiler/vorbis_decode.cpp
    content_compiler/mp3_decode.cpp
    content_compiler/resampler.cpp
    content_compiler/dr_mp3_impl.c
    content_compiler/qoa_impl.c
    ../3rdParty/stb/stb_vorbis.c
    content_compiler/main.cpp)

add_custom_target(RunContentCompiler ALL
    COMMAND "${CMAKE_COMMAND}" -E echo "Running: $<TARGET_FILE:CompileContent>"
    COMMAND "${CMAKE_COMMAND}" -E env "$<TARGET_FILE:CompileContent>" ${CONTENT_COMPILER_ARGS}
    COMMENT "Always running content compiler"
)


add_dependencies(RunContentCompiler CompileContent)

target_include_directories(CompileContent PRIVATE
    ../3rdParty/lodepng
    ../3rdParty/qoa
    ../3rdParty/dr_libs
    ../3rdParty/stb)

target_link_libraries(CompileContent lodepng)

set_property(TARGET CompileContent PROPERTY CXX_STANDARD 17)

