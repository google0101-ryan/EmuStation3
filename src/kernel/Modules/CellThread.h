#pragma once

#include <kernel/types.h>
#include <kernel/Memory.h>
#include <cpu/PPU.h>

class Thread
{
public:
	Thread(uint64_t entry, uint64_t ret_addr, uint64_t stackSize, uint64_t argPtr, uint64_t namePtr, MemoryManager& manager, bool readEntry = true);

	void Switch(CellPPU* ppu);
	void Save(CellPPU* ppu);

	uint32_t GetID() {return id;}
private:
	State state;
	std::string name;
	int flags;
	int32_t priority;
	uint64_t argPtr;
	uint32_t id;
};

Thread* GetCurrentThread();
Thread* Reschedule();

namespace CellThread
{

uint32_t sysGetThreadId(uint32_t ptr, CellPPU* ppu);
uint32_t sysPPUGetThreadStackInformation(uint32_t ptr, CellPPU* ppu);
uint32_t sysPPUThreadCreate(uint32_t threadIdPtr, uint32_t paramPtr, uint64_t arg, int32_t prio, uint32_t stackSize, uint64_t flags, uint32_t namePtr, CellPPU* ppu);
uint32_t sysPPUThreadOnce(uint64_t onceCtrlPtr, uint64_t initFuncPtr, CellPPU* ppu);

}