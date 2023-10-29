#include "Syscall.h"
#include "types.h"

#include <stdio.h>

Result sysMMapperAllocateMemory(size_t size, uint64_t flags, size_t alignment, uint64_t ptrAddr, CellPPU* ppu)
{
    printf("sysMMapperAllocateMemory(0x%08lx, 0x%08lx, 0x%08lx, 0x%08lx)\n", size, flags, alignment, ptrAddr);

    if (size == 0)
    {
        return CELL_EALIGN;
    }

    switch (flags & 0xf00)
    {
    case 0:
    case 0x400:
    {
        if (size % 0x100000)
            return CELL_EALIGN;
        break;
    }
    case 0x200:
    {
        if (size % 0x10000)
            return CELL_EALIGN;
        break;
    }
    default:
        return CELL_EINVAL;
    }

    uint64_t addr = ppu->GetManager()->main_mem->Alloc(size);

    ppu->GetManager()->Write64(addr, ptrAddr);
    return CELL_OK;
}


#define ARG0 ppu->GetReg(3)
#define ARG1 ppu->GetReg(4)
#define ARG2 ppu->GetReg(5)
#define ARG3 ppu->GetReg(6)
#define RETURN(x) ppu->SetReg(3, x)

void Syscalls::DoSyscall(CellPPU *ppu)
{
    switch (ppu->GetReg(11))
    {
    case 0x14A:
        RETURN(sysMMapperAllocateMemory(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    default:
        printf("[LV2]: unknown syscall 0x%04x\n", ppu->GetReg(11));
        exit(1);
    }
}