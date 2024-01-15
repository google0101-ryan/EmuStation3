#include "PPU.h"
#include <kernel/Syscall.h>

#include <stdio.h>
#include <string.h>
#include <vector>
#include <csignal>
#include <stdexcept>

CellPPU* dump_ppu;

void atexit_func()
{
    dump_ppu->Dump();
}

void sigfunc(int sig)
{
    exit(1);
}

CellPPU::CellPPU(uint64_t entry, uint64_t ret_addr, MemoryManager& manager)
    : manager(manager)
{
    memset(r, 0, sizeof(r));

    // for (int i = 4; i<32; ++i)
    //     if (i != 6)
    //         r[i] = (i+1) * 0x10000;

    sp = manager.stack->Alloc(0x10000) + 0x10000;
    r[1] = sp;
    r[2] = manager.Read32(entry+4);

    pc = manager.Read32(entry);
    lr = ret_addr;
    ctr = pc;
    cr.CR = 0x22000082;

    uint64_t prx_mem = manager.prx_mem->Alloc(0x10000);
    manager.Write64(prx_mem, 0xDEADBEEFABADCAFE);

    sp -= 0x10;

    r[3] = 1;
    r[4] = sp;
    r[5] = sp + 0x10;

    const char* arg = "TMP.ELF\0\0";

    sp -= 8;
    for (int i = 0; i < 8; i++)
        manager.Write8(sp+i, arg[i]);

    r[0] = pc;
    r[8] = entry;
    r[11] = 0x80;
    r[12] = 0x100000;
    r[13] = prx_mem + 0x7060;
    r[28] = r[4];
    r[29] = r[3];
    r[31] = r[5];

    printf("Stack for main thread is at 0x%08lx\n", r[1]);
    printf("Entry is at 0x%08lx, return address 0x%08lx\n", r[0], ret_addr);

    InitInstructionTable();

    dump_ppu = this;
    std::atexit(atexit_func);
    std::signal(SIGINT, sigfunc);
}

void CellPPU::Run()
{
    uint32_t opcode = manager.Read32(pc);

    // if (pc == 0x16264)
    // {
    //     printf("cellGcmFinish(0x%08x, %d)\n", r[3], r[4]);
    // }

    // if (pc == 0x162E4)
    // {
    //     printf("cellGcmSetReferenceCommand(0x%08x, %d)\n", r[3], r[4]);
    //     printf("0x%08x\n", manager.Read32(r[3] + 8));
    //     canDisassemble = true;
    // }

    // if (pc == 0x16348)
    // {
    //     canDisassemble = false;
    // }

    pc += 4;

    if (canDisassemble)
        printf("0x%08x (0x%08lx): ", opcode, pc);

    if (opcode == 0x60000000)
    {
        if (canDisassemble)
            printf("nop\n");
        return;
    }
    else if (opcode == 0x44000002)
    {
        printf("syscall\n");
        Syscalls::DoSyscall(this);
        return;
    }

    if (!opcodes[(opcode >> 26) & 0x3F])
    {
        printf("Unknown opcode 0x%08x\n", opcode);
        throw std::runtime_error("Failed to execute opcode");
    }

    opcodes[(opcode >> 26) & 0x3F](opcode);
}

void CellPPU::Dump()
{
    for (int i = 0; i < 32; i++)
        printf("r%d\t->\t0x%08lx\n", i, r[i]);
    printf("lr\t->\t0x%08lx\n", lr);
    printf("ctr\t->\t0x%08lx\n", ctr);
    printf("cr0\t->\t%d\n", cr.cr0);
    printf("cr1\t->\t%d\n", cr.cr1);
    printf("cr2\t->\t%d\n", cr.cr2);
    printf("cr3\t->\t%d\n", cr.cr3);
    printf("cr4\t->\t%d\n", cr.cr4);
    printf("cr5\t->\t%d\n", cr.cr5);
    printf("cr6\t->\t%d\n", cr.cr6);
    printf("cr7\t->\t%d\n", cr.cr7);
    printf("XER: [%s]\n", xer.ca ? "c" : ".");
    printf("pc:\t->\t0x%08lx\n", pc);
    for (int i = 0; i < 32; i++)
        printf("f%d\t->\t%f (0x%08lx)\n", i, fpr[i].f, fpr[i].u);
    for (int i = 0; i < 32; i++)
        printf("v%d\t->\t0x%08lx%08lx\n", i, vpr[i].u64[1], vpr[i].u64[0]);
}
