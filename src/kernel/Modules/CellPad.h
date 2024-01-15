#pragma once

#include <stdint.h>
#include <cpu/PPU.h>
#include <kernel/types.h>

// Pad (DS3 input) related functions
namespace CellPad
{

uint32_t cellPadInit(uint32_t max_pads);

};