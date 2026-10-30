#pragma once

// What the game tells tools about its state: the simulator's per-frame trace and its bot link (the C# autoplay bot
// drives the port through it). Each call writes a NUL-terminated string into buf (at most cap bytes, truncated)
// and returns its length. The simulator and the link build (ANODYNE_LINK) call these.

#ifdef __cplusplus
extern "C" {
#endif

// Takes each piece of a streamed answer (the ...To calls), in order.
typedef void (*GameProbe_Sink)(const char* data, int len);

// One JSON object describing the current frame: mode, map, screen, player, dialogue, inventory and the entities on
// screen (see game_probe.cpp for the fields).
int GameProbe_Json(char* buf, int cap);
// The same, handed to sink in pieces of up to cap - 1 bytes through scratch (whole of any length).
void GameProbe_JsonTo(GameProbe_Sink sink, char* scratch, int cap);

// The current map's collision: "W H " then two hex digits per 16x16 cell, row by row (blocked sides, tile event;
// see World_GetCell).
int GameProbe_Collision(char* buf, int cap);
void GameProbe_CollisionTo(GameProbe_Sink sink, char* scratch, int cap);

// TSV trace in the C# bot's format (src/tools/bot/golden/*.trace.tsv): the header line, then one row per frame.
int GameProbe_TraceHeader(char* buf, int cap);
int GameProbe_TraceRow(unsigned long long frame, char* buf, int cap);

// Test shortcut: warps to (x, y) of map, with the broom, like a door would. 0 if there's no such map.
int GameProbe_Warp(const char* map, int x, int y);
// Parity replays: GlobalState.checkpoint (the bot sets its own on the death screen). 0 if there's no such map.
int GameProbe_SetCheckpoint(const char* map, int x, int y);
// Test shortcut: back to full health.
void GameProbe_Heal(void);
// Parity replays from a saved game (the C# bot's parts that start from a save): gives what the save has. `what` is
// "brooms" (all four), "jump" (the boots), "cards" (the first n) or "hearts" (n, full). 0 if `what` is none of those.
int GameProbe_Give(const char* what, int n);
// Test shortcut: sets an EventRegister value (seen by entities made from then on).
void GameProbe_SetEvent(const char* name, int value);
// Parity replays: GlobalState.RNG as the C#'s is after `draws` numbers (reseeded with the deterministic seed). The
// screen shake starts over too, unless `keepShake`.
void GameProbe_SetRng(unsigned draws, bool keepShake);
// Parity replays: the game's own frame count (RecordingManager.Now's), set to `frame` first if `set`.
unsigned GameProbe_GameFrame(bool set, unsigned frame);

#ifdef __cplusplus
}
#endif
