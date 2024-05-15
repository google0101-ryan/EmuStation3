#include "CellPad.h"

uint32_t CellPad::cellPadInit(uint32_t max_pads)
{
	printf("cellPadInit(%d)\n", max_pads);
	return CELL_OK;
}

uint32_t CellPad::cellGetPadInfo(uint32_t infoPtr, CellPPU* ppu)
{
	for (int i = 0; i < 647; i++)
		ppu->GetManager()->Write8(infoPtr+i, 0);
	ppu->GetManager()->Write32(infoPtr+0x00, 7); // Max connected
	ppu->GetManager()->Write32(infoPtr+0x04, 1); // Connected count
	ppu->GetManager()->Write32(infoPtr+0x08, 0); // ???
	ppu->GetManager()->Write16(infoPtr+0x0C, 0x054C); // SONY Corp
	ppu->GetManager()->Write16(infoPtr+0x10A, 0x0268); // DS3 Controller
	ppu->GetManager()->Write16(infoPtr+0x208, 1); // Connected

	printf("cellGetPadInfo(0x%08x)\n", infoPtr);

	return 0;
}

uint32_t CellPad::cellGetPadData(uint32_t port, uint32_t dataPtr, CellPPU *ppu)
{
	// TODO: Actually fill out data structure
	printf("cellGetPadData(%d, 0x%08x)\n", port, dataPtr);
	return 0;
}
