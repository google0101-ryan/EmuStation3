#pragma once

#include <kernel/types.h>

namespace CellHeap
{

uint32_t sys_heap_create_heap(uint32_t heap_addr, uint32_t align, uint32_t size);

}