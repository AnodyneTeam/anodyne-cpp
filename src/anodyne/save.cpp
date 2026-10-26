#include "save.h"

#include <string.h>

#include "dialog.h"
#include "game_state.h"
#include "world.h"

// Emulators and flash carts look for this tag to give the cartridge its 32KB of SRAM.
__attribute__((used, aligned(4))) const char kSaveTypeTag[] = "SRAM_V113";

namespace
{
constexpr uint32_t kMagic = 0x444F4E41; // "ANOD"
constexpr uint32_t kVersion = 1;
constexpr int kSlotSize = 0x2800; // 10KB each: 3 fit in the 32KB
constexpr int kHeaderSize = 16;   // magic, version, length, checksum

volatile uint8_t* const kSram = reinterpret_cast<volatile uint8_t*>(0x0E000000);

uint8_t gBuf[kSlotSize] __attribute__((section(".sbss")));
int gSlot = 0;

// SRAM sits on an 8-bit bus: byte accesses only.
void SramWrite(int offset, const uint8_t* data, int size)
{
    for (int i = 0; i < size; i++)
    {
        kSram[offset + i] = data[i];
    }
}

void SramRead(int offset, uint8_t* data, int size)
{
    for (int i = 0; i < size; i++)
    {
        data[i] = kSram[offset + i];
    }
}

uint32_t Checksum(const uint8_t* data, int size)
{
    uint32_t sum = 0x12345678;
    for (int i = 0; i < size; i++)
    {
        sum = (sum << 5) + (sum >> 27) + data[i];
    }
    return sum;
}

uint32_t Word(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void PutWord(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

// Reads a slot's body into gBuf; returns its length, or -1 if the slot isn't a valid save.
int ReadSlot(int slot)
{
    if (slot < 0 || slot >= kSaveSlots)
    {
        return -1;
    }
    uint8_t header[kHeaderSize];
    SramRead(slot * kSlotSize, header, kHeaderSize);
    const int len = static_cast<int>(Word(header + 8));
    if (Word(header) != kMagic || Word(header + 4) != kVersion || len <= 0 || len > kSlotSize - kHeaderSize)
    {
        return -1;
    }
    SramRead(slot * kSlotSize + kHeaderSize, gBuf, len);
    return Checksum(gBuf, len) == Word(header + 12) ? len : -1;
}
} // namespace

void Save_SetSlot(int slot)
{
    gSlot = slot;
}

int Save_Slot()
{
    return gSlot;
}

void SaveWriter::Put(const void* data, int size)
{
    if (!mOk || mLen + size > mCap)
    {
        mOk = false;
        return;
    }
    memcpy(mBuf + mLen, data, static_cast<size_t>(size));
    mLen += size;
}

void SaveReader::Get(void* data, int size)
{
    if (!mOk || mPos + size > mLen)
    {
        mOk = false;
        memset(data, 0, static_cast<size_t>(size));
        return;
    }
    memcpy(data, mBuf + mPos, static_cast<size_t>(size));
    mPos += size;
}

bool Save_Write(int slot)
{
    if (slot < 0 || slot >= kSaveSlots)
    {
        return false;
    }
    SaveWriter w = { gBuf, kSlotSize - kHeaderSize, 0, true };
    GameState_Save(w);
    Dialog_SaveState(w);
    const int deaths = World_Deaths();
    w.Put(&deaths, sizeof(deaths));
    if (!w.mOk)
    {
        return false;
    }
    uint8_t header[kHeaderSize];
    PutWord(header, kMagic);
    PutWord(header + 4, kVersion);
    PutWord(header + 8, static_cast<uint32_t>(w.mLen));
    PutWord(header + 12, Checksum(gBuf, w.mLen));
    SramWrite(slot * kSlotSize + kHeaderSize, gBuf, w.mLen);
    SramWrite(slot * kSlotSize, header, kHeaderSize); // (the header last: a save cut short stays invalid)
    return true;
}

bool Save_Exists(int slot)
{
    return ReadSlot(slot) >= 0;
}

bool Save_Read(int slot)
{
    const int len = ReadSlot(slot);
    if (len < 0)
    {
        return false;
    }
    SaveReader r = { gBuf, len, 0, true };
    GameState_Load(r);
    Dialog_LoadState(r);
    int deaths = 0;
    r.Get(&deaths, sizeof(deaths));
    World_SetDeaths(deaths);
    return r.mOk;
}
