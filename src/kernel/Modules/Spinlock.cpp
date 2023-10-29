#include "Spinlock.h"

#include <stdio.h>

void SpinlockModule::sysSpinlockInitialize(uint64_t lockPtr, CellPPU* ppu)
{
    printf("[sysPrxForUser]: sysSpinlockInitialize(0x%08lx)\n", lockPtr);

    if (lockPtr)
    {
        ppu->GetManager()->Write32(lockPtr, 0);
    }
}