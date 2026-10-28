#pragma once

#include <stdint.h>

// The bot link over the link port, for the ROM (ANODYNE_LINK builds): the simulator's line protocol
// (platform/sim/sim_ipc.cpp) carried over JOY Bus, so a tool on the other end of the cable (tools/link/mgba_link on
// mGBA) can drive the ROM as SimHost drives the simulator, and check it against a C# recording frame by frame.
//
// On the wire everything is 32-bit JOY Bus words of four bytes each, low byte first; zero bytes are padding.
//   ROM -> host (JOY_TRANS): text lines, each ending in '\n'; a 0x01 byte asks the host for its next line.
//   host -> ROM (JOY_RECV): that line, ending in '\n'.
// Each frame the ROM sends the state line, then takes commands until "keys" (see Link_OnFrame).

// Once at boot: puts the link port in JOY Bus mode.
void Link_Init();

// Once a frame, before the keys are read: unless the last "keys" is still being held, sends the state and takes
// the host's commands until its next "keys".
void Link_OnFrame();

// The keys the host holds down (KEYINPUT bits, 1 = pressed).
uint16_t Link_Keys();
