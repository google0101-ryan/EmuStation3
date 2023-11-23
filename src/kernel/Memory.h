#pragma once

#include <stdint.h>
#include <vector>
#include <stddef.h>

#define FLAG_R 1
#define FLAG_W 2

class MemoryManager;

// To keep track 
class MemoryBlock
{
    friend class MemoryManager;
private:
    uint8_t* data;
    size_t len;
    uint64_t begin;

    struct Block
    {
        uint64_t addr;
        uint64_t end;
    };

    std::vector<Block> used_mem;
public:
    MemoryBlock(uint64_t start, uint64_t end, MemoryManager* manager, bool map = true);

    uint64_t Alloc(uint64_t size);
    void MarkUsed(uint64_t addr, uint64_t size);

    uint64_t GetStart() const {return begin;}
    uint64_t GetSize() const {return len;}
    uint64_t GetAvailable() const
    {
        uint64_t used = 0;
        for (auto& b : used_mem)
            used += b.end - b.addr;
        return GetSize() - used;
    }
};

class MemoryManager
{
public:
    MemoryManager();
    ~MemoryManager();

    void Write8(uint64_t addr, uint8_t data, bool slow = false);
    void Write16(uint64_t addr, uint16_t data, bool slow = false);
    void Write32(uint64_t addr, uint32_t data, bool slow = false);
    void Write64(uint64_t addr, uint64_t data, bool slow = false);

    uint8_t Read8(uint64_t addr, bool slow = false);
    uint16_t Read16(uint64_t addr, bool slow = false);
    uint32_t Read32(uint64_t addr, bool slow = false);
    uint64_t Read64(uint64_t addr, bool slow = false);

    void MarkMemoryRegion(uint64_t start, uint64_t end, int flags); // 1 = R, 2 = W
    uint8_t* GetRawPtr(uint64_t offset);

    void MapMemory(uint64_t start, uint64_t end, uint8_t* ptr);

    void DumpRam();

    MemoryBlock* stack;
    MemoryBlock* main_mem;
    MemoryBlock* prx_mem;
    MemoryBlock* RSXCmdMem;
    MemoryBlock* RSXFBMem;
private:
    uint8_t** pages;
};