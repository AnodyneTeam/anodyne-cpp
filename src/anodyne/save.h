#pragma once

#include <stdint.h>

// GlobalState.Save: the game's progress in battery-backed SRAM, three slots (the title's files 1-3).

// A flat byte stream the state modules write themselves into, field by field.
struct SaveWriter final
{
    uint8_t* mBuf;
    int mCap;
    int mLen;
    bool mOk;
    void Put(const void* data, int size);
};

struct SaveReader final
{
    const uint8_t* mBuf;
    int mLen;
    int mPos;
    bool mOk;
    void Get(void* data, int size);
};

constexpr int kSaveSlots = 3;

// GlobalState.CurrentSaveGame: the file the title started or loaded (0-2).
void Save_SetSlot(int slot);
int Save_Slot();

// GlobalState.SaveGame(slot): writes the progress (GameState, presets, events, dialogue, deaths).
bool Save_Write(int slot);
// Whether the slot holds a save of this ROM's format.
bool Save_Exists(int slot);
// GlobalState.LoadSave: reads the slot back into the state (false, and the state untouched, if it isn't valid).
bool Save_Read(int slot);
