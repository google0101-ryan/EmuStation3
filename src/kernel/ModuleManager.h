#pragma once

#include <stdint.h>

class CellPPU;

namespace Modules
{

void DoHLECall(uint32_t nid, CellPPU* ppu);

}