#include "PPU.h"

#include <stdio.h>
#include <bit>
#include <stdexcept>
#include <format>

#define rt_d ((opcode >> 21) & 0x1F)
#define ra_d ((opcode >> 16) & 0x1F)
#define si_d (opcode & 0xFFFF)

using std::placeholders::_1;

void CellPPU::InitInstructionTable()
{
    opcodes[0x0B] = std::bind(&CellPPU::Cmpi, this, _1);
    opcodes[0x0E] = std::bind(&CellPPU::Addi, this, _1);
    opcodes[0x10] = std::bind(&CellPPU::BranchCond, this, _1);
    opcodes[0x12] = std::bind(&CellPPU::Branch, this, _1);
    opcodes[0x18] = std::bind(&CellPPU::Ori, this, _1);
    opcodes[0x19] = std::bind(&CellPPU::Oris, this, _1);
    opcodes[0x1E] = std::bind(&CellPPU::Rldicr, this, _1);
    opcodes[0x1F] = std::bind(&CellPPU::Code1F, this, _1);
    opcodes[0x3A] = std::bind(&CellPPU::Ld, this, _1);
    opcodes[0x3E] = std::bind(&CellPPU::Std, this, _1);
}

bool CellPPU::CheckCondition(uint32_t bo, uint32_t bi)
{
    const uint8_t bo0 = (bo & 0x10) ? 1 : 0;
    const uint8_t bo1 = (bo & 0x08) ? 1 : 0;
    const uint8_t bo2 = (bo & 0x04) ? 1 : 0;
    const uint8_t bo3 = (bo & 0x02) ? 1 : 0;

    if (!bo2) --ctr;

    const uint8_t ctr_ok = bo2 | ((ctr != 0) ^ bo3);
    const uint8_t cond_ok = bo0 | (IsCR(bi) ^ (~bo1 & 1));

    return ctr_ok && cond_ok;
}

void CellPPU::Cmpi(uint32_t opcode)
{
    uint8_t bf = (opcode >> 23) & 0x7;
    bool l = (opcode >> 21) & 1;
    uint8_t ra = (opcode >> 16) & 0x1F;
    int64_t si = (int64_t)(int16_t)(opcode & 0xffff);
    int64_t a = l ? (int64_t)r[ra] : (int64_t)(int32_t)(r[ra] & 0xFFFFFFFF);

    uint8_t c;
    if (a < si) c = 8;
    else if (a > si) c = 4;
    else c = 2;

    cr.cr[bf].val = c;

    if constexpr (canDisassemble)
    {
        if (l)
            printf("cmpdi cr%d,r%d,%d\n", bf, ra, (int16_t)(opcode & 0xffff));
        else
            printf("cmpwi cr%d,r%d,%d\n", bf, ra, (int16_t)(opcode & 0xffff));
    }
}

void CellPPU::Addi(uint32_t opcode)
{
    uint8_t rt = rt_d;
    uint8_t ra = ra_d;
    uint16_t si = (int16_t)(int8_t)si_d;

    uint64_t val_ra = r[ra];
    if (ra == 0)
        val_ra = 0;
    
    uint64_t result = val_ra + si;
    r[rt] = result;

    if constexpr (canDisassemble)
    {
        if (!ra)
            printf("li r%d, 0x%04x\n", rt, si);
        else if ((int16_t)si >= 0)
            printf("addi r%d, r%d, 0x%04x\n", rt, ra, si);
        else
            printf("subi r%d, r%d, 0x%04x", rt, ra, -((int16_t)si));
    }
}

void CellPPU::BranchCond(uint32_t opcode)
{
    bool lk = opcode & 1;
    bool aa = (opcode >> 1) & 1;
    int16_t bd = (int16_t)opcode & 0xFFFC;
    uint8_t bi = (opcode >> 16) & 0x1F;
    uint8_t bo = (opcode >> 21) & 0x1F;
    
    uint64_t target_addr;
    if (aa)
        target_addr = (int64_t)bd;
    else
        target_addr = (pc-4) + (int64_t)bd;

    if constexpr (canDisassemble)
    {
        printf("bc%s%s %d,%d,0x%08lx\n", lk ? "l" : "", aa ? "a" : "", bo, bi, target_addr);
    }

    if (CheckCondition(bo, bi))
    {
        if (lk)
            lr = pc;
        
        pc = target_addr;
        printf("Branch taken\n");
    }
}

template<class T>
T sign_extend(T x, const int bits) {
    T m = 1;
    m <<= bits - 1;
    return (x ^ m) - m;
}

void CellPPU::Branch(uint32_t opcode)
{
    int32_t li = sign_extend<int32_t>(opcode & 0x3FFFFFC, 26);
    bool aa = (opcode >> 1) & 1;
    bool lk = opcode & 1;

    if (lk)
        lr = pc;

    if (aa)
    {
        pc = (int64_t)(int32_t)li;
        printf("b%sa 0x%08lx\n", lk ? "l" : "", pc);
    }
    else
    {
        pc -= 4;
        pc += (int64_t)li;
        printf("b%s 0x%08lx\n", lk ? "l" : "", pc);
    }
}

void CellPPU::Ori(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    uint64_t ui = si_d;

    r[ra] = r[rs] | ui;

    printf("ori r%d, r%d, 0x%04lx\n", ra, rs, ui);
}

void CellPPU::Oris(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    uint64_t ui = si_d;

    r[ra] = r[rs] | (ui << 16);

    printf("oris r%d, r%d, 0x%04lx\n", ra, rs, ui);
}

void CellPPU::Rldicr(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int sh = ((opcode >> 11) & 0x1F);
    int me = (opcode >> 5) & 0x3F;
    sh = sh | ((opcode >> 1) & 1) << 5;
    bool rc = opcode & 1;

    uint64_t val = r[rs];
    val = std::rotl(val, sh);

    uint64_t mask = 0;
    for (int i = 0; i < 64; i++)
    {
        if (i >= me)
            mask |= (1 << i);
    }

    r[ra] = val & mask;

    if (rc)
    {
        int64_t val = (int64_t)r[ra];
        if (val < 0) cr.cr[0].val = 0b1000;
        else if (val > 0) cr.cr[0].val = 0b0100;
        else cr.cr[0].val = 0b10;
    }

    if constexpr (canDisassemble)
        printf("rldicr%s r%d, r%d, %d, %d\n", rc ? "." : "", ra, rs, sh, me);
}

void CellPPU::Code1F(uint32_t opcode)
{
    uint32_t xo = (opcode >> 1) & 0x3FF;

    switch (xo)
    {
    case 0x153:
        Mfspr(opcode);
        break;
    default:
        printf(std::format("Unhandled opcode 0x1F {:#x}\n", xo).c_str());
        exit(1);
    }
}

void CellPPU::Mfspr(uint32_t opcode)
{
    int rt = ((opcode >> 21) & 0x1F);
    uint16_t spr = (opcode >> 11) & 0x3FF;

    spr = (spr >> 5) | ((spr & 0x1F) << 5);

    switch (spr)
    {
    case 8:
        r[rt] = lr;
        if constexpr (canDisassemble)
            printf("mflr r%d\n", rt);
        break;
    default:
        printf("Unknown move from spr %d\n", spr);
        exit(1);
    }
}

void CellPPU::Ld(uint32_t opcode)
{
    int rt = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int16_t ds = opcode & 0xFFFC;
    bool u = (opcode & 1);

    uint64_t offs;
    if (ra == 0)
        offs = 0;
    else
        offs = r[ra];

    uint64_t ea = offs + ds;

    r[rt] = manager->Read64(ea);

    if (u)
        r[ra] = ea;

    if constexpr (canDisassemble)
        printf("ld%s r%d, %d(r%d)\n", u ? "u" : "", rt, ds, ra);
}

void CellPPU::Std(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int16_t ds = opcode & 0xFFFC;
    bool u = opcode & 1;

    uint64_t offs;
    if (ra == 0)
        offs = 0;
    else
        offs = r[ra];

    uint64_t ea = offs + ds;

    manager->Write64(ea, r[rs]);

    if (u)
        r[ra] = ea;

    if constexpr (canDisassemble)
    {
        printf("std%s r%d, %d(r%d)\n", u ? "u" : "", rs, ds, ra);
    }
}
