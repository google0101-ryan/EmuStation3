#pragma once

#include <kernel/types.h>
#include <kernel/Memory.h>
#include <cpu/PPU.h>

class Thread
{
public:
	Thread(uint64_t entry, uint64_t ret_addr, uint64_t stackSize, uint64_t argPtr, uint64_t namePtr, MemoryManager& manager, bool readEntry = true, uint64_t tls = 0);
	~Thread();

	void Switch(CellPPU* ppu);
	void Save(CellPPU* ppu);

	uint32_t GetID() {return id;}

	enum ThreadState
	{
		Running,
		Sleeping,
		Waiting
	} threadState;
private:
	State state;
	std::string name;
	int flags;
	int32_t priority;
	uint64_t argPtr;
	uint32_t id;
	uint32_t stack;
	uint64_t prx_mem;
	MemoryManager& manager;
};

Thread* GetCurrentThread();
Thread* Reschedule();

namespace CellThread
{

void sysInitializeTLS(uint64_t mainThreadID, uint32_t tlsSegAddr, uint32_t tlsSegSize, uint32_t tlsMemSize, CellPPU* ppu);
uint32_t sysGetThreadId(uint32_t ptr, CellPPU* ppu);
uint32_t sysPPUGetThreadStackInformation(uint32_t ptr, CellPPU* ppu);
uint32_t sysPPUThreadCreate(uint32_t threadIdPtr, uint32_t paramPtr, uint64_t arg, int32_t prio, uint32_t stackSize, uint64_t flags, uint32_t namePtr, CellPPU* ppu);
uint32_t sysPPUThreadOnce(uint64_t onceCtrlPtr, uint64_t initFuncPtr, CellPPU* ppu);
uint32_t sysPPUThreadGetPriority(uint32_t threadId, uint32_t prioPtr, CellPPU* ppu);
uint32_t sysPPUThreadExit(uint32_t exitCode, CellPPU* ppu);

}