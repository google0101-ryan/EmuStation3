#include "kernel/Memory.h"
#include "kernel/Modules/VFS.h"
#include "loaders/Elf.h"
#include "cpu/PPU.h"
#include "rsx/rsx.h"

#include <exception>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string.h>

bool running = false;

int main(int argc, char** argv)
{
    // All variables go out of scope when an exception is thrown
    // Allowing all destructors to run
    try
    {
        if (argc != 2 || (argc > 1 && !strcmp(argv[1], "--help")))
        {
            printf("Usage: %s <self/elf> [options]\n", argv[0]);
            printf("Options:\n");
            printf("\t--help: Display this message and exit\n");
            return 0;
        }

        MemoryManager manager = MemoryManager();
        
        ElfLoader loader = ElfLoader(argv[1], manager);
        auto entry = loader.LoadIntoMemory();

        VFS::InitVFS();

        // Create the fallback trampoline (in case a program returns)
        uint64_t ret_addr = manager.main_mem->Alloc(4);
        manager.Write32(ret_addr, 0x44000042);

        CellPPU* ppu = new CellPPU(entry, ret_addr, manager);

        rsx->Init();
        rsx->SetMman(&manager);

        int cycles = 0;

        while (1)
        {
            if (cycles >= 533333)
            {
                rsx->Present();
                cycles = 0;
            }
            cycles++;
            ppu->Run();
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