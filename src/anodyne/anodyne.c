
#include <stdbool.h>
#include <stdio.h>

#include "dialog_manager.h"
#include "tonc.h"

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

int main(void)
{
 //   dialog_manager_test();
    
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG0;

	// Init BG 0 for text on screen entries.
	tte_init_se_default(0, BG_CBB(0)|BG_SBB(31));

	tte_write("#{P:72,64}");		// Goto (72, 64).
	tte_write("hello");		// Print "Hello world!"

    // pal_bg_mem
    

    
	while(1);

    return 0;
}
