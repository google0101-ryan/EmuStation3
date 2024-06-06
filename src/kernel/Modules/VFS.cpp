#include "VFS.h"

#include <filesystem>
#include <unordered_map>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <cerrno>
#include <cassert>

static std::string basePath = "";

struct MountPoint
{
    std::string base, path;
};

std::vector<MountPoint> mntPoints;
std::unordered_map<int, FILE*> fds;

static int curFd = 4;

void VFS::InitVFS()
{
#ifdef __linux__
    basePath = getenv("HOME") + std::string("/.CellEmu");
#else
#endif

    // Create our VFS structure
    std::filesystem::create_directories(basePath + "/dev_hdd0");
    Mount("dev_hdd0", "/dev_hdd0");
}

void VFS::Mount(const char *base, const char *mnt)
{
    printf("Mounting %s on %s\n", (basePath + "/" + base).c_str(), mnt);
    if (!std::filesystem::exists(basePath + "/" + base))
        std::filesystem::create_directories(basePath + "/" + base);
    mntPoints.push_back({basePath + "/" + base, mnt});
}

const char* VFS::GetMountPoint(const char*& path)
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
        return ".";
    }

    path += mntPoints[index].path.size();
    return mntPoints[index].base.c_str();
}

const char *VFS::GetbasePath()
{
    return basePath.c_str();
}

uint32_t VFS::cellFsOpen(uint32_t namePtr, int32_t oflags, uint32_t fdPtr, CellPPU *ppu)
{
    const char* name = (char*)ppu->GetManager()->GetRawPtr(namePtr);
    
    printf("cellFsOpen(\"%s\", %d, 0x%08x)\n", name, oflags, fdPtr);
    
    std::string fullPath = std::string(GetMountPoint(name));
    fullPath += name;
    

    // Create directories if needed
    int fd = curFd++;
    if (oflags & 0x200)
    {    
        std::string dirs = fullPath.substr(0, fullPath.find_last_of('/'));
        std::filesystem::create_directories(dirs);
        fds[fd] = fopen(fullPath.c_str(), "w+");
    }
    else
        fds[fd] = fopen(fullPath.c_str(), "rwb");

    if (fds[fd] == NULL)
    {
        printf("ERROR: Couldn't open file \"%s\": %s\n", fullPath.c_str(), strerror(errno));
        fd = -1;
        ppu->GetManager()->Write32(fdPtr, fd);
        return CELL_ENOENT;
    }

    ppu->GetManager()->Write32(fdPtr, fd);
    return CELL_OK;
}

uint32_t VFS::cellFsSeek(uint32_t fd, uint32_t offs, uint32_t whence, uint32_t offsPtr, CellPPU* ppu)
{
    printf("cellFsSeekl(%d, %d, %d, 0x%08x)\n", fd, offs, whence, offsPtr);

    fseek(fds[fd], offs, whence);
    auto filePos = ftell(fds[fd]);
    ppu->GetManager()->Write64(offsPtr, filePos);
    return CELL_OK;
}

uint32_t VFS::cellFsWrite(uint32_t fd, uint32_t bufPtr, uint32_t size, uint32_t writtenPtr, CellPPU *ppu)
{
    printf("cellFsWrite(%d, 0x%08x, %d, 0x%08x)\n", fd, bufPtr, size, writtenPtr);

    size_t size_written = fwrite(ppu->GetManager()->GetRawPtr(bufPtr), 1, size, fds[fd]);
	assert(size_written == size);
    return CELL_OK;
}

uint32_t VFS::cellFsClose(uint32_t fd)
{
    printf("cellFsClose(%d)\n", fd);
    fds[fd] = NULL;
    return CELL_OK;
}

enum
{
	CELL_FS_S_IFDIR = 0040000,	//directory
	CELL_FS_S_IFREG = 0100000,	//regular
	CELL_FS_S_IFLNK = 0120000,	//symbolic link
	CELL_FS_S_IFWHT = 0160000,	//unknown

	CELL_FS_S_IRUSR = 0000400,	//R for owner
	CELL_FS_S_IWUSR = 0000200,	//W for owner
	CELL_FS_S_IXUSR = 0000100,	//X for owner

	CELL_FS_S_IRGRP = 0000040,	//R for group
	CELL_FS_S_IWGRP = 0000020,	//W for group
	CELL_FS_S_IXGRP = 0000010,	//X for group

	CELL_FS_S_IROTH = 0000004,	//R for other
	CELL_FS_S_IWOTH = 0000002,	//W for other
	CELL_FS_S_IXOTH = 0000001,	//X for other
};

uint32_t VFS::cellFsFstat(uint32_t fd, uint32_t statPtr, CellPPU* ppu)
{
    printf("cellFsFstat(%d, 0x%08x)\n", fd, statPtr);
    if (fd == 1)
    {
        ppu->GetManager()->Write32(statPtr+0x00, CELL_FS_S_IRUSR | CELL_FS_S_IWUSR | CELL_FS_S_IXUSR |
            CELL_FS_S_IRGRP | CELL_FS_S_IWGRP | CELL_FS_S_IXGRP |
            CELL_FS_S_IROTH | CELL_FS_S_IWOTH | CELL_FS_S_IXOTH | CELL_FS_S_IFREG);
        
        ppu->GetManager()->Write32(statPtr+0x04, 0);
        ppu->GetManager()->Write32(statPtr+0x08, 0);
        ppu->GetManager()->Write32(statPtr+0x0C, 0);
        ppu->GetManager()->Write32(statPtr+0x10, 0);
        ppu->GetManager()->Write32(statPtr+0x14, 0);
        ppu->GetManager()->Write32(statPtr+0x18, 0);
        ppu->GetManager()->Write32(statPtr+0x1C, 4096);
    }
    else if (fds.find(fd) != fds.end())
    {
        printf("Error: Couldn't stat unknown fd (which exists) %d!\n", fd);
        exit(1);
    }
    else
    {
        printf("Error: Couldn't stat unknown fd %d!\n", fd);
        return CELL_ENOENT;
    }

    return CELL_OK;
}

uint32_t VFS::cellVfsFstat(uint32_t namePtr, uint32_t statPtr, CellPPU *ppu)
{
    const char* name = (char*)ppu->GetManager()->GetRawPtr(namePtr);
    
    printf("cellFsStat(\"%s\", 0x%08x)\n", name, statPtr);
    
    std::string fullPath = std::string(GetMountPoint(name));
    fullPath += name;
    
    if (!std::filesystem::exists(fullPath))
    {
        printf("Failed to stat file\n");
        return CELL_ENOENT;
    }

    printf("TODO: stat file!\n");
    exit(1);
}
