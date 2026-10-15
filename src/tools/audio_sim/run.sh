#!/bin/sh
# Builds and runs the host-side audio timing/sequencing simulator (see
# main.cpp's header comment for exactly what it does and doesn't verify).
#
# Requires the real project to have already been built at least once (needs
# ../../build/tools/audio_registry.h and audio_blob.bin from CompileContent).
#
# Usage: ./run.sh [toolsDir] [loopIterationsToVerify]

set -e
cd "$(dirname "$0")"

BUILD_TOOLS_DIR="${1:-../../build/tools}"
LOOP_ITERATIONS="${2:-3}"
OUT_DIR="${TMPDIR:-/tmp}/audio_sim_build"
mkdir -p "$OUT_DIR"

gcc -std=c11 -O2 -I../../3rdParty/qoa \
    -c ../../anodyne/audio_qoa_impl.c -o "$OUT_DIR/audio_qoa_impl_host.o"

g++ -std=c++17 -O2 -Wall -Wextra \
    -I../../3rdParty/qoa \
    -c ../../anodyne/audio_decode_core.cpp -o "$OUT_DIR/audio_decode_core_host.o"

g++ -std=c++17 -O2 -Wall -Wextra \
    -I../../3rdParty/qoa \
    -I"$BUILD_TOOLS_DIR" \
    -c main.cpp -o "$OUT_DIR/audio_sim_main.o"

g++ "$OUT_DIR/audio_sim_main.o" "$OUT_DIR/audio_decode_core_host.o" "$OUT_DIR/audio_qoa_impl_host.o" \
    -o "$OUT_DIR/audio_sim" -lm

"$OUT_DIR/audio_sim" "$BUILD_TOOLS_DIR" "$LOOP_ITERATIONS"
