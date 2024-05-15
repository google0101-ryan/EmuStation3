#pragma once

#include <kernel/types.h>
#include <kernel/Memory.h>
#include <cpu/PPU.h>

namespace CellSysUtil
{
	
int32_t sysUtilGetSystemParamInt(int32_t id, uint64_t paramPtr, CellPPU* ppu);

}