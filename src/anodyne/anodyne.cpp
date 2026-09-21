
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "dialog_manager.h"
#include "tonc.h"

#include "obj_vram_allocator.hpp"

struct MapPalData final
{
    uint32_t mPalSize = 0;
    const uint16_t* mPalData = nullptr;
};

struct MapTileSetData final
{
    uint32_t mTileSetSize = 0;
    const uint8_t* mTileSetData = nullptr;
};

struct MapScreenBlockData final
{
    uint32_t mMapWidth = 0;
    uint32_t mMapHeight = 0;
    const uint16_t* mMapData = nullptr;
};

struct MapEntry final
{
    MapPalData mPalData;
    MapTileSetData mTileSetData;
    MapScreenBlockData mScreenBlockData;
};

#include "../build/tools/map_registry.h"

// kMapCount is provided from map_registry_.h
static uint32_t gCurrentMap = 0;

/*
static void dialog_manager_test(void)
{
    struct dialog_manager dm;
    struct binary_blob data;
    dialog_manager_init(&dm, &data);

    dialog_manager_random(&dm, npc_id_empty1, scene_id_empty3);

    dialog_manager_free(&dm);
}
*/

void CopyMapRegionToBG0(int mapX, int mapY)
{
    const int MAP_W = kMaps[gCurrentMap].mScreenBlockData.mMapWidth;
    const int MAP_H = kMaps[gCurrentMap].mScreenBlockData.mMapHeight;

    const int VIEW_W = 32;
    const int VIEW_H = 32;

    for (int y = 0; y < VIEW_H; y++)
    {
        int srcY = mapY + y;
        if (srcY < 0) srcY = 0;
        if (srcY >= MAP_H) srcY = MAP_H - 1;

        for (int x = 0; x < VIEW_W; x++)
        {
            int srcX = mapX + x;
            if (srcX < 0) srcX = 0;
            if (srcX >= MAP_W) srcX = MAP_W - 1;

            // ROW-MAJOR INDEXING — THIS IS THE IMPORTANT PART
            uint16_t tile = kMaps[gCurrentMap].mScreenBlockData.mMapData[srcY * MAP_W + srcX];

            se_mem[31][y * VIEW_W + x] = tile;
        }
    }
}

static void SetCurrentMap(uint32_t mapIndex)
{
    gCurrentMap = mapIndex;

    // --- Palette (256 halfwords) ---
    memcpy16(pal_bg_mem, kMaps[gCurrentMap].mPalData.mPalData, kMaps[gCurrentMap].mPalData.mPalSize);

    // --- Tiles (8bpp, byte data -> halfword count) ---
    memcpy16(tile_mem, kMaps[gCurrentMap].mTileSetData.mTileSetData, kMaps[gCurrentMap].mScreenBlockData.mMapWidth * kMaps[gCurrentMap].mScreenBlockData.mMapHeight);

    CopyMapRegionToBG0(0, 0);
    // --- Map (uint16_t entries) ---
   // memcpy16(se_mem[31], gMapData, sizeof(gMapData) / 2);

   /// for (int i = 0; i < 1024; i++)
    //    se_mem[31][i] = 5;

}

int main()
{
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG0 | DCNT_OBJ;
    REG_BG1CNT = 1;
    REG_BG2CNT = 0;
    REG_BG3CNT = 0;

    REG_BG0CNT = BG_CBB(0) | BG_SBB(31) | BG_8BPP | BG_REG_32x32;


    SetCurrentMap(gCurrentMap);

    int camX = 8 * 10;
    int camY = 8 * 10;

    int lastTileX = -1;
    int lastTileY = -1;

    ObjVramAllocator objVram;

    int id = objVram.alloc(512);

    TILE* pYoung = objVram.ptr(id);

    //objVram.free(id);


    while (1)
    {
        key_poll();

        // ------------------------------------------------------------
        // MAP CYCLING
        // ------------------------------------------------------------
        if (key_hit(KEY_START))
        {
            // Next map
            gCurrentMap = (gCurrentMap + 1) % kMapCount;
            SetCurrentMap(gCurrentMap);

            // Reset camera + tile window
            camX = 8 * 10;
            camY = 8 * 10;
            lastTileX = -1;
            lastTileY = -1;
        }

        if (key_hit(KEY_SELECT))
        {
            // Previous map
            if (gCurrentMap == 0)
                gCurrentMap = kMapCount - 1;
            else
                gCurrentMap--;

            SetCurrentMap(gCurrentMap);

            // Reset camera + tile window
            camX = 8 * 10;
            camY = 8 * 10;
            lastTileX = -1;
            lastTileY = -1;
        }

        // ------------------------------------------------------------
        // SCROLLING (slow)
        // ------------------------------------------------------------
        static int scrollTimer = 0;
        scrollTimer++;

        if (scrollTimer >= 120)
        {
            scrollTimer = 0;

            if (key_is_down(KEY_LEFT))  camX--;
            if (key_is_down(KEY_RIGHT)) camX++;
            if (key_is_down(KEY_UP))    camY--;
            if (key_is_down(KEY_DOWN))  camY++;
        }

        // ------------------------------------------------------------
        // CAMERA CLAMPING
        // ------------------------------------------------------------
        int mapWpx = kMaps[gCurrentMap].mScreenBlockData.mMapWidth  * 8;
        int mapHpx = kMaps[gCurrentMap].mScreenBlockData.mMapHeight * 8;

        if (camX < 0) camX = 0;
        if (camY < 0) camY = 0;
        if (camX > mapWpx - 240) camX = mapWpx - 240;
        if (camY > mapHpx - 160) camY = mapHpx - 160;

        // ------------------------------------------------------------
        // TILE + SUBPIXEL SCROLLING
        // ------------------------------------------------------------
        int tileX = camX >> 3;
        int tileY = camY >> 3;

        int subX = camX & 7;
        int subY = camY & 7;

        REG_BG0HOFS = subX;
        REG_BG0VOFS = subY;

        // ------------------------------------------------------------
        // TILE WINDOW UPDATE
        // ------------------------------------------------------------
        if (tileX != lastTileX || tileY != lastTileY)
        {
            CopyMapRegionToBG0(tileX, tileY);
            lastTileX = tileX;
            lastTileY = tileY;
        }
    }
}
