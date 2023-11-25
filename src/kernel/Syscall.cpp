#include "Syscall.h"
#include "types.h"

#include <stdio.h>

uint32_t sysMMapperAllocateMemory(size_t size, uint64_t flags, size_t alignment, uint64_t ptrAddr, CellPPU* ppu)
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

uint32_t sysGetUserMemorySize(uint64_t mem_info_addr, CellPPU* ppu)
{
    ppu->GetManager()->Write32(mem_info_addr, ppu->GetManager()->main_mem->GetSize());
    ppu->GetManager()->Write32(mem_info_addr, ppu->GetManager()->main_mem->GetAvailable());

    printf("sysGetUserMemorySize(0x%08lx)\n", mem_info_addr);
    return CELL_OK;
}

uint32_t sysMMapperAllocate(uint32_t size, uint32_t flags, uint32_t alloc_addr, CellPPU* ppu)
{
    printf("sysMMapperAllocate(0x%08x, 0x%08x, 0x%08x)\n", size, flags, alloc_addr);

    uint32_t addr;
    switch (flags)
    {
    case 0x400:
        if (size & 0xfffff) return CELL_EALIGN;
        addr = ppu->GetManager()->main_mem->Alloc(size);
        break;
    case 0x200:
        if (size & 0xffff) return CELL_EALIGN;
        addr = ppu->GetManager()->main_mem->Alloc(size);
        break;
    default:
        return CELL_EINVAL;
    }

    if (!addr) return CELL_ENOMEM;

    printf("Memory allocated [addr 0x%lx, size 0x%x]\n", addr, size);
    ppu->GetManager()->Write32(alloc_addr, addr);

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
    case 0x08D:
        RETURN(CELL_OK);
        break;
    case 0x14A:
        RETURN(sysMMapperAllocateMemory(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    case 0x15C:
        RETURN(sysMMapperAllocate(ARG0, ARG1, ARG2, ppu));
        break;
    case 0x160:
        RETURN(sysGetUserMemorySize(ARG0, ppu));
        break;
    case 0x193:
        printf("\n%s\n", (char*)ppu->GetManager()->GetRawPtr(ARG1));
        RETURN(CELL_OK);
        break;
    case 0x3DC:
        break;
    default:
        printf("[LV2]: unknown syscall 0x%04lx\n", ppu->GetReg(11));
        exit(1);
    }
}