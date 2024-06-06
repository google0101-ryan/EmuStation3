#pragma once

#include <string>
#include <util.h>
#include <vector>

struct SfoEntry
{
    std::string key;
    std::string strValue;
    int32_t intValue;

    enum Type
    {
        SFO_TYPE_STRING,
        SFO_TYPE_INT
    } type;
};

class SFO
{
private:
    BEGIN_LE_STRUCT(SfoHeader)
        LE_MEMBER_32(magic);
        LE_MEMBER_32(version);
        LE_MEMBER_32(keyTableStart);
        LE_MEMBER_32(dataTableStart);
        LE_MEMBER_32(tableEntries);
    END_LE_STRUCT() header;

    BEGIN_LE_STRUCT(SfoIndexTableEntry)
        LE_MEMBER_16(keyOffset);
        LE_MEMBER_16(dataFmt);
        LE_MEMBER_32(dataLen);
        LE_MEMBER_32(dataMaxLen);
        LE_MEMBER_32(dataOffset);
        uint32_t offset;
    END_BE_STRUCT();

    std::vector<SfoIndexTableEntry> table;

    uint8_t* buf;
public:
    SFO();

    SfoEntry GetEntry(std::string key);
};

extern SFO* gSFO;