#include "rsx.h"

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <stdexcept>
#include <cassert>
#include <algorithm>
#include <kernel/Modules/CellGcm.h>
#include <iostream>

RSX rsxLocal;
RSX* rsx = &rsxLocal;

uint32_t start_time, frame_time;

void RSX::Init()
{
    SDL_Init(SDL_INIT_EVERYTHING);
    window = SDL_CreateWindow("PS3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920 / 2, 1080 / 2, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 1920, 1080);

    start_time = SDL_GetTicks();
}

void RSX::Present()
{
    if (!framebuffer) return;

    SDL_UpdateTexture(texture, NULL, framebuffer, framebuffers[0].pitch);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    flipped = true;

    frame_time = SDL_GetTicks() - start_time;
    float fps = (frame_time > 0) ? 1000.0f / frame_time : 0.0f;

    start_time = SDL_GetTicks();

    static char buf[4096];
    snprintf(buf, 4096, "PS3 - %f fps", fps);
    SDL_SetWindowTitle(window, buf);
}

void RSX::SetFramebuffer(int id, uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height)
{
    auto& fb = framebuffers[id];
    fb.offs = offset;
    fb.pitch = pitch;
    fb.width = width;
    fb.height = height;
    fb.bpp = (fb.width / pitch) * 8;
}

uint32_t Read32(uint8_t* ptr, uint64_t& offs, uint32_t& get)
{
    uint32_t data = __builtin_bswap32(*(uint32_t*)&ptr[offs]);
    offs += 4;
    get += 4;
    return data;
}

#define case_16(a, m) \
    case a + m: \
    case a + m * 2: \
    case a + m * 3: \
    case a + m * 4: \
    case a + m * 5: \
    case a + m * 6: \
    case a + m * 7: \
    case a + m * 8: \
    case a + m * 9: \
    case a + m * 10: \
    case a + m * 11: \
    case a + m * 12: \
    case a + m * 13: \
    case a + m * 14: \
    case a + m * 15: \
    index = (cmd - a) / m; \
    case a \

void RSX::DoCommands(uint8_t *buf, uint32_t size)
{
    uint64_t offs = 0;

    std::vector<uint32_t> callStack;

    uint32_t get =  manager->Read32(CellGcm::GetControlAddress()+4);
    uint32_t put =  manager->Read32(CellGcm::GetControlAddress());
    buf = manager->GetRawPtr(CellGcm::GetIOAddres() + get);

    printf("0x%08x\n", CellGcm::GetIOAddres());

    while (get < put)
    {
        printf("0x%08x, 0x%08x\n", get, put);
        uint32_t cmd = Read32(buf, offs, get);
        const uint32_t count = (cmd >> 18) & 0x7ff;

        if (cmd & 0x20000000)
        {
            // Jump command
            offs = cmd & ~0x20000000;
            printf("Jump to 0x%08lx\n", offs);
            continue;
        }
        else if (cmd & 2)
        {
            printf("TODO: call\n");
            exit(1);
        }
        else if (cmd == 0x20000)
        {
            printf("RETURN\n");
            if (callStack.empty())
            {
                throw std::runtime_error("Cannot return with empty call stack");
            }
            else
            {
                offs = callStack.back();
                callStack.pop_back();
            }
        }

        std::vector<uint32_t> args;

        for (int i = 0; i < count; i++)
        {
            args.push_back(Read32(buf, offs, get));
        }

        DoCmd(cmd, cmd & 0x3FFFF, args, count);
    }

    std::cout << "Done processing CMD list\n";

    manager->Write32(CellGcm::GetControlAddress()+4, get);
}

void RSX::DoCmd(uint32_t fullCmd, uint32_t cmd, std::vector<uint32_t> &args, int argCount)
{
    int index = 0;
    
    switch (cmd)
    {
    case 0x050:
        printf("NV406ETCL_SET_REF(0x%08x)\n", args[0]);
        manager->Write32(CellGcm::GetControlAddress()+8, args[0]);
        break;
    case 0x064:
        printf("NV406ETCL_SEMAPHORE_OFFSET(0x%08x)\n", args[0]);
        semaphoreOffset = args[0];
        break;
    case 0x068:
        printf("NV406ETCL_SEMAPHORE_ACQUIRE(0x%08x)\n", args[0]);
        break;
    case 0x100:
        printf("NV40TCL_NOP()\n");
        break;
    case 0x18C:
        dmaColorB = args[0];
        printf("NV40TCL_DMA_COLOR1(0x%08x)\n", dmaColorB);
        break;
    case 0x194:
        dmaColorA = args[0];
        printf("NV40TCL_DMA_COLOR0(0x%08x)\n", dmaColorA);
        break;
    case 0x198:
        dmaColorZ = args[0];
        printf("NV40TCL_DMA_ZETA(0x%08x)\n", dmaColorZ);
        break;
    case 0x1B4:
    {
        dmaColorC = args[0];
        if (argCount == 2)
        {
            dmaColorD = args[1];
            printf("NV40TCL_DMA_COLOR2_COLOR3(0x%08x, 0x%08x)\n", dmaColorC, dmaColorD);
        }
        else
            printf("NV40TCL_DMA_COLOR2(0x%08x)\n", dmaColorC);
        break;
    }
    case 0x1D88:
        break;
    case 0x200:
        break;
    case 0x208:
        colorPitch = args[1];
        color0Offset = args[2];
        zOffset = args[3];
        printf("NV40TCL_RT_FORMAT(0x%08x, 0x%08x, 0x%08x)\n", colorPitch, color0Offset, zOffset);
        break;
    case 0x220:
        printf("NV40TCL_RT_ENABLE(0x%08x)\n", args[0]);
        assert(args[0] == 0x1);
        break;
    case 0x22C:
        zPitch = args[0];
        printf("NV40TCL_ZETA_PITCH(0x%08x)\n", zPitch);
        break;
    case 0x280:
        break;
    case 0x2B8:
        printf("NV40TCL_WINDOW_OFFSET\n");
        break;
    case 0x304:
        alpha_test_enable = args[0];
        printf("NV40TCL_ALPHA_TEST_ENABLE(%d)\n", alpha_test_enable);
        break;
    case 0x310:
        blend_enable = args[0];
        printf("NV40TCL_BLEND_ENABLE(%d)\n", blend_enable);
        break;
    case 0x314:
    {
        blend_sfunc_rgb = args[0] & 0xffff;
        blend_sfunc_alpha = args[0] >> 16;

        if (argCount == 2)
        {
            blend_dfunc_rgb = args[1] & 0xffff;
            blend_dfunc_alpha = args[1] >> 16;
            printf("NV30_BLEND_SFUNC_DFUNC(%x, %x, %x, %x)\n", blend_sfunc_rgb, blend_sfunc_alpha, blend_dfunc_rgb, blend_dfunc_alpha);
        }
        else
            printf("NV30_BLEND_SFUNC(%x, %x)\n", blend_sfunc_rgb, blend_sfunc_alpha);
        break;
    }
    case 0x324:
    {
        uint32_t mask = args[0];
        r_mask = (mask & 0x01000000) ? true : false;
        g_mask = (mask & 0x00010000) ? true : false;
        b_mask = (mask & 0x00000100) ? true : false;
        a_mask = (mask & 0x00000001) ? true : false;
        printf("NV4097_SET_COLOR_MASK(%d, %d, %d, %d)\n", r_mask, g_mask, b_mask, a_mask);
        break;
    }
    case 0x370:
        break;
    case 0x380:
        break;
    case 0x394:
    {
        depth_min = (float&)args[0];

        if (argCount == 2)
        {
            depth_max = (float&)args[1];
            printf("NV30_DEPTH_MIN_MAX(%f, %f)\n", depth_min, depth_max);
        }
        else
            printf("NV30_DEPTH_MIN(%f)\n", depth_min);
        break;
    }
    case 0x8C0:
    {
        scissor_width = (args[0] >> 16);
        scissor_x = (args[0] & 0xffff);

        if (argCount == 2)
        {
            scissor_height = (args[1] >> 16);
            scissor_y = (args[1] & 0xffff);
            printf("NV30_SCISSOR_HORIZ_VERT(%d, %d, %d, %d)\n", scissor_width, scissor_height, scissor_x, scissor_y);
        }
        else
            printf("NV30_SCISSOR_HORIZ(%d, %d)\n", scissor_width, scissor_x);
        break;
    }
    case 0x8E4:
    {
        fpShaderOffs = args[0];
        printf("NV40TCL_FP_ADDRESS(0x%08x)\n", args[0]);
        break;
    }
    case 0xA00:
    {
        viewport_width = (args[0] >> 16);
        viewport_x = (args[0] & 0xffff);

        if (argCount == 2)
        {
            viewport_height = (args[1] >> 16);
            viewport_y = (args[1] & 0xffff);
            printf("NV30_VIEWPORT_HORIZ_VERT(%d, %d, %d, %d)\n", viewport_width, viewport_height, viewport_x, viewport_y);
        }
        else
            printf("NV30_VIEWPORT_HORIZ(%d, %d)\n", viewport_width, viewport_x);
        break;
    }
    case 0xA20:
        printf("NV30_VIEWPORT_OFFSET\n");
        break;
    case 0xA6C:
        depthTestFunc = (DepthTestFunc)args[0];
        printf("NV30_DEPTH_TEST_FUNC(%s)\n", DepthFuncToString(depthTestFunc).c_str());
        break;
    case 0xA74:
        depthTestEnabled = args[0];
        printf("NV30_DEPTH_TEST_ENABLED(%s)\n", depthTestEnabled ? "GCM_TRUE" : "GCM_FALSE");
        break;
    case_16(0xB40, 4):
    {
        break;
    }
    case 0xB80:
        for (int i = 0; i < argCount; i++) vpe.AddInstruction(args[i]);
        printf("NV30_3D_VP_UPLOAD_INST()\n");
        break;
    case 0x1680 ... 0x16BC:
    {
        curBinding.offset = args[0] & 0xfffffff;
        printf("NV40TCL_VTXBUF_ADDRESS(0x%08x)\n", curBinding.offset);
        vpe.AddBinding(curBinding);
        break;
    }
    case 0x1710:
        printf("NV40TCL_VTX_CACHE_INVALIDATE2()\n");
        break;
    case 0x1714:
        printf("NV40TCL_VTX_CACHE_INVALIDATE()\n");
        break;
    case 0x1740 ... 0x177C:
    {
        curBinding.attribute = (cmd - 0x1740) / 4;
        curBinding.stride = (args[0] >> 8) & 0xff;
        curBinding.elems = (args[0] >> 4) & 0xf;
        curBinding.dtype = args[0] & 0xf;

        printf("NV40TCL_VTXFMT(%d, %d, %d, %d)\n", curBinding.attribute, curBinding.stride, curBinding.elems, curBinding.dtype);
        break;
    }
    case 0x1800:
        printf("NV40TCL_GET_REPORT()\n");
        break;
    case 0x1808:
        primitiveType = args[0];
        printf("NV40TCL_BEGIN_END(%d)\n", primitiveType);
        assert(primitiveType == BEGIN_END_TRIANGLES || primitiveType == BEGIN_END_STOP || primitiveType == BEGIN_END_QUADS);
        break;
    case 0x1814:
    {
        Vertex vertices[3];
        uint32_t a0 = args[0];
        printf("NV40TCL_DRAW(0x%08x)\n", a0);
        if (primitiveType == BEGIN_END_TRIANGLES)
        {
            int index = 0;
            for (int i = 0; i < ((a0 >> 24) & 0xff)+1; i++)
            {
                vpe.SetIndex(i);
                vpe.DoVertexShader(manager);
                vertices[index].pos = vpe.GetOutputPos();
                vertices[index].color = vpe.GetOutputColor();
                index++;
                if (index == 3)
                {
                    DrawTriangle(vertices[0], vertices[1], vertices[2]);
                    index = 0;
                }
            }
        }
        break;
    }
    case 0x1828:
    {
        printf("NV40TCL_POLYGON_MODE_FRONT(0x%04x)\n", args[0]);
        break;
    }
    case_16(0x1840, 4):
    {
        uint32_t a0 = args[0];
        textures[index].pitch = a0 & 0xFFFFF;
        textures[index].depth = a0 >> 20;
        printf("NV40TCL_TEXTURE_CONTROL3(%d, %d)\n", textures[index].pitch, textures[index].depth);
        break;
    }
    case_16(0x1A00, 0x20):
    {
        Texture& tex = textures[index];
        tex.offset = args[0];
        uint32_t a1 = args[1];
        uint8_t location = (a1 & 0x3) - 1;
        bool cubemap = (a1 >> 2) & 1;
        uint8_t dimension = (a1 >> 4) & 0xf;
        uint8_t format = (a1 >> 8) & 0x1f;
        uint16_t mipmap = (a1 >> 16) & 0xffff;
        printf("NV40TCL_TEX_OFFSET(%d, 0x%08x, %d, %d, %d, %d, %d)\n", index, tex.offset, location, cubemap, dimension, format, cubemap);
        tex.cubemap = cubemap;
        tex.dimension = dimension;
        tex.format = format;
        tex.mipmap = mipmap;
        break;
    }
    case_16(0x1A08, 0x20):
    {
        Texture& tex = textures[index];
        printf("NV40TCL_TEX_ADDRESS(0x%08x)\n", args[0]);
        break;
    }
    case_16(0x1A0C, 0x20):
    {
        Texture& tex = textures[index];
        tex.enable = args[0] >> 31;
        printf("NV40TCL_TEX_CONTROL0(%d)\n", tex.enable);
        break;
    }
    case_16(0x1A10, 0x20):
    {
        Texture& tex = textures[index];
        tex.swizzle = args[0];
        printf("NV40TCL_TEX_SWIZZLE(0x%08x)\n", tex.swizzle);
        break;
    }
    case_16(0x1A14, 0x20):
    {
        Texture& tex = textures[index];
        printf("NV40TCL_TEX_FILTER(0x%08x)\n", args[0]);
        break;
    }
    case_16(0x1A18, 0x20):
    {
        auto& tex = textures[index];
        uint32_t a0 = args[0];
        auto width = a0 >> 16;
        auto height = a0 & 0xFFFF;
        tex.width = width;
        tex.height = height;
        printf("NV40TCL_TEX_RECT(%d, %d)\n", width, height);
        break;
    }
    case 0x1D60:
    {
        shaderControl = args[0];
        printf("NV40TCL_FP_CONTROL(0x%08x)\n", shaderControl);
        break;
    }
    case 0x1D8C:
    {
        printf("NV40TCL_ZSTENCIL_CLEAR_VALUE(0x%06x, 0x%02x)\n", args[0] >> 8, args[0] & 0xff);
        break;
    }
    case 0x1D90:
    {
        clearColor.r = (args[0] >> 24) & 0xff;
        clearColor.g = (args[0] >> 16) & 0xff;
        clearColor.b = (args[0] >> 8) & 0xff;
        clearColor.a = (args[0] >> 0) & 0xff;
        printf("NV30_CLEAR_COLOR(%d, %d, %d, %d)\n", clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        break;
    }
    case 0x1D94:
        ClearFramebuffers(args[0]);
        break;
    case 0x1E9C:
    {
        vpe.SetVPOffs(args[0]);
        printf("NV30_VERTEX_PROGRAM_UPLOAD(0x%08x)\n", args[0]);
        break;
    }
    case 0x1EF8:
        printf("NV40TCL_TRANSFORM_TIMEOUT(0x%08x)\n", args[0]);
        break;
    case 0x1EFC:
        vpe.SetConstOffs(args[0]);
        printf("NV40TCL_VP_UPLOAD_CONST_ID(0x%08x)\n", args[0]);
        for (int i = 1; i < argCount; i++) vpe.UploadConst(args[i]);
        for (int i = 1; i < argCount; i += 4)
        {
            float x = (float&)args[i];
            float y = (float&)args[i+1];
            float z = (float&)args[i+2];
            float w = (float&)args[i+3];
            printf("Vertex constant (%f, %f, %f, %f)\n", x, y, z, w);
        }
        break;
    case 0x1FF0:
        vpe.SetInputMask(args[0]);
        printf("NV40TCL_VP_ATTRIB_EN(0x%08x)\n", args[0]);
        break;
    case 0x1FF4:
        vpe.SetResultMask(args[0]);
        printf("NV40TCL_VP_RESULT_EN(0x%08x)\n", args[0]);
        break;
    case 0x3FEAD:
        Present();
        break;
    default:
        printf("Unknown RSX command 0x%05x (0x%08x, ", cmd, fullCmd);
        for (int i = 0; i < argCount; i++)
            printf("0x%08x, ", args[i]);
        printf("\b\b)\n");
        throw std::runtime_error("Unknown RSX command");
    }
}

void RSX::ClearFramebuffers(uint32_t mask)
{
    uint32_t colorMask = 0xFFFFFFFF; // Clear none of the colors by default

    framebuffer = manager->GetRawPtr(manager->RSXFBMem->GetStart() + color0Offset);
    depthBuffer = manager->GetRawPtr(manager->RSXFBMem->GetStart() + zOffset);

    uint32_t depthMask = 0xFFFFFFFF;
    if (mask & 1)
        depthMask = 0;

    if (mask & (1 << 7))
        colorMask &= ~0xFF; // Clear the A component
    if (mask & (1 << 6))
        colorMask &= ~0xFF00; // Clear the B component
    if (mask & (1 << 5))
        colorMask &= ~0xFF0000; // Clear the G component
    if (mask & (1 << 4))
        colorMask &= ~0xFF000000; // Clear the R component

    uint32_t color = (clearColor.r << 24) | (clearColor.g << 16) | (clearColor.b << 8) | (clearColor.a);
    
    for (uint32_t y = 0; y < framebuffers[0].height; y++)
    {
        for (uint32_t x = 0; x < framebuffers[0].width; x++)
        {
            *(uint32_t*)&framebuffer[(x*4) + (y * colorPitch)] &= colorMask;
            *(uint32_t*)&framebuffer[(x*4) + (y * colorPitch)] |= color & ~colorMask;
            *(uint32_t*)&depthBuffer[(x*4) + (y * zPitch)] &= depthMask;
        }
    }

    printf("NV40TCL_CLEAR_BUFFERS(0x%08x)\n", mask);
}

float orient2d(vec4& v1, vec4& v2, vec4& v3)
{
    return (v2.x - v1.x) * (v3.y - v1.y) - (v3.x - v1.x) * (v2.y - v1.y);
}

#undef printf

void RSX::DrawTriangle(Vertex v0, Vertex v1, Vertex v2)
{
    auto p0 = v0.pos, p1 = v1.pos, p2 = v2.pos;

    if (orient2d(p0, p1, p2) < 0)
        std::swap(p1, p2);
    
    int32_t min_x = std::min({p0.x, p1.x, p2.x});
    int32_t max_x = std::max({p0.x, p1.x, p2.x});
    int32_t min_y = std::min({p0.y, p1.y, p2.y});
    int32_t max_y = std::max({p0.y, p1.y, p2.y});

    int32_t A12 = p0.x - p1.y;
    int32_t B12 = p1.x - p0.x;
    int32_t A23 = p1.y - p2.y;
    int32_t B23 = p2.x - p1.x;
    int32_t A31 = p2.y - p0.y;
    int32_t B31 = p0.x - p2.x;

    vec4 min_corner = {};
    min_corner.x = min_x;
    min_corner.y = min_y;
    int32_t w1_row = orient2d(p1, p2, min_corner);
    int32_t w2_row = orient2d(p2, p0, min_corner);
    int32_t w3_row = orient2d(p0, p1, min_corner);

    float r1 = v0.color.x;
    float r2 = v1.color.x;
    float r3 = v2.color.x;

    float g1 = v0.color.y;
    float g2 = v1.color.y;
    float g3 = v2.color.y;

    float b1 = v0.color.z;
    float b2 = v1.color.z;
    float b3 = v2.color.z;

    float z1 = v0.pos.z;
    float z2 = v1.pos.z;
    float z3 = v2.pos.z;

    int32_t divider = orient2d(p0, p1, p2);

    printf("Drawing triangle at (%f, %f), (%f, %f), (%f, %f)\n", p0.x, p0.y, p1.x, p1.y, p2.x, p2.y);
    printf("(%f, %f, %f) (%f, %f, %f) (%f, %f, %f)\n", r1, g1, b1, r2, g2, b2, r3, g3, b3);

    for (float y = min_y; y <= max_y; y++)
    {
        int32_t w1 = w1_row;
        int32_t w2 = w2_row;
        int32_t w3 = w3_row;

        for (float x = min_x; x <= max_x; x++)
        {
            if ((w1 | w2 | w3) >= 0)
            {
                uint32_t pos = ((x*4) + (y*colorPitch));
                int r = ((float) r1 * w1 + (float) r2 * w2 + (float) r3 * w3) / divider;
				int g = ((float) g1 * w1 + (float) g2 * w2 + (float) g3 * w3) / divider;
				int b = ((float) b1 * w1 + (float) b2 * w2 + (float) b3 * w3) / divider;
                float z = (z1*w1 + z2*w2 + z3*w3) / divider;
                uint32_t color = 0;
                if (r_mask)
                    color |= (r << 24);
                if (g_mask)
                    color |= (g << 16);
                if (b_mask)
                    color |= (b << 8);
                color |= 0xFF;
                if (x <= viewport_width && x >= viewport_x && y <= viewport_height && y >= viewport_y
                    && (*(float*)&depthBuffer[pos] == 0 || *(float*)&depthBuffer[pos] < z))
                {
                    *(uint32_t*)&framebuffer[pos] = color;
                    *(float*)&depthBuffer[pos] = z;
                }
            }
            w1 += A23;
            w2 += A31;
            w3 += A12;
        }
        w1_row += B23;
        w2_row += B31;
        w3_row += B12;
    }
}
