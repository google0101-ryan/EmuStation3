#pragma once

#include <kernel/types.h>
#include <cpu/PPU.h>

enum CellDiscGameError : uint32_t
{
	CELL_DISCGAME_ERROR_INTERNAL      = 0x8002bd01,
	CELL_DISCGAME_ERROR_NOT_DISCBOOT  = 0x8002bd02,
	CELL_DISCGAME_ERROR_PARAM         = 0x8002bd03,
};

namespace CellGame
{

uint32_t cellGameBootCheck(uint32_t typePtr, uint32_t attribPtr, uint32_t sizePtr, uint32_t dirNamePtr, CellPPU* ppu);
uint32_t cellGameContentPermit(uint32_t contentInfoPtr, uint32_t usrdirPtr, CellPPU* ppu);
uint32_t cellGamePatchCheck(uint32_t sizePtr, CellPPU* ppu);
uint32_t cellDiscGameGetBootDiscInfo(uint32_t infoPtr, CellPPU* ppu);
uint32_t cellSysCacheMount(uint32_t infoPtr, CellPPU* ppu);
uint32_t cellGameDataCheckCreate2(uint32_t version, uint32_t dirNamePtr, uint32_t errDialog, uint32_t callback, uint32_t container, CellPPU* ppu);

}