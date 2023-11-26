#include "CellThread.h"

uint32_t CellThread::sysGetThreadId(uint32_t ptr, CellPPU *ppu)
{
    ppu->GetManager()->Write64(ptr, 0x0000000000000024ULL);
    // printf("sysThreadGetId(0x%08lx)\n", ptr);
    return CELL_OK;
}

uint32_t CellThread::sysPPUGetThreadStackInformation(uint32_t ptr, CellPPU *ppu)
{
    ppu->GetManager()->Write32(ptr, ppu->GetStackAddr());
    ppu->GetManager()->Write32(ptr+4, 0x10000);

    printf("sysPPUGetThreadStackInformation(0x%08x)\n", ptr);

    return CELL_OK;
}
