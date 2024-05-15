#pragma once

#include <cstdint>
#include <cpu/PPU.h>
#include <kernel/types.h>

namespace CellResc
{

uint32_t cellRescInit(uint32_t initCfgPtr, CellPPU* ppu);
uint32_t cellRescVideoResId2RescBufferMode(uint32_t id, uint32_t outPtr, CellPPU* ppu);

}