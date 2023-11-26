#pragma once

#include <cstdint>
#include <cpu/PPU.h>
#include <kernel/types.h>

namespace CellGcm
{

enum CellVideoOutOutputState : int32_t
{
	CELL_VIDEO_OUT_OUTPUT_STATE_ENABLED,
	CELL_VIDEO_OUT_OUTPUT_STATE_DISABLED,
	CELL_VIDEO_OUT_OUTPUT_STATE_PREPARING,
};

enum : uint32_t
{
    CELL_VIDEO_OUT_ERROR_NOT_IMPLEMENTED          = 0x8002b220,
	CELL_VIDEO_OUT_ERROR_ILLEGAL_CONFIGURATION    = 0x8002b221,
	CELL_VIDEO_OUT_ERROR_ILLEGAL_PARAMETER        = 0x8002b222,
	CELL_VIDEO_OUT_ERROR_PARAMETER_OUT_OF_RANGE   = 0x8002b223,
	CELL_VIDEO_OUT_ERROR_DEVICE_NOT_FOUND         = 0x8002b224,
	CELL_VIDEO_OUT_ERROR_UNSUPPORTED_VIDEO_OUT    = 0x8002b225,
	CELL_VIDEO_OUT_ERROR_UNSUPPORTED_DISPLAY_MODE = 0x8002b226,
	CELL_VIDEO_OUT_ERROR_CONDITION_BUSY           = 0x8002b227,
	CELL_VIDEO_OUT_ERROR_VALUE_IS_NOT_SET         = 0x8002b228,
};

enum CellVideoOut : int32_t
{
    CELL_VIDEO_OUT_PRIMARY = 0,
    CELL_VIDEO_OUT_SECONDARY,
};

enum CellVideoOutScanMode : int32_t
{
	CELL_VIDEO_OUT_SCAN_MODE_INTERLACE,
	CELL_VIDEO_OUT_SCAN_MODE_PROGRESSIVE,
};

uint32_t cellGcmInitBody(uint32_t ctxtPtr, uint32_t cmdSize, uint32_t ioSize, uint32_t ioAddrPtr, CellPPU* ppu);
uint32_t cellVideOutGetState(uint32_t videoOut, uint32_t deviceIndex, uint32_t statePtr, CellPPU* ppu);
uint32_t cellGetResolution(uint32_t resId, uint32_t resPtr, CellPPU* ppu);
uint32_t cellVideoOutConfigure(uint32_t videoOut, uint32_t configPtr);
uint32_t cellGcmSetFlipMode(int mode);
uint32_t cellGcmGetConfiguration(uint32_t configPtr, CellPPU* ppu);
uint32_t cellGcmAddressToOffset(uint32_t address, uint32_t offsPtr, CellPPU* ppu);
uint32_t cellGcmSetDisplayBuffer(uint8_t bufId, uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height, CellPPU* ppu);
int cellGcmSetFlipCommand(uint32_t contextPtr, uint32_t id, CellPPU* ppu);
uint32_t cellGcmGetControlRegister(CellPPU* ppu);
uint32_t cellGcmGetFlipStatus();
void cellGcmResetFlipStatus();
uint32_t cellGcmGetTiledPitchSize(uint32_t size);
uint32_t cellGcmSetTileInfo(uint8_t index, uint8_t location, uint32_t offset, uint32_t size, uint32_t pitch, uint8_t comp, uint16_t base, uint8_t bank, CellPPU* ppu);
uint32_t cellGcmBindTile(uint8_t index);

// System call (custom)
void cellGcmCallback(CellPPU* ppu);

// Utility
uint32_t GetIOAddres();
uint32_t GetControlAddress();

void Dump();

}