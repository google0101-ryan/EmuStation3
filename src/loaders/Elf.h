#pragma once

class MemoryManager;

#include <cstdint>
#include <memory>
#include <string>
#include <fstream>
#include <cstdint>
#include <vector>
#include <unordered_map>

#include "util.h"

// Module calls work like this on PS3:
// When being loaded, the module NID table is overwritten with pointers
// To module functions
// Inside the module function stub, it loads r0 with the pointer, 
// moves that into CTR, and then does BCTRL to jump to it
// If the NID table isn't overwritten, then CTR will contain the NID on BCTRL
// Which means we can compare BCTRL's destination with the NID list to detect module calls
// Boom.

extern std::unordered_map<uint32_t, uint32_t> syscall_nids;

class ElfLoader
{
private:
    MemoryManager& mman;

    uint8_t* buf;
    uint8_t* cur_p;
    size_t len;

    BEGIN_BE_STRUCT(ElfHeader)
        uint8_t e_ident[16];
        BE_MEMBER_16(e_type);
        BE_MEMBER_16(e_machine);
        BE_MEMBER_32(e_version);
        BE_MEMBER_64(e_entry);
        BE_MEMBER_64(e_phoff);
        BE_MEMBER_64(e_shoff);
        BE_MEMBER_32(e_flags);
        BE_MEMBER_16(e_ehsize);
        BE_MEMBER_16(e_phentsize);
        BE_MEMBER_16(e_phnum);
        BE_MEMBER_16(e_shentsize);
        BE_MEMBER_16(e_shnum);
        BE_MEMBER_16(e_shstrndx);
    END_BE_STRUCT() hdr;

    BEGIN_BE_STRUCT(ElfPhdr)
        BE_MEMBER_32(p_type);
        BE_MEMBER_32(p_flags);
        BE_MEMBER_64(p_offset);
        BE_MEMBER_64(p_vaddr);
        BE_MEMBER_64(p_paddr);
        BE_MEMBER_64(p_filesz);
        BE_MEMBER_64(p_memsz);
        BE_MEMBER_64(p_align);
    END_BE_STRUCT();

    BEGIN_BE_STRUCT(ProcParamInfo)
        BE_MEMBER_32(size);
        BE_MEMBER_32(magic);
        BE_MEMBER_32(version);
        BE_MEMBER_32(sdk_version);
        BE_MEMBER_32(primary_prio);
        BE_MEMBER_32(primary_stacksize);
        BE_MEMBER_32(malloc_pagesize);
        BE_MEMBER_32(ppc_seg);
    END_BE_STRUCT();

    BEGIN_BE_STRUCT(ProcPrxParam)
        BE_MEMBER_32(size);
        BE_MEMBER_32(magic);
        BE_MEMBER_32(version);
        BE_MEMBER_32(pad0);
        BE_MEMBER_32(libentstart);
        BE_MEMBER_32(libentend);
        BE_MEMBER_32(libstubstart);
        BE_MEMBER_32(libstubend);
        BE_MEMBER_16(ver);
        BE_MEMBER_16(pad1);
        BE_MEMBER_32(pad2);
    END_BE_STRUCT();

    BEGIN_BE_STRUCT(StubHeader)
        BE_MEMBER_8(s_size);
        BE_MEMBER_8(s_unk0);
        BE_MEMBER_16(s_version);
        BE_MEMBER_16(s_unk1);
        BE_MEMBER_16(s_imports);
        BE_MEMBER_32(s_unk2);
        BE_MEMBER_32(s_unk3);
        BE_MEMBER_32(s_modulename);
        BE_MEMBER_32(s_nid);
        BE_MEMBER_32(s_text);
        BE_MEMBER_32(s_unk4);
        BE_MEMBER_32(s_unk5);
        BE_MEMBER_32(s_unk6);
        BE_MEMBER_32(s_unk7);
    END_BE_STRUCT();

    std::vector<ElfPhdr*> phdrs;

    uint16_t Read16(uint8_t*& ptr);
    uint32_t Read32(uint8_t*& ptr);
    uint64_t Read64(uint8_t*& ptr);
public:
    ElfLoader(std::string fname, MemoryManager& mman);
    ~ElfLoader();

    uint64_t LoadIntoMemory();
};