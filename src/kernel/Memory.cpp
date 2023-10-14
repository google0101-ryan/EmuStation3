#include "Memory.h"

#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <format>
#include <cstring>

uint8_t* base;

#define PAGE_SIZE (64*1024)

MemoryManager::MemoryManager()
{
    r_pages = new uint8_t*[0x2'0000'0000 / PAGE_SIZE];
    w_pages = new uint8_t*[0x2'0000'0000 / PAGE_SIZE];

    base = (uint8_t*)mmap64((void*)0x2'0000'0000, 0x2'0000'0000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

    if (base == (uint8_t*)MAP_FAILED)
    {
        printf("Failed to mmap 8GB of memory! %s\n", strerror(errno));
        exit(1);
    }

    for (size_t i = 0; i < 0xE000'0000ULL; i += PAGE_SIZE)
    {
        r_pages[i / PAGE_SIZE] = (base + i);
        w_pages[i / PAGE_SIZE] = (base + i);
    }

    for (size_t i = 0xE000'0000ULL; i < 0xE060'0000ULL; i += PAGE_SIZE)
    {
        r_pages[i / PAGE_SIZE] = nullptr; // SPU memory is slow
        w_pages[i / PAGE_SIZE] = nullptr; // SPU memory is slow
    }

    for (size_t i = 0xE060'0000ULL; i < 0x2'0000'0000; i += PAGE_SIZE)
    {
        r_pages[i / PAGE_SIZE] = (base + i);
        w_pages[i / PAGE_SIZE] = (base + i);
    }

    stack = new MemoryBlock(0xD0000000ULL, 0x100000000ULL);
    main_mem = new MemoryBlock(0x00010000, 0x2FFF0000);
}

MemoryManager::~MemoryManager()
{
    printf("[Mem]: Unmapping memory...\n");
    munmap(base, 0x2'0000'0000);
    delete[] r_pages;
}

void MemoryManager::MarkMemoryRegion(uint64_t, uint64_t, int)
{
    // for (uint64_t i = start; i < end; i += PAGE_SIZE)
    // {
    //     if (flags & FLAG_R)
    //         r_pages[i / PAGE_SIZE] = (base + i);
    //     else
    //         r_pages[i / PAGE_SIZE] = nullptr;
    //     if (flags & FLAG_W)
    //         w_pages[i / PAGE_SIZE] = (base + i);
    //     else
    //         w_pages[i / PAGE_SIZE] = nullptr;
    // }
}

void MemoryManager::Write8(uint64_t addr, uint8_t data, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!w_pages[page])
        {
            printf("No memory mapped for 0x%08lx, trying slow I/O\n", addr);
            Write8(addr, data, true);
        }

        w_pages[page][page_offs] = data;
    }
    else
    {
        throw std::runtime_error(std::format("Error: Write {:#04x} to unknown addr {:#010x}", data, addr));
    }
}

void MemoryManager::Write32(uint64_t addr, uint32_t data, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!w_pages[page])
        {
            printf("No memory mapped for 0x%08lx, trying slow I/O\n", addr);
            Write32(addr, data, true);
        }

        *(uint32_t*)&w_pages[page][page_offs] = __bswap_32(data);
    }
    else
    {
        throw std::runtime_error(std::format("Error: Write64 {:#04x} to unknown addr {:#010x}", data, addr));
    }
}

void MemoryManager::Write64(uint64_t addr, uint64_t data, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!w_pages[page])
        {
            printf("No memory mapped for 0x%08lx, trying slow I/O\n", addr);
            Write64(addr, data, true);
        }

        *(uint64_t*)&w_pages[page][page_offs] = __bswap_64(data);
    }
    else
    {
        throw std::runtime_error(std::format("Error: Write64 {:#04x} to unknown addr {:#010x}", data, addr));
    }
}

uint32_t MemoryManager::Read32(uint64_t addr, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!r_pages[page])
        {
            printf("No memory mapped for 0x%08lx, trying slow I/O\n", addr);
            return Read32(addr, true);
        }

        return __bswap_32(*(uint32_t*)&r_pages[page][page_offs]);
    }
    else
    {
        throw std::runtime_error(std::format("Error: Read32 from unknown addr {:#010x}", addr));
    }
}

uint64_t MemoryManager::Read64(uint64_t addr, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!r_pages[page])
        {
            printf("No memory mapped for 0x%08lx, trying slow I/O\n", addr);
            return Read64(addr, true);
        }

        return __bswap_64(*(uint64_t*)&r_pages[page][page_offs]);
    }
    else
    {
        throw std::runtime_error(std::format("Error: Read64 from unknown addr {:#010x}", addr));
    }
}

MemoryBlock::MemoryBlock(uint64_t start, uint64_t end)
{
    begin = start;
    len = (end - start);
}

uint64_t MemoryBlock::Alloc(uint64_t size)
{
    for (uint64_t addr = begin; addr < (begin+len) - size;)
    {
        bool is_good_addr = true;

        for (uint32_t i = 0; i < used_mem.size(); i++)
        {
            if ((addr >= used_mem[i].addr && addr < used_mem[i].end)
                || (used_mem[i].addr >= addr && used_mem[i].addr < addr + size))
            {
                is_good_addr = false;
                addr = used_mem[i].end;
                break;
            }
        }

        if (!is_good_addr) continue;

        MarkUsed(addr, size);

        return addr;
    }

    throw std::runtime_error("OOM Error!");
}

void MemoryBlock::MarkUsed(uint64_t addr, uint64_t size)
{
    Block block;
    block.addr = addr;
    block.end = addr+size;
    used_mem.push_back(block);
}
