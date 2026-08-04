mkdir build
set -e
cd build
cmake ..
make -j2

./tools/CompileContent
cd ..
~/Downloads/mGBA-0.10.5-appimage-x64.appimage build/anodyne/first.gba
