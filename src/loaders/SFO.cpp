#include "SFO.h"
#include "kernel/Modules/VFS.h"
#include <fstream>
#include <string.h>

static const char* SFO_PATH = "/dev_bdvd/PS3_GAME/PARAM.SFO";

SFO* gSFO;

SFO::SFO()
{
    auto fullPath = std::string(VFS::GetMountPoint(SFO_PATH));
    fullPath += SFO_PATH;

    printf("Opening file \"%s\"\n", fullPath.c_str());

    std::ifstream file(fullPath, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        printf("Failed to open file!\n");
        exit(1);
    }

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    buf = new uint8_t[size];

    file.read((char*)buf, size);
    file.close();

    uint8_t* origBuf = buf;

    header.Read_magic(buf);
    header.Read_version(buf);
    header.Read_keyTableStart(buf);
    header.Read_dataTableStart(buf);
    header.Read_tableEntries(buf);

    printf("Key table starts at 0x%08x, data table at 0x%08x, %d entries\n", header.keyTableStart, header.dataTableStart, header.tableEntries);

    uint32_t offset = header.dataTableStart;

    for (int i = 0; i < header.tableEntries; i++)
    {
        SfoIndexTableEntry entry;
        entry.Read_keyOffset(buf);
        entry.Read_dataFmt(buf);
        entry.Read_dataLen(buf);
        entry.Read_dataMaxLen(buf);
        entry.Read_dataOffset(buf);
        entry.offset = offset;
        offset += entry.dataMaxLen;

        table.push_back(entry);
    }

    buf = origBuf;

    for (auto& entry: table)
        GetEntry(std::string((const char*)&buf[header.keyTableStart + entry.keyOffset]));
}

SfoEntry SFO::GetEntry(std::string key)
{
    SfoEntry ret;
    ret.key = key;

    SfoIndexTableEntry* found = NULL;
    for (auto& entry : table)
    {
        uint32_t offset = header.keyTableStart + entry.keyOffset;
        if (!strcmp((const char*)&buf[offset], key.c_str()))
        {
            found = &entry;
            break;
        }
    }

    if (!found)
    {
        printf("Failed to get PARAM.SFO key \"%s\"\n", key.c_str());
        exit(1);
    }

    ret.type = (found->dataFmt == 0x0404) ? SfoEntry::SFO_TYPE_INT : SfoEntry::SFO_TYPE_STRING;

    switch (found->dataFmt)
    {
    case 0x204:
    {
        uint32_t offs = header.dataTableStart + found->dataOffset;
        while (buf[offs] != 0)
        {
            ret.strValue.push_back(buf[offs++]);
        }
        
        printf("Found key \"%s\" with value \"%s\"\n", key.c_str(), ret.strValue.c_str());
        break;
    }
    case 0x404:
    {
        ret.intValue = *(uint32_t*)&buf[header.dataTableStart + found->dataOffset];
        printf("Found key \"%s\" with value 0x%08x (0x%08x)\n", key.c_str(), ret.intValue, found->dataOffset);
        break;
    }
    default:
        printf("Unknown fmt 0x%x\n", found->dataFmt);
        exit(1);
    }

    return ret;
}
