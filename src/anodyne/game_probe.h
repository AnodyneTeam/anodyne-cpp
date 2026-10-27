#pragma once

// What the game tells tools about its state: the simulator's per-frame trace and its bot link (the C# autoplay bot
// drives the port through it). Each call writes a NUL-terminated string into buf (at most cap bytes, truncated)
// and returns its length. Only the simulator build calls these.

#ifdef __cplusplus
extern "C" {
#endif

// One JSON object describing the current frame: mode, map, screen, player, dialogue, inventory and the entities on
// screen (see game_probe.cpp for the fields).
int GameProbe_Json(char* buf, int cap);

// The current map's collision: "W H " then two hex digits per 16x16 cell, row by row (blocked sides, tile event;
// see World_GetCell).
int GameProbe_Collision(char* buf, int cap);

// TSV trace in the C# bot's format (src/tools/bot/golden/*.trace.tsv): the header line, then one row per frame.
int GameProbe_TraceHeader(char* buf, int cap);
int GameProbe_TraceRow(unsigned long long frame, char* buf, int cap);

// Test shortcut: warps to (x, y) of map, with the broom, like a door would. 0 if there's no such map.
int GameProbe_Warp(const char* map, int x, int y);
// Parity replays: GlobalState.checkpoint (the bot sets its own on the death screen). 0 if there's no such map.
int GameProbe_SetCheckpoint(const char* map, int x, int y);
// Test shortcut: back to full health.
void GameProbe_Heal(void);
// Parity replays: GlobalState.RNG as the C#'s is after `draws` numbers (reseeded with the deterministic seed). The
// screen shake starts over too, unless `keepShake`.
void GameProbe_SetRng(unsigned draws, bool keepShake);
// Parity replays: the game's own frame count (RecordingManager.Now's), set to `frame` first if `set`.
unsigned GameProbe_GameFrame(bool set, unsigned frame);

#ifdef __cplusplus
}
#endif
