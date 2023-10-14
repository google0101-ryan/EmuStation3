#pragma once

#include <stdint.h>
#include <vector>

#define FLAG_R 1
#define FLAG_W 2

class MemoryManager;

// To keep track 
class MemoryBlock
{
private:
    size_t len;
    uint64_t begin;

    struct Block
    {
        uint64_t addr;
        uint64_t end;
    };

    std::vector<Block> used_mem;
public:
    MemoryBlock(uint64_t start, uint64_t end);

    uint64_t Alloc(uint64_t size);
    void MarkUsed(uint64_t addr, uint64_t size);

    uint64_t GetStart() const {return begin;}
};

class MemoryManager
{
public:
    MemoryManager();
    ~MemoryManager();

    void Write8(uint64_t addr, uint8_t data, bool slow = false);
    void Write32(uint64_t addr, uint32_t data, bool slow = false);
    void Write64(uint64_t addr, uint64_t data, bool slow = false);

    uint32_t Read32(uint64_t addr, bool slow = false);
    uint64_t Read64(uint64_t addr, bool slow = false);

    void MarkMemoryRegion(uint64_t start, uint64_t end, int flags); // 1 = R, 2 = W
    uint8_t* GetRawPtr(uint64_t offset) {return base + offset;}

    MemoryBlock* stack;
    MemoryBlock* main_mem;
private:

    uint8_t* base;
    uint8_t** r_pages;
    uint8_t** w_pages;
};