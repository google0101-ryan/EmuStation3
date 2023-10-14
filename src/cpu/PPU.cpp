#include "PPU.h"

#include <stdio.h>
#include <string.h>

CellPPU* dump_ppu;

void atexit_func()
{
    dump_ppu->Dump();
}

CellPPU::CellPPU(uint64_t entry, uint64_t ret_addr, MemoryManager *manager)
    : manager(manager)
{
    memset(r, 0, sizeof(r));

    for (int i = 4; i<32; ++i)
        if (i != 6)
            r[i] = (i+1) * 0x10000;

    r[1] = manager->stack->Alloc(0x10000) + 0x10000;
    r[2] = manager->Read32(entry+4);
    r[0] = manager->Read32(entry);
    r[11] = 0x80;
    r[8] = entry;
    r[28] = r[4];
    r[29] = r[3];

    pc = r[0];
    lr = ret_addr;
    ctr = pc;

    printf("Stack for main thread is at 0x%08lx\n", r[1]);
    printf("Entry is at 0x%08lx, return address 0x%08lx\n", r[0], ret_addr);

    InitInstructionTable();

    dump_ppu = this;
    std::atexit(atexit_func);
}

void CellPPU::Run()
{
    uint32_t opcode = manager->Read32(pc);
    pc += 4;

    if (!opcodes[(opcode >> 26) & 0x3F])
    {
        printf("Unknown opcode 0x%08x\n", opcode);
        exit(1);
    }

    printf("0x%02x: ", (opcode >> 26) & 0x3F);

    opcodes[(opcode >> 26) & 0x3F](opcode);
}

void CellPPU::Dump()
{
    for (int i = 0; i < 32; i++)
        printf("r%d\t->\t0x%08lx\n", i, r[i]);
    printf("lr\t->\t0x%08lx\n", lr);
    printf("ctr\t->\t0x%08lx\n", ctr);
    for (int i = 0; i < 8; i++)
        printf("cr%d\t->\t%d\n", i, cr.cr[i].val);
}
