#include "VFS.h"

#include <filesystem>
#include <unordered_map>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <cerrno>

static std::string basePath = "";

struct MountPoint
{
    std::string base, path;
};

std::vector<MountPoint> mntPoints;
std::unordered_map<int, FILE*> fds;

static int curFd = 3;

void VFS::InitVFS()
{
#ifdef __linux__
    basePath = getenv("HOME") + std::string("/.CellEmu");
#else
#endif

    // Create our VFS structure
    std::filesystem::create_directories(basePath + "/dev_hdd0");
    Mount(basePath.c_str(), "/");
}

void VFS::Mount(const char *base, const char *mnt)
{
    mntPoints.push_back({base, mnt});
}

const char* GetMountPoint(const char*& path)
{
    int index = -1;
    for (int i = 0; i < mntPoints.size(); i++)
    {
        if (!strncmp(mntPoints[i].path.c_str(), path, mntPoints[i].path.size()))
            index = i;
    }

    if (index == -1)
    {
        printf("Unknown mp for \"%s\"\n", path);
        exit(1);
    }

    path += mntPoints[index].path.size();
    return mntPoints[index].base.c_str();
}

uint32_t VFS::cellFsOpen(uint32_t namePtr, int32_t oflags, uint32_t fdPtr, CellPPU *ppu)
{
    const char* name = (char*)ppu->GetManager()->GetRawPtr(namePtr);
    
    printf("cellFsOpen(\"%s\", %d, 0x%08lx)\n", name, oflags, fdPtr);
    
    std::string fullPath = std::string(GetMountPoint(name)) + name;
    

    // Create directories if needed
    std::string dirs = fullPath.substr(0, fullPath.find_last_of('/'));
    std::filesystem::create_directories(dirs);

    int fd = curFd++;
    fds[fd] = fopen(fullPath.c_str(), "w+");

    if (fds[fd] == NULL)
    {
        printf("ERROR: Couldn't open file \"%s\": %s\n", fullPath.c_str(), strerror(errno));
        fd = -1;
        ppu->GetManager()->Write32(fdPtr, fd);
    }

    ppu->GetManager()->Write32(fdPtr, fd);
    return CELL_OK;
}

uint32_t VFS::cellFsSeek(uint32_t fd, uint32_t offs, uint32_t whence, uint32_t offsPtr, CellPPU* ppu)
{
    printf("cellFsSeekl(%d, %d, %d, 0x%08lx)\n", fd, offs, whence, offsPtr);

    fseek(fds[fd], offs, whence);
    auto filePos = ftell(fds[fd]);
    ppu->GetManager()->Write64(offsPtr, filePos);
    return CELL_OK;
}

uint32_t VFS::cellFsWrite(uint32_t fd, uint32_t bufPtr, uint32_t size, uint32_t writtenPtr, CellPPU *ppu)
{
    printf("cellFsWrite(%d, 0x%08x, %d, 0x%08x)\n", fd, bufPtr, size, writtenPtr);

    fwrite(ppu->GetManager()->GetRawPtr(bufPtr), 1, size, fds[fd]);
    return CELL_OK;
}

uint32_t VFS::cellFsClose(uint32_t fd)
{
    printf("cellFsClose(%d)\n", fd);
    fds[fd] = NULL;
    return CELL_OK;
}
