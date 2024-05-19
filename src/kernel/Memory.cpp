#include "Memory.h"

#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <format>
#include <cstring>
#include <fstream>
#include <rsx/rsx.h>
#include <kernel/Modules/CellGcm.h>
#include <cpu/SPU.h>

#define PAGE_SIZE (64*1024)

MemoryManager::MemoryManager()
{
    pages = new uint8_t*[0x100000000ULL / PAGE_SIZE];

    memset(pages, 0, (0x100000000ULL / PAGE_SIZE) * sizeof(uint8_t*));
    memset(pages, 0, (0x100000000ULL / PAGE_SIZE) * sizeof(uint8_t*));

    stack = new MemoryBlock(0xD0000000ULL, 0xE0000000ULL, this);
    main_mem = new MemoryBlock(0x00010000, 0x2FFF0000, this);
    prx_mem = new MemoryBlock(0x30000000, 0x40000000, this);
    RSXCmdMem = new MemoryBlock(0x40000000, 0x50000000, this);
    RSXFBMem = new MemoryBlock(0xC0000000, 0xD0000000, this);
}

MemoryManager::~MemoryManager()
{
    printf("[Mem]: Unmapping memory...\n");
    DumpRam();
    delete[] pages;
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
    //         pages[i / PAGE_SIZE] = (base + i);
    //     else
    //         pages[i / PAGE_SIZE] = nullptr;
    // }
}

void MemoryManager::MapMemory(uint64_t start, uint64_t end, uint8_t *ptr)
{
    printf("Mapping memory from 0x%08lx -> 0x%08lx\n", start, end);
    for (size_t i = 0; i < (end-start); i += PAGE_SIZE)
    {
        pages[(i+start) / PAGE_SIZE] = (ptr + i);
    }
}

void MemoryManager::SetRSXControlReg(uint32_t addr)
{
    rsx_control_addr = addr;
}

uint8_t* MemoryManager::GetRawPtr(uint64_t offs)
{
    return pages[offs / PAGE_SIZE] + (offs % PAGE_SIZE);
}

void MemoryManager::DumpRam()
{
    std::ofstream out("mem.bin");
    out.write((char*)main_mem->data, 0x10000000ULL);
    out.close();
    
    out.open("stack.bin");
    out.write((char*)stack->data, 0x10000);
    out.close();

    out.open("rsx_cmd.bin");
    out.write((char*)main_mem->data+0x00200000, 0x7000);
    out.close();

    out.open("rsxfbmem.bin");
    out.write((char*)RSXFBMem->data, 0x10000000);
    out.close();
}

void MemoryManager::Write8(uint32_t addr, uint8_t data, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!pages[page])
        {
            
            Write8(addr, data, true);
        }

        pages[page][page_offs] = data;
    }
    else
    {
        throw std::runtime_error(std::format("Error: Write {:#04x} to unknown addr {:#010x}", data, addr));
    }
}

void MemoryManager::Write16(uint32_t addr, uint16_t data, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!pages[page])
        {
            
            Write16(addr, data, true);
        }

        *(uint16_t*)&pages[page][page_offs] = __bswap_16(data);
    }
    else
    {
        throw std::runtime_error(std::format("Error: Write16 {:#04x} to unknown addr {:#010x}", data, addr));
    }
}

void MemoryManager::Write32(uint32_t addr, uint32_t data, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!pages[page])
        {
            
            Write32(addr, data, true);
			return;
        }

        *(uint32_t*)&pages[page][page_offs] = __bswap_32(data);
    }
    else
    {
		if (addr >= 0xE0000000 && addr < 0xE0600000)
		{
			auto spu = (addr >> 20) & 0xF;
			assert(spu < 6);

			if (addr & 0xF0000)
			{
				printf("Write to problem storage register 0x%04x on SPU %d\n", addr & 0xFFFF, spu);
				g_spus[spu]->WriteProblemStorage(addr & 0xFFFF, data);
				return;
			}
			else
			{
				printf("Write to SPU %d local store\n", spu);
			}
		}

        throw std::runtime_error(std::format("Error: Write32 {:#04x} to unknown addr {:#010x}", data, addr));
    }
    
    if (rsx_control_addr && addr == rsx_control_addr)
    {
        uint32_t get = Read32(addr+4);
        if (data != get)
            rsx->DoCommands(GetRawPtr(CellGcm::GetIOAddres() + get), data - get);
        return;
    }
}

void MemoryManager::Write64(uint32_t addr, uint64_t data, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!pages[page])
        {
            
            Write64(addr, data, true);
        }

        *(uint64_t*)&pages[page][page_offs] = __bswap_64(data);
    }
    else
    {
        throw std::runtime_error(std::format("Error: Write64 {:#04x} to unknown addr {:#010x}", data, addr));
    }
}

uint8_t MemoryManager::Read8(uint32_t addr, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!pages[page])
        {
            
            return Read8(addr, true);
        }

        return pages[page][page_offs];
    }
    else
    {
        throw std::runtime_error(std::format("Error: Read8 from unknown addr {:#010x}", addr));
    }
}

uint16_t MemoryManager::Read16(uint32_t addr, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!pages[page])
        {
            
            return Read16(addr, true);
        }

        return __bswap_16(*(uint16_t*)&pages[page][page_offs]);
    }
    else
    {
        throw std::runtime_error(std::format("Error: Read16 from unknown addr {:#010x}", addr));
    }
}

uint32_t MemoryManager::Read32(uint32_t addr, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!pages[page])
        {
            
            return Read32(addr, true);
        }

        return __bswap_32(*(uint32_t*)&pages[page][page_offs]);
    }
    else
    {
		if (addr >= 0xE0000000 && addr < 0xE0600000)
		{
			auto spu = (addr >> 20) & 0xF;
			assert(spu < 6);

			if (addr & 0xF0000)
			{
				printf("Read from problem storage register 0x%04x on SPU %d\n", addr & 0xFFFF, spu);
				return g_spus[spu]->ReadProblemStorage(addr & 0xFFFF);
			}
			else
			{
				printf("Read from SPU %d local store\n", spu);
			}
		}

        throw std::runtime_error(std::format("Error: Read32 from unknown addr {:#010x}", addr));
    }
}

uint64_t MemoryManager::Read64(uint32_t addr, bool slow)
{
    if (!slow)
    {
        auto page = addr / PAGE_SIZE;
        auto page_offs = addr % PAGE_SIZE;

        if (!pages[page])
        {
            
            return Read64(addr, true);
        }

        return __bswap_64(*(uint64_t*)&pages[page][page_offs]);
    }
    else
    {
        printf("Unmapped addr 0x%08lx!\n", addr);
        throw std::runtime_error(std::format("Error: Read64 from unknown addr {:#010x}", addr).c_str());
    }
}

MemoryBlock::MemoryBlock(uint64_t start, uint64_t end, MemoryManager* manager, bool map)
{
    begin = start;
    len = (end - start);
    data = new uint8_t[len];
    if (map)
        manager->MapMemory(start, end, data);
}

uint64_t MemoryBlock::Alloc(uint64_t size)
{
    for (uint32_t addr = begin; addr < (begin+len) - size;)
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

void MemoryBlock::MarkUsed(uint32_t addr, uint64_t size)
{
    Block block;
    block.addr = addr;
    block.end = addr+size;
    used_mem.push_back(block);
}
