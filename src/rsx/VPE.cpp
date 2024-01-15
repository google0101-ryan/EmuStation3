#include "VPE.h"
#include <stdio.h>
#include <stdlib.h>
#include <bitset>
#include <math.h>
#include <algorithm>
#include <array>

// #define printf(x, ...) 0

int bitExtract(int number, int p, int k)
{
    int mask = 0;
    for (int i = 0; i < k; i++)
        mask |= (1 << i);
    return (number >> p) & mask;
}

#define GET_BITS bitExtract

union D0
{
    uint32_t hex;

    struct
    {
        uint32_t addr_swz : 2;
        uint32_t mask_w : 2;
        uint32_t mask_z : 2;
        uint32_t mask_y : 2;
        uint32_t mask_x : 2;
        uint32_t cond : 3;
        uint32_t cond_test_enable : 1;
        uint32_t cond_update_enable_0 : 1;
        uint32_t dst_tmp : 6;
        uint32_t src0_abs : 1;
        uint32_t src1_abs : 1;
        uint32_t src2_abs : 1;
        uint32_t addr_reg_sel_1 : 1;
        uint32_t cond_reg_sel_1 : 1;
        uint32_t saturate : 1;
        uint32_t index_input : 1;
        uint32_t : 1;
        uint32_t cond_update_enable_1 : 1;
        uint32_t vec_result : 1;
        uint32_t : 1;
    };
};

union D1
{
    uint32_t hex;

    struct
    {
        uint32_t src0h : 8;
        uint32_t input_src : 4;
        uint32_t const_src : 10;
        uint32_t vop : 5;
        uint32_t sop : 5;
    };
};

union D2
{
    uint32_t hex;

    struct
    {
        uint32_t src2h : 6;
        uint32_t src1 : 17;
        uint32_t src0l : 9;
    };
    struct
    {
        uint32_t iaddrh : 6;
        uint32_t : 26;
    };
    struct
    {
        uint32_t : 8;
        uint32_t tex_num : 2;
        uint32_t : 22;
    };
};

union D3
{
    uint32_t hex;

    struct
    {
        uint32_t end : 1;
        uint32_t index_const : 1;
        uint32_t dst : 5;
        uint32_t sca_dst_tmp : 6;
        uint32_t vec_writemask_w : 1;
        uint32_t vec_writemask_z : 1;
        uint32_t vec_writemask_y : 1;
        uint32_t vec_writemask_x : 1;
        uint32_t sca_writemask_w : 1;
        uint32_t sca_writemask_z : 1;
        uint32_t sca_writemask_y : 1;
        uint32_t sca_writemask_x : 1;
        uint32_t src2l : 11;
    };

    struct
    {
        uint32_t : 23;
        uint32_t branch_index : 5;
        uint32_t brb_cond_true : 1;
        uint32_t iaddrl : 3;
    };
};

D0 d0;
D1 d1;
D2 d2;
D3 d3;

float dot(vec4 a, vec4 b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z) + (a.w * b.w);
}

void VertexShader::DoVertexShader(MemoryManager* manager)
{
    this->manager = manager;

    for (int i = 0; i < 16; i++)
    {
        dest[i].x = 0.0f;
        dest[i].y = 0.0f;
        dest[i].z = 0.0f;
        dest[i].w = 1.0f;
    }

    int callstack[8];
    int stack_ptr = 0;
    int current_instruction = progEntry;

    d3.end = false;

    while ((current_instruction-progEntry) < 512)
    {
        if (d3.end)
            break;
        
        d0.hex = *(uint32_t*)&memory[current_instruction];
        d1.hex = *(uint32_t*)&memory[current_instruction+4];
        d2.hex = *(uint32_t*)&memory[current_instruction+8];
        d3.hex = *(uint32_t*)&memory[current_instruction+12];
        current_instruction += 16;

        std::string op_disasm, dest_disasm, op1_disasm, op2_disasm;
        
        if (d1.vop != 0)
        {
            vec4 value = read_src(0, op1_disasm);
            switch (d1.vop)
            {
            case 1: op_disasm = "mov"; break;
            case 7:
            {
                op_disasm = "dp4";
                float v = dot(value, read_src(1, op2_disasm));
                value = {v, v, v, v};
                break;
            }
            default:
                printf("Unknown vector op %d\n", d1.vop);
                exit(1);
            }

            write_vec(value, dest_disasm);

            if (op2_disasm.size())
                printf("%s %s,%s,%s\n", op_disasm.c_str(), dest_disasm.c_str(), op1_disasm.c_str(), op2_disasm.c_str());
            else
                printf("%s %s,%s\n", op_disasm.c_str(), dest_disasm.c_str(), op1_disasm.c_str());
        }

        if (d1.sop)
        {
            printf("TODO: Non-zero scalar op\n");
            exit(1);
        }
    }

    dest[0].x = (dest[0].x+1.0)*1280 / 2;
    dest[0].y = (1.0 - dest[0].y)*720 / 2;

    printf("Output vertex: (%f, %f, %f, %f)\n", dest[0].x, dest[0].y, dest[0].z, dest[0].w);
}

void VertexShader::SetVPOffs(uint32_t offs)
{
    progOffs = progEntry = offs;
}

void VertexShader::AddInstruction(uint32_t instr)
{
    *(uint32_t*)&memory[progOffs] = instr;
    progOffs += 4;
}

void VertexShader::SetInputMask(uint32_t mask)
{
    inputMask = mask;
}

void VertexShader::SetResultMask(uint32_t mask)
{
    resultMask = mask;
}

void VertexShader::SetConstOffs(uint32_t offs)
{
    constOffs = (offs*sizeof(vec4));
    constBegin = 0;
}

void VertexShader::UploadConst(uint32_t value)
{
    *(uint32_t*)&memory[constOffs] = value;
    constOffs += 4;
}

void VertexShader::AddBinding(VertexBinding& binding)
{
    bindings.push_back(binding);
}

void VertexShader::SetPosition(float pos[4])
{
    // Find our position binding
    VertexBinding* position;

    for (auto& binding : bindings)
    {
        if (binding.attribute == GCM_VERTEX_ATTRIBUTE_POS)
            position = &binding;
    }

    if (!position)
    {
        printf("[WARN]: No position set\n");
        return;
    }

    position->SetF32(pos);
}

void VertexShader::SetColor(uint8_t color[4])
{
    // Find our position binding
    VertexBinding* colorBinding;

    for (auto& binding : bindings)
    {
        if (binding.attribute == GCM_VERTEX_ATTRIBUTE_COLOR0)
            colorBinding = &binding;
    }

    if (!colorBinding)
    {
        printf("[WARN]: No color set\n");
        return;
    }

    colorBinding->SetU8(color);
}

vec4 VertexShader::read_src(int index, std::string& disasm)
{
    uint32_t src = 0;
    vec4 value;
    bool do_abs = false;

    switch (index)
    {
    case 0:
        src = (GET_BITS(d1.hex, 0, 8) << 9) | GET_BITS(d2.hex, 23, 9);
        do_abs = d0.src0_abs;
        break;
    case 1:
        src = GET_BITS(d2.hex, 6, 17);
        do_abs = GET_BITS(d0.hex, 22, 1);
        break;
    }

    uint32_t reg_type = GET_BITS(src, 0, 2);
    uint32_t tmp_src = GET_BITS(src, 2, 6);

    switch (reg_type)
    {
    case 2:
    {
        value = read_location(d1.input_src, disasm);
        break;
    }
    case 3:
        if (d3.index_const)
            assert(0);
        else
            value = *(vec4*)&memory[constBegin + (d1.const_src*sizeof(vec4))];
        disasm = "c[" + std::to_string(d1.const_src) + "] (" + std::to_string(value.x) 
                        + ", " + std::to_string(value.y) 
                        + ", " + std::to_string(value.z) 
                        + ", " + std::to_string(value.w) + ")";
        break;
    default:
        printf("Unknown source type %d\n", reg_type);
        exit(1);
    }

    if (GET_BITS(src, 8, 8) != 0x1B)
    {
        printf("TODO: SWIZZLING!\n");
        exit(1);
    }

    if (do_abs)
    {
        value.x = fabs(value.x);
        value.y = fabs(value.y);
        value.z = fabs(value.z);
        value.w = fabs(value.w);
    }

    if (GET_BITS(src, 16, 1))
    {
        value.x = -value.x;
        value.y = -value.y;
        value.z = -value.z;
        value.w = -value.w;
    }

    return value;
}

#define BINDING_LOCATION(x) manager->Read32(manager->RSXFBMem->GetStart() + binding.offset + (binding.stride * vtx_index) + (x));
#define BINDING_LOCATION_U8(x) manager->Read8(manager->RSXFBMem->GetStart() + binding.offset + (binding.stride * vtx_index) + (x));

vec4 VertexShader::read_location(int index, std::string& disasm)
{
    auto binding = GetBinding(index);

    printf("Binding is at 0x%08x\n", binding.offset);

    std::array<std::string, 16> locNames =
    {
        "in_pos",
        "in_weight",
        "in_normal",
        "in_color0",
        "in_color1",
        "in_fog",
        "in_color_index",
        "in_edgeflag",
        "in_tex0",
        "in_tex1",
        "in_tex2",
        "in_tex3",
        "in_tex4",
        "in_tex5",
        "in_tex6",
    };

    disasm = locNames[index];

    vec4 ret;
    if (binding.dtype == GCM_VERTEX_DATA_TYPE_F32)
    {
        if (binding.elems == 4)
        {
            uint32_t tmp = BINDING_LOCATION(0);
            ret.x = (float&)tmp;
            tmp = BINDING_LOCATION(4);
            ret.y = (float&)tmp;
            tmp = BINDING_LOCATION(8);
            ret.z = (float&)tmp;
            tmp = BINDING_LOCATION(12);
            ret.w = (float&)tmp;
            printf("(%f, %f, %f, %f)\n", ret.x, ret.y, ret.z, ret.w);
        }
        else if (binding.elems == 3)
        {
            uint32_t tmp = BINDING_LOCATION(0);
            ret.x = (float&)tmp;
            tmp = BINDING_LOCATION(4);
            ret.y = (float&)tmp;
            tmp = BINDING_LOCATION(8);
            ret.z = (float&)tmp;
            ret.w = 0.0f;
            printf("(%f, %f, %f)\n", ret.x, ret.y, ret.z);
        }
        else if (binding.elems == 2)
        {
            uint32_t tmp = BINDING_LOCATION(0);
            ret.x = (float&)tmp;
            tmp = BINDING_LOCATION(4);
            ret.y = (float&)tmp;
            ret.z = 0.0f;
            ret.w = 0.0f;
            printf("%f, %f\n", ret.x, ret.y);
        }
        else
        {
            printf("Unknown F32 count %d\n", binding.elems);
            exit(1);
        }
    }
    else
    {
        if (binding.elems == 4)
        {
            ret.x = BINDING_LOCATION_U8(0);
            ret.y = BINDING_LOCATION_U8(1);
            ret.z = BINDING_LOCATION_U8(2);
            ret.w = BINDING_LOCATION_U8(3);
            printf("%f, %f, %f, %f\n", ret.x, ret.y, ret.z, ret.w);
        }
        else if (binding.elems == 3)
        {
            ret.x = BINDING_LOCATION_U8(0);
            ret.y = BINDING_LOCATION_U8(1);
            ret.z = BINDING_LOCATION_U8(2);
            ret.w = 1.0f;
        }
        else
        {
            printf("Unknown U8 count %d\n", binding.elems);
            exit(1);
        }
    }

    return ret;
}

float mix(float x, float y, float a)
{
    return x * (1 - a) + y*a;
}

void VertexShader::write_vec(vec4 value, std::string& disasm)
{
    if (d0.saturate)
    {
        value.x = std::clamp(value.x, 0.0f, 1.0f);
        value.y = std::clamp(value.y, 0.0f, 1.0f);
        value.z = std::clamp(value.z, 0.0f, 1.0f);
        value.w = std::clamp(value.w, 0.0f, 1.0f);
    }

    if (d0.cond_test_enable)
    {
        printf("TODO: Condition testing\n");
        exit(1);
    }

    if (d0.dst_tmp == 0x3f && !d0.vec_result)
    {
        printf("TODO: dst_tmp == 0x3f\n");
        exit(1);
    }
    else if (d0.vec_result && d3.dst < 16)
    {
        if (d3.vec_writemask_x)
            dest[d3.dst].x = value.x;
        if (d3.vec_writemask_y)
            dest[d3.dst].y = value.y;
        if (d3.vec_writemask_z)
            dest[d3.dst].z = value.z;
        if (d3.vec_writemask_w)
            dest[d3.dst].w = value.w;

        std::array<std::string, 16> outputNames =
        {
            "out_pos",
            "out_col0",
            "out_col1",
            "out_bfc0",
            "out_bfc1",
            "out_clipDistance[0]",
            "out_clipDistance[1]",
            "out_tex0",
            "out_tex1",
            "out_tex2",
            "out_tex3",
            "out_tex4",
            "out_tex5",
            "out_tex6",
        };

        disasm = outputNames[d3.dst];
        std::string componentDisasm = ".";
        if (d3.vec_writemask_x) componentDisasm += "x";
        if (d3.vec_writemask_y) componentDisasm += "y";
        if (d3.vec_writemask_z) componentDisasm += "z";
        if (d3.vec_writemask_w) componentDisasm += "w";

        if (componentDisasm == ".xyzw")
            componentDisasm = "";
        disasm += componentDisasm;
    }
    else if (d0.dst_tmp != 0x3f)
    {
        if (d3.vec_writemask_x)
            dest[d0.dst_tmp].x = value.x;
        if (d3.vec_writemask_x)
            dest[d0.dst_tmp].y = value.y;
        if (d3.vec_writemask_x)
            dest[d0.dst_tmp].z = value.z;
        if (d3.vec_writemask_x)
            dest[d0.dst_tmp].w = value.w;
        disasm = "R" + std::to_string(d0.dst_tmp);
    }
}
