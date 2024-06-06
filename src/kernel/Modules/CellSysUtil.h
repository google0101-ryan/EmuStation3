#pragma once

#include <kernel/types.h>
#include <kernel/Memory.h>
#include <cpu/PPU.h>

namespace CellSysUtil
{
	
int32_t sysUtilGetSystemParamInt(int32_t id, uint64_t paramPtr, CellPPU* ppu);
uint32_t cellSysutilRegisterCallback(uint32_t slot, uint32_t funcAddr, uint32_t userdata, CellPPU* ppu);
uint32_t cellSysutilCheckCallback(CellPPU* ppu);
uint32_t cellSysutilGetSystemParamString(int32_t id, uint32_t paramPtr, CellPPU* ppu);

}