#pragma once

#include <stdint.h>
#include <cpu/PPU.h>
#include <kernel/types.h>

// Pad (DS3 input) related functions
namespace CellPad
{

uint32_t cellPadInit(uint32_t max_pads);
uint32_t cellGetPadInfo(uint32_t infoPtr, CellPPU* ppu);
uint32_t cellGetPadData(uint32_t port, uint32_t dataPtr, CellPPU* ppu);

};