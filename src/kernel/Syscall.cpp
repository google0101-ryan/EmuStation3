#include "Syscall.h"
#include "Modules/CellThread.h"
#include "Modules/CellGcm.h"
#include "Modules/VFS.h"
#include "types.h"

#include <stdio.h>

uint32_t sysProcessGetParamSFO(uint32_t bufPtr)
{
	printf("sysProcessGetParamSFO(0x%08x)\n", bufPtr);
	return CELL_ENOENT;
}

uint32_t sysMMapperAllocateAddress(size_t size, uint64_t flags, size_t alignment, uint64_t ptrAddr, CellPPU* ppu)
{
    printf("sysMMapperAllocateAddress(0x%08lx, 0x%08lx, 0x%08lx, 0x%08lx)\n", size, flags, alignment, ptrAddr);

    if (size == 0)
    {
        return CELL_EALIGN;
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

uint32_t sysMMapperSearchAndMapMemory(uint32_t start_addr, uint32_t mem_id, uint64_t flags, uint32_t allocAddr, CellPPU* ppu)
{
    printf("sysMMapperSearchAndMapMemory(0x%08x, 0x%08x, 0x%08lx, 0x%08x)\n", start_addr, mem_id, flags, allocAddr);
    ppu->GetManager()->Write32(allocAddr, mapInfo[mem_id].start);
    return CELL_OK;
}

#define ARG0 ppu->GetReg(3)
#define ARG1 ppu->GetReg(4)
#define ARG2 ppu->GetReg(5)
#define ARG3 ppu->GetReg(6)
#define ARG4 ppu->GetReg(7)
#define RETURN(x) ppu->SetReg(3, x)

uint32_t kernel_id = 1;

void Syscalls::DoSyscall(CellPPU *ppu)
{
    switch (ppu->GetReg(11))
    {
	case 0x01E:
		RETURN(sysProcessGetParamSFO(ARG0));
		break;
    case 0x031:
        RETURN(CellThread::sysPPUGetThreadStackInformation(ARG0, ppu));
        break;
    case 0x05F:
        printf("_sys_lwmutex_create(0x%08lx, %ld, 0x%08lx, %ld, 0x%08lx)\n", ARG0, ARG1, ARG2, ARG3, ARG4);
        ppu->GetManager()->Write32(ARG0, kernel_id++);
        RETURN(CELL_OK);
        break;
    case 0x064:
        printf("sys_mutex_create(0x%08x, 0x%08x)\n", ARG0, ARG1);
        ppu->GetManager()->Write32(ARG0, kernel_id++);
        RETURN(CELL_OK);
        break;
    case 0x066:
        // printf("sys_mutex_lock(%d)\n", ARG0);
        RETURN(CELL_OK);
        break;
    case 0x068:
        // printf("sys_mutex_unlock(%d)\n", ARG0);
        RETURN(CELL_OK);
        break;
    case 0x069:
        printf("sys_cond_create(0x%08lx, %ld, 0x%08lx)\n", ARG0, ARG1, ARG2);
        ppu->GetManager()->Write32(ARG0, kernel_id++);
        RETURN(CELL_OK);
        break;
    case 0x078:
        printf("_sys_rwlock_create(0x%08lx, 0x%08lx)\n", ARG0, ARG1);
        ppu->GetManager()->Write32(ARG0, kernel_id++);
        RETURN(CELL_OK);
        break;
    case 0x080:
        printf("sys_event_queue_create(0x%08lx, 0x%08lx, 0x%08x, %d)\n", ARG0, ARG1, ARG2, (int32_t)ARG3);
        ppu->GetManager()->Write32(ARG0, kernel_id++);
        RETURN(CELL_OK);
        break;
    case 0x086:
        printf("sys_event_port_create(0x%08lx, %d, 0x%08lx)\n", ARG0, (int32_t)ARG1, ARG2);
        ppu->GetManager()->Write32(ARG0, kernel_id++);
        RETURN(CELL_OK);
        break;
    case 0x08C:
        printf("sys_event_port_connect_ipc(%d, %d)\n", (int32_t)ARG0, (int32_t)ARG1);
        RETURN(CELL_OK);
        break;
    case 0x08D:
        // printf("sleep_timer_usleep(%d)\n", ARG0);
        RETURN(CELL_OK);
        break;
	case 0x093:
		printf("sys_time_get_timebase_frequency()\n");
		RETURN(80000000ull);
		break;
	case 0x0A9:
		printf("sys_util_initialize(%ld, %ld)\n", ARG0, ARG1);
		RETURN(CELL_OK);
		break;
    case 0x14A:
        RETURN(sysMMapperAllocateAddress(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    case 0x151:
        RETURN(sysMMapperSearchAndMapMemory(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    case 0x15C:
        RETURN(sysMMapperAllocate(ARG0, ARG1, ARG2, ppu));
        break;
    case 0x160:
        RETURN(sysGetUserMemorySize(ARG0, ppu));
        break;
    case 0x193:
    {
        char* buf = (char*)ppu->GetManager()->GetRawPtr(ARG1);
        for (int i = 0; buf[i]; i++)
        {
            if (buf[i] == 0x0A)
            {
                buf[i] = 0;
                break;
            }
        }
        printf("%s\n", buf);
        RETURN(CELL_OK);
        break;
    }
    case 0x321:
        RETURN(VFS::cellFsOpen(ARG0, ARG1, ARG2, ppu));
        break;
	case 0x323:
		RETURN(VFS::cellFsWrite(ARG0, ARG1, ARG2, ARG3, ppu));
		break;
    case 0x329:
        RETURN(VFS::cellFsFstat(ARG0, ARG1, ppu));
        break;
    case 0x3DC:
        break;
    case 0x3FF:
        CellGcm::cellGcmCallback(ppu);
        RETURN(CELL_OK);
        break;
    default:
        printf("[LV2]: unknown syscall 0x%04lx\n", ppu->GetReg(11));
        exit(1);
    }
}