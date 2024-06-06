#pragma once

#include <kernel/types.h>
#include <cpu/PPU.h>

namespace VFS
{

void InitVFS();
void Mount(const char* base, const char* mnt);
const char* GetMountPoint(const char*& path);
const char* GetbasePath();

uint32_t cellFsOpen(uint32_t namePtr, int32_t oflags, uint32_t fdPtr, CellPPU* ppu);
uint32_t cellFsSeek(uint32_t fd, uint32_t offs, uint32_t whence, uint32_t offsPtr, CellPPU* ppu);
uint32_t cellFsWrite(uint32_t fd, uint32_t bufPtr, uint32_t size, uint32_t writtenPtr, CellPPU* ppu);
uint32_t cellFsClose(uint32_t fd);
uint32_t cellFsFstat(uint32_t fd, uint32_t statPtr, CellPPU* ppu);
uint32_t cellVfsFstat(uint32_t namePtr, uint32_t statPtr, CellPPU* ppu);

}
