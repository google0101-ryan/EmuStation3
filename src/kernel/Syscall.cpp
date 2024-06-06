#include "Syscall.h"
#include "Modules/CellThread.h"
#include "Modules/CellGcm.h"
#include "Modules/VFS.h"
#include "Modules/CellSpurs.h"
#include "types.h"

#include <stdio.h>
#include <ctime>
#include <queue>

uint32_t sysProcessGetParamSFO(uint32_t bufPtr)
{
	printf("sysProcessGetParamSFO(0x%08x)\n", bufPtr);
	return CELL_ENOENT;
}

uint32_t sysTimeGetCurrentTime(uint64_t secPtr, uint64_t nSecPtr, CellPPU* ppu)
{
    printf("sysTimeGetCurrentTime(secPtr=*0x%08x, nSecPtr=*0x%08x)\n", secPtr, nSecPtr);

    if (!secPtr)
    {
        return CELL_EFAULT;
    }

    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);

    ppu->GetManager()->Write64(secPtr, ts.tv_sec);

    if (!nSecPtr)
    {
        return CELL_EFAULT;
    }

    ppu->GetManager()->Write64(nSecPtr, ts.tv_nsec);

    return CELL_OK;
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
    ppu->GetManager()->Write32(mem_info_addr+0x4, ppu->GetManager()->main_mem->GetAvailable());

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

    printf("Memory allocated [addr 0x%x, size 0x%x]\n", addr, size);
    ppu->GetManager()->Write32(alloc_addr, addr);

    return CELL_OK;
}

uint32_t sysMMapperSearchAndMapMemory(uint32_t start_addr, uint32_t mem_id, uint64_t flags, uint32_t allocAddr, CellPPU* ppu)
{
    printf("sysMMapperSearchAndMapMemory(0x%08x, 0x%08x, 0x%08lx, 0x%08x)\n", start_addr, mem_id, flags, allocAddr);
    ppu->GetManager()->Write32(allocAddr, mapInfo[mem_id].start);
    return CELL_OK;
}

uint64_t spuId = 0;
uint32_t sys_raw_spu_create(uint64_t spuIdPtr, CellPPU* ppu)
{
	ppu->GetManager()->Write32(spuIdPtr, spuId++);
	return CELL_OK;
}

#define ARG0 ppu->GetReg(3)
#define ARG1 ppu->GetReg(4)
#define ARG2 ppu->GetReg(5)
#define ARG3 ppu->GetReg(6)
#define ARG4 ppu->GetReg(7)
#define ARG5 ppu->GetReg(8)
#define RETURN(x) ppu->SetReg(3, x)

uint32_t kernel_id = 1;

class KernelObject
{
public:
    KernelObject()
    {
        id = kernel_id++;
    }

    enum Type
    {
        KERNEL_OBJECT_NONE = 0,
        KERNEL_OBJECT_KEVENTQUEUE,
        KERNEL_OBJECT_KEVENTPORT,
        KERNEL_OBJECT_KEVENTFLAG,
    } type;

    uint32_t id;
};

std::unordered_map<uint32_t, KernelObject*> kObjects;

struct EventQueueData
{
    uint64_t source;
    uint64_t data0;
    uint64_t data1;
    uint64_t data2;
};

struct EvtQWaitingThread
{
    Thread* thread;
    uint64_t dataPtr;
};

class EventQueue : public KernelObject
{
public:
    EventQueue()
    {
        type = KERNEL_OBJECT_KEVENTQUEUE;
    }

    std::queue<EventQueueData> queue;
    uint64_t ipcKey;
    int size;
    std::vector<EvtQWaitingThread> waitingThreads;
};

uint32_t sysEventQueueCreate(uint32_t queueIdPtr, uint32_t attrPtr, uint64_t ipc_key, int32_t size, CellPPU* ppu)
{
    printf("sysEventQueueCreate(0x%08x, 0x%08x, 0x%08lx, %d)\n", queueIdPtr, attrPtr, ipc_key, size);

    EventQueue* queue = new EventQueue();
    queue->ipcKey = ipc_key;
    queue->size = size;

    if (kObjects.contains(queue->id))
        printf("Duplicate ID!\n");
    kObjects[queue->id] = queue;

    ppu->GetManager()->Write32(queueIdPtr, queue->id);

    return CELL_OK;
}

uint32_t sysEventQueueReceive(uint32_t eventQueueId, uint64_t dataPtr, uint64_t timeout, CellPPU* ppu)
{
    printf("sysEventQueueReceive(0x%08x, 0x%08lx, 0x%08lx)\n", eventQueueId, dataPtr, timeout);

    if (!kObjects.contains(eventQueueId))
    {
        printf("No such event queue\n");
        return CELL_EINVAL;
    }

    auto queue = reinterpret_cast<EventQueue*>(kObjects[eventQueueId]);

    if (queue->type != KernelObject::KERNEL_OBJECT_KEVENTQUEUE)
    {
        printf("Invalid kernel object ID\n");
        return CELL_EINVAL;
    }

    if (queue->queue.empty())
    {
        // Put thread to sleep
        EvtQWaitingThread wait;
        wait.thread = GetCurrentThread();
        wait.dataPtr = dataPtr;
        queue->waitingThreads.push_back(wait);
        GetCurrentThread()->threadState = Thread::Waiting;
        GetCurrentThread()->Save(ppu);
        Reschedule()->Switch(ppu);
        ppu->GetState().pc = ppu->GetState().lr;
    }
    else
    {
        printf("TODO: Actual data\n");
        exit(1);
    }

    return CELL_OK;
}

class EventPort : public KernelObject
{
public:
    EventPort()
    {
        type = KERNEL_OBJECT_KEVENTPORT;
    }

    EventQueue* queue;
    uint64_t name;
    int type;
};

uint32_t sysEventPortCreate(uint32_t idPtr, int portType, uint64_t name, CellPPU* ppu)
{
    printf("sysEventPortCreate(0x%08x, %d, 0x%08lx)\n", idPtr, portType, name);

    EventPort* port = new EventPort();
    port->name = name;
    port->type = portType;

    if (kObjects.contains(port->id))
        printf("Duplicate kernel ID detected\n");

    kObjects[port->id] = port;

    ppu->GetManager()->Write32(idPtr, port->id);

    return CELL_OK;
}

uint32_t sysEventPortConnectLocal(uint32_t portId, uint32_t queueId)
{
    printf("sysEventPortConnect(%d, %d)\n", portId, queueId);

    if (!kObjects.contains(portId) || !kObjects.contains(queueId))
    {
        printf("No such port/queue\n");
        return CELL_EINVAL;
    }

    auto port = reinterpret_cast<EventPort*>(kObjects[portId]);
    auto queue = reinterpret_cast<EventQueue*>(kObjects[queueId]);

    port->queue = queue;

    return CELL_OK;
}

class EventFlag : public KernelObject
{
public:
    EventFlag(uint64_t initial, uint32_t flags, uint32_t protocol)
    : bits(initial),
    flags(flags),
    protocol(protocol)
    {
    }

    uint64_t bits;
    uint32_t flags;
    uint32_t protocol;
};

uint32_t sysEventFlagCreate(uint32_t idPtr, uint32_t attrPtr, uint64_t initial, CellPPU* ppu)
{
    printf("sysEventFlagCreate(0x%08x, 0x%08x, 0x%08lx)\n", idPtr, attrPtr, initial);

    uint32_t protocol = ppu->GetManager()->Read32(attrPtr);
    uint32_t flags = ppu->GetManager()->Read32(attrPtr+16);

    EventFlag* flag = new EventFlag(initial, flags, protocol);

    kObjects[flag->id] = flag;

    ppu->GetManager()->Write32(idPtr, flag->id);

    return CELL_OK;
}

void Syscalls::DoSyscall(CellPPU *ppu)
{
    switch (ppu->GetReg(11))
    {
	case 0x01E:
		RETURN(sysProcessGetParamSFO(ARG0));
		break;
    case 0x030:
        RETURN(CellThread::sysPPUThreadGetPriority(ARG0, ARG1, ppu));
        break;
    case 0x031:
        RETURN(CellThread::sysPPUGetThreadStackInformation(ARG0, ppu));
        break;
    case 0x052:
        RETURN(sysEventFlagCreate(ARG0, ARG1, ARG2, ppu));
        break;
    case 0x05F:
        printf("_sys_lwmutex_create(0x%08lx, %ld, 0x%08lx, %ld, 0x%08lx)\n", ARG0, ARG1, ARG2, ARG3, ARG4);
        ppu->GetManager()->Write32(ARG0, kernel_id++);
        RETURN(CELL_OK);
        break;
    case 0x064:
        printf("sys_mutex_create(0x%08lx, 0x%08lx)\n", ARG0, ARG1);
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
        RETURN(sysEventQueueCreate(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    case 0x082:
        RETURN(sysEventQueueReceive(ARG0, ARG1, ARG2, ppu));
        break;
    case 0x086:
        RETURN(sysEventPortCreate(ARG0, ARG1, ARG2, ppu));
        break;
    case 0x088:
        RETURN(sysEventPortConnectLocal(ARG0, ARG1));
        break;
    case 0x08C:
        printf("sys_event_port_connect_ipc(%d, %d)\n", (int32_t)ARG0, (int32_t)ARG1);
        RETURN(CELL_OK);
        break;
    case 0x08D:
        // printf("sleep_timer_usleep(%d)\n", ARG0);
        RETURN(CELL_OK);
        break;
    case 0x091:
        RETURN(sysTimeGetCurrentTime(ARG0, ARG1, ppu));
        break;
	case 0x093:
		printf("sys_time_get_timebase_frequency()\n");
		RETURN(80000000ull);
		break;
	case 0x0A0:
		RETURN(sys_raw_spu_create(ARG0, ppu));
		break;
	case 0x0A9:
		printf("sys_util_initialize(%ld, %ld)\n", ARG0, ARG1);
		RETURN(CELL_OK);
		break;
	case 0x0AA:
		RETURN(CellSpu::sysSpuThreadGroupCreate(ARG0, ARG1, ARG2, ARG3, ppu));
		break;
	case 0x0AC:
		RETURN(CellSpu::sysSpuThreadInitialize(ARG0, ARG1, ARG2, ARG3, ARG4, ARG5, ppu));
		break;
	case 0x0AD:
		RETURN(CellSpu::sysSpuThreadGroupStart(ARG0));
		break;
	case 0x0B2:
		RETURN(CellSpu::sysSpuThreadGroupJoin(ARG0, ARG1, ARG2, ppu));
		break;
	case 0x0B8:
		RETURN(CellSpu::sysSpuThreadWriteSnr(ARG0, ARG1, ARG2));
		break;
	case 0x0BB:
		RETURN(CellSpu::sysSpuThreadSetSpuCfg(ARG0, ARG1));
		break;
    case 0x14A:
        RETURN(sysMMapperAllocateAddress(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    case 0x151:
        RETURN(sysMMapperSearchAndMapMemory(ARG0, ARG1, ARG2, ARG3, ppu));
        break;
    case 0x155:
        printf("sys_memory_container_create(0x%08lx, 0x%08lx)\n", ARG0, ARG1);
        ppu->GetManager()->Write32(ARG0, kernel_id++);
        RETURN(CELL_OK);
        break;
    case 0x156:
        printf("sys_memory_container_destroy(0x%08lx)\n", ARG0);
        RETURN(CELL_OK);
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
		ppu->GetManager()->DumpRam();
        printf("[LV2]: unknown syscall 0x%04lx\n", ppu->GetReg(11));
        exit(1);
    }
}