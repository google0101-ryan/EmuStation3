#pragma once

#include <cstdint>
#include <cpu/PPU.h>
#include <kernel/types.h>

namespace MutexModule
{

uint32_t sysLwMutexCreate(uint64_t mutexptr, uint64_t attrptr, CellPPU* ppu);
uint32_t sysLwMutexLock(uint64_t mutexptr, uint64_t timeout, CellPPU* ppu);
uint32_t sysLwMutexUnlock(uint64_t mutexptr, CellPPU* ppu);

}