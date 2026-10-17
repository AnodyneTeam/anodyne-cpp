#pragma once

#include <stdint.h>

// The C# pixelation shader, for the title and the warps. The hardware mosaic stops at 16px blocks, the shader keeps
// going (about 28px at the end of the title, 48px leaving BLANK), so past 10 the blocks are built in software: the
// screen is sampled at the top-left pixel of each block (as the shader does) and shown as an affine BG scaled up
// to the block size. Needs the BG scroll offsets because the scroll registers are write-only.
struct MosaicBgOffsets final
{
    int16_t x[4] = {};
    int16_t y[4] = {};
    // Where the shader's block grid starts on the GBA screen: the top-left of the C# screen. The playfield is at x=40
    // and the C# draws a 20px HUD above the game area (the title screen is shifted up by 10 instead).
    int originX = 40;
    int originY = -20;
};

// `pix` is the C# Pixelation (8.8 fixed, 1.0 = off). Call every frame while the effect runs, after the sprites are drawn.
void Mosaic_Apply(int pix, const MosaicBgOffsets& offsets);

// Called from the VBlank interrupt: writes the blocks computed since the last frame. No-op when nothing is pending.
void Mosaic_OnVBlank();

// Back to the plain screen: clears the mosaic and, if the software blocks were showing, restores the display, palette
// and VRAM they borrowed. Safe to call when nothing is active.
void Mosaic_End();
