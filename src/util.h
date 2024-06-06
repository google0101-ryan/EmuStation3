#pragma once

#define BEGIN_BE_STRUCT(x) \
struct x \
{

#define BEGIN_LE_STRUCT(x) \
struct x \
{

#define BE_MEMBER_64(x) \
uint64_t x; \
void Read_##x(uint8_t *&ptr) \
{ \
    x = ((uint64_t)*ptr++) << 56; \
    x |= ((uint64_t)*ptr++) << 48; \
    x |= ((uint64_t)*ptr++) << 40; \
    x |= ((uint64_t)*ptr++) << 32; \
    x |= ((uint64_t)*ptr++) << 24; \
    x |= ((uint64_t)*ptr++) << 16; \
    x |= ((uint64_t)*ptr++) << 8; \
    x |= ((uint64_t)*ptr++); \
}

#define BE_MEMBER_32(x) \
uint32_t x; \
void Read_##x(uint8_t*& ptr) {x = (*ptr++) << 24; x |= (*ptr++) << 16; x |= (*ptr++) << 8; x |= (*ptr++);}

#define LE_MEMBER_32(x) \
uint32_t x; \
void Read_##x(uint8_t*& ptr) {x = *(uint32_t*)ptr; ptr += 4;}

#define BE_MEMBER_16(x) \
uint16_t x; \
void Read_##x(uint8_t*& ptr) {x = (*ptr++) << 8; x |= (*ptr++);}

#define LE_MEMBER_16(x) \
uint16_t x; \
void Read_##x(uint8_t*& ptr) {x = *(uint16_t*)ptr; ptr += 2;}

#define BE_MEMBER_8(x) \
uint8_t x; \
void Read_##x(uint8_t*& ptr) {(x) = *ptr++;}

#define END_BE_STRUCT() \
}

#define END_LE_STRUCT() \
}

extern bool running;