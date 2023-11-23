#pragma once

#include <cstdint>
#include <cpu/PPU.h>
#include <kernel/types.h>

namespace MutexModule
{

Result sysLwMutexCreate(uint64_t mutexptr, uint64_t attrptr, CellPPU* ppu);
Result sysLwMutexLock(uint64_t mutexptr, uint64_t timeout, CellPPU* ppu);
Result sysLwMutexUnlock(uint64_t mutexptr, CellPPU* ppu);

}