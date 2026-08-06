
add_executable(gbafix
    ${SOURCES}gbafix/gbafix.c
)

add_library(lodepng 
    ../3rdParty/lodepng/lodepng.cpp)

add_executable(CompileContent 
    content_compiler/map_compiler.cpp
    content_compiler/dialog_compiler.cpp
    content_compiler/main.cpp)

target_include_directories(CompileContent PRIVATE ../3rdParty/lodepng)

target_link_libraries(CompileContent lodepng)

set_property(TARGET CompileContent PROPERTY CXX_STANDARD 17)

