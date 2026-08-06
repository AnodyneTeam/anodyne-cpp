
#include <stdbool.h>
#include <stdio.h>

#include "dialog_manager.h"
#include "tonc.h"

//#define HAVE_DATA

#ifdef HAVE_DATA
#include "../build/STREET_palette.h"
#include "../build/STREET_tiles.h"
#include "../build/STREET_map.h"
#endif

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
    const int MAP_W = 100;
    const int MAP_H = 140;

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
            uint16_t tile = gMapData[srcY * MAP_W + srcX];

            se_mem[31][y * VIEW_W + x] = tile;
        }
    }
}

int main()
{
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG0;
    REG_BG1CNT = 1;
    REG_BG2CNT = 0;
    REG_BG3CNT = 0;

    REG_BG0CNT = BG_CBB(0) | BG_SBB(31) | BG_8BPP | BG_REG_32x32;

    #ifdef HAVE_DATA
    // --- Palette (256 halfwords) ---
    memcpy16(pal_bg_mem, gTilesetPalette, 256);

    // --- Tiles (8bpp, byte data → halfword count) ---
    memcpy16(tile_mem, gTilesetData, sizeof(gTilesetData) / 2);

    // --- Map (uint16_t entries) ---
   // memcpy16(se_mem[31], gMapData, sizeof(gMapData) / 2);

   /// for (int i = 0; i < 1024; i++)
    //    se_mem[31][i] = 5;

    CopyMapRegionToBG0(20, 40);


    #endif

    int camX = 8*10;
    int camY = 8*10;

    int lastTileX = -1;
    int lastTileY = -1;

    while (1)
    {
        key_poll();
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

        // clamp in pixels
        int mapWpx = 100 * 8;
        int mapHpx = 140 * 8;

        if (camX < 0) camX = 0;
        if (camY < 0) camY = 0;
        if (camX > mapWpx - 240) camX = mapWpx - 240;
        if (camY > mapHpx - 160) camY = mapHpx - 160;

        // compute tile + subpixel offsets
        int tileX = camX >> 3;
        int tileY = camY >> 3;

        int subX = camX & 7;
        int subY = camY & 7;

        // scroll inside the tile
        REG_BG0HOFS = subX;
        REG_BG0VOFS = subY;

        // update tile window only when needed
        if (tileX != lastTileX || tileY != lastTileY)
        {
            CopyMapRegionToBG0(tileX, tileY);
            lastTileX = tileX;
            lastTileY = tileY;
        }
    }


}

