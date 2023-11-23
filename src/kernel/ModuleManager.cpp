#include "ModuleManager.h"
#include "../cpu/PPU.h"

// Modules
#include "Modules/Spinlock.h"
#include "Modules/Mutex.h"
#include "Modules/CellGcm.h"
#include "Modules/CellThread.h"
#include "Modules/VFS.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdexcept>

#define ARG0 ppu->GetReg(3)
#define ARG1 ppu->GetReg(4)
#define ARG2 ppu->GetReg(5)
#define ARG3 ppu->GetReg(6)
#define ARG4 ppu->GetReg(7)
#define ARG5 ppu->GetReg(8)
#define ARG6 ppu->GetReg(9)
#define ARG7 ppu->GetReg(10)
#define RETURN(x) ppu->SetReg(3, x)

uint64_t GetSystemTime()
{
    printf("sysGetSystemTime()\n");
    while (true)
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);

        const uint64_t result = static_cast<uint64_t>(ts.tv_sec) * 1000000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000u;

        if (result) return result;
    }

    return 0;
}

void Modules::DoHLECall(uint32_t nid, CellPPU* ppu)
{
    // TODO: This is terrible and will get massive
    // TODO: Change this to a lookup table
    switch (nid)
    {
    case 0x0bae8772:
        RETURN(CellGcm::cellVideoOutConfigure(ARG0, ARG1));
        break;
    case 0x1573dc3f:
        RETURN(MutexModule::sysLwMutexLock(ARG0, ARG1, ppu));
        break;
    case 0x15bae46b:
        RETURN(CellGcm::cellGcmInitBody(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    case 0x1bc200f4:
        RETURN(MutexModule::sysLwMutexUnlock(ARG0, ppu));
        break;
    case 0x21397818:
        RETURN(CellGcm::cellGcmSetFlipCommand(ARG0, ARG1, ppu));
        break;
    case 0x21ac3697:
        RETURN(CellGcm::cellGcmAddressToOffset(ARG0, ARG1, ppu));
        break;
    case 0x2c847572:
        printf("sysProcessAtExitSpawn(0x%08lx)\n", ARG0);
        RETURN(CELL_OK);
        break;
    case 0x2CB51F0D:
        RETURN(VFS::cellFsClose(ARG0));
        break;
    case 0x2f85c0ef:
        RETURN(MutexModule::sysLwMutexCreate(ARG0, ARG1, ppu));
        break;
    case 0x350d454e:
        RETURN(CellThread::sysGetThreadId(ARG0, ppu));
        break;
    case 0x4ae8d215:
        RETURN(CellGcm::cellGcmSetFlipMode(ARG0));
        break;
    case 0x5267cb35:
        SpinlockModule::sysSpinlockUnlock(ARG0, ppu);
        RETURN(CELL_OK);
        break;
    case 0x718bf5f8:
        RETURN(VFS::cellFsOpen(ARG0, ARG1, ARG2, ppu));
        break;
    case 0x72a577ce:
        RETURN(CellGcm::cellGcmGetFlipStatus());
        break;
    case 0x744680a2:
        printf("TODO: sysThreadInitializeTLS(0x%08lx, 0x%08lx, 0x%08lx, 0x%08lx)\n", ARG0, ARG1, ARG2, ARG3);
        RETURN(CELL_OK);
        break;
    case 0x8461e528:
        RETURN(GetSystemTime());
        break;
    case 0x887572d5:
        RETURN(CellGcm::cellVideOutGetState(ARG0, ARG1, ARG2, ppu));
        break;
    case 0x8c2bb498:
        SpinlockModule::sysSpinlockInitialize(ARG0, ppu);
        RETURN(CELL_OK);
        break;
    case 0x96328741:
        printf("sysProcessAt_ExitSpawn(0x%08lx)\n", ARG0);
        RETURN(CELL_OK);
        break;
    case 0x9d98afa0:
        printf("cellSysutilRegisterCallback(%ld, 0x%08lx, 0x%08lx)\n", ARG0, ARG1, ARG2);
        RETURN(CELL_OK);
        break;
    case 0xa285139d:
        SpinlockModule::sysSpinlockLock(ARG0, ppu);
        RETURN(CELL_OK);
        break;
    case 0xa397d042:
        RETURN(VFS::cellFsSeek(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    case 0xa53d12ae:
        RETURN(CellGcm::cellGcmSetDisplayBuffer(ARG0, ARG1, ARG2, ARG3, ARG4, ppu));
        break;
    case 0xa547adde:
        RETURN(CellGcm::cellGcmGetControlRegister(ppu));
        break;
    case 0xb2e761d4:
        CellGcm::cellGcmResetFlipStatus();
        break;
    case 0xe315a0b2:
        RETURN(CellGcm::cellGcmGetConfiguration(ARG0, ppu));
        break;
    case 0xe558748d:
        RETURN(CellGcm::cellGetResolution(ARG0, ARG1, ppu));
        break;
    case 0xecdcf2ab:
        RETURN(VFS::cellFsWrite(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    default:
        printf("Called unknown function with nid 0x%08x\n", nid);
        throw std::runtime_error("Unknown function NID");
    }
}