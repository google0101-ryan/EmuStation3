#include "CellGame.h"
#include "loaders/SFO.h"
#include "kernel/Modules/VFS.h"

#include <string.h>
#include <filesystem>

uint32_t CellGame::cellGameBootCheck(uint32_t typePtr, uint32_t attribPtr, uint32_t sizePtr, uint32_t dirNamePtr, CellPPU *ppu)
{
    printf("cellGameBootCheck(type=*0x%x, attributes=*0x%x, size=*0x%x, dirName=*0x%x)\n", typePtr, attribPtr, sizePtr, dirNamePtr);

    char dirName[32+1] = {0};
    memcpy(dirName, ppu->GetManager()->GetRawPtr(dirNamePtr), 32);

    printf("DirName = %s\n", dirName);

    ppu->GetManager()->Write32(typePtr, 1);
    ppu->GetManager()->Write32(attribPtr, 1);
    if (sizePtr)
    {
        ppu->GetManager()->Write32(sizePtr+0x00, 40*1024*1024-1);
        ppu->GetManager()->Write32(sizePtr+0x04, -1);
        ppu->GetManager()->Write32(sizePtr+0x08, 4);
    }

    return CELL_OK;
}

uint32_t CellGame::cellGameContentPermit(uint32_t contentInfoPtr, uint32_t usrdirPtr, CellPPU* ppu)
{
    printf("cellGameContentPermit(0x%08x, 0x%08x)\n", contentInfoPtr, usrdirPtr);

    std::string dir = "/dev_bdvd/PS3_GAME";
    strncpy((char*)ppu->GetManager()->GetRawPtr(contentInfoPtr), dir.c_str(), 128);
    strncpy((char*)ppu->GetManager()->GetRawPtr(usrdirPtr), (dir + "/USRDIR").c_str(), 128);

    return CELL_OK;
}

uint32_t CellGame::cellGamePatchCheck(uint32_t sizePtr, CellPPU *ppu)
{
    if (sizePtr)
    {
        ppu->GetManager()->Write32(sizePtr+0x00, 40*1024*1024-1);
        ppu->GetManager()->Write32(sizePtr+0x04, -1);
        ppu->GetManager()->Write32(sizePtr+0x08, 0);
    }
    return CELL_OK;
}

uint32_t CellGame::cellDiscGameGetBootDiscInfo(uint32_t infoPtr, CellPPU* ppu)
{
    printf("cellDiscGameGetBootDiscInfo(0x%08x)\n", infoPtr);

    if (!infoPtr)
    {
        return CELL_DISCGAME_ERROR_PARAM;
    }

    auto parental = gSFO->GetEntry("PARENTAL_LEVEL");
    auto title = gSFO->GetEntry("TITLE_ID");

    memcpy(ppu->GetManager()->GetRawPtr(infoPtr), title.strValue.c_str(), title.strValue.size());
    ppu->GetManager()->Write32(infoPtr+12, parental.intValue);

    return CELL_OK;
}

uint32_t CellGame::cellSysCacheMount(uint32_t infoPtr, CellPPU *ppu)
{
    printf("cellSysCacheMount(0x%08x)\n", infoPtr);

    auto path = std::string("dev_hdd1/caches");

    auto title = gSFO->GetEntry("TITLE_ID");
    path += "/" + title.strValue + "_" + std::string((const char*)ppu->GetManager()->GetRawPtr(infoPtr));
    
    printf("Cache path is %s\n", path.c_str());
    
    auto fullPath = std::string(VFS::GetbasePath()) + "/" + path;
    if (!std::filesystem::exists(fullPath))
        std::filesystem::create_directories(fullPath);
    
    VFS::Mount(path.c_str(), "/dev_hdd1");

    memcpy(ppu->GetManager()->GetRawPtr(infoPtr+32), "/dev_hdd1\0", 10);

    return 1;
}

uint32_t CellGame::cellGameDataCheckCreate2(uint32_t version, uint32_t dirNamePtr, uint32_t errDialog, uint32_t callback, uint32_t container, CellPPU* ppu)
{
    std::string game_dir = (const char*)ppu->GetManager()->GetRawPtr(dirNamePtr);
    std::string dir = "/dev_hdd0/game/" + game_dir;

    const uint32_t newData = 1;

    const std::string usrdir = dir + "/USRDIR";

    printf("cellGameDataCheckCreate2(%d, \"%s\", %d, 0x%08x, 0x%08x, 0x%08x)\n", version, dir.c_str(), errDialog, callback, container);

    return CELL_OK;
}
