
add_executable(gbafix
    ${SOURCES}gbafix/gbafix.c
)


add_executable(CompileContent content_compiler/dialog_compiler.cpp content_compiler/main.cpp)
set_property(TARGET CompileContent PROPERTY CXX_STANDARD 17)

