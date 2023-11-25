#pragma once

#include <stdint.h>
#include <vector>
#include <cassert>
#include <string>
#include <kernel/Memory.h>

enum
{
    GCM_VERTEX_ATTRIBUTE_POS = 0,
    GCM_VERTEX_ATTRIBUTE_WEIGHT,
    GCM_VERTEX_ATTRIBUTE_NORMAL,
    GCM_VERTEX_ATTRIBUTE_COLOR0,
    GCM_VERTEX_ATTRIBUTE_COLOR1,
    GCM_VERTEX_ATTRIBUTE_FOG,
    GCM_VERTEX_ATTRIBUTE_COLOR_INDEX,
    GCM_VERTEX_ATTRIBUTE_POINT_SIZE = GCM_VERTEX_ATTRIBUTE_COLOR_INDEX,
    GCM_VERTEX_ATTRIBUTE_EDGEFLAG,
    GCM_VERTEX_ATTRIBUTE_TEX0,
    GCM_VERTEX_ATTRIBUTE_TEX1,
    GCM_VERTEX_ATTRIBUTE_TEX2,
    GCM_VERTEX_ATTRIBUTE_TEX3,
    GCM_VERTEX_ATTRIBUTE_TEX4,
    GCM_VERTEX_ATTRIBUTE_TEX5,
    GCM_VERTEX_ATTRIBUTE_TEX6,
    GCM_VERTEX_ATTRIBUTE_TANGENT = GCM_VERTEX_ATTRIBUTE_TEX6,
    GCM_VERTEX_ATTRIBUTE_TEX7,
    GCM_VERTEX_ATTRIBUTE_BINORMAL = GCM_VERTEX_ATTRIBUTE_TEX7,
};

enum
{
    GCM_VERTEX_DATA_TYPE_F32 = 2,
    GCM_VERTEX_DATA_TYPE_U8 = 4,
};

struct VertexBinding
{
    uint8_t attribute; // Specify the attribute being set, i.e. COLOR0 or BITANGENT
    uint32_t offset; // Offset into vertex of this binding
    uint8_t stride; // Distance in bytes between bindings
    uint8_t elems; // The number of elements, i.e. a vec3 will set this to 3
    uint8_t dtype; // The type of this binding, i.e. a vec3 will be F32

    float vec[4]; // 9, 1, 2, or 3 of these may be used
    uint8_t u8[4]; // 9, 1, 2, or 3 of these may be used

    void SetF32(float* vec)
    {
        assert(dtype == GCM_VERTEX_DATA_TYPE_F32);
        for (int i = 0; i < elems; i++)
        {
            uint32_t f = __builtin_bswap32((uint32_t&)vec[i]);
            this->vec[i] = (float&)f;
        }
    }

    void SetU8(uint8_t* vec)
    {
        assert(dtype == GCM_VERTEX_DATA_TYPE_U8);
        for (int i = 0; i < elems; i++)
            this->u8[i] = vec[i];
    }
};

struct vec4
{
    float x, y, z, w;
};

class VertexShader
{
public:
    void DoVertexShader(MemoryManager* manager);

    void SetVPOffs(uint32_t offs);
    void AddInstruction(uint32_t instr);
    void SetInputMask(uint32_t mask);
    void SetResultMask(uint32_t mask);
    void SetConstOffs(uint32_t offs);
    void UploadConst(uint32_t value);
    void AddBinding(VertexBinding& binding);

    VertexBinding GetPosition()
    {
        VertexBinding* position;

        for (auto& binding : bindings)
        {
            if (binding.attribute == GCM_VERTEX_ATTRIBUTE_POS)
                position = &binding;
        }

        return *position;
    }

    VertexBinding GetColors()
    {
        VertexBinding* colors;

        for (auto& binding : bindings)
        {
            if (binding.attribute == GCM_VERTEX_ATTRIBUTE_COLOR0)
                colors = &binding;
        }

        return *colors;
    }

    VertexBinding GetBinding(int type)
    {
        VertexBinding* bind;

        for (auto& binding : bindings)
        {
            if (binding.attribute == type)
                bind = &binding;
        }

        return *bind;
    }

    void SetPosition(float pos[4]);
    void SetColor(uint8_t color[4]);

    void SetIndex(int i) {vtx_index = i;}

    vec4 GetOutputPos() {return dest[0];}
    vec4 GetOutputColor() {return dest[1];}
private:
    MemoryManager* manager;
    int vtx_index;

    vec4 read_src(int index, std::string& disasm);
    vec4 read_location(int index, std::string& disasm);
    void write_vec(vec4 value, std::string& disasm);

    uint32_t inputMask = 0, resultMask = 0;
    uint32_t progOffs = 0, progEntry = 0, constOffs = 0, constBegin = 0;
    uint8_t memory[0x10000];
    std::vector<VertexBinding> bindings;

    float in_pos[4]; // r0
    uint8_t in_color[4]; // r1

    vec4 dest[16], tmp[16];
};