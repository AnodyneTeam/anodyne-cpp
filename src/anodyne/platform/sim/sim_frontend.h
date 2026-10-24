#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace sim
{
struct FrontendOptions final
{
    int mScale = 3;
    bool mHeadless = false;    // no window (and no audio device)
    bool mFast = false;        // don't hold the frame rate at 59.73 Hz
    bool mAudio = true;
    bool mIoTrap = true;
    int64_t mMaxFrames = -1;   // quit after this many frames
    std::string mInputPath;    // .gbainput run-length key masks to play
    std::string mRecordPath;   // write the keys pressed as .gbainput
    std::string mWavPath;      // write the sound output
    std::string mShotDir;      // with mShotEvery: write a PNG every N frames
    int mShotEvery = 0;
    std::vector<std::pair<uint64_t, std::string>> mShots; // frame -> PNG path
    int mIpcPort = 0;          // >0: serve the bot protocol on 127.0.0.1:port (sim_ipc.cpp)
    std::string mTracePath;    // per-frame game state trace (TSV, the C# bot's trace format)
};

bool Frontend_ParseArgs(int argc, char** argv, FrontendOptions& out);
void Frontend_Init(const FrontendOptions& options);
void Frontend_Shutdown();
// Called at the start of VBlank with the finished frame: present it, pace, and set KEYINPUT for the next frame.
void Frontend_OnVBlank(const uint32_t* frame);
// Writes the frame as a PNG.
bool Frontend_SavePng(const uint32_t* frame, const char* path);
} // namespace sim
