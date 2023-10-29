#include "ModuleManager.h"
#include "../cpu/PPU.h"

// Modules
#include "Modules/Spinlock.h"

#include <stdio.h>
#include <stdlib.h>

#define ARG0 ppu->GetReg(3)
#define RETURN(x) ppu->SetReg(3, x)

void Modules::DoHLECall(uint32_t nid, CellPPU* ppu)
{
    // TODO: This is terrible and will get massive
    // TODO: Change this to a lookup table
    switch (nid)
    {
    case 0x8c2bb498:
        SpinlockModule::sysSpinlockInitialize(ARG0, ppu);
        RETURN(0);
        break;
    default:
        printf("Called unknown function with nid 0x%08x\n", nid);
        exit(1);
    }
}