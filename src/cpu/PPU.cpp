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
    State copy = state;

	uint32_t retAddr = state.pc;
	state.lr = state.pc;
    state.pc = addr;
    printf("Running callback at 0x%08x, 0x%08x\n", state.pc, retAddr);

	while (state.pc != retAddr)
	{
		Run();
	}

    state = copy;
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

    
    switch ((opcode >> 26) & 0x3F)
    {
    case 0x04: CellPPU::G_04(opcode); break;
    case 0x07: CellPPU::Mulli(opcode); break;
    case 0x08: CellPPU::Subfic(opcode); break;
    case 0x0A: CellPPU::Cmpli(opcode); break;
    case 0x0B: CellPPU::Cmpi(opcode); break;
    case 0x0C: CellPPU::Addic(opcode); break;
	case 0x0F: CellPPU::Addis(opcode); break;
    case 0x0E: CellPPU::Addi(opcode); break;
    case 0x10: CellPPU::BranchCond(opcode); break;
    case 0x12: CellPPU::Branch(opcode); break;
    case 0x13: CellPPU::G_13(opcode); break;
	case 0x14: CellPPU::Rlwimi(opcode); break;
    case 0x15: CellPPU::Rlwinm(opcode); break;
    case 0x17: CellPPU::Rlwnm(opcode); break;
    case 0x18: CellPPU::Ori(opcode); break;
    case 0x19: CellPPU::Oris(opcode); break;
    case 0x1A: CellPPU::Xori(opcode); break;
    case 0x1B: CellPPU::Xoris(opcode); break;
    case 0x1C: CellPPU::Andi(opcode); break;
    case 0x1E: CellPPU::G_1E(opcode); break;
    case 0x1F: CellPPU::G_1F(opcode); break;
    case 0x20: CellPPU::Lwz(opcode); break;
    case 0x21: CellPPU::Lwzu(opcode); break;
	case 0x22: CellPPU::Lbz(opcode); break;
    case 0x23: CellPPU::Lbzu(opcode); break;
    case 0x24: CellPPU::Stw(opcode); break;
    case 0x25: CellPPU::Stwu(opcode); break;
    case 0x26: CellPPU::Stb(opcode); break;
    case 0x27: CellPPU::Stbu(opcode); break;
    case 0x28: CellPPU::Lhz(opcode); break;
    case 0x29: CellPPU::Lhzu(opcode); break;
    case 0x2C: CellPPU::Sth(opcode); break;
    case 0x30: CellPPU::Lfs(opcode); break;
    case 0x32: CellPPU::Lfd(opcode); break;
    case 0x34: CellPPU::Stfs(opcode); break;
    case 0x35: CellPPU::Stfsu(opcode); break;
    case 0x36: CellPPU::Stfd(opcode); break;
    case 0x3A: CellPPU::G_3A(opcode); break;
    case 0x3B: CellPPU::G_3B(opcode); break;
    case 0x3E: CellPPU::G_3E(opcode); break;
    case 0x3F: CellPPU::G_3F(opcode); break;
    default:
        printf("Unknown opcode 0x%08x\n", opcode);
        throw std::runtime_error("Failed to execute opcode");
    }
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
	for (int i = 0; i < 6; i++)
        spus[i]->Dump();
}
