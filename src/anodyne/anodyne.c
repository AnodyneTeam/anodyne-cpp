
#include <stdbool.h>
#include <stdio.h>

#include "dialog_manager.h"
#include "tonc.h"

//#include "../build/STREET_palette.h"
//#include "../build/STREET_tiles.h"

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

    REG_BG0CNT = BG_CBB(0) | BG_SBB(31) | BG_8BPP | BG_REG_32x32;

//    memcpy16(pal_bg_mem, gTilesetPalette, 256 / 2);
 //   memcpy16(tile_mem, gTilesetData, sizeof(gTilesetData));

    // memcpy16(se_mem[31], gTilemap, mapSize);

    while(1);
}
