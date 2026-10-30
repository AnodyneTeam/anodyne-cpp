#pragma once

#include "map_data.h"

// Darkness.Render with a texture (the *_overlay / *_hardlight maps): the C# blends a gradient over every pixel of the
// game screen, which the GBA can't. Here the colours of the BG palette and of the entities' OBJ banks are blended
// with each 8px band's texture colour, and an interrupt swaps the band's palettes in as the screen is drawn.
// The game keeps writing plain colours to palette RAM: once a frame TexDark_Frame takes any it changed as the new
// base colours.

void TexDark_Init();
// Once a frame while the play state draws: the texture (nullptr: none), its alpha (Darkness.alpha, 0-256), the map's
// ExtraBlend over it and its FG_Blend under it (nullptr: none), the grayscale between the darkness and the
// ExtraBlend (any of these on alone runs without a darkness texture) and a tinting flash over all (FadeEffect: 0-32 of
// the way to tintColour).
void TexDark_Frame(const DarkTexture* texture, int alpha, int tint, uint16_t tintColour,
                   const DarkTexture* extra = nullptr, const DarkTexture* fg = nullptr, bool gray = false);
// A stand-in texture for Darkness.Render without one: the screen mixed towards black by alpha.
const DarkTexture* TexDark_Plain();
// How many of the BG colours the map uses (from 0): only those are blended. Those from fgFrom on are only the FG
// layer's, which the FG_Blend leaves alone.
void TexDark_SetBgColours(int count, int fgFrom);
bool TexDark_Active();
// Puts the plain colours back and stops (another screen takes over the palettes).
void TexDark_Off();
