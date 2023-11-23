#pragma once

#include <kernel/types.h>
#include <kernel/Memory.h>
#include <cpu/PPU.h>

namespace CellThread
{

Result sysGetThreadId(uint32_t ptr, CellPPU* ppu);

}