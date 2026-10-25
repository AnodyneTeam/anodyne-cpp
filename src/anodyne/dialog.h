#pragma once

#include "obj_vram_allocator.hpp"

// Dialogue: the compiled English text (dialogue_EN.txt) and the text box that plays it, ported from AnodyneSharp's
// DialogueManager / DialogueState / TextBox / TextWriter.
//
// The box is drawn with 8 semi-transparent OBJs (OAM 120-127, OBJ palette bank 15, 136 tiles of OBJ VRAM while it
// is open) that carry the box texture and the typed text, so it works over any world or the black intro screen.

// Gives the box the OBJ VRAM allocator (once at start-up).
void Dialog_Init(ObjVramAllocator& vram);

// The map the dialogue "area" lookups use (GlobalState.CURRENT_MAP_NAME).
void Dialog_SetArea(const char* mapName);

// DialogueManager.GetDialogue: the line `id` of npc/area/scene (id -1 = the scene's next line; a scene remembers its
// progress, loops at its LOOP line). Returns text with \n as a real newline and '^' pause markers. *top = the
// scene wants the box at the top of the screen. The area defaults to the one from Dialog_SetArea.
const char* Dialog_Get(const char* npc, const char* scene, int id = -1, bool* top = nullptr);
const char* Dialog_GetFor(const char* npc, const char* area, const char* scene, int id = -1, bool* top = nullptr);

// Opens the box and starts typing `text` (must stay valid while open: ROM strings are). `intro` = IntroState's
// box: 4x speed-up and a red button prompt instead of the blinking arrow. Needs OBJ VRAM; does nothing if it is
// full. Only one dialogue at a time.
void Dialog_Open(const char* text, bool top = false, bool intro = false);

bool Dialog_IsOpen();

// Per frame while open (also on the frame it closes). Returns true while the box is still open afterwards.
bool Dialog_Update();

// Plain text on the screen with no box (the death screen's "Continue?" menu): `count` lines of black text, line i at
// (xs[i], ys[i]) of the playfield (ys within 0-40 of `top`, the y of the text block), and a selector arrow at
// (selectorX, selectorY) of the playfield unless selectorY < 0. Uses the box's OBJs, so not while it is open.
void Dialog_ShowLabels(const char* const* lines, const int* xs, const int* ys, int count, int top, int selectorX,
                       int selectorY);
void Dialog_HideLabels();

// Closes the box immediately (hides the OBJs, frees the VRAM, restores the blend registers).
void Dialog_Close();
