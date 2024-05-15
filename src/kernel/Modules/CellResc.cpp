#include "CellResc.h"

struct RescInitCfg
{
	uint32_t size;
	uint32_t resource_policy;
	uint32_t support_modes;
	uint32_t ratio_mode;
	uint32_t pal_temporal_mode;
	uint32_t interlace_mode;
	uint32_t flip_mode;
} rescConfig;

uint32_t CellResc::cellRescInit(uint32_t initCfgPtr, CellPPU *ppu)
{
	printf("cellRescInit(0x%08x)\n", initCfgPtr);

	rescConfig.size = ppu->GetManager()->Read32(initCfgPtr+0x00);
	rescConfig.resource_policy = ppu->GetManager()->Read32(initCfgPtr+0x04);
	rescConfig.support_modes = ppu->GetManager()->Read32(initCfgPtr+0x08);
	rescConfig.ratio_mode = ppu->GetManager()->Read32(initCfgPtr+0x0C);
	rescConfig.pal_temporal_mode = ppu->GetManager()->Read32(initCfgPtr+0x10);
	rescConfig.interlace_mode = ppu->GetManager()->Read32(initCfgPtr+0x14);
	rescConfig.flip_mode = ppu->GetManager()->Read32(initCfgPtr+0x18);

	return CELL_OK;
}

uint32_t CellResc::cellRescVideoResId2RescBufferMode(uint32_t id, uint32_t outPtr, CellPPU *ppu)
{
	printf("cellRescVideoResId2RescBufferMode(0x%08x, 0x%08x)\n", id, outPtr);

	uint32_t resId = 0;
	switch (id)
	{
	case 2: resId = 4; break;
	default:
		printf("[CellResc]: Unknown resolution ID %d\n", id);
		exit(1);
	}

	ppu->GetManager()->Write32(outPtr, resId);

	return CELL_OK;
}
