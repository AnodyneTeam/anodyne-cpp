
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
    memcpy16(se_mem[31], gMapData, sizeof(gMapData) / 2);
    #endif

    while(1);
}
