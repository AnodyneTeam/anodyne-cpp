#pragma once

#include <tonc.h>

class ObjVramAllocator final {
public:
    static constexpr int BlockSize  = 32;          // bytes per tile (4bpp)
    static constexpr int TotalBytes = 32 * 1024;   // OBJ VRAM size
    static constexpr int BlockCount = TotalBytes / BlockSize;

    enum BlockState : u8 {
        UNUSED   = 0,
        USED     = 1,
        CONTINUE = 2
    };

    ObjVramAllocator() {
        for (int i = 0; i < BlockCount; i++)
            map[i] = UNUSED;
    }

    int alloc(int sizeBytes) {
        int blocksNeeded = sizeBytes / BlockSize;
        if (blocksNeeded <= 0 || sizeBytes % BlockSize != 0)
            return -1;

        for (int i = 0; i + blocksNeeded <= BlockCount; ) {

            if (i % blocksNeeded != 0) {
                i += (blocksNeeded - (i % blocksNeeded));
                continue;
            }

            int j;
            for (j = 0; j < blocksNeeded; j++) {
                if (map[i + j] != UNUSED)
                    break;
            }

            if (j == blocksNeeded) {
                map[i] = USED;
                for (j = 1; j < blocksNeeded; j++)
                    map[i + j] = CONTINUE;
                return i;      // block ID == tile index
            }

            i += blocksNeeded;
        }

        return -1;
    }

    void free(int blockId) {
        if (blockId < 0 || blockId >= BlockCount)
            return;
        if (map[blockId] != USED)
            return;

        map[blockId] = UNUSED;

        int i = blockId + 1;
        while (i < BlockCount && map[i] == CONTINUE) {
            map[i] = UNUSED;
            i++;
        }
    }

    // blockId -> OBJ VRAM pointer (tonc tile_mem[4] = OBJ tiles)
    TILE* ptr(int blockId) const {
        if (blockId < 0 || blockId >= BlockCount)
            return nullptr;
        return &tile_mem[4][blockId];        // TILE is already one 32-byte 4bpp tile
    }

private:
    u8 map[BlockCount];
};
