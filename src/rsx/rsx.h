#pragma once

#include <kernel/types.h>
#include <vector>
#include <string>
#include <kernel/Memory.h>
#include <SDL2/SDL.h>

#include "VPE.h"

struct Color
{
    uint8_t r, g, b, a;
};

struct Vertex
{
    vec4 pos, color;
};

enum DepthTestFunc
{
    NEVER = 0x200,
    LESS,
    EQUAL,
    LEQUAL,
    GREATER,
    NOTEQUAL,
    GEQUAL,
    ALWAYS,
}; 

enum
{
    BEGIN_END_STOP,
    BEGIN_END_POINTS,
    BEGIN_END_LINES,
    BEGIN_END_LINE_LOOP,
    BEGIN_END_LINE_STRIP,
    BEGIN_END_TRIANGLES,
    BEGIN_END_TRIANGLE_STRIP,
    BEGIN_END_TRIANGLE_FAN,
    BEGIN_END_QUADS,
    BEGIN_END_QUAD_STRIP,
    BEGIN_END_POLYGON
};

struct Texture
{
    uint32_t offset;
    uint8_t format;
    uint8_t dimension;
    bool cubemap;
    uint16_t mipmap;
    uint32_t pitch, depth;
    uint32_t width, height;
    uint32_t swizzle; // I really don't want to touch swizzling
    bool enable;
};

class RSX
{
public:
    void Init();
    void Present();

    void SetFramebuffer(int id, uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height);
    void DoCommands(uint8_t* buf, uint32_t size);

    void SetMman(MemoryManager* manager) {this->manager = manager;}

    bool& GetFlipped() {return flipped;}
private:
    bool flipped = false;

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;

    void DoCmd(uint32_t fullCmd, uint32_t cmd, std::vector<uint32_t>& args, int argCount);
    void ClearFramebuffers(uint32_t mask);

    void DrawTriangle(Vertex v0, Vertex v1, Vertex v2);

    MemoryManager* manager;

    uint8_t* framebuffer;
    uint8_t* depthBuffer;

    bool r_mask, g_mask, b_mask, a_mask;
    Color clearColor;
    uint32_t viewport_width, viewport_height, viewport_x, viewport_y;
    uint32_t scissor_width, scissor_height, scissor_x, scissor_y;
    float depth_min, depth_max;
    bool depthTestEnabled;
    DepthTestFunc depthTestFunc;

    uint64_t semaphoreOffset;

    VertexShader vpe;
    VertexBinding curBinding;

    uint32_t fpShaderOffs; // Offset from the beginning of RSXCMDMem of the fragment shader
    uint32_t shaderControl;

    uint32_t dmaColorA, dmaColorB, dmaColorC, dmaColorD, dmaColorZ;

    uint32_t zPitch, zOffset, colorPitch, color0Offset;

    uint32_t primitiveType;

    bool blend_enable = false;
    uint16_t blend_sfunc_rgb, blend_sfunc_alpha, blend_dfunc_rgb, blend_dfunc_alpha;

    bool alpha_test_enable = false;

    // Contains data on the framebuffer with the id 0-7
    // We can calculate BPP from (pitch / width)*8
    struct RsxFramebuffer
    {
        uint32_t offs, pitch, width, height, bpp;
    } framebuffers[8];

    Texture textures[16];
};

extern RSX* rsx;

inline std::string DepthFuncToString(DepthTestFunc func)
{
    switch (func)
    {
    case NEVER:
        return "NV30_3D_DEPTH_FUNC_NEVER";
    case LESS:
        return "NV30_3D_DEPTH_FUNC_LESS";
    case EQUAL:
        return "NV30_3D_DEPTH_FUNC_EQUAL";
    case LEQUAL:
        return "NV30_3D_DEPTH_FUNC_LEQUAL";
    case GREATER:
        return "NV30_3D_DEPTH_FUNC_GREATER";
    case NOTEQUAL:
        return "NV30_3D_DEPTH_FUNC_NOTEQUAL";
    case GEQUAL:
        return "NV30_3D_DEPTH_FUNC_GEQUAL";
    case ALWAYS:
        return "NV30_3D_DEPTH_FUNC_ALWAYS";
    }

    return "NV30_3D_DEPTH_FUNC_INVALID";
}