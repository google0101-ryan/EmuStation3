#include "CellThread.h"
#include <cstring>
#include <vector>
#include <memory>

size_t curThreadIndex = 0;
std::vector<Thread*> threads;

static uint32_t tls_addr = 0;
static uint32_t tls_file = 0;
static uint32_t tls_zero = 0;
static uint32_t tls_size = 0;
static uint32_t tls_area = 0;
static uint32_t tls_max = 0;
static std::unique_ptr<bool[]> tls_map;

Thread* GetCurrentThread()
{
	return threads[curThreadIndex];
}

Thread *Reschedule()
{
	while (true)
	{
		curThreadIndex++;
		if (curThreadIndex >= threads.size())
			curThreadIndex = 0;
		if (threads[curThreadIndex]->threadState == Thread::Waiting)
			continue;
		return threads[curThreadIndex];
	}
}

static uint32_t ppu_alloc_tls(MemoryManager& manager)
{
	uint32_t addr = 0;

	for (uint32_t i = 0; i < tls_max; i++)
	{
		if (!tls_map[i] && (tls_map[i] = true))
		{
			addr = tls_area + i * tls_size;
		}
	}

	if (!addr)
	{
		addr = manager.main_mem->Alloc(tls_size);
	}

	memset(manager.GetRawPtr(addr), 0, 0x30);
	memcpy(manager.GetRawPtr(addr + 0x30), manager.GetRawPtr(tls_addr), tls_file);
	memset(manager.GetRawPtr(addr+0x30+tls_file), 0, tls_zero);

	return addr;
}

void CellThread::sysInitializeTLS(uint64_t mainThreadID, uint32_t tlsSegAddr, uint32_t tlsSegSize, uint32_t tlsMemSize, CellPPU* ppu)
{
	printf("sysInitializeTLS(0x%08lx, 0x%08x, 0x%08x, 0x%08x)\n", mainThreadID, tlsSegAddr, tlsSegSize, tlsMemSize);

	if (ppu->GetReg(13) != 0)
	{
		printf("Non-zero r13!\n");
		return;
	}

	tls_addr = tlsSegAddr;
	tls_file = tlsSegSize;
	tls_zero = tlsMemSize - tlsSegSize;
	tls_size = tlsMemSize+0x30;
	tls_area = ppu->GetManager()->main_mem->Alloc(0x40000) + 0x30;
	tls_max = (0x40000 - 0x30) / tls_size;
	tls_map = std::make_unique<bool[]>(tls_max);

	ppu->SetReg(13, ppu_alloc_tls(*ppu->GetManager()) + 0x7000 + 0x30);
}

uint32_t CellThread::sysGetThreadId(uint32_t ptr, CellPPU *ppu)
{
    ppu->GetManager()->Write64(ptr, GetCurrentThread()->GetID());
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

static uint32_t globalRetAddr = 0;

uint32_t CellThread::sysPPUThreadCreate(uint32_t threadIdPtr, uint32_t paramPtr, 
										uint64_t arg, int32_t prio, 
										uint32_t stackSize, uint64_t flags, 
										uint32_t namePtr, CellPPU* ppu)
{
	uint32_t entry = ppu->GetManager()->Read32(paramPtr);
	const char* name = (const char*)ppu->GetManager()->GetRawPtr(namePtr);

	printf("0x%08lx: sysThreadCreateEx(0x%08x, %s, 0x%08lx, %d, 0x%08x, 0x%08lx)\n", ppu->GetState().lr, entry, name, arg, prio, stackSize, flags);
	
	Thread* t = new Thread(entry, globalRetAddr, stackSize, arg, namePtr, *ppu->GetManager(), false, paramPtr+4);
	
	if (flags & 0x2)
	{
		return CELL_OK;
	}

	printf("Switching to newly-created thread\n");
	threads[curThreadIndex]->Save(ppu);
	curThreadIndex = threads.size()-1;
	t->Switch(ppu);
	ppu->threadSwapped = true;

	return CELL_OK;
}

uint32_t CellThread::sysPPUThreadOnce(uint64_t onceCtrlPtr, uint64_t initFuncPtr, CellPPU *ppu)
{
	int32_t ctrl = ppu->GetManager()->Read32(onceCtrlPtr);
	printf("%d\n", ctrl);

	if (ctrl == 0)
	{
		ppu->RunSubroutine(initFuncPtr);

		ppu->GetManager()->Write32(onceCtrlPtr, 1);
	}

	return CELL_OK;
}

uint32_t CellThread::sysPPUThreadGetPriority(uint32_t threadId, uint32_t prioPtr, CellPPU* ppu)
{
	ppu->GetManager()->Write32(prioPtr, 1001);
	printf("sysPPUThreadGetPriority(id=%d, priop=*0x%08x)\n", threadId, prioPtr);
	return CELL_OK;
}

uint32_t CellThread::sysPPUThreadExit(uint32_t exitCode, CellPPU* ppu)
{
	printf("sysPPUThreadExit(%d)\n", exitCode);

	auto t = GetCurrentThread();

	threads.erase(threads.begin()+curThreadIndex);

	if (!threads.size())
	{
		printf("Main thread exited!\n");
		exit(1);
	}

	delete t;

	t = Reschedule();
	t->Switch(ppu);

	return CELL_OK;
}

Thread::Thread(uint64_t entry, uint64_t ret_addr, uint64_t stackSize, uint64_t argPtr, uint64_t namePtr, MemoryManager &manager, bool isModule, uint64_t tls)
: manager(manager)
{
	 memset(&state, 0, sizeof(state));

    // for (int i = 4; i<32; ++i)
    //     if (i != 6)
    //         r[i] = (i+1) * 0x10000;

	stack = manager.stack->Alloc(stackSize);
    state.sp = stack + stackSize;
    state.r[1] = state.sp;
	
	if (isModule)
		state.r[2] = manager.Read32(entry+4);
	else
		state.r[2] = manager.Read32(tls);

	if (tls_size != 0)
		state.r[13] = ppu_alloc_tls(manager);

	if (isModule)
	    state.pc = manager.Read32(entry);
	else
		state.pc = entry;
	if (state.pc == 0x7c0802a4)
		state.pc = entry;
    state.lr = ret_addr;

	if (!globalRetAddr)
		globalRetAddr = ret_addr;

    state.ctr = state.pc;
    state.cr.cr = 0x22000082;

    prx_mem = manager.prx_mem->Alloc(0x10000);
    manager.Write64(prx_mem, 0xDEADBEEFABADCAFE);

    state.sp -= 0x10;

	if (!argPtr)
	    state.r[3] = 1;
	else
		state.r[3] = argPtr;
	if (isModule)
	{
		state.r[4] = state.sp-0x8;
		state.r[5] = state.sp + 0x10;

		const char* arg = "TMP.ELF\0\0";

		state.sp -= 8;
		for (int i = 0; i < 8; i++)
			manager.Write8(state.sp+i, arg[i]);
	}

    state.r[0] = state.pc;
    state.r[8] = state.sp;
    state.r[11] = entry;
    state.r[12] = 0x100000;
    state.r[13] = prx_mem + 0x7060;
    state.r[28] = state.r[4];
    state.r[29] = state.r[3];
    state.r[31] = state.r[5];

	if (namePtr)
		name = (char*)manager.GetRawPtr(namePtr);
	else
		name.clear();

    printf("Stack for thread is at 0x%08lx\n", state.r[1]);
    printf("Entry is at 0x%08lx, return address 0x%08lx\n", state.r[0], ret_addr);
	if (!name.empty())
		printf("Name: %s\n", name.c_str());
	
	id = threads.size()+0x24;
	threads.push_back(this);

	threadState = Sleeping;
}

Thread::~Thread()
{
	if (stack)
		manager.stack->Free(stack);
	if (prx_mem)
		manager.prx_mem->Free(prx_mem);
}

void Thread::Switch(CellPPU *ppu)
{
	if (!name.empty())
		printf("Switching to thread %s (0x%08lx)\n", name.c_str(), state.pc);
	else
		printf("Switching to thread %d (0x%08lx)\n", id, state.pc);
	ppu->SetState(state);
	threadState = Running;
}

void Thread::Save(CellPPU* ppu)
{
	state = ppu->GetState();
}