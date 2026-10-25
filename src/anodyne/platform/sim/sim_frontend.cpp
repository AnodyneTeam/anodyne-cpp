// The simulator's outside world: SDL3 window, keyboard/gamepad, frame pacing, scripted input (.gbainput), input
// recording, screenshots, the sound device and the bot link (sim_ipc.cpp).

#include "sim_frontend.h"
#include "sim_internal.h"
#include "sim_ipc.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deque>

#include "lodepng.h"

namespace sim
{
namespace
{
// GBA key bits (KEYINPUT, active low): A B Select Start Right Left Up Down R L.
enum : uint16_t
{
    K_A = 1 << 0,
    K_B = 1 << 1,
    K_SELECT = 1 << 2,
    K_START = 1 << 3,
    K_RIGHT = 1 << 4,
    K_LEFT = 1 << 5,
    K_UP = 1 << 6,
    K_DOWN = 1 << 7,
    K_R = 1 << 8,
    K_L = 1 << 9,
};

FrontendOptions gOpt;
SDL_Window* gWindow = nullptr;
SDL_Renderer* gRenderer = nullptr;
SDL_Texture* gTexture = nullptr;
SDL_Gamepad* gPad = nullptr;
bool gQuit = false;
bool gTurbo = false; // Tab held: fast forward
uint64_t gNextFrameNs = 0;
constexpr uint64_t kFrameNs = 1000000000ull * kCyclesPerFrame / kClockHz; // 16.74ms (59.73 Hz)

struct Run final
{
    uint32_t mFrames;
    uint16_t mMask;
};
std::deque<Run> gPlayback;
bool gPlaybackLoaded = false;
FILE* gRecord = nullptr;
uint16_t gRecordMask = 0;
uint32_t gRecordCount = 0;

bool LoadInput(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f)
    {
        return false;
    }
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#')
        {
            continue;
        }
        unsigned frames = 0;
        unsigned mask = 0;
        if (sscanf(line, "%u %x", &frames, &mask) == 2 && frames > 0)
        {
            gPlayback.push_back({ frames, static_cast<uint16_t>(mask & 0x3FF) });
        }
    }
    fclose(f);
    gPlaybackLoaded = true;
    return true;
}

void FlushRecord()
{
    if (gRecord && gRecordCount)
    {
        fprintf(gRecord, "%u %03x\n", gRecordCount, gRecordMask);
        gRecordCount = 0;
    }
}

void Record(uint16_t mask)
{
    if (!gRecord)
    {
        return;
    }
    if (gRecordCount && mask != gRecordMask)
    {
        FlushRecord();
    }
    gRecordMask = mask;
    gRecordCount++;
}

uint16_t LiveKeys()
{
    uint16_t mask = 0;
    if (gWindow)
    {
        const bool* ks = SDL_GetKeyboardState(nullptr);
        if (ks[SDL_SCANCODE_X] || ks[SDL_SCANCODE_K])
        {
            mask |= K_A;
        }
        if (ks[SDL_SCANCODE_Z] || ks[SDL_SCANCODE_J])
        {
            mask |= K_B;
        }
        if (ks[SDL_SCANCODE_BACKSPACE] || ks[SDL_SCANCODE_RSHIFT])
        {
            mask |= K_SELECT;
        }
        if (ks[SDL_SCANCODE_RETURN])
        {
            mask |= K_START;
        }
        if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D])
        {
            mask |= K_RIGHT;
        }
        if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A])
        {
            mask |= K_LEFT;
        }
        if (ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W])
        {
            mask |= K_UP;
        }
        if (ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_S])
        {
            mask |= K_DOWN;
        }
        if (ks[SDL_SCANCODE_Q])
        {
            mask |= K_L;
        }
        if (ks[SDL_SCANCODE_E])
        {
            mask |= K_R;
        }
        gTurbo = ks[SDL_SCANCODE_TAB];
    }
    if (gPad)
    {
        const struct
        {
            SDL_GamepadButton mButton;
            uint16_t mKey;
        } kMap[] = {
            { SDL_GAMEPAD_BUTTON_SOUTH, K_A },        { SDL_GAMEPAD_BUTTON_EAST, K_B },
            { SDL_GAMEPAD_BUTTON_BACK, K_SELECT },    { SDL_GAMEPAD_BUTTON_START, K_START },
            { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, K_RIGHT }, { SDL_GAMEPAD_BUTTON_DPAD_LEFT, K_LEFT },
            { SDL_GAMEPAD_BUTTON_DPAD_UP, K_UP },     { SDL_GAMEPAD_BUTTON_DPAD_DOWN, K_DOWN },
            { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, K_R }, { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, K_L },
        };
        for (const auto& m : kMap)
        {
            if (SDL_GetGamepadButton(gPad, m.mButton))
            {
                mask |= m.mKey;
            }
        }
        const int lx = SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_LEFTX);
        const int ly = SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_LEFTY);
        mask |= lx > 16000 ? K_RIGHT : (lx < -16000 ? K_LEFT : 0);
        mask |= ly > 16000 ? K_DOWN : (ly < -16000 ? K_UP : 0);
    }
    // The D-pad can't press opposite directions at once.
    if ((mask & (K_LEFT | K_RIGHT)) == (K_LEFT | K_RIGHT))
    {
        mask &= ~(K_LEFT | K_RIGHT);
    }
    if ((mask & (K_UP | K_DOWN)) == (K_UP | K_DOWN))
    {
        mask &= ~(K_UP | K_DOWN);
    }
    return mask;
}

uint16_t NextKeys()
{
    if (Ipc_Active())
    {
        return Ipc_Keys();
    }
    if (gPlaybackLoaded)
    {
        if (!gPlayback.empty())
        {
            Run& r = gPlayback.front();
            const uint16_t mask = r.mMask;
            if (--r.mFrames == 0)
            {
                gPlayback.pop_front();
            }
            return mask;
        }
        if (!gWindow)
        {
            return 0;
        }
    }
    return LiveKeys();
}

void PollEvents()
{
    if (!gWindow)
    {
        return;
    }
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        switch (ev.type)
        {
        case SDL_EVENT_QUIT:
            gQuit = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.scancode == SDL_SCANCODE_ESCAPE)
            {
                gQuit = true;
            }
            else if (ev.key.scancode == SDL_SCANCODE_F12)
            {
                char name[64];
                snprintf(name, sizeof(name), "shot_%06llu.png", static_cast<unsigned long long>(FrameCount()));
                Frontend_SavePng(Ppu_Frame(), name);
                Log("saved %s", name);
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gPad)
            {
                gPad = SDL_OpenGamepad(ev.gdevice.which);
            }
            break;
        default:
            break;
        }
    }
}

void Present(const uint32_t* frame)
{
    if (!gWindow)
    {
        return;
    }
    SDL_UpdateTexture(gTexture, nullptr, frame, kScreenW * 4);
    SDL_SetRenderDrawColor(gRenderer, 0, 0, 0, 255);
    SDL_RenderClear(gRenderer);
    SDL_RenderTexture(gRenderer, gTexture, nullptr, nullptr);
    SDL_RenderPresent(gRenderer);
}

void Pace()
{
    if (gOpt.mFast || gTurbo || gOpt.mHeadless || Ipc_Active())
    {
        gNextFrameNs = 0;
        return;
    }
    const uint64_t now = SDL_GetTicksNS();
    if (gNextFrameNs == 0 || now > gNextFrameNs + 4 * kFrameNs)
    {
        gNextFrameNs = now; // first frame, or we fell far behind: don't try to catch up
    }
    gNextFrameNs += kFrameNs;
    if (gNextFrameNs > now)
    {
        SDL_DelayPrecise(gNextFrameNs - now);
    }
}

void Usage()
{
    fprintf(stderr,
            "usage: anodyne_sim [options]\n"
            "  --scale N          window scale (default 3)\n"
            "  --headless         no window, no sound device (for tests and bots)\n"
            "  --fast             don't hold 59.73 fps (Tab fast-forwards while held)\n"
            "  --frames N         quit after N frames\n"
            "  --input FILE       play keys from a .gbainput file (\"<frames> <hex mask>\" lines)\n"
            "  --record FILE      record the keys pressed as .gbainput\n"
            "  --shot N:FILE.png  screenshot at frame N (repeatable)\n"
            "  --shot-every N:DIR screenshot every N frames into DIR\n"
            "  --wav FILE         write the sound output\n"
            "  --no-audio         no sound device\n"
            "  --no-io-trap       poll IO registers once per line instead of trapping each write\n"
            "  --no-render        don't draw the frames (faster; screenshots come out blank)\n"
            "  --ipc PORT         serve the bot protocol on 127.0.0.1:PORT (lock-step, see sim_ipc.h)\n"
            "  --trace FILE       per-frame game state trace (TSV)\n"
            "keys: arrows/WASD d-pad, X/K = A, Z/J = B, Enter = Start, Backspace = Select, Q/E = L/R, F12 shot, Esc quit\n");
}
} // namespace

bool Frontend_ParseArgs(int argc, char** argv, FrontendOptions& o)
{
    for (int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        const bool hasNext = i + 1 < argc;
        if (!strcmp(a, "--scale") && hasNext)
        {
            o.mScale = atoi(argv[++i]);
        }
        else if (!strcmp(a, "--headless"))
        {
            o.mHeadless = true;
        }
        else if (!strcmp(a, "--fast"))
        {
            o.mFast = true;
        }
        else if (!strcmp(a, "--frames") && hasNext)
        {
            o.mMaxFrames = atoll(argv[++i]);
        }
        else if (!strcmp(a, "--input") && hasNext)
        {
            o.mInputPath = argv[++i];
        }
        else if (!strcmp(a, "--record") && hasNext)
        {
            o.mRecordPath = argv[++i];
        }
        else if (!strcmp(a, "--wav") && hasNext)
        {
            o.mWavPath = argv[++i];
        }
        else if (!strcmp(a, "--no-audio"))
        {
            o.mAudio = false;
        }
        else if (!strcmp(a, "--no-io-trap"))
        {
            o.mIoTrap = false;
        }
        else if (!strcmp(a, "--no-render"))
        {
            o.mRender = false;
        }
        else if (!strcmp(a, "--ipc") && hasNext)
        {
            o.mIpcPort = atoi(argv[++i]);
        }
        else if (!strcmp(a, "--trace") && hasNext)
        {
            o.mTracePath = argv[++i];
        }
        else if ((!strcmp(a, "--shot") || !strcmp(a, "--shot-every")) && hasNext)
        {
            const char* spec = argv[++i];
            const char* colon = strchr(spec, ':');
            if (!colon)
            {
                Usage();
                return false;
            }
            const uint64_t n = strtoull(spec, nullptr, 10);
            if (!strcmp(a, "--shot"))
            {
                o.mShots.push_back({ n, colon + 1 });
            }
            else
            {
                o.mShotEvery = static_cast<int>(n);
                o.mShotDir = colon + 1;
            }
        }
        else
        {
            Usage();
            return false;
        }
    }
    return true;
}

void Frontend_Init(const FrontendOptions& options)
{
    gOpt = options;
    SDL_InitFlags flags = SDL_INIT_EVENTS;
    if (!gOpt.mHeadless)
    {
        flags |= SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;
    }
    const bool audioDevice = gOpt.mAudio && !gOpt.mHeadless;
    if (audioDevice)
    {
        flags |= SDL_INIT_AUDIO;
    }
    if (!SDL_Init(flags))
    {
        Fatal("SDL_Init: %s", SDL_GetError());
    }
    if (!gOpt.mHeadless)
    {
        if (!SDL_CreateWindowAndRenderer("Anodyne (GBA simulator)", kScreenW * gOpt.mScale, kScreenH * gOpt.mScale,
                                         SDL_WINDOW_RESIZABLE, &gWindow, &gRenderer))
        {
            Fatal("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
        }
        SDL_SetRenderVSync(gRenderer, 0);
        SDL_SetRenderLogicalPresentation(gRenderer, kScreenW, kScreenH, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        gTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kScreenW, kScreenH);
        SDL_SetTextureScaleMode(gTexture, SDL_SCALEMODE_NEAREST);
    }
    Apu_Init(audioDevice, gOpt.mWavPath.empty() ? nullptr : gOpt.mWavPath.c_str());
    if (!gOpt.mInputPath.empty() && !LoadInput(gOpt.mInputPath.c_str()))
    {
        Fatal("can't read %s", gOpt.mInputPath.c_str());
    }
    if (!gOpt.mRecordPath.empty())
    {
        gRecord = fopen(gOpt.mRecordPath.c_str(), "w");
        if (gRecord)
        {
            fprintf(gRecord, "# frames mask(hex, 1=pressed): A B Sel Start Right Left Up Down R L\n");
        }
    }
    Trace_Open(gOpt.mTracePath.empty() ? nullptr : gOpt.mTracePath.c_str());
    if (gOpt.mIpcPort > 0)
    {
        Ipc_Listen(gOpt.mIpcPort);
    }
}

void Frontend_Shutdown()
{
    FlushRecord();
    if (gRecord)
    {
        fclose(gRecord);
        gRecord = nullptr;
    }
    Trace_Close();
    Ipc_Close();
    Apu_Shutdown();
    if (gPad)
    {
        SDL_CloseGamepad(gPad);
        gPad = nullptr;
    }
    SDL_Quit();
}

void Frontend_OnVBlank(const uint32_t* frame)
{
    const uint64_t n = FrameCount(); // frames completed so far (this one included)
    Present(frame);
    for (const auto& shot : gOpt.mShots)
    {
        if (shot.first == n)
        {
            Frontend_SavePng(frame, shot.second.c_str());
        }
    }
    if (gOpt.mShotEvery > 0 && n % static_cast<uint64_t>(gOpt.mShotEvery) == 0)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/frame_%06llu.png", gOpt.mShotDir.c_str(), static_cast<unsigned long long>(n));
        Frontend_SavePng(frame, path);
    }
    Trace_Frame(n);
    // Debug: ANODYNE_SIM_DUMP=<frame> prints the display registers and writes VRAM+palette+OAM to /tmp/sim_vram.bin.
    if (getenv("ANODYNE_SIM_DUMP") && static_cast<uint64_t>(atoll(getenv("ANODYNE_SIM_DUMP"))) == n)
    {
        fprintf(stderr, "frame %llu IO:", static_cast<unsigned long long>(n));
        for (uint32_t ofs = 0; ofs < 0x60; ofs += 2)
        {
            fprintf(stderr, "%s%03X=%04X", (ofs % 16) ? " " : "\n  ", ofs, Io16(ofs));
        }
        fprintf(stderr, "\n");
        FILE* f = fopen("/tmp/sim_vram.bin", "wb");
        fwrite(Vram(), 1, kVramSize, f);
        fwrite(Pal(), 1, kPalSize, f);
        fwrite(Oam(), 1, kOamSize, f);
        fclose(f);
    }
    if (gOpt.mMaxFrames >= 0 && static_cast<int64_t>(n) >= gOpt.mMaxFrames)
    {
        gQuit = true;
    }
    PollEvents();
    if (gQuit)
    {
        exit(0);
    }
    if (Ipc_Active())
    {
        Ipc_OnFrame(n, frame); // sends the state, waits for the bot's keys
    }
    const uint16_t keys = NextKeys();
    Record(keys);
    IoSet16(0x130, static_cast<uint16_t>(~keys & 0x3FF));
    Pace();
}

bool Frontend_SavePng(const uint32_t* frame, const char* path)
{
    std::vector<unsigned char> rgba(kScreenW * kScreenH * 4);
    for (int i = 0; i < kScreenW * kScreenH; i++)
    {
        rgba[i * 4 + 0] = static_cast<unsigned char>(frame[i] >> 16);
        rgba[i * 4 + 1] = static_cast<unsigned char>(frame[i] >> 8);
        rgba[i * 4 + 2] = static_cast<unsigned char>(frame[i]);
        rgba[i * 4 + 3] = 255;
    }
    const unsigned err = lodepng::encode(path, rgba, kScreenW, kScreenH);
    if (err)
    {
        Log("can't write %s: %s", path, lodepng_error_text(err));
        return false;
    }
    return true;
}
} // namespace sim
