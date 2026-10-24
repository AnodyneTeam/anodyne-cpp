#pragma once

// Bot link: a lock-step line protocol on a localhost TCP socket, so an outside program (the C# autoplay bot's
// simulator adapter) can play the port.
//
// Every time the simulator needs keys it sends one line:
//     {"frame":N,"state":{...GameProbe_Json...}}
// and then reads command lines until one of them advances time:
//     keys <hex mask> [frames]   hold these keys (KEYINPUT bit order, 1 = pressed) for that many frames (default 1)
//     shot <path.png>            save the current frame                       -> "ok" / "error ..."
//     peek <hex addr> <len>      read memory (EWRAM/IWRAM/IO/PAL/VRAM/OAM)     -> hex bytes
//     state                      the state line again
//     quit                       exit the simulator
// While held keys last, frames run without waiting for the bot.

#include <stdint.h>

namespace sim
{
void Ipc_Listen(int port);
void Ipc_Close();
bool Ipc_Active();
// Called once per frame at VBlank, before the keys for the next frame are taken.
void Ipc_OnFrame(uint64_t frame, const uint32_t* pixels);
uint16_t Ipc_Keys();

void Trace_Open(const char* path);
void Trace_Frame(uint64_t frame);
void Trace_Close();
} // namespace sim
