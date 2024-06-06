#include "kernel/Memory.h"
#include "kernel/Modules/VFS.h"
#include "kernel/Modules/CellThread.h"
#include "loaders/Elf.h"
#include "loaders/SFO.h"
#include "cpu/PPU.h"
#include "rsx/rsx.h"

#include <exception>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <memory>
#include <string.h>

bool running = false;
extern CellPPU* dump_ppu;

void signal(int)
{
    dump_ppu->GetManager()->DumpRam();
    exit(1);
}

int main(int argc, char** argv)
{
    // All variables go out of scope when an exception is thrown
    // Allowing all destructors to run
    try
    {
        if (argc < 2 || (argc > 1 && !strcmp(argv[1], "--help")))
        {
            printf("Usage: %s <self/elf> [path to game content] [options]\n", argv[0]);
            printf("Options:\n");
            printf("\t--help: Display this message and exit\n");
            return 0;
        }

        MemoryManager manager = MemoryManager();
        
        ElfLoader loader = ElfLoader(argv[1], manager);
        auto entry = loader.LoadIntoMemory();

        VFS::InitVFS();
        if (argc > 2)
            VFS::Mount(argv[2], "/dev_bdvd");

        // Create the fallback trampoline (in case a program returns)
        uint64_t ret_addr = manager.main_mem->Alloc(4);
        manager.Write32(ret_addr, 0x44000042);

        std::signal(SIGSEGV, signal);

        CellPPU* ppu = new CellPPU(manager);
		Thread* mainThread = new Thread(entry, ret_addr, 0x10000, 0, 0, manager);
		Reschedule()->Switch(ppu);
		

        rsx->Init();
        rsx->SetMman(&manager);

        if (argc > 2)
            gSFO = new SFO();

        int cycles = 0;

        while (1)
        {
            if (cycles >= 5333333)
            {
                rsx->Present();
                cycles = 0;
            }
            cycles++;
            ppu->Run();
            if ((cycles % 10) == 0)
            {
                for (int i = 0; i < 6; i++)
                    ppu->spus[i]->Run();
            }
        }
    }
    catch (std::exception& e)
    {
        printf("***************************ERROR***************************\n");
        printf("Received error: %s        \n", e.what());
        printf("***********************************************************\n");
        
        return -1;
    }

    return 0;
}
