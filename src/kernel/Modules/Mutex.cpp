#include "Mutex.h"

#include <stdio.h>

typedef struct sys_lwmutex
{
    uint64_t lock_var;
    uint32_t attribute;
    uint32_t recursive_count;
    uint32_t sleep_queue;
    uint32_t _pad;
} sys_lwmutex_t;

struct sys_lwmutex_attr
{
    uint32_t attr_protocol;
    uint32_t attr_recursive;
    char name[8];
};

uint32_t MutexModule::sysLwMutexCreate(uint64_t mutexptr, uint64_t attrptr, CellPPU *ppu)
{
    char name[9] = {0};
    for (int i = 0; i < 8; i++)
        name[i] = ppu->GetManager()->Read8(attrptr+8+i);
    printf("Initialized mutex \"%s\": lwmutex* = 0x%08lx, lwmutex_attr* = 0x%08lx\n", name, mutexptr, attrptr);

    uint32_t protocol = ppu->GetManager()->Read32(attrptr);
    uint32_t recursive = ppu->GetManager()->Read32(attrptr+4);
    
    // Initialize lwmutex structure
    ppu->GetManager()->Write64(mutexptr, 0);
    ppu->GetManager()->Write32(mutexptr+8, recursive | protocol);
    ppu->GetManager()->Write32(mutexptr+12, 0);
    ppu->GetManager()->Write32(mutexptr+16, 0);

    return CELL_OK;
}

uint32_t MutexModule::sysLwMutexLock(uint64_t mutexptr, uint64_t timeout, CellPPU *ppu)
{
    if (ppu->GetManager()->Read64(mutexptr))
    {
        printf("TODO: Tried to lock a locked mutex!\n");
    }

    ppu->GetManager()->Write64(mutexptr, 1);

    printf("sysLwMutexLock(0x%08lx, 0x%08lx)\n", mutexptr, timeout);

    return CELL_OK;
}

uint32_t MutexModule::sysLwMutexUnlock(uint64_t mutexptr, CellPPU *ppu)
{
    ppu->GetManager()->Write64(mutexptr, 0);

    printf("sysLwMutexUnlock(0x%08lx)\n", mutexptr);

    return CELL_OK;
}
