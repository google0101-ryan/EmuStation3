#pragma once

#include <cstdint>
#include <unordered_map>

extern uint32_t kernel_id;

enum : uint32_t
{
    CELL_OK =     0x00000000,
    CELL_CANCEL = 0x00000001,
    CELL_EAGAIN = 0x80010001,
    CELL_EINVAL = 0x80010002,
    CELL_ENOSYS = 0x80010003,
    CELL_ENOMEM = 0x80010004,
    CELL_EALIGN = 0x80010010,
};



struct memory_map_info
{
    uint64_t start, size;
};

extern std::unordered_map<uint32_t, memory_map_info> mapInfo;