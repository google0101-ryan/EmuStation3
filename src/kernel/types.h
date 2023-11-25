#pragma once

#include <cstdint>

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