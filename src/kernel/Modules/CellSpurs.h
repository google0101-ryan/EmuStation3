#pragma once

#include <cstdint>
#include <string>
#include <kernel/types.h>

class CellPPU;

class SpuThread
{
public:
	uint64_t arg0, arg1, arg2, arg3;
	uint64_t entry;
	uint32_t id;
	static uint32_t curId;
	std::string name;
	bool running;
	uint64_t cfg;
	int spu;
};

namespace CellSpurs
{

uint32_t cellSpursAttributeInitialize(uint32_t attrPtr, uint32_t revision, uint32_t sdkVersion, uint32_t nSpus, int32_t spuPriority, int32_t ppuPriority, bool exitIfNoWork, CellPPU* ppu);
uint32_t cellSpursAttributeEnableSpuPrintfIfAvailable(uint32_t attrPtr, CellPPU* ppu);

uint32_t sysSpuImageImport(uint64_t imagePtr, uint32_t src, uint32_t size, uint32_t arg4, CellPPU* ppu);
uint32_t sysSpuImageLoad(uint32_t spuIndex, uint64_t imagePtr, CellPPU* ppu);
uint32_t sysSpuImageClose(uint64_t imagePtr, CellPPU* ppu);
uint32_t sysSpuThreadGroupCreate(uint64_t idPtr, int num, int prio, uint64_t attrPtr, CellPPU* ppu);
uint32_t sysSpuThreadInitialize(uint64_t idPtr, uint32_t groupId, uint32_t spuNum, uint64_t imagePtr, uint64_t attrPtr, uint64_t argPtr, CellPPU* ppu);
uint32_t sysSpuThreadGroupStart(uint32_t groupId);
uint32_t sysSpuThreadGroupJoin(uint32_t groupId, uint64_t causePtr, uint64_t statusPtr, CellPPU* ppu);
uint32_t sysSpuThreadSetSpuCfg(uint32_t threadId, uint64_t value);
uint32_t sysSpuThreadWriteSnr(uint32_t id, int number, uint32_t value);

}