#include "CellSysUtil.h"

#include <string.h>
#include <string>

class CallbackManager
{
public:
	struct RegisteredCB
	{
		uint32_t callback;
		uint32_t userdata;
	};

	RegisteredCB callbacks[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
} cbManager;

int32_t CellSysUtil::sysUtilGetSystemParamInt(int32_t id, uint64_t paramPtr, CellPPU* ppu)
{
	printf("sysUtilGetSystemParamInt(0x%03x, 0x%08lx)\n", id, paramPtr);

	uint32_t value = 0;

	switch (id)
	{
	case 0x111:
		value = 1; // CELL_SYSUTIL_LANG_EN_US
		break;
	case 0x112:
		value = 1; // CELL_SYSUTIL_ENTER_BUTTON_CROSS
		break;
	case 0x122:
		return 0x8002b102;
	default:
		printf("Unknown sysutil parameter id 0x%03x\n", id);
		exit(1);
	}

	ppu->GetManager()->Write32(paramPtr, value);
	return CELL_OK;
}

uint32_t CellSysUtil::cellSysutilRegisterCallback(uint32_t slot, uint32_t funcAddr, uint32_t userdata, CellPPU* ppu)
{
	printf("0x%08lx: cellSysutilRegisterCallback(%d, 0x%08x, 0x%08x)\n", ppu->GetState().lr, slot, funcAddr, userdata);

	if (slot >= 4)
	{
		return 0x8002b102;
	}

	cbManager.callbacks[slot] = {funcAddr, userdata};

    return CELL_OK;
}

uint32_t CellSysUtil::cellSysutilCheckCallback(CellPPU *ppu)
{
	for (int i = 0; i < 4; i++)
	{
		if (cbManager.callbacks[i].callback != 0)
		{
			printf("Running callback\n");
			ppu->SetReg(3, cbManager.callbacks[i].userdata);
			ppu->SetReg(2, ppu->GetManager()->Read32(cbManager.callbacks[i].callback+4));
			ppu->RunSubroutine(ppu->GetManager()->Read32(cbManager.callbacks[i].callback));
			cbManager.callbacks[i].callback = 0;
			printf("Done\n");
		}
	}

    return CELL_OK;
}

uint32_t CellSysUtil::cellSysutilGetSystemParamString(int32_t id, uint32_t paramPtr, CellPPU* ppu)
{
	std::string value;

	switch (id)
	{
	case 0x131:
		value = "user"; // default username. TODO: Make this customizable
		break;
	default:
		printf("Unknown system string parameter id 0x%03x!\n", id);
		exit(1);
	}

	strncpy((char*)ppu->GetManager()->GetRawPtr(paramPtr), value.c_str(), value.size());

	return CELL_OK;
}
