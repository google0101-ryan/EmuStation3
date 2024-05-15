#include "CellSysUtil.h"

int32_t CellSysUtil::sysUtilGetSystemParamInt(int32_t id, uint64_t paramPtr, CellPPU* ppu)
{
	printf("sysUtilGetSystemParamInt(0x%03x, 0x%08lx)\n", id, paramPtr);

	uint32_t value = 0;

	switch (id)
	{
	case 0x112:
		value = 1; // CELL_SYSUTIL_ENTER_BUTTON_CROSS
		break;
	default:
		printf("Unknown sysutil parameter id 0x%03x\n", id);
		exit(1);
	}

	ppu->GetManager()->Write32(paramPtr, value);
	return CELL_OK;
}