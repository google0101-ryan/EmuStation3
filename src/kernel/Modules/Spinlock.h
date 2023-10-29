#pragma once

#include <cstdint>
#include <cpu/PPU.h>

namespace SpinlockModule
{

void sysSpinlockInitialize(uint64_t lockPtr, CellPPU* ppu);

}