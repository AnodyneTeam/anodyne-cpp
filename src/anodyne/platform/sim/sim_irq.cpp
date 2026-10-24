// Interrupts: tonc's irq_* switchboard (compiled from libtonc's own tonc_irq.c) with its master ISR address kept
// in the simulator instead of at 0x03007FFC (a host function pointer is 8 bytes, the BIOS slot is 4), and a C
// version of tonc's isr_master.

#include "sim_internal.h"

extern "C"
{
#include "tonc_memmap.h"
#include "tonc_types.h"
#include "tonc_irq.h"

extern fnptr gSimIsrMain;
fnptr gSimIsrMain = nullptr;

// tonc_isr_master.s: acknowledge what is pending in IE & IF (IF and the BIOS flags at 0x03007FF8) and call the
// first handler in __isr_table that wants one of them.
void isr_master(void)
{
    const uint16_t pending = static_cast<uint16_t>(sim::Io16(0x200) & sim::Io16(0x202));
    sim::IoSet16(0x202, static_cast<uint16_t>(sim::Io16(0x202) & ~pending));
    REG_IFBIOS |= pending;
    for (IRQ_REC* rec = __isr_table; rec->flag; rec++)
    {
        if (rec->flag & pending)
        {
            if (rec->isr)
            {
                rec->isr();
            }
            break;
        }
    }
}

// The nesting master needs CPU mode switches; on the host it behaves like the plain one.
void isr_master_nest(void)
{
    isr_master();
}
}

namespace sim
{
namespace
{
bool gInIrq = false;
}

void Irq_Raise(uint16_t flag)
{
    IoSet16(0x202, static_cast<uint16_t>(Io16(0x202) | flag));
}

void Irq_Service()
{
    if (gInIrq)
    {
        return; // the CPU masks IRQs while one is being handled (no nesting)
    }
    while ((Io16(0x208) & 1) && (Io16(0x200) & Io16(0x202)))
    {
        if (!gSimIsrMain)
        {
            // No master ISR: the BIOS would jump to a null pointer. Acknowledge so the loop ends.
            Log("interrupt 0x%04X pending with no master ISR installed", Io16(0x200) & Io16(0x202));
            IoSet16(0x202, static_cast<uint16_t>(Io16(0x202) & ~Io16(0x200)));
            return;
        }
        gInIrq = true;
        gSimIsrMain();
        gInIrq = false;
    }
}
} // namespace sim
