#pragma once

#include <kernel/types.h>
#include <kernel/Memory.h>
#include <cpu/PPU.h>

namespace CellThread
{

uint32_t sysGetThreadId(uint32_t ptr, CellPPU* ppu);
uint32_t sysPPUGetThreadStackInformation(uint32_t ptr, CellPPU* ppu);

}