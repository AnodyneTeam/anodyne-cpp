// libtonc's tonc_irq.c, unchanged, except that the master ISR pointer lives in the simulator (gSimIsrMain)
// rather than in the 4-byte BIOS slot at 0x03007FFC, which an 8-byte host function pointer would overrun.
#include "tonc_memmap.h"
#include "tonc_types.h"

#undef REG_ISR_MAIN
extern fnptr gSimIsrMain;
#define REG_ISR_MAIN gSimIsrMain

#include "../../../3rdParty/libtonc/src/tonc_irq.c"
