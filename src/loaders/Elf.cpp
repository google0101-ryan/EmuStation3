#include "Elf.h"
#include "kernel/Memory.h"

#include <cassert>
#include <cstring>
#include <unordered_map>

std::unordered_map<uint32_t, uint32_t> syscall_nids;

#define SCE_MAGIC (('S' << 16) | ('C' << 8) | 'E')
#define ELF_MAGIC (('E' << 16) | ('L' << 8) | 'F')

uint16_t ElfLoader::Read16(uint8_t*& ptr)
{
    uint16_t val;
    for (int i = 2; i >= 0; i--)
        val |= (*ptr++) << (i*8);
    return val;
}

uint32_t ElfLoader::Read32(uint8_t *&ptr)
{
    uint32_t val;
    for (int i = 4; i >= 0; i--)
        val |= (*ptr++) << (i*8);
    return val;
}

uint64_t ElfLoader::Read64(uint8_t *&ptr)
{
    uint64_t val;
    for (int i = 8; i >= 0; i--)
        val |= (*ptr++) << (i*8);
    return val;
}

ElfLoader::ElfLoader(std::string fname, MemoryManager &mman)
: mman(mman)
{
    std::ifstream file(fname, std::ios::binary | std::ios::ate);

    len = file.tellg();
    file.seekg(0, std::ios::beg);

    buf = new uint8_t[len];
    file.read((char*)buf, len);

    file.close();

    cur_p = buf;

    // Parse the ELF file and load it into the PS3's memory
    if (buf[0] == 'S' && buf[1] == 'C' && buf[2] == 'E')
    {
        printf("[ELF]: SELF file, skipping header\n");
        cur_p += 0x90; // Skip SCE header
        buf += 0x90;
    }

    if (cur_p[0] != '\x7f' || cur_p[1] != 'E' || cur_p[2] != 'L' || cur_p[3] != 'F')
        throw std::runtime_error("Couldn't process file: invalid magic");

    printf("[ELF]: Reading header\n");
    
    for (int i = 0; i < 16; i++)
        hdr.e_ident[i] = *cur_p++;
    
    hdr.Read_e_type(cur_p);

    if (hdr.e_type != 2)
        throw std::runtime_error("Couldn't process file: invalid type");
    
    hdr.Read_e_machine(cur_p);

    if (hdr.e_machine != 21)
        throw std::runtime_error("Couldn't process file: invalid machine");
    
    hdr.Read_e_version(cur_p);

    if (hdr.e_version != 1)
        throw std::runtime_error("Couldn't process file: invalid version");
    
    hdr.Read_e_entry(cur_p);

    printf("Entry point is 0x%08lx\n", hdr.e_entry);

    assert(hdr.e_entry < UINT32_MAX && "entrypoint is out of reach!");

    hdr.Read_e_phoff(cur_p);
    hdr.Read_e_shoff(cur_p);
    hdr.Read_e_flags(cur_p);
    hdr.Read_e_ehsize(cur_p);
    hdr.Read_e_phentsize(cur_p);
    hdr.Read_e_phnum(cur_p);
    hdr.Read_e_shentsize(cur_p);
    hdr.Read_e_shnum(cur_p);
    hdr.Read_e_shstrndx(cur_p);

    printf("Program headers start at offset 0x%08lx\n", hdr.e_phoff);
    // Load Phdrs from buf
    cur_p = buf + hdr.e_phoff;

    for (size_t i = 0; i < hdr.e_phnum; i++)
    {
        auto phdr = new ElfPhdr;

        phdr->Read_p_type(cur_p);
        phdr->Read_p_flags(cur_p);
        phdr->Read_p_offset(cur_p);
        phdr->Read_p_vaddr(cur_p);
        phdr->Read_p_paddr(cur_p);
        phdr->Read_p_filesz(cur_p);
        phdr->Read_p_memsz(cur_p);
        phdr->Read_p_align(cur_p);

        printf("Found phdr of type 0x%08x\n", phdr->p_type);
        printf("\tVaddr: 0x%08lx\n", phdr->p_vaddr);
        printf("\tMemsz: 0x%08lx\n", phdr->p_memsz);
        printf("\toffset: 0x%08lx\n", phdr->p_offset);

        phdrs.push_back(phdr);
    }
}

ElfLoader::~ElfLoader()
{
    printf("[ELF]: Freeing file mem...\n");

    for (size_t i = 0; i < phdrs.size(); i++)
    {
        if (phdrs[i])
            delete phdrs[i];
    }

    phdrs.clear();
}

uint64_t ElfLoader::LoadIntoMemory()
{
    for (auto& phdr : phdrs)
    {
        cur_p = buf + phdr->p_offset;

        if (phdr->p_type == 1)
        {
            if (phdr->p_memsz == 0)
                continue;

            auto ptr = mman.GetRawPtr(phdr->p_vaddr);
            
            printf("Loading segment 0x%08lx->0x%08lx (0x%08lx %p)\n", phdr->p_offset, phdr->p_vaddr, phdr->p_memsz, ptr);

            for (int i = 0; i < phdr->p_filesz; i++)
                mman.Write8(phdr->p_vaddr+i, *cur_p++);

            if (phdr->p_memsz > phdr->p_filesz)
                for (int i = phdr->p_filesz; i < phdr->p_memsz; i++)
                    mman.Write8(phdr->p_vaddr+i, 0);

            printf("Region is ");

            int flags;
            if (phdr->p_flags & 4)
            {
                printf("R");
                flags |= FLAG_R;
            }
            if (phdr->p_flags & 2)
            {
                printf("W");
                flags |= FLAG_W;
            }

            if (!flags)
                printf("None (Weird)");
            
            printf("\n");
            
            mman.main_mem->MarkUsed(phdr->p_vaddr, phdr->p_memsz);
        }
        else if (phdr->p_type == 0x60000001)
        {
            if (phdr->p_filesz == 0) continue;

            auto loc = mman.GetRawPtr(phdr->p_vaddr);

            ProcParamInfo info;
            info.Read_size(loc);
            info.Read_magic(loc);
            info.Read_version(loc);
            info.Read_sdk_version(loc);
            info.Read_primary_prio(loc);
            info.Read_primary_stacksize(loc);
            info.Read_malloc_pagesize(loc);
            info.Read_ppc_seg(loc);

            if (info.magic != 0x13bcc5f6)
            {
                printf("ERROR: Bad magic for process param: 0x%08x\n", info.magic);
                throw std::runtime_error("Bad proc parm magic");
            }
            else
            {
                printf("*** sdk version 0x%x\n", info.sdk_version);
                printf("*** primary prio %d\n", info.primary_prio);
                printf("*** primary stacksize 0x%x\n", info.primary_stacksize);
                printf("*** malloc pagesize 0x%x\n", info.malloc_pagesize);
                printf("*** ppc seg 0x%x\n", info.ppc_seg);
            }
        }
        else if (phdr->p_type == 0x60000002)
        {
            if (!phdr->p_filesz) continue;

            printf("Found prx param at 0x%08lx\n", phdr->p_vaddr);

            auto loc = mman.GetRawPtr(phdr->p_vaddr);
            ProcPrxParam prx;

            prx.Read_size(loc);
            prx.Read_magic(loc);
            prx.Read_version(loc);
            prx.Read_pad0(loc);
            prx.Read_libentstart(loc);
            prx.Read_libentend(loc);
            prx.Read_libstubstart(loc);
            prx.Read_libstubend(loc);
            prx.Read_ver(loc);
            prx.Read_pad1(loc);
            prx.Read_pad2(loc);

            if (prx.magic != 0x1b434cec)
            {
                printf("ERROR: Bad magic for prx param: 0x%08x\n", prx.magic);
                throw std::runtime_error("Bad prx parm magic");
            }

            printf("*** size: 0x%x\n", prx.size);
			printf("*** magic: 0x%x\n", prx.magic);
			printf("*** version: 0x%x\n", prx.version);
			printf("*** libentstart: 0x%x\n", prx.libentstart);
			printf("*** libentend: 0x%x\n", prx.libentend);
			printf("*** libstubstart: 0x%x\n", prx.libstubstart);
			printf("*** libstubend: 0x%x\n", prx.libstubend);
			printf("*** ver: 0x%x\n", prx.ver);

            for (uint32_t i = prx.libstubstart; i < prx.libstubend; i += sizeof(StubHeader))
            {
                StubHeader stubHdr;

                auto loc = mman.GetRawPtr(i);

                stubHdr.Read_s_size(loc);
                stubHdr.Read_s_unk0(loc);
                stubHdr.Read_s_version(loc);
                stubHdr.Read_s_unk1(loc);
                stubHdr.Read_s_imports(loc);
                stubHdr.Read_s_unk2(loc);
                stubHdr.Read_s_unk3(loc);
                stubHdr.Read_s_modulename(loc);
                stubHdr.Read_s_nid(loc);
                stubHdr.Read_s_text(loc);
                stubHdr.Read_s_unk4(loc);
                stubHdr.Read_s_unk5(loc);
                stubHdr.Read_s_unk6(loc);
                stubHdr.Read_s_unk7(loc);

                std::string modName;

                auto modNameLoc = mman.GetRawPtr(stubHdr.s_modulename);
                while (*modNameLoc)
                    modName += *modNameLoc++;
                
                printf("Loading module %s, %d imports (0x%x)\n", modName.c_str(), stubHdr.s_imports, stubHdr.s_nid);

                for (int i = 0; i < stubHdr.s_imports; i++)
                {
                    uint32_t nid = mman.Read32(stubHdr.s_nid + i * 4);
                    uint32_t addr = mman.Read32(stubHdr.s_text + i * 4);

                    printf("\tLoading import 0x%08x at 0x%08x\n", nid, addr);

                    syscall_nids[addr] = nid;
                }
            }
        }
        else
        {
            printf("Unknown segment header type 0x%08x\n", phdr->p_type);
        }
    }

    return hdr.e_entry;
}
