#include "PPU.h"
#include <loaders/Elf.h>
#include <kernel/ModuleManager.h>

#include <stdio.h>
#include <bit>
#include <stdexcept>
#include <format>

static uint64_t rotate_mask[64][64];
void InitRotateMask()
{
	static bool inited = false;
	if(inited) return;

	for(uint32_t mb=0; mb<64; mb++) for(uint32_t me=0; me<64; me++)
	{
		const uint64_t mask = ((uint64_t)-1 >> mb) ^ ((me >= 63) ? 0 : (uint64_t)-1 >> (me + 1));	
		rotate_mask[mb][me] = mb > me ? ~mask : mask;
	}

	inited = true;
}

#define rt_d ((opcode >> 21) & 0x1F)
#define rs_d rt_d
#define ra_d ((opcode >> 16) & 0x1F)
#define rb_d ((opcode >> 11) & 0x1F)
#define si_d (opcode & 0xFFFF)

using std::placeholders::_1;

void CellPPU::InitInstructionTable()
{
    opcodes[0x08] = std::bind(&CellPPU::Subfic, this, _1);
    opcodes[0x0B] = std::bind(&CellPPU::Cmpi, this, _1);
    opcodes[0x0E] = std::bind(&CellPPU::Addi, this, _1);
    opcodes[0x0F] = std::bind(&CellPPU::Addis, this, _1);
    opcodes[0x10] = std::bind(&CellPPU::BranchCond, this, _1);
    opcodes[0x12] = std::bind(&CellPPU::Branch, this, _1);
    opcodes[0x13] = std::bind(&CellPPU::BranchCondR, this, _1);
    opcodes[0x18] = std::bind(&CellPPU::Ori, this, _1);
    opcodes[0x19] = std::bind(&CellPPU::Oris, this, _1);
    opcodes[0x1E] = std::bind(&CellPPU::Rldicr, this, _1);
    opcodes[0x1F] = std::bind(&CellPPU::Code1F, this, _1);
    opcodes[0x20] = std::bind(&CellPPU::Lwz, this, _1);
    opcodes[0x3A] = std::bind(&CellPPU::Ld, this, _1);
    opcodes[0x3E] = std::bind(&CellPPU::Std, this, _1);

    InitRotateMask();
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

void CellPPU::Subfic(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int64_t si = (int64_t)(int16_t)si_d;

    r[rt] = si - (int64_t)r[ra];
    xer.ca = r[ra] <= si;

    printf("subfic r%d,r%d,%d\n", rt, ra, si);
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
    int16_t si = (int16_t)si_d;

    int64_t val_ra = r[ra];
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
            printf("subi r%d, r%d, 0x%04x\n", rt, ra, -((int16_t)si));
    }
}

void CellPPU::Addis(uint32_t opcode)
{
    uint8_t rt = rt_d;
    uint8_t ra = ra_d;
    int32_t si = si_d;

    si <<= 16;

    if (ra == 0)
    {
        r[rt] = si;
        printf("lis r%d,0x%08x\n", rt, si);
    }
    else
    {
        r[rt] = r[ra] + si;
        if (si < 0)
            printf("subis r%d,r%d,%d\n", rt, ra, -si);
        else
            printf("addis r%d,r%d,%d\n", rt, ra, si);
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

    uint64_t old_lr = lr;

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

void CellPPU::BranchCondR(uint32_t opcode)
{
    uint8_t bi = (opcode >> 16) & 0x1F;
    uint8_t bo = (opcode >> 21) & 0x1F;

    bool lk = opcode & 1;

    uint64_t old_lk = lr;

    if (lk)
        lr = pc;

    if (CheckCondition(bo, bi))
    {
        switch ((opcode >> 1) & 0x3FF)
        {
        case 0x10:
            pc = lr;
            printf("bclr%s\n", lk ? "l" : "");
            break;
        case 0x210:
            if (syscall_nids.find(r[12]) != syscall_nids.end())
            {
                Modules::DoHLECall(syscall_nids[r[12]], this);
                break;
            }
            else
            {
                pc = ctr;
                printf("bctr%s (0x%08lx)\n", lk ? "l" : "", ctr);
                break;
            }
        default:
            printf("Branch to unknown register 0x%03x\n", (opcode >> 1) & 0x3FF);
            exit(1);
        }
        printf("Branch taken\n");
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

    r[ra] = val & rotate_mask[0][me];

    if (rc)
    {
        int64_t val = (int64_t)r[ra];
        if (val < 0) cr.cr[0].val = 0x8;
        else if (val > 0) cr.cr[0].val = 0x4;
        else cr.cr[0].val = 0x2;
    }

    if constexpr (canDisassemble)
        printf("rldicr%s r%d, r%d, %d, %d\n", rc ? "." : "", ra, rs, sh, me);
}

void CellPPU::Code1F(uint32_t opcode)
{
    if (((opcode >> 1) & 0x3FF) == 0x1D3)
    {
        Mtspr(opcode);
        return;
    }
    else if (((opcode >> 1) & 0x3FF) == 0x153)
    {
        Mfspr(opcode);
        return;
    }
    else if (((opcode >> 1) & 0x1FF) == 0x28)
    {
        Subf(opcode);
        return;
    }
    else if (((opcode >> 2) & 0x1FF) == 0x19D)
    {
        Sradi(opcode);
        return;
    }
    else if (((opcode >> 1) & 0x1FF) == 0xCA)
    {
        Addze(opcode);
        return;
    }
    else if (((opcode >> 1) & 0x1FF) == 0x1BC)
    {
        Or(opcode);
        return;
    }
    else if (((opcode >> 1) & 0x1FF) == 0x10A)
    {
        Add(opcode);
        return;
    }
    
    printf(std::format("Unhandled opcode 0x1F ({:#x})\n", opcode).c_str());
    exit(1);
}

void CellPPU::Subf(uint32_t opcode)
{
    if ((opcode >> 10) & 1)
    {
        printf("SUBFO!\n");
        exit(1);
    }

    int rt = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int rb = ((opcode >> 11) & 0x1F);
    bool rc = opcode & 1;

    if (rc)
    {
        r[rt] = r[rb] - r[ra];
        if ((int64_t)r[rt] < 0) cr.cr[0].val = 8;
        else if ((int64_t)r[rt] > 0) cr.cr[0].val = 4;
        else if ((int64_t)r[rt] == 0) cr.cr[0].val = 2;
        printf("subf. r%d,r%d,r%d\n", rt, rb, ra);
    }
    else
    {
        r[rt] = r[rb] - r[ra];
        printf("subf r%d,r%d,r%d\n", rt, rb, ra);
    }
}

void CellPPU::Addze(uint32_t opcode)
{
    if ((opcode >> 10) & 1)
    {
        printf("ADDZEO!\n");
        exit(1);
    }

    int rt = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);

    int64_t ra_ = r[ra];

    r[rt] = ra_ + xer.ca;
    xer.ca = (ra_ > (~xer.ca));

    if (opcode & 1)
    {
        if ((int64_t)r[rt] < 0) cr.cr[0].val = 8;
        else if ((int64_t)r[rt] > 0) cr.cr[0].val = 4;
        else if (r[rt] == 0) cr.cr[0].val = 2;
        printf("addze. r%d,r%d\n", rt, ra);
    }
    else
        printf("addze r%d,r%d\n", rt, ra);
}

void CellPPU::Add(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;
    bool oe = (opcode >> 10) & 1;
    bool rc = opcode & 1;

    if (oe)
    {
        printf("ERROR: Unhandled addo[.]\n");
        exit(1);
    }

    r[rt] = r[ra] + r[rb];

    if (rc)
    {
        int64_t val = (int64_t)r[rt];
        if (val < 0) cr.cr[0].val = 0x8;
        else if (val > 0) cr.cr[0].val = 0x4;
        else cr.cr[0].val = 0x2;
        printf("add. r%d,r%d,r%d", rt, ra, rb);
    }
    else
        printf("add r%d,r%d,r%d\n", rt, ra, rb);
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

void CellPPU::Sradi(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int sh = (((opcode >> 11) & 0x1F) << 1) | ((opcode >> 1) & 1);
    bool rc = opcode & 1;

    int64_t rs_ = r[rs];
    r[ra] = rs_ >> sh;

    xer.ca = (rs_ < 0) && ((r[ra] << sh) != rs_);
    
    if (rc)
    {
        if ((int64_t)r[ra] < 0) cr.cr[0].val = 8;
        else if ((int64_t)r[ra] > 0) cr.cr[0].val = 4;
        else if (r[ra] == 0) cr.cr[0].val = 2;
    }

    printf("sradi r%d,r%d,%d\n", ra, rs, sh);
}

void CellPPU::Or(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    r[ra] = r[rs] | r[rb];

    if (opcode & 1)
    {
        int64_t val = (int64_t)r[ra];
        if (val < 0) cr.cr[0].val = 0x8;
        else if (val > 0) cr.cr[0].val = 0x4;
        else cr.cr[0].val = 0x2;
    }

    if (rs == rb)
        printf("mv%s r%d,r%d\n", (opcode & 1) ? "." : "", ra, rs);
    else
        printf("or%s r%d,r%d,r%d\n", (opcode&1) ? "." : "", ra, rs, rb);
}

void CellPPU::Mtspr(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    uint16_t spr = (opcode >> 11) & 0x3FF;

    spr = (spr >> 5) | ((spr & 0x1F) << 5);

    switch (spr)
    {
    case 8:
        lr = r[rs];
        if constexpr (canDisassemble)
            printf("mtlr r%d\n", rs);
        break;
    case 9:
        ctr = r[rs];
        if constexpr (canDisassemble)
            printf("mtctr r%d\n", rs);
        break;
    default:
        printf("Unknown move to spr %d\n", spr);
        exit(1);
    }
}

void CellPPU::Lwz(uint32_t opcode)
{
    uint8_t rt = (opcode >> 21) & 0x1F;
    uint8_t ra = (opcode >> 16) & 0x1F;
    int32_t d = (int32_t)(int16_t)(opcode & 0xFFFF);

    uint64_t ea;
    if (ra)
        ea = r[ra] + d;
    else
        ea = d;

    r[rt] = manager->Read32(ea);

    printf("lwz r%d, %d(r%d) (0x%08lx)\n", rt, d, ra, ea);
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
