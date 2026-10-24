#include "sim_internal.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define SIM_POISON(addr, size) ASAN_POISON_MEMORY_REGION(addr, size)
#else
#define SIM_POISON(addr, size) ((void)(addr), (void)(size))
#endif

namespace sim
{
namespace
{
struct Region final
{
    uint32_t mBase;
    uint32_t mSize;
};

// Every region the game may touch. EWRAM/IWRAM come from gba_cart.ld's MEMORY block, the rest are fixed by the
// hardware. The IO page is mapped separately (sim_io.cpp needs two views of it).
const Region kRegions[] = {
    { SIM_EWRAM_ORIGIN, SIM_EWRAM_LENGTH },
    { kPalBase, kPalSize },
    { kVramBase, kVramSize },
    { kOamBase, kOamSize },
};

uint8_t* gIoAlias = nullptr; // the simulator's writable view of the IO page
uint8_t* gIoShadow = nullptr;

size_t PageRound(size_t size)
{
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return (size + page - 1) & ~(page - 1);
}

void MapFixed(const Region& r)
{
    const size_t mapped = PageRound(r.mSize);
    void* want = reinterpret_cast<void*>(static_cast<uintptr_t>(r.mBase));
    void* got = mmap(want, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (got != want)
    {
        Fatal("could not map GBA memory at 0x%08X (%u bytes): is the binary linked high enough (-Ttext-segment)?",
              r.mBase, r.mSize);
    }
    // The hardware region ends here: anything past it is an out-of-bounds access (no mirrors are simulated).
    if (mapped > r.mSize)
    {
        SIM_POISON(static_cast<uint8_t*>(got) + r.mSize, mapped - r.mSize);
    }
}

// IWRAM is a memfd so its last page can also appear at the top of the 0x03xxxxxx mirror range: the BIOS
// interrupt flags/vector are addressed as 0x03FFFFF8/0x03FFFFFC (REG_BASE - 8, tonc's REG_IFBIOS), which the
// hardware mirrors to 0x03007FF8/0x03007FFC. No other mirror is mapped.
void MapIwram()
{
    const size_t page = PageRound(1);
    const size_t size = PageRound(SIM_IWRAM_LENGTH);
    const int fd = memfd_create("gba_iwram", 0);
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(size)) != 0)
    {
        Fatal("memfd_create for IWRAM failed");
    }
    void* want = reinterpret_cast<void*>(static_cast<uintptr_t>(SIM_IWRAM_ORIGIN));
    void* got = mmap(want, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
    void* mirrorWant = reinterpret_cast<void*>(static_cast<uintptr_t>(0x04000000u - page));
    void* mirror = mmap(mirrorWant, page, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, fd,
                        static_cast<off_t>(size - page));
    close(fd);
    if (got != want || mirror != mirrorWant)
    {
        Fatal("could not map IWRAM at 0x%08X", SIM_IWRAM_ORIGIN);
    }
}
} // namespace

void Memory_Init()
{
    for (const Region& r : kRegions)
    {
        MapFixed(r);
    }
    MapIwram();

    // IO: one memfd page mapped twice - read-only for the game at 0x04000000 (writes trap, see sim_io.cpp) and
    // writable for the simulator.
    const size_t page = PageRound(1);
    const int fd = memfd_create("gba_io", 0);
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(page)) != 0)
    {
        Fatal("memfd_create for the IO page failed");
    }
    void* want = reinterpret_cast<void*>(static_cast<uintptr_t>(kIoBase));
    void* game = mmap(want, page, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
    gIoAlias = static_cast<uint8_t*>(mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    close(fd);
    if (game != want || gIoAlias == MAP_FAILED)
    {
        Fatal("could not map the IO page at 0x%08X", kIoBase);
    }
    gIoShadow = new uint8_t[kIoSize]();
    // 0x000-0x3FF are the registers; 0x800 (memory control) shares the page and stays valid.
    SIM_POISON(static_cast<uint8_t*>(game) + kIoSize, 0x800 - kIoSize);
    SIM_POISON(static_cast<uint8_t*>(game) + 0x804, page - 0x804);

    // Power-on values the game may rely on.
    IoSet16(0x130, 0x03FF); // KEYINPUT: nothing pressed (active low)
    IoSet16(0x088, 0x0200); // SOUNDBIAS
    IoSet16(0x020, 0x0100); // BG2PA / BG2PD / BG3PA / BG3PD: identity
    IoSet16(0x026, 0x0100);
    IoSet16(0x030, 0x0100);
    IoSet16(0x036, 0x0100);
}

uint8_t* Io()
{
    return gIoAlias;
}

uint8_t* IoShadow()
{
    return gIoShadow;
}

void IoSet16(uint32_t ofs, uint16_t value)
{
    memcpy(gIoAlias + ofs, &value, 2);
    if (ofs + 2 <= kIoSize)
    {
        memcpy(gIoShadow + ofs, &value, 2);
    }
}

uint8_t* Vram()
{
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(kVramBase));
}

uint16_t* Pal()
{
    return reinterpret_cast<uint16_t*>(static_cast<uintptr_t>(kPalBase));
}

uint16_t* Oam()
{
    return reinterpret_cast<uint16_t*>(static_cast<uintptr_t>(kOamBase));
}

bool IsMapped(uint32_t addr, uint32_t size)
{
    for (const Region& r : kRegions)
    {
        if (addr >= r.mBase && addr + size <= r.mBase + r.mSize)
        {
            return true;
        }
    }
    if (addr >= SIM_IWRAM_ORIGIN && addr + size <= SIM_IWRAM_ORIGIN + SIM_IWRAM_LENGTH)
    {
        return true;
    }
    return addr >= kIoBase && addr + size <= kIoBase + kIoSize;
}
} // namespace sim
