#pragma once

// Pixelate test scene, the counterpart of PixelateTest in AnodyneSharp (Drawing/Effects/Pixelate.cs): the same fixed
// 160x160 pattern at the playfield position, run through the real Mosaic_Apply at a chosen block size, so the two
// can be compared pixel by pixel (src/tools/pixelate_test/). Hold L+R at boot to get it; L/R then step the block
// size (1..48), or set gPixelateTestStride from a debugger/script.
void PixelateTest_Enter();
void PixelateTest_Update();
