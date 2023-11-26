#include "CellGcm.h"
#include <rsx/rsx.h>

#include <stdexcept>

struct CellGcmConfig
{
    uint32_t localAddress;
    uint32_t ioAddress;
    uint32_t localSize;
    uint32_t ioSize;
    uint32_t memoryFrequency;
    uint32_t coreFrequency;
} config;

struct
{
    uint32_t begin;
    uint32_t end;
    uint32_t current;
    uint32_t callback;
} context;

struct GcmInfo
{
    uint32_t context_addr, control_addr;
    uint32_t tiles_addr;
} gcm_info;

struct CellGcmTileInfo
{
    uint32_t tile;
    uint32_t limit;
    uint32_t pitch;
    uint32_t format;
};

struct GcmTileInfo
{
    uint8_t location;
    uint32_t offset;
    uint32_t size;
    uint32_t pitch;
    uint32_t comp;
    uint16_t base;
    uint8_t bank;
    bool bound;

    CellGcmTileInfo Pack()
    {
        CellGcmTileInfo tile;
        tile.tile = (location + 1) | (bank << 4) | ((offset / 0x10000) << 16) | (location << 31);
        tile.limit = ((offset + size - 1) / 0x10000) << 16 | (location << 31);
        tile.pitch = (pitch / 0x100) << 8;
        tile.format = base | ((base + ((size - 1) / 0x10000)) << 13) | (comp << 26) | (1 << 30);
        return tile;
    }
} tiles[15];

uint32_t CellGcm::cellGcmInitBody(uint32_t ctxtPtr, uint32_t cmdSize, uint32_t ioSize, uint32_t ioAddrPtr, CellPPU *ppu)
{
    printf("cellGcmInitBody(0x%08x, 0x%08x, 0x%08x, 0x%08x)\n", ctxtPtr, cmdSize, ioSize, ioAddrPtr);
    
    const uint32_t local_size = 0xf900000;
    const uint32_t local_addr = ppu->GetManager()->RSXFBMem->GetStart();

    config.ioSize = ioSize;
    config.ioAddress = ioAddrPtr;
    config.localSize = local_size;
    config.localAddress = local_addr;
    config.memoryFrequency = 650000000;
    config.coreFrequency = 500000000;

    ppu->GetManager()->RSXFBMem->Alloc(local_size);
    ppu->GetManager()->RSXCmdMem->Alloc(cmdSize);

    uint32_t rsxCallback = ppu->GetManager()->main_mem->Alloc(4 * 4) + 4;
    ppu->GetManager()->Write32(rsxCallback - 4, rsxCallback);

    ppu->GetManager()->Write32(rsxCallback, 0x396003FF);
    ppu->GetManager()->Write32(rsxCallback+4, 0x44000002);
    ppu->GetManager()->Write32(rsxCallback+8, 0x4E800020);

    uint32_t ctx_begin = ioAddrPtr;
    uint32_t ctx_size = 32*1024-4;
    context.begin = ctx_begin;
    context.end = ctx_begin + ctx_size;
    context.current = context.begin;
    context.callback = rsxCallback-4;

    gcm_info.context_addr = ppu->GetManager()->main_mem->Alloc(0x1000);
    gcm_info.control_addr = gcm_info.context_addr + 0x40;
    ppu->GetManager()->SetRSXControlReg(gcm_info.control_addr);
    gcm_info.tiles_addr = ppu->GetManager()->main_mem->Alloc(sizeof(CellGcmTileInfo) * 15);

    ppu->GetManager()->Write32(ctxtPtr, gcm_info.context_addr);

    ppu->GetManager()->Write32(gcm_info.context_addr+0x0, context.begin);
    ppu->GetManager()->Write32(gcm_info.context_addr+0x4, context.end);
    ppu->GetManager()->Write32(gcm_info.context_addr+0x8, context.current);
    ppu->GetManager()->Write32(gcm_info.context_addr+0xC, context.callback);

    printf("GCM context at 0x%08x (wrote to 0x%08x)\n", gcm_info.context_addr, ctxtPtr);

    return CELL_OK;
}

struct CellVideoOutDisplayMode
{
    uint8_t resolutionId;
    uint8_t scanMode;
    uint8_t conversion;
    uint8_t aspect;
    uint8_t reserved[2];
    uint16_t refreshRates;
};

struct VideoOutState
{
    uint8_t state;
    uint8_t colorSpace;
    uint8_t reserved[6];
    CellVideoOutDisplayMode displayMode;
} videoState;

uint32_t CellGcm::cellVideOutGetState(uint32_t videoOut, uint32_t deviceIndex, uint32_t statePtr, CellPPU* ppu)
{
    printf("cellVideoOutGetState(%d, %d, 0x%08x)\n", videoOut, deviceIndex, statePtr);

    if (!statePtr)
    {
        return CELL_VIDEO_OUT_ERROR_ILLEGAL_PARAMETER;
    }

    const auto device_count = (videoOut == CELL_VIDEO_OUT_PRIMARY) ? 1 : 0;

    if (device_count < 0 || deviceIndex >= device_count)
        return CELL_VIDEO_OUT_ERROR_DEVICE_NOT_FOUND;

    switch (videoOut)
    {
    case CELL_VIDEO_OUT_PRIMARY:
    {
        ppu->GetManager()->Write8(statePtr+offsetof(VideoOutState, state), CELL_VIDEO_OUT_OUTPUT_STATE_ENABLED);
        ppu->GetManager()->Write8(statePtr+offsetof(VideoOutState, colorSpace), 0x01);
        ppu->GetManager()->Write8(statePtr+offsetof(VideoOutState, displayMode)+offsetof(CellVideoOutDisplayMode, resolutionId), 0x01);
        ppu->GetManager()->Write8(statePtr+offsetof(VideoOutState, displayMode)+offsetof(CellVideoOutDisplayMode, scanMode), CELL_VIDEO_OUT_SCAN_MODE_PROGRESSIVE);
        ppu->GetManager()->Write8(statePtr+offsetof(VideoOutState, displayMode)+offsetof(CellVideoOutDisplayMode, conversion), 0x00);
        ppu->GetManager()->Write8(statePtr+offsetof(VideoOutState, displayMode)+offsetof(CellVideoOutDisplayMode, aspect), 0x02);
        ppu->GetManager()->Write16(statePtr+offsetof(VideoOutState, displayMode)+offsetof(CellVideoOutDisplayMode, refreshRates), 0x01);

        return CELL_OK;
    }
    case CELL_VIDEO_OUT_SECONDARY:
        ppu->GetManager()->Write8(statePtr+offsetof(VideoOutState, state), CELL_VIDEO_OUT_OUTPUT_STATE_DISABLED);
        return CELL_OK;
    }

    return CELL_VIDEO_OUT_ERROR_UNSUPPORTED_VIDEO_OUT;
}

uint32_t CellGcm::cellGetResolution(uint32_t resId, uint32_t resPtr, CellPPU* ppu)
{
    uint16_t width, height;

    printf("cellVideoOutGetResolution(%d, 0x%08x)\n", resId, resPtr);

    if (!resPtr)
    {
        return CELL_VIDEO_OUT_ERROR_ILLEGAL_PARAMETER;
    }

    switch (resId)
    {
    case 0x01:
        width = 1920;
        height = 1080;
        break;
    default:
        printf("Unknown resolution ID 0x%08x\n", resId);
        throw std::runtime_error("Couldn't get resolution\n");
    }

    ppu->GetManager()->Write16(resPtr, width);
    ppu->GetManager()->Write16(resPtr+2, height);

    return CELL_OK;
}

uint32_t CellGcm::cellVideoOutConfigure(uint32_t videoOut, uint32_t configPtr)
{
    printf("TODO: cellVideoOutConfigure(0x%08x, 0x%08x)\n", videoOut, configPtr);
    return CELL_OK;
}

uint32_t CellGcm::cellGcmSetFlipMode(int mode)
{
    printf("cellGcmSetFlipMode(%d)\n", mode);

    return CELL_OK;
}

uint32_t CellGcm::cellGcmGetConfiguration(uint32_t configPtr, CellPPU* ppu)
{
    printf("cellGcmGetConfiguration(0x%08x)\n", configPtr);

    ppu->GetManager()->Write32(configPtr, config.localAddress);
    ppu->GetManager()->Write32(configPtr+4, config.ioAddress);
    ppu->GetManager()->Write32(configPtr+8, config.localSize);
    ppu->GetManager()->Write32(configPtr+12, config.ioSize);
    ppu->GetManager()->Write32(configPtr+16, config.memoryFrequency);
    ppu->GetManager()->Write32(configPtr+20, config.coreFrequency);

    return CELL_OK;
}

uint32_t CellGcm::cellGcmAddressToOffset(uint32_t address, uint32_t offsPtr, CellPPU* ppu)
{
    printf("cellGcmAddressToOffset(0x%08x, 0x%08x)\n", address, offsPtr);

    uint64_t base;

    auto mman = ppu->GetManager();
    auto main_mem = ppu->GetManager()->main_mem;
    auto fb_mem = ppu->GetManager()->RSXFBMem;
    
    if (address >= fb_mem->GetStart() && address < fb_mem->GetStart()+fb_mem->GetSize())
    {
        base = fb_mem->GetStart();
    }
    else
    {
        base = config.ioAddress;
    }

    mman->Write32(offsPtr, address - base);
    return CELL_OK;
}

uint32_t CellGcm::cellGcmSetDisplayBuffer(uint8_t bufId, uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height, CellPPU *ppu)
{
    printf("cellGcmSetDisplayBuffer(%d, 0x%08x, 0x%08x, %d, %d)\n", bufId, offset, pitch, width, height);
    rsx->SetFramebuffer(bufId, offset, pitch, width, height);
    return CELL_OK;
}

enum
{
	CELL_GCM_ERROR_FAILURE				= 0x802100ff,
	CELL_GCM_ERROR_INVALID_ENUM			= 0x80210002,
	CELL_GCM_ERROR_INVALID_VALUE		= 0x80210003,
	CELL_GCM_ERROR_INVALID_ALIGNMENT	= 0x80210004,
};

int CellGcm::cellGcmSetFlipCommand(uint32_t contextPtr, uint32_t id, CellPPU *ppu)
{
    if (id >= 8)
    {
        return CELL_GCM_ERROR_INVALID_VALUE;
    }

    uint32_t current = ppu->GetManager()->Read32(contextPtr+8);
    uint32_t end = ppu->GetManager()->Read32(contextPtr+4);
    uint32_t begin = ppu->GetManager()->Read32(contextPtr);

    if (current+4 >= end)
    {
        printf("bad flip!\n");
        throw std::runtime_error("bad flip");
    }

    ppu->GetManager()->Write32(current, 0x3fead | (1 << 18));
    ppu->GetManager()->Write32(current+4, id);
    ppu->GetManager()->Write32(contextPtr+8, current+8);

    // rsx->DoCommands(ppu->GetManager()->GetRawPtr(begin), current+8-begin);

    return id;
}

uint32_t CellGcm::cellGcmGetControlRegister(CellPPU* ppu)
{
    return gcm_info.control_addr;
}

uint32_t CellGcm::cellGcmGetFlipStatus()
{
    printf("cellGcmGetFlipStatus() = %d\n", rsx->GetFlipped());
    return !rsx->GetFlipped();
}

void CellGcm::cellGcmResetFlipStatus()
{
    printf("cellGcmResetFlipStatus()\n");
    rsx->GetFlipped() = false;
}

const uint32_t tiled_pitches[] = 
{
	0x00000000, 0x00000200, 0x00000300, 0x00000400,
	0x00000500, 0x00000600, 0x00000700, 0x00000800,
	0x00000A00, 0x00000C00, 0x00000D00, 0x00000E00,
	0x00001000, 0x00001400, 0x00001800, 0x00001A00,
	0x00001C00, 0x00002000, 0x00002800, 0x00003000,
	0x00003400, 0x00003800, 0x00004000, 0x00005000,
	0x00006000, 0x00006800, 0x00007000, 0x00008000,
	0x0000A000, 0x0000C000, 0x0000D000, 0x0000E000,
	0x00010000
};

uint32_t CellGcm::cellGcmGetTiledPitchSize(uint32_t size)
{
    printf("cellGcmGetTiledPitchSize(%d)\n", size);

    for (size_t i = 0; i < std::size(tiled_pitches) - 1; i++)
    {
        if (tiled_pitches[i] < size && size <= tiled_pitches[i+1])
            return tiled_pitches[i+1];
    }

    return 0;
}

uint32_t CellGcm::cellGcmSetTileInfo(uint8_t index, uint8_t location, uint32_t offset, uint32_t size, uint32_t pitch, uint8_t comp, uint16_t base, uint8_t bank, CellPPU* ppu)
{
    printf("cellGcmSetTileInfo(%d, %d, 0x%08x, 0x%08x, %d, %d, %d, %d)\n", index, location, offset, size, pitch, comp, base, bank);
    
    if (index >= 15 || base >= 800 || bank >= 4)
        return CELL_GCM_ERROR_INVALID_VALUE;
    
    if (offset & 0xffff || size & 0xffff || pitch & 0xf)
        return CELL_GCM_ERROR_INVALID_ALIGNMENT;
    
    if (location >= 2 || (comp != 0 && (comp < 7 || comp > 12)))
        return CELL_GCM_ERROR_INVALID_ENUM;
    
    if (comp)
    {
        printf("TODO: Compression of tile info!\n");
    }

    auto& tile = tiles[index];
    tile.location = location;
    tile.offset = offset;
    tile.size = size;
    tile.pitch = pitch;
    tile.comp = comp;
    tile.base = base;
    tile.bank = bank;
    auto tileData = tile.Pack();

    ppu->GetManager()->Write32((gcm_info.tiles_addr + sizeof(CellGcmTileInfo) * index), tileData.tile);
    ppu->GetManager()->Write32((gcm_info.tiles_addr + sizeof(CellGcmTileInfo) * index)+4, tileData.limit);
    ppu->GetManager()->Write32((gcm_info.tiles_addr + sizeof(CellGcmTileInfo) * index)+8, tileData.pitch);
    ppu->GetManager()->Write32((gcm_info.tiles_addr + sizeof(CellGcmTileInfo) * index)+12, tileData.format);

    return CELL_OK;
}

uint32_t CellGcm::cellGcmBindTile(uint8_t index)
{
    printf("cellGcmBindTile(%d)\n", index);

    if (index >= 15)
        return CELL_GCM_ERROR_INVALID_VALUE;
    
    tiles[index].bound = true;
    return CELL_OK;
}

void CellGcm::cellGcmCallback(CellPPU* ppu)
{
    uint32_t begin = ppu->GetManager()->Read32(gcm_info.context_addr);
    uint32_t current = ppu->GetManager()->Read32(gcm_info.context_addr+8);
    uint32_t put = ppu->GetManager()->Read32(gcm_info.control_addr);
    uint32_t get = ppu->GetManager()->Read32(gcm_info.control_addr+4);

    int32_t res = current - begin - put;

    if (res > 0)
        memcpy(ppu->GetManager()->GetRawPtr(begin), ppu->GetManager()->GetRawPtr(current - res), res);
    
    ppu->GetManager()->Write32(gcm_info.context_addr+8, begin + res);
    ppu->GetManager()->Write32(gcm_info.control_addr, put);
    ppu->GetManager()->Write32(gcm_info.control_addr+4, 0);
}

uint32_t CellGcm::GetIOAddres()
{
    return config.ioAddress;
}

uint32_t CellGcm::GetControlAddress()
{
    return gcm_info.control_addr;
}
