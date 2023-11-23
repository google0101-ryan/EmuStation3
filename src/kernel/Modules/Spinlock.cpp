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

void SpinlockModule::sysSpinlockLock(uint64_t lockPtr, CellPPU *ppu)
{
    printf("[sysPrxForUser]: sysSpinlockLock(0x%08lx)\n", lockPtr);

    if (ppu->GetManager()->Read32(lockPtr))
    {
        printf("TODO: Lock already locked spinlock\n");
        exit(1);
    }

    ppu->GetManager()->Write32(lockPtr, 1);
}

void SpinlockModule::sysSpinlockUnlock(uint64_t lockPtr, CellPPU *ppu)
{
    printf("[sysPrxForUser]: sysSpinlockUnlock(0x%08lx)\n", lockPtr);

    ppu->GetManager()->Write32(lockPtr, 0);
}
