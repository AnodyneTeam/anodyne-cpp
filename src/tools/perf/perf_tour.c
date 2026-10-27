// Times the GBA ROM on mGBA: boots the perf build (cmake -DANODYNE_PERF=ON), starts a new game, then warps to every
// screen of every map through the ROM's PerfProbe and records the most CPU time a frame took there.
//
//   cc -O2 perf_tour.c -o perf_tour -lmgba   (Debian/Ubuntu: apt install libmgba-dev)
//   ./perf_tour <first.gba> <probe address, from: arm-none-eabi-nm first.elf | grep gPerfProbe> [frames per screen]
//               [__IWRAM_END__ address, to also report how much of the user stack was never touched]
//
// Prints one line per screen (map, screen, max cycles, % of a frame, frames over budget, entities) and a summary.
//
// With PERF_PROFILE=map,x,y,frames set it profiles that one screen instead: it steps the CPU an instruction at a time
// and writes "pc lr cycles" for every pair that ran to perf_profile.txt (tools/perf/symbolise.py groups them).
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    KEY_A = 1 << 0,
    KEY_START = 1 << 3,
};
enum
{
    P_MAGIC = 0,
    P_FRAMES = 4,
    P_LAST = 8,
    P_MAX = 12,
    P_LAG = 16,
    P_MODE = 20,
    P_CELLSW = 24,
    P_CELLSH = 28,
    P_MAPCOUNT = 32,
    P_CMDSEQ = 36,
    P_CMDMAP = 40,
    P_CMDX = 44,
    P_CMDY = 48,
    P_ACK = 52,
    P_MAP = 56,
    P_ENTITIES = 60,
};
static const unsigned kFrameCycles = 280896;
static const unsigned kModeMapViewer = 1;

static struct mCore* gCore;
static unsigned gProbe;
static unsigned gSeq;
static color_t* gVideo;
static unsigned gW, gH;

static unsigned Rd(unsigned off)
{
    return gCore->busRead32(gCore, gProbe + off);
}

static void Wr(unsigned off, unsigned v)
{
    gCore->busWrite32(gCore, gProbe + off, v);
}

static void Run(int frames, unsigned keys)
{
    gCore->setKeys(gCore, keys);
    for (int i = 0; i < frames; i++)
    {
        gCore->runFrame(gCore);
    }
    gCore->setKeys(gCore, 0);
}

static void Tap(unsigned keys, int wait)
{
    Run(2, keys);
    Run(wait, 0);
}

// The screen as a PPM (color_t is 32-bit XBGR here).
static void Shot(const char* path, const color_t* video, unsigned w, unsigned h)
{
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (unsigned i = 0; i < w * h; i++)
    {
        const unsigned c = (unsigned)video[i];
        fputc(c & 0xFF, f);
        fputc((c >> 8) & 0xFF, f);
        fputc((c >> 16) & 0xFF, f);
    }
    fclose(f);
}

static bool WarpOnce(int map, int x, int y)
{
    Wr(P_CMDMAP, map);
    Wr(P_CMDX, x);
    Wr(P_CMDY, y);
    Wr(P_CMDSEQ, ++gSeq);
    Run(140, 0); // the warp's fade out and in
    // A dialogue (or what's left of a cutscene) can hold it back: talk through it.
    for (int i = 0; i < 10 && (int)Rd(P_MAP) != map; i++)
    {
        Tap(KEY_A, 20);
    }
    return (int)Rd(P_MAP) == map;
}

static void Warp(int map, int x, int y)
{
    if (!WarpOnce(map, x, y) && !WarpOnce(map, x, y))
    {
        fprintf(stderr, "warp to map %d at %d,%d didn't land (on map %d, mode %u)\n", map, x, y, (int)Rd(P_MAP),
                Rd(P_MODE));
        Shot("perf_fail.ppm", gVideo, gW, gH);
    }
}

static void NoLog(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args)
{
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <rom> <probe address> [frames per screen] [__IWRAM_END__]\n", argv[0]);
        return 1;
    }
    static struct mLogger logger = { .log = NoLog };
    mLogSetDefaultLogger(&logger);
    gProbe = (unsigned)strtoul(argv[2], NULL, 16);
    const int frames = argc > 3 ? atoi(argv[3]) : 120;

    gCore = mCoreFind(argv[1]);
    if (!gCore || !gCore->init(gCore))
    {
        fprintf(stderr, "no core for %s\n", argv[1]);
        return 1;
    }
    mCoreInitConfig(gCore, NULL);
    unsigned w, h;
    gCore->desiredVideoDimensions(gCore, &w, &h);
    color_t* video = malloc(w * h * sizeof(color_t));
    gCore->setVideoBuffer(gCore, video, w);
    gVideo = video;
    gW = w;
    gH = h;
    if (!mCoreLoadFile(gCore, argv[1]))
    {
        fprintf(stderr, "can't load %s\n", argv[1]);
        return 1;
    }
    gCore->reset(gCore);

    // The title, then a new game (as simlink.py does).
    Run(200, 0);
    Tap(KEY_START, 90);
    Tap(KEY_A, 90);
    for (int i = 0; i < 300 && Rd(P_MODE) != kModeMapViewer; i++)
    {
        Tap(KEY_A, 20);
    }
    if (Rd(P_MAGIC) != 0x46524550 || Rd(P_MODE) != kModeMapViewer)
    {
        fprintf(stderr, "never reached play (magic %08x mode %u): screen in perf_fail.ppm\n", Rd(P_MAGIC), Rd(P_MODE));
        Shot("perf_fail.ppm", video, w, h);
        return 1;
    }
    Run(60, 0);

    const char* profile = getenv("PERF_PROFILE");
    if (profile)
    {
        int map = 0, x = 0, y = 0, profileFrames = 60;
        sscanf(profile, "%d,%d,%d,%d", &map, &x, &y, &profileFrames);
        Warp(map, x, y);
        Run(30, 0);
        Shot("perf_profile.ppm", video, w, h);
        // (pc, lr) pairs, so symbolise.py can also put a leaf function's time (soft float...) on its caller.
        enum { kSlots = 1 << 20 };
        struct Hit { unsigned pc, lr, cycles; };
        struct Hit* hits = calloc(kSlots, sizeof(struct Hit));
        struct ARMCore* cpu = gCore->cpu;
        struct mTiming* timing = &((struct GBA*)gCore->board)->timing;
        const unsigned endFrame = gCore->frameCounter(gCore) + profileFrames;
        for (long steps = 0; gCore->frameCounter(gCore) < endFrame && steps < 100000000; steps++) // (or a hang)
        {
            // (the PC register runs two instructions ahead)
            const unsigned pc = (unsigned)cpu->gprs[ARM_PC] - (cpu->cpsr.t ? 4 : 8);
            // (code addresses only: ARM routines use LR as a scratch register, and the table would fill up)
            unsigned lr = (unsigned)cpu->gprs[ARM_LR];
            lr = (lr >> 24) == 0x08 || (lr >> 24) == 0x09 || (lr >> 24) == 0x03 ? lr : 0;
            const uint64_t before = mTimingGlobalTime(timing);
            gCore->step(gCore);
            const unsigned spent = (unsigned)(mTimingGlobalTime(timing) - before);
            unsigned slot = ((pc * 2654435761u) ^ (lr * 40503u)) & (kSlots - 1);
            while (hits[slot].cycles && (hits[slot].pc != pc || hits[slot].lr != lr))
            {
                slot = (slot + 1) & (kSlots - 1);
            }
            hits[slot].pc = pc;
            hits[slot].lr = lr;
            hits[slot].cycles += spent ? spent : 1;
        }
        FILE* f = fopen("perf_profile.txt", "w");
        for (unsigned i = 0; i < kSlots; i++)
        {
            if (hits[i].cycles)
            {
                fprintf(f, "%x %x %u\n", hits[i].pc, hits[i].lr, hits[i].cycles);
            }
        }
        fclose(f);
        printf("# profile of map %d at %d,%d over %d frames in perf_profile.txt\n", map, x, y, profileFrames);
        gCore->deinit(gCore);
        return 0;
    }

    const int maps = (int)Rd(P_MAPCOUNT);
    unsigned worst = 0;
    int screens = 0;
    int slowScreens = 0;
    unsigned lagTotal = 0;
    const char* from = getenv("PERF_FROM_MAP"); // (to pick a tour up part way)
    const char* to = getenv("PERF_TO_MAP");
    for (int m = from ? atoi(from) : 0; m < (to ? atoi(to) + 1 : maps); m++)
    {
        Warp(m, 80, 80);
        const int gw = (int)Rd(P_CELLSW) / 10;
        const int gh = (int)Rd(P_CELLSH) / 10;
        for (int gy = 0; gy < gh; gy++)
        {
            for (int gx = 0; gx < gw; gx++)
            {
                if (gx || gy)
                {
                    Warp(m, gx * 160 + 80, gy * 160 + 80);
                }
                Wr(P_MAX, 0);
                Wr(P_LAG, 0);
                Run(frames, 0);
                const unsigned max = Rd(P_MAX);
                const unsigned lag = Rd(P_LAG);
                printf("%d %d,%d %u %.1f%% %u %u\n", m, gx, gy, max, 100.0 * max / kFrameCycles, lag,
                       Rd(P_ENTITIES));
                fflush(stdout);
                screens++;
                slowScreens += lag ? 1 : 0;
                lagTotal += lag;
                worst = max > worst ? max : worst;
            }
        }
    }
    printf("# %d screens, %d with dropped frames (%u frames), worst frame %u cycles (%.1f%%)\n", screens, slowScreens,
           lagTotal, worst, 100.0 * worst / kFrameCycles);
    if (argc > 4)
    {
        // The ROM painted the free stack with 'STCK' at boot: the run of it left is what was never used.
        const unsigned bottom = (unsigned)strtoul(argv[4], NULL, 16);
        unsigned free = 0;
        while (gCore->busRead32(gCore, bottom + free) == 0x5354434B)
        {
            free += 4;
        }
        printf("# user stack never used: %u bytes above 0x%08x\n", free, bottom);
        // (and how much is still painted anywhere: a stray write at the bottom isn't the stack running out)
        unsigned painted = 0;
        unsigned lowestUsed = 0;
        for (unsigned addr = bottom; addr < 0x03007F00; addr += 4)
        {
            if (gCore->busRead32(gCore, addr) == 0x5354434B)
            {
                painted += 4;
            }
            else if (!lowestUsed && addr > bottom + free + 64)
            {
                lowestUsed = addr;
            }
        }
        printf("# still painted: %u bytes; first used word past the bottom: 0x%08x\n", painted, lowestUsed);
    }
    gCore->deinit(gCore);
    return 0;
}
