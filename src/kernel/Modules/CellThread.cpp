#include "CellThread.h"
#include <cstring>
#include <vector>

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

uint32_t CellThread::sysPPUThreadCreate(uint32_t threadIdPtr, uint32_t paramPtr, 
										uint64_t arg, int32_t prio, 
										uint32_t stackSize, uint64_t flags, 
										uint32_t namePtr, CellPPU* ppu)
{
	uint32_t entry = ppu->GetManager()->Read32(paramPtr+4);
	const char* name = (const char*)ppu->GetManager()->GetRawPtr(namePtr);

	printf("sysThreadCreateEx(0x%08x, %s, 0x%08x, %d, 0x%08x, 0x%08lx)\n", entry, name, arg, prio, stackSize, flags);
	
	Thread* t = new Thread(entry, 0, stackSize, arg, namePtr, *ppu->GetManager(), false);
	
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

size_t curThreadIndex = 0;
std::vector<Thread*> threads;

Thread::Thread(uint64_t entry, uint64_t ret_addr, uint64_t stackSize, uint64_t argPtr, uint64_t namePtr, MemoryManager &manager, bool readEntry)
{
	 memset(&state, 0, sizeof(state));

    // for (int i = 4; i<32; ++i)
    //     if (i != 6)
    //         r[i] = (i+1) * 0x10000;

    state.sp = manager.stack->Alloc(stackSize) + stackSize;
    state.r[1] = state.sp;
    state.r[2] = manager.Read32(entry+4);

	if (readEntry)
	    state.pc = manager.Read32(entry);
	else
		state.pc = entry;
	if (state.pc == 0x7c0802a4)
		state.pc = entry;
    state.lr = ret_addr;
    state.ctr = state.pc;
    state.cr.cr = 0x22000082;

    uint64_t prx_mem = manager.prx_mem->Alloc(0x10000);
    manager.Write64(prx_mem, 0xDEADBEEFABADCAFE);

    state.sp -= 0x10;

	if (!argPtr)
	    state.r[3] = 1;
	else
		state.r[3] = argPtr;
    state.r[4] = state.sp;
    state.r[5] = state.sp + 0x10;

    const char* arg = "TMP.ELF\0\0";

    state.sp -= 8;
    for (int i = 0; i < 8; i++)
        manager.Write8(state.sp+i, arg[i]);

    state.r[0] = state.pc;
    state.r[8] = entry;
    state.r[11] = 0x80;
    state.r[12] = 0x100000;
    state.r[13] = prx_mem + 0x7060;
    state.r[28] = state.r[4];
    state.r[29] = state.r[3];
    state.r[31] = state.r[5];

	if (namePtr)
		name = (char*)manager.GetRawPtr(namePtr);

    printf("Stack for thread is at 0x%08lx\n", state.r[1]);
    printf("Entry is at 0x%08lx, return address 0x%08lx\n", state.r[0], ret_addr);
	if (!name.empty())
		printf("Name: %s\n", name.c_str());
	
	id = threads.size()+0x24;
	threads.push_back(this);
}

void Thread::Switch(CellPPU *ppu)
{
	printf("Switching to thread %s\n", name.c_str());
	ppu->SetState(state);
}

void Thread::Save(CellPPU* ppu)
{
	state = ppu->GetState();
}

Thread* GetCurrentThread()
{
	return threads[curThreadIndex];
}

Thread *Reschedule()
{
	curThreadIndex++;
	if (curThreadIndex == threads.size())
		curThreadIndex = 0;
	return threads[curThreadIndex];
}