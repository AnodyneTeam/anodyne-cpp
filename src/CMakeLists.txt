PROJECT(Anodyne LANGUAGES C CXX ASM)
cmake_minimum_required(VERSION 3.25)

# build with host compiler
add_subdirectory(tools)

# built with GBA toolchain/arm
add_subdirectory("3rdParty/libtonc")
add_subdirectory(anodyne)
