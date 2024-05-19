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

SPU* g_spus[6];

CellPPU::CellPPU(MemoryManager& manager)
    : manager(manager)
{
    InitInstructionTable();

    dump_ppu = this;
    std::atexit(atexit_func);
    std::signal(SIGINT, sigfunc);

	for (int i = 0; i < 6; i++)
	{
		if (!spus[i])
		{
			spus[i] = new SPU(&manager);
			g_spus[i] = spus[i];
		}
	}
}

void CellPPU::RunSubroutine(uint32_t addr)
{
	uint32_t oldLr = state.lr;
	uint32_t retAddr = state.pc;
	state.lr = state.pc;

	while (state.pc != retAddr)
	{
		Run();
	}

	state.lr = oldLr;
}

void CellPPU::Run()
{
    uint32_t opcode = manager.Read32(state.pc);

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

    state.pc += 4;

    if (canDisassemble)
        printf("0x%08x (0x%08lx): ", opcode, state.pc);

    if (opcode == 0x60000000)
    {
        if (canDisassemble)
            printf("nop\n");
        return;
    }
    else if (opcode == 0x44000002)
    {
		if (canDisassemble)
			printf("syscall\n");
        Syscalls::DoSyscall(this);
        return;
    }
	else if (opcode == 0x4c00012c)
	{
		if (canDisassemble)
			printf("isync\n");
		return;
	}

    if (!opcodes[(opcode >> 26) & 0x3F])
    {
        printf("Unknown opcode 0x%08x\n", opcode);
        throw std::runtime_error("Failed to execute opcode");
    }

    opcodes[(opcode >> 26) & 0x3F](opcode);

	for (int i = 0; i < 6; i++)
		spus[i]->Run();
}

void CellPPU::Dump()
{
    for (int i = 0; i < 32; i++)
        printf("r%d\t->\t0x%08lx\n", i, state.r[i]);
    printf("lr\t->\t0x%08lx\n", state.lr);
    printf("ctr\t->\t0x%08lx\n", state.ctr);
    printf("cr0\t->\t%d\n", state.cr.cr0);
    printf("cr1\t->\t%d\n", state.cr.cr1);
    printf("cr2\t->\t%d\n", state.cr.cr2);
    printf("cr3\t->\t%d\n", state.cr.cr3);
    printf("cr4\t->\t%d\n", state.cr.cr4);
    printf("cr5\t->\t%d\n", state.cr.cr5);
    printf("cr6\t->\t%d\n", state.cr.cr6);
    printf("cr7\t->\t%d\n", state.cr.cr7);
    printf("XER: [%s]\n", state.xer.ca ? "c" : ".");
    printf("pc:\t->\t0x%08lx\n", state.pc);
    for (int i = 0; i < 32; i++)
        printf("f%d\t->\t%f (0x%08lx)\n", i, state.fpr[i].f, state.fpr[i].u);
    for (int i = 0; i < 32; i++)
        printf("v%d\t->\t0x%016lx%016lx (%f, %f, %f, %f)\n", i, state.vpr[i].u64[1], state.vpr[i].u64[0], state.vpr[i].f[0], state.vpr[i].f[1], state.vpr[i].f[2], state.vpr[i].f[3]);
	spus[0]->Dump();
}
