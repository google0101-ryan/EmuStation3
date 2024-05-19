#include "PPU.h"
#include <loaders/Elf.h>
#include <kernel/ModuleManager.h>

#include <stdio.h>
#include <bit>
#include <stdexcept>
#include <format>
#include <cassert>
#include <string.h>
#include <emmintrin.h>
#include <math.h>
#include <tmmintrin.h>

float rsqrt(float f) 
{
    return 1 / sqrtf(f);
}

template<typename T>
struct add_flags_result_t
{
	T result;
	bool carry;

	add_flags_result_t() = default;

	// Straighforward ADD with flags
	add_flags_result_t(T a, T b)
		: result(a + b)
		, carry(result < a)
	{
	}

	// Straighforward ADC with flags
	add_flags_result_t(T a, T b, bool c)
		: add_flags_result_t(a, b)
	{
		add_flags_result_t r(result, c);
		result = r.result;
		carry |= r.carry;
	}
};

static add_flags_result_t<uint64_t> add64_flags(uint64_t a, uint64_t b)
{
	return {a, b};
}

static add_flags_result_t<uint64_t> add64_flags(uint64_t a, uint64_t b, bool carry)
{
	return {a, b, carry};
}

static const uint64_t DOUBLE_SIGN = 0x8000000000000000ULL;
static const uint64_t DOUBLE_EXP  = 0x7FF0000000000000ULL;
static const uint64_t DOUBLE_FRAC = 0x000FFFFFFFFFFFFFULL;
static const uint64_t DOUBLE_ZERO = 0x0000000000000000ULL;

static uint64_t branchTarget(const uint64_t pc, const int64_t imm)
{
    return pc + (imm & ~0x3ULL);
}

// https://stackoverflow.com/a/5996635/17853274
uint128_t sll128(uint128_t n, size_t shift)
{
	if (shift >= 32)
	{
		n.u32[0] = n.u32[1];
		n.u32[1] = n.u32[2];
		n.u32[2] = n.u32[3];
		n.u32[3] = 0;
		sll128(n, shift-32);
	}
	else
	{
		n.u32[0] = (n.u32[0] << shift) | (n.u32[1] >> (32-shift));
		n.u32[1] = (n.u32[1] << shift) | (n.u32[2] >> (32-shift));
		n.u32[2] = (n.u32[2] << shift) | (n.u32[3] >> (32-shift));
		n.u32[3] = (n.u32[3] << shift);
	}
	return n;
}

uint32_t lastBranchTarget = 0;

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
#define rc_d ((opcode >> 6) & 0x1F)

#define vD ((opcode >> 21) & 0x1F)
#define vS ((opcode >> 21) & 0x1F)
#define vA ((opcode >> 16) & 0x1F)
#define vB ((opcode >> 11) & 0x1F)
#define vC ((opcode >> 5) & 0x1F)

using std::placeholders::_1;

void CellPPU::InitInstructionTable()
{
    opcodes[0x04] = std::bind(&CellPPU::G_04, this, _1);
	opcodes[0x07] = std::bind(&CellPPU::Mulli, this, _1);
    opcodes[0x08] = std::bind(&CellPPU::Subfic, this, _1);
    opcodes[0x0A] = std::bind(&CellPPU::Cmpli, this, _1);
    opcodes[0x0B] = std::bind(&CellPPU::Cmpi, this, _1);
    opcodes[0x0C] = std::bind(&CellPPU::Addic, this, _1);
	opcodes[0x0F] = std::bind(&CellPPU::Addis, this, _1);
    opcodes[0x0E] = std::bind(&CellPPU::Addi, this, _1);
    opcodes[0x10] = std::bind(&CellPPU::BranchCond, this, _1);
    opcodes[0x12] = std::bind(&CellPPU::Branch, this, _1);
    opcodes[0x13] = std::bind(&CellPPU::G_13, this, _1);
	opcodes[0x14] = std::bind(&CellPPU::Rlwimi, this, _1);
    opcodes[0x15] = std::bind(&CellPPU::Rlwinm, this, _1);
    opcodes[0x17] = std::bind(&CellPPU::Rlwnm, this, _1);
    opcodes[0x18] = std::bind(&CellPPU::Ori, this, _1);
    opcodes[0x19] = std::bind(&CellPPU::Oris, this, _1);
    opcodes[0x1A] = std::bind(&CellPPU::Xori, this, _1);
    opcodes[0x1B] = std::bind(&CellPPU::Xoris, this, _1);
    opcodes[0x1E] = std::bind(&CellPPU::G_1E, this, _1);
    opcodes[0x1F] = std::bind(&CellPPU::G_1F, this, _1);
    opcodes[0x20] = std::bind(&CellPPU::Lwz, this, _1);
    opcodes[0x21] = std::bind(&CellPPU::Lwzu, this, _1);
	opcodes[0x22] = std::bind(&CellPPU::Lbz, this, _1);
    opcodes[0x23] = std::bind(&CellPPU::Lbzu, this, _1);
    opcodes[0x24] = std::bind(&CellPPU::Stw, this, _1);
    opcodes[0x25] = std::bind(&CellPPU::Stwu, this, _1);
    opcodes[0x26] = std::bind(&CellPPU::Stb, this, _1);
    opcodes[0x27] = std::bind(&CellPPU::Stbu, this, _1);
    opcodes[0x28] = std::bind(&CellPPU::Lhz, this, _1);
    opcodes[0x29] = std::bind(&CellPPU::Lhzu, this, _1);
    opcodes[0x2C] = std::bind(&CellPPU::Sth, this, _1);
    opcodes[0x30] = std::bind(&CellPPU::Lfs, this, _1);
    opcodes[0x32] = std::bind(&CellPPU::Lfd, this, _1);
    opcodes[0x34] = std::bind(&CellPPU::Stfs, this, _1);
    opcodes[0x35] = std::bind(&CellPPU::Stfsu, this, _1);
    opcodes[0x36] = std::bind(&CellPPU::Stfd, this, _1);
    opcodes[0x3A] = std::bind(&CellPPU::G_3A, this, _1);
    opcodes[0x3B] = std::bind(&CellPPU::G_3B, this, _1);
    opcodes[0x3E] = std::bind(&CellPPU::G_3E, this, _1);
    opcodes[0x3F] = std::bind(&CellPPU::G_3F, this, _1);

    InitRotateMask();
}

bool CellPPU::CheckCondition(uint32_t bo, uint32_t bi)
{
    const uint8_t bo0 = (bo & 0x10) ? 1 : 0;
    const uint8_t bo1 = (bo & 0x08) ? 1 : 0;
    const uint8_t bo2 = (bo & 0x04) ? 1 : 0;
    const uint8_t bo3 = (bo & 0x02) ? 1 : 0;

    if (!bo2) --state.ctr;

    const uint8_t ctr_ok = bo2 | ((state.ctr != 0) ^ bo3);
    const uint8_t cond_ok = bo0 | (IsCR(bi) ^ (~bo1 & 1));

    return ctr_ok && cond_ok;
}

uint64_t rotl64(const uint64_t x, const uint8_t n) {return (x << n) | (x >> (64-n));}
uint32_t rotl32(const uint32_t x, const uint8_t n) {return (x << n) | (x >> (32-n));}

void CellPPU::G_04(uint32_t opcode)
{
    switch (opcode & 0x7FF)
    {
	case 0x14A:
		Vrsqrtefp(opcode);
		break;
	case 0x28C:
		Vspltw(opcode);
		break;
	case 0x484:
		Vor(opcode);
		break;
    case 0x4C4:
        Vxor(opcode);
        break;
    default:
		switch (opcode & 0x3F)
		{
		case 0x02A:
			Vsel(opcode);
			break;
		case 0x2B:
			Vperm(opcode);
			break;
		case 0x2C:
			Vsldoi(opcode);
			break;
		case 0x2E:
			Vmaddfp(opcode);
			break;
		case 0x2F:
			Vnmsubfp(opcode);
			break;
		default:
			switch (opcode & 0x7FF)
			{
			case 0x04A:
				Vsubfp(opcode);
				break;
			case 0x08C:
				Vmrghw(opcode);
				break;
			case 0x184:
				Vslw(opcode);
				break;
			case 0x18C:
				Vmrglw(opcode);
				break;
			case 0x38C:
				Vspltisw(opcode);
				break;
			default:
				printf("Unknown AltiVec instruction 0x%03x (0x%08x)\n", (opcode & 0x7FF), opcode);
				throw std::runtime_error("Couldn't execute AltiVec instruction");
			}
		}
    }
}

void CellPPU::Vsel(uint32_t opcode)
{
	int va = vA;
	int vb = vB;
	int vc = vC;
	int vd = vD;

	for (int i = 0; i < 2; i++)
		state.vpr[vd].u64[i] = (state.vpr[vb].u64[i] & state.vpr[vc].u64[i]) | (state.vpr[va].u64[i] & ~state.vpr[vc].u64[i]);

	if (canDisassemble)
		printf("vsel v%d,v%d,v%d,v%d\n", vd, va, vb, vc);
}

void CellPPU::Vperm(uint32_t opcode)
{
	int va = vA;
	int vb = vB;
	int vc = vC;
	int vd = vD;

	__m128i a, b, c;
	memcpy(&a, state.vpr[va].u8, 16);
	memcpy(&b, state.vpr[vb].u8, 16);
	memcpy(&c, state.vpr[vc].u8, 16);

	const auto index = _mm_andnot_si128(c, _mm_set1_epi8(0x1F));
	const auto mask = _mm_cmpgt_epi8(index, _mm_set1_epi8(0xf));
	const auto sa = _mm_shuffle_epi8(a, index);
	const auto sb = _mm_shuffle_epi8(b, index);
	const auto res = _mm_or_si128(_mm_and_si128(mask, sa), _mm_andnot_si128(mask, sb));
	memcpy(state.vpr[vd].u8, &res, 16);

	if (canDisassemble)
		printf("vperm v%d,v%d,v%d,v%d\n", vd, va, vb, vc);
}

void CellPPU::Vsldoi(uint32_t opcode)
{
	int vd = vD;
    int va = vA;
    int vb = vB;
	int shb = ((opcode >> 6) & 0xF) << 3;

	uint8_t src[32];
	memcpy(src, &state.vpr[vb].u128, 16);
	memcpy(&src[16], &state.vpr[vb].u128, 16);
	
	for (int b = 0; b < 16; b++)
	{
		state.vpr[vd].u8[15 - b] = src[31 - (b + shb)];
	}

	if (canDisassemble)
		printf("vsldoi v%d,v%d,v%d,%d\n", vd, va, vb, shb);
}

void CellPPU::Vnmsubfp(uint32_t opcode)
{
	int vd = vD;
    int va = vA;
    int vb = vB;
	int vc = vC;

	for (int i = 0; i < 4; i++)
		state.vpr[vd].f[i] = -(float)((state.vpr[va].f[i] * state.vpr[vc].f[i]) - state.vpr[vb].f[i]);
	
	if (canDisassemble)
		printf("vnmsubfp v%d,v%d,v%d,v%d\n", vd, va, vc, vb);
}

void CellPPU::Vmaddfp(uint32_t opcode)
{
	int vd = vD;
    int va = vA;
    int vb = vB;
	int vc = vC;

	for (int i = 0; i < 4; i++)
		state.vpr[vd].f[i] = (state.vpr[va].f[i] * state.vpr[vc].f[i]) + state.vpr[vd].f[i];
	
	if (canDisassemble)
		printf("vmaddfp v%d,v%d,v%d,v%d\n", vd, va, vc, vb);
}

void CellPPU::Vsubfp(uint32_t opcode)
{
	int vd = vD;
    int va = vA;
    int vb = vB;

	for (int i = 0; i < 4; i++)
		state.vpr[vd].f[i] = state.vpr[va].f[i] - state.vpr[vb].f[i];
	
	if (canDisassemble)
		printf("vsubfp v%d,v%d,v%d\n", vd, va, vb);
}

void CellPPU::Vmrghw(uint32_t opcode)
{
	int vd = vD;
	int va = vA;
	int vb = vB;

	state.vpr[vd].u32[0] = state.vpr[va].u32[0];
	state.vpr[vd].u32[1] = state.vpr[vb].u32[0];
	state.vpr[vd].u32[2] = state.vpr[va].u32[1];
	state.vpr[vd].u32[3] = state.vpr[vb].u32[1];

	if (canDisassemble)
		printf("vmrghw v%d,v%d,v%d\n", vd, va, vb);
}

void CellPPU::Vrsqrtefp(uint32_t opcode)
{
	uint8_t vd = vD;
	uint8_t vb = vB;

	for (int i = 0; i < 4; i++)
	{
			state.vpr[vd].f[i] = rsqrt(state.vpr[vb].f[i]);
	}
	
	if (canDisassemble)
		printf("vrsqrtefp v%d,v%d\n", vd, vb);
}

void CellPPU::Vslw(uint32_t opcode)
{
	uint8_t vd = vD;
	uint8_t va = vA;
	uint8_t vb = vB;

	for (int i = 0; i < 4; i++)
	{
		int sh = state.vpr[vb].u32[i] & 0x1F;
		state.vpr[vd].u32[i] = state.vpr[va].u32[i] << sh;
	}

	if (canDisassemble)
		printf("vslw v%d,v%d,v%d\n", vd, va, vb);
}

void CellPPU::Vmrglw(uint32_t opcode)
{
	int vd = vD;
	int va = vA;
	int vb = vB;

	state.vpr[vd].u32[0] = state.vpr[va].u32[2];
	state.vpr[vd].u32[1] = state.vpr[vb].u32[2];
	state.vpr[vd].u32[2] = state.vpr[va].u32[3];
	state.vpr[vd].u32[3] = state.vpr[vb].u32[3];

	if (canDisassemble)
		printf("vmrglw v%d,v%d,v%d\n", vd, va, vb);
}

void CellPPU::Vspltw(uint32_t opcode)
{
	uint8_t vd = vD;
	uint8_t vb = vB;
	uint8_t uimm = (opcode >> 16) & 0x1F;

	for (int i = 0; i < 4; i++)
		state.vpr[vd].u32[i] = state.vpr[vb].u32[uimm];
	
	if (canDisassemble)
		printf("vspltw v%d,v%d,%d\n", vd, vb, uimm);
}

void CellPPU::Vspltisw(uint32_t opcode)
{
	uint8_t vd = vD;
	uint8_t vb = vB;
	uint8_t simm = (opcode >> 16) & 0x1F;
	uint32_t imm = (simm & 0x10) ? (uint32_t)-1 : simm;

	for (int i = 0; i < 4; i++)
		state.vpr[vd].u32[i] = imm;
	
	if (canDisassemble)
		printf("vspltisw v%d,v%d,0x%08x\n", vd, vb, imm);
}

void CellPPU::Vor(uint32_t opcode)
{
	int vd = vD;
    int va = vA;
    int vb = vB;

    state.vpr[vd].u32[0] = state.vpr[va].u32[0] | state.vpr[vb].u32[0];
    state.vpr[vd].u32[1] = state.vpr[va].u32[1] | state.vpr[vb].u32[1];
    state.vpr[vd].u32[2] = state.vpr[va].u32[2] | state.vpr[vb].u32[2];
    state.vpr[vd].u32[3] = state.vpr[va].u32[3] | state.vpr[vb].u32[3];

    if (canDisassemble)
        printf("vor v%d, v%d, v%d\n", vd, va, vb);
}

void CellPPU::Vxor(uint32_t opcode)
{
    int vd = vD;
    int va = vA;
    int vb = vB;

    state.vpr[vd].u32[0] = state.vpr[va].u32[0] ^ state.vpr[vb].u32[0];
    state.vpr[vd].u32[1] = state.vpr[va].u32[1] ^ state.vpr[vb].u32[1];
    state.vpr[vd].u32[2] = state.vpr[va].u32[2] ^ state.vpr[vb].u32[2];
    state.vpr[vd].u32[3] = state.vpr[va].u32[3] ^ state.vpr[vb].u32[3];

    if (canDisassemble)
        printf("vxor v%d, v%d, v%d\n", vd, va, vb);
}

void CellPPU::Mulli(uint32_t opcode)
{
	int rd = rt_d;
	int ra = ra_d;
	int16_t simm16 = (int16_t)si_d;

	state.r[rd] = (int64_t)state.r[ra] * simm16;

	if (canDisassemble)
		printf("mulli r%d,r%d,%d\n", rd, ra, simm16);
}

void CellPPU::Subfic(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int64_t si = (int64_t)(int16_t)si_d;
	uint64_t a = state.r[ra];

	const auto res = add64_flags(~a, si, 1);
	state.r[rt] = res.result;
	state.xer.ca = res.carry;

    if (canDisassemble)
        printf("subfic r%d,r%d,%ld\n", rt, ra, si);
}

void CellPPU::Cmpli(uint32_t opcode)
{
    uint8_t bf = (opcode >> 23) & 0x7;
    bool l = (opcode >> 21) & 1;
    uint8_t ra = ra_d;
    uint64_t ui = si_d;
    
    UpdateCRnU(l, bf, state.r[ra], ui);

    if (canDisassemble)
        printf("cmpl%si state.cr%d,r%d,%ld\n", l ? "d" : "w", bf, ra, ui);
}

void CellPPU::Cmpi(uint32_t opcode)
{
    uint8_t bf = (opcode >> 23) & 0x7;
    bool l = (opcode >> 21) & 1;
    uint8_t ra = (opcode >> 16) & 0x1F;
    int64_t si = (int64_t)(int16_t)(opcode & 0xffff);
    
    UpdateCRnS(l, bf, state.r[ra], si);

    if (canDisassemble)
        printf("cmp%si state.cr%d,r%d,%ld\n", l ? "d" : "w", bf, ra, si);
}

void CellPPU::Addic(uint32_t opcode)
{
	uint8_t rt = rt_d;
    uint8_t ra = ra_d;
    int64_t si = (int64_t)(int16_t)si_d;

	state.xer.ca = ((uint32_t)si < !((uint32_t)state.r[ra]));
	state.r[rt] = state.r[ra] + si;

	if (canDisassemble)
	{
		if ((int16_t)si >= 0)
            printf("addic r%d, r%d, 0x%04lx\n", rt, ra, si);
        else
            printf("subic r%d, r%d, 0x%04x\n", rt, ra, -((int16_t)si));
    }
}

void CellPPU::Addi(uint32_t opcode)
{
    uint8_t rt = rt_d;
    uint8_t ra = ra_d;
    int64_t si = (int64_t)(int16_t)si_d;

    state.r[rt] = ra ? (state.r[ra] + si) : si;

    if (canDisassemble)
    {
        if (!ra)
            printf("li r%d, 0x%04lx\n", rt, si);
        else if ((int16_t)si >= 0)
            printf("addi r%d, r%d, 0x%04lx\n", rt, ra, si);
        else
            printf("subi r%d, r%d, 0x%04x\n", rt, ra, -((int16_t)si));
    }
}

void CellPPU::Addis(uint32_t opcode)
{
    uint8_t rt = rt_d;
    uint8_t ra = ra_d;
    int32_t si = (int16_t)si_d;

    state.r[rt] = (int64_t)(ra ? (state.r[ra] + (si << 16)) :  (int64_t)(int32_t)(si << 16));

    if (canDisassemble)
        printf("addis r%d,r%d,%d\n", rt, ra, si);
}

void CellPPU::BranchCond(uint32_t opcode)
{
    bool lk = opcode & 1;
    bool aa = (opcode >> 1) & 1;
    int64_t bd = (int64_t)(int16_t)(opcode & 0xFFFC);
    uint8_t bi = (opcode >> 16) & 0x1F;
    uint8_t bo = (opcode >> 21) & 0x1F;
    
    uint64_t target = branchTarget((aa ? 0 : state.pc-4), bd);
    lastBranchTarget = target;
    
	if (lk) state.lr = state.pc;

    if (canDisassemble)
        printf("bc 0x%08lx (0x%08lx) ", target, bd);

    if (!CheckCondition(bo, bi))
    {
        if (canDisassemble)
            printf("[passed]\n");
    }
    else
    {
        state.pc = target;
        if (canDisassemble)
            printf("[taken]\n");
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

    if (lk) state.lr = state.pc;

    state.pc = branchTarget(aa ? 0 : state.pc - 4, li);

    if (canDisassemble)
        printf("b%s%s 0x%08lx\n", lk ? "l" : "", aa ? "a" : "", state.pc);

    lastBranchTarget = state.pc;
}

void CellPPU::G_13(uint32_t opcode)
{
    uint16_t field = (opcode >> 1) & 0x3FF;

    switch (field)
    {
    case 0x010:
        Bclr(opcode);
        break;
    case 0x1C1:
        Cror(opcode);
        break;
    case 0x210:
        Bcctr(opcode);
        break;
    default:
        printf("Unknown opcode 0x%02x (0x%08x)\n", field, opcode);
        exit(1);
    }
}

void CellPPU::Bclr(uint32_t opcode)
{
    int bo = (opcode >> 21) & 0x1F;
    int bi = (opcode >> 16) & 0x1F;
    int bh = (opcode >> 11) & 0x3;
    bool lk = opcode & 1;

    if (canDisassemble)
        printf("bcstate.lr ");
	
	uint64_t old_lr = state.lr;
	if (lk) state.lr = state.pc;

    if (!CheckCondition(bo, bi))
    {
        if (canDisassemble)
            printf("[passed]\n");
        return;
    }
    else
    {
        state.pc = branchTarget(0, old_lr);
        if (canDisassemble)
            printf("[taken]\n");
    }
}

void CellPPU::Cror(uint32_t opcode)
{
    int crbt = rt_d;
    int crba = ra_d;
    int crbb = rb_d;

    const uint8_t v = IsCR(crba) | IsCR(crbb);
    SetCRBit2(crbt, v & 1);
    if (canDisassemble)
        printf("cror state.cr%d,state.cr%d,state.cr%d\n", crbt, crba, crbb);
}

void CellPPU::Bcctr(uint32_t opcode)
{
    int bo = (opcode >> 21) & 0x1F;
    int bi = (opcode >> 16) & 0x1F;
    int bh = (opcode >> 11) & 0x3;
    bool lk = opcode & 1;

    if (canDisassemble)
        printf("bcctr ");

    if (bo & 0x10 || IsCR(bi) == (bo & 0x8))
    {
        if (lk) state.lr = state.pc;

        if (syscall_nids.find(state.r[12]) != syscall_nids.end()
                 && syscall_nids.find(lastBranchTarget) != syscall_nids.end())
        {
            Modules::DoHLECall(syscall_nids[state.r[12]], this);
            state.pc = state.lr;
        }
        else
        {
            if (canDisassemble)
                printf("[taken] (0x%08x)\n", state.lr);
            state.pc = branchTarget(0, state.ctr);
        }
    }
    else if (canDisassemble)
        printf("[passed]\n");
}

void CellPPU::Rlwimi(uint32_t opcode)
{
	uint8_t rs = rs_d;
	uint8_t ra = ra_d;
	uint8_t sh = (opcode >> 11) & 0x1F;
	uint8_t mb = (opcode >> 6) & 0x1F;
    uint8_t me = (opcode >> 1) & 0x1F;
	bool rc = opcode & 1;

	uint32_t mask = rotate_mask[32 + mb][32 + me];
	state.r[ra] = (state.r[ra] & ~mask) | (std::rotl<uint32_t>(state.r[rs], sh) & mask);
	if (rc)
		UpdateCR0<int32_t>(state.r[ra]);
	
	if (canDisassemble)
		printf("rlwimi r%d,r%d,%d,%d,%d (0x%08x)\n", ra, rs, sh, mb, me, mask);
}

// void CellPPU::BranchCondR(uint32_t opcode)
// {
//     uint8_t bi = (opcode >> 16) & 0x1F;
//     uint8_t bo = (opcode >> 21) & 0x1F;

//     bool lk = opcode & 1;

//     uint64_t old_lk = state.lr;

//     if (lk)
//         state.lr = state.pc;

//     switch ((opcode >> 1) & 0x3FF)
//     {
//     case 0x210:
//         if (CheckCondition(bo, bi))
//         {
//             if (syscall_nids.find(state.r[12]) != syscall_nids.end()
//                 && syscall_nids.find(lastBranchTarget) != syscall_nids.end())
//             {
//                 Modules::DoHLECall(syscall_nids[state.r[12]], this);
//                 state.pc = state.lr;
//                 break;
//             }
//             else
//             {
//                 state.pc = state.ctr;
//                 printf("bctr%s (0x%08lx)\n", lk ? "l" : "", state.ctr);
//                 break;
//             }
//         }
//         else
//             break;
//     default:
//         printf("Branch to unknown register 0x%03x\n", (opcode >> 1) & 0x3FF);
//         exit(1);
//     }
// }

void CellPPU::Ori(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    uint64_t ui = si_d;

    state.r[ra] = state.r[rs] | ui;

    if (canDisassemble)
        printf("ori r%d, r%d, 0x%04lx\n", ra, rs, ui);
}

void CellPPU::Oris(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    uint64_t ui = si_d;

    state.r[ra] = state.r[rs] | (ui << 16);

    if (canDisassemble)
        printf("oris r%d, r%d, 0x%04lx\n", ra, rs, ui);
}

void CellPPU::Xori(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    uint64_t ui = si_d;

    state.r[ra] = state.r[rs] ^ ui;

    if (canDisassemble)
        printf("xori r%d, r%d, 0x%04lx\n", ra, rs, ui);
}

void CellPPU::Xoris(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    uint64_t ui = si_d;

    state.r[ra] = state.r[rs] ^ (ui << 16);

    if (canDisassemble)
        printf("xoris r%d, r%d, 0x%04lx\n", ra, rs, ui << 16);
}

void CellPPU::G_1E(uint32_t opcode)
{
    uint8_t field = (opcode >> 2) & 0x7;

    switch (field)
    {
    case 0x00:
        Rldicl(opcode);
        break;
    case 0x01:
        Rldicr(opcode);
        break;
    case 0x02:
        Rldic(opcode);
        break;
    case 0x03:
        Rldimi(opcode);
        break;
    default:
        printf("Unknown opcode (0x%02x) 0x%08x\n", field, opcode);
        exit(1);
    }
}

void CellPPU::Rldicl(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int sh = ((opcode >> 11) & 0x1F) | (((opcode >> 1) & 1) << 5);
    int mb = (opcode >> 5) & 0x3F;
    mb = ((mb & 0x3E) >> 1) | ((mb & 1) << 5);

    state.r[ra] = rotl64(state.r[rs], sh) & rotate_mask[mb][63];
    if (opcode & 1)
        UpdateCR0<int64_t>(state.r[ra]);
    if (canDisassemble)
    {
        if (sh == 0)
            printf("cstate.lrldi r%d,r%d,%d (0x%08lx, 0x%08lx)\n", ra, rs, mb, rotate_mask[mb][63], state.r[ra]);
        else
            printf("rldicl r%d,r%d,%d,%d (0x%08lx)\n", rs, ra, sh, mb, rotate_mask[mb][63]);
    }
}

void CellPPU::Rldicr(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int sh = ((opcode >> 11) & 0x1F);
    int me = (opcode >> 5) & 0x3F;
    me = ((me & 0x3E) >> 1) | ((me & 1) << 5);
    sh = sh | ((opcode >> 1) & 1) << 5;
    bool rc = opcode & 1;

    state.r[ra] = rotl64(state.r[rs], sh) & rotate_mask[0][me];

    if (rc)
        UpdateCR0<int64_t>(state.r[ra]);

    if (canDisassemble)
        printf("rldicr%s r%d, r%d, %d, %d (0x%08lx)\n", rc ? "." : "", ra, rs, sh, me, rotate_mask[0][me]);
}

void CellPPU::Rldic(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int sh = ((opcode >> 11) & 0x1F) | (((opcode >> 1) & 1) << 5);
    int mb = (opcode >> 5) & 0x3F;
    mb = ((mb & 0x3E) >> 1) | ((mb & 1) << 5);

    state.r[ra] = rotl64(state.r[rs], sh) & rotate_mask[mb][63-sh];
    if (opcode & 1)
        UpdateCR0<int64_t>(state.r[ra]);
    
    if (canDisassemble)
        printf("rldic r%d,r%d,%d,%d (0x%08lx)\n", rs, ra, sh, mb, rotate_mask[mb][63-sh]);
}

void CellPPU::Rldimi(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int sh = ((opcode >> 11) & 0x1F) | (((opcode >> 1) & 1) << 5);
    int mb = (opcode >> 5) & 0x3F;
    mb = ((mb & 0x3E) >> 1) | ((mb & 1) << 5);

    const uint64_t mask = rotate_mask[mb][63-sh];
    state.r[ra] = (state.r[ra] & ~mask) | (rotl64(state.r[rs], sh) & mask);
    if (opcode & 1)
        UpdateCR0<int64_t>(state.r[ra]);
    
    if (canDisassemble)
        printf("rldimi r%d,r%d,%d,%d (0x%08lx)\n", ra, rs, sh, mb, mask);
}

void CellPPU::G_1F(uint32_t opcode)
{ 
    uint16_t field = (opcode >> 1) & 0x3FF;

    switch (field)
    {
    case 0x000:
        Cmp(opcode);
        break;
	case 0x006:
		Lvsl(opcode);
		break;
	case 0x008:
		Subfc(opcode);
		break;
    case 0x009:
        Mulhdu(opcode);
        break;
	case 0x00A:
		Addc(opcode);
		break;
    case 0x00B:
        Mulhwu(opcode);
        break;
    case 0x013:
        Mfcr(opcode);
        break;
    case 0x014:
        Lwarx(opcode);
        break;
    case 0x015:
        Ldx(opcode);
        break;
    case 0x017:
        Lwzx(opcode);
        break;
    case 0x018:
        Slw(opcode);
        break;
    case 0x01A:
        Cntlzw(opcode);
        break;
    case 0x01B:
        Sld(opcode);
        break;
    case 0x01C:
        And(opcode);
        break;
    case 0x020:
        Cmpl(opcode);
        break;
	case 0x026:
		Lvsr(opcode);
		break;
    case 0x028:
        Subf(opcode);
        break;
    case 0x03A:
        Cntlzd(opcode);
        break;
    case 0x03C:
        Andc(opcode);
        break;
	case 0x04B:
		Mulhw(opcode);
		break;
	case 0x054:
		Ldarx(opcode);
		break;
	case 0x057:
		Lbzx(opcode);
		break;
	case 0x067:
		Lvx(opcode);
		break;
    case 0x068:
        Neg(opcode);
        break;
    case 0x0D6:
        Stdcx(opcode);
        break;
    case 0x0CA:
        Addze(opcode);
        break;
    case 0x07C:
        Nor(opcode);
        break;
    case 0x090:
        Mtocrf(opcode);
        break;
    case 0x095:
        Stdx(opcode);
        break;
    case 0x096:
        Stwcx(opcode);
        break;
    case 0x097:
        Stwx(opcode);
        break;
    case 0x0E7:
        Stvx(opcode);
        break;
    case 0xE9:
        Mulld(opcode);
        break;
    case 0x0EB:
        Mullw(opcode);
        break;
    case 0x10A:
        Add(opcode);
        break;
    case 0x116:
        if (canDisassemble)
            printf("DCTB\n");
        break;
    case 0x13C:
        Xor(opcode);
        break;
    case 0x153:
        Mfspr(opcode);
        break;
	case 0x173:
		Mftb(opcode);
		break;
    case 0x1BC:
        Or(opcode);
        break;
    case 0x1C9:
        Divdu(opcode);
        break;
    case 0x1CB:
        Divwu(opcode);
        break;
    case 0x1D3:
        Mtspr(opcode);
        break;
    case 0x1E9:
        Divd(opcode);
        break;
    case 0x1EB:
        Divw(opcode);
        break;
    case 0x217:
        Lfsx(opcode);
        break;
    case 0x218:
        Srw(opcode);
        break;
	case 0x21B:
		Srd(opcode);
		break;
    case 0x256:
        if (canDisassemble)
            printf("sync\n");
        break;
    case 0x297:
        Stfsx(opcode);
        break;
    case 0x338:
        Srawi(opcode);
        break;
    case 0x33A:
    case 0x33B:
        Sradi(opcode);
        break;
	case 0x356:
		if (canDisassemble)
			printf("eieio\n");
		break;
    case 0x39A:
        Extsh(opcode);
        break;
    case 0x3BA:
        Extsb(opcode);
        break;
    case 0x3D7:
        Stfiwx(opcode);
        break;
    case 0x3DA:
        Extsw(opcode);
        break;
    default:
        printf(std::format("Unhandled opcode ({:#x}) ({:#x})\n", field, opcode).c_str());
        throw std::runtime_error("Unable to execute Group 1F opcode");
    }
}

void CellPPU::Cmp(uint32_t opcode)
{
    uint8_t bf = (opcode >> 23) & 0x7;
    bool l = (opcode >> 21) & 1;
    uint8_t ra = ra_d;
    uint8_t rb = rb_d;

    UpdateCRnS(l, bf, state.r[ra], state.r[rb]);

    if (canDisassemble)
        printf("cmp%s state.cr%d,r%d,r%d\n", !l ? "w" : "d", bf, ra, rb);
}

#define MAKE_128(x, y) {.u64 = {(x), (y)}}

static uint128_t LVSL_SHIFT[16] =
{
	MAKE_128(0x0001020304050607, 0x08090A0B0C0D0E0F),
	MAKE_128(0x0102030405060708, 0x090A0B0C0D0E0F10),
	MAKE_128(0x0203040506070809, 0x0A0B0C0D0E0F1011),
	MAKE_128(0x030405060708090A, 0x0B0C0D0E0F101112),
	MAKE_128(0x0405060708090A0B, 0x0C0D0E0F10111213),
	MAKE_128(0x05060708090A0B0C, 0x0D0E0F1011121314),
	MAKE_128(0x060708090A0B0C0D, 0x0E0F101112131415),
	MAKE_128(0x0708090A0B0C0D0E, 0x0F10111213141516),
	MAKE_128(0x08090A0B0C0D0E0F, 0x1011121314151617),
	MAKE_128(0x090A0B0C0D0E0F10, 0x1112131415161718),
	MAKE_128(0x0A0B0C0D0E0F1011, 0x1213141516171819),
	MAKE_128(0x0B0C0D0E0F101112, 0x131415161718191A),
	MAKE_128(0x0C0D0E0F10111213, 0x1415161718191A1B),
	MAKE_128(0x0D0E0F1011121314, 0x15161718191A1B1C),
	MAKE_128(0x0E0F101112131415, 0x161718191A1B1C1D),
	MAKE_128(0x0F10111213141516, 0x1718191A1B1C1D1E),
};

void CellPPU::Lvsl(uint32_t opcode)
{
	int vd = vD;
	int ra = ra_d;
	int rb = rb_d;

	uint32_t sh;
	if (ra == 0)
		sh = state.r[rb];
	else
		sh = state.r[ra] + state.r[rb];
	
	state.vpr[vd] = LVSL_SHIFT[sh & 0xF];

	if (canDisassemble)
		printf("lvsl v%d,r%d,r%d\n", vd, ra, rb);
}

void CellPPU::Subfc(uint32_t opcode)
{
	int rt = rt_d;
	int ra = ra_d;
	int rb = rb_d;
	bool rc = opcode & 1;

	state.r[rt] = ~state.r[ra] + state.r[rb] + 1;

	state.xer.ca = add64_flags(~state.r[ra], state.r[rb], true).carry;

	if (rc)
		UpdateCR0<int64_t>(state.r[rt]);
	
	if (canDisassemble)
		printf("subfc r%d,r%d,r%d\n", rt, ra, rb);
}

uint64_t mulhi64(uint64_t a, uint64_t b) 
{
    unsigned __int128 prod =  a * (unsigned __int128)b;
    return prod >> 64;
 }

void CellPPU::Mulhdu(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

    state.r[rt] = mulhi64(state.r[ra], state.r[rb]);
    if (opcode & 1)
        UpdateCR0<int64_t>(state.r[rt]);
    if (canDisassemble)
        printf("mulhdu r%d,r%d,r%d\n", rt, ra, rb);
}

void CellPPU::Addc(uint32_t opcode)
{
	int ra = ra_d;
    int rb = rb_d;
    int rt = rt_d;
	bool rc = opcode & 1;

	uint64_t ra_ = state.r[ra];
	uint64_t rb_ = state.r[rb];

	const auto res = add64_flags(ra_, rb_, false);
	state.r[rt] = res.result;
	state.xer.ca = res.carry;

	if (rc)
		UpdateCR0<int32_t>(state.r[rt]);

	if (canDisassemble)
		printf("addc r%d,r%d,r%d\n", rt, ra, rb);
}

void CellPPU::Mulhwu(uint32_t opcode)
{
    int ra = ra_d;
    int rb = rb_d;
    int rt = rt_d;

    uint32_t a = state.r[ra];
    uint32_t b = state.r[rb];
    state.r[rt] = ((uint64_t)a * (uint64_t)b) >> 32;
    if (opcode & 1)
        UpdateCR0<int32_t>(state.r[rt]);
    
    if (canDisassemble)
        printf("mulhwu r%d,r%d,r%d\n", rt, ra, rb);
}

void CellPPU::Mfcr(uint32_t opcode)
{
    int rt = rt_d;
    bool a = (opcode >> 20) & 1;
    uint32_t crm = (opcode >> 12) & 0xFF;

    if (a)
    {
        uint32_t n = 0, count = 0;
        for (uint32_t i = 0; i < 8; i++)
        {
            if (crm & (1 << i))
            {
                n = i;
                count++;
            }
        }

        if (count == 1)
        {
            state.r[rt] = (uint64_t)GetCR(7 - n) << (n*4);
        }
        else
            state.r[rt] = 0;
        if (canDisassemble)
            printf("mfocrf r%d,0x%08x (%d)\n", rt, crm, n);
    }
    else
    {
        state.r[rt] = state.cr.cr;
        if (canDisassemble)
            printf("mfcr r%d\n", rt);
    }
}

void CellPPU::Lwarx(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

    state.r[rt] = manager.Read32(ra ? state.r[ra] + state.r[rb] : state.r[rb]);

    if (canDisassemble)
        printf("lwarx r%d, r%d(r%d)\n", rt, rb, ra);
}

void CellPPU::Ldx(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

    state.r[rt] = manager.Read64(ra ? state.r[ra] + state.r[rb] : state.r[rb]);

    if (canDisassemble)
        printf("ldx r%d, r%d(r%d)\n", rt, rb, ra);
}

void CellPPU::Lwzx(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

    state.r[rt] = manager.Read32(ra ? state.r[ra] + state.r[rb] : state.r[rb]);

    if (canDisassemble)
        printf("lwzx r%d, r%d(r%d)\n", rt, rb, ra);
}

void CellPPU::Slw(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;
    bool rc = opcode & 1;

    int sh = state.r[rb] & 0x3F;

    uint32_t r_ = (uint32_t)(state.r[rs] << sh);
    state.r[ra] = r_;

    if (rc)
        UpdateCR0<int32_t>(state.r[ra]);

    if (canDisassemble)
        printf("slw r%d,r%d,r%d\n", ra,rs,rb);
}

void CellPPU::Cntlzw(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;

    uint64_t reg = state.r[rs];

    int i = 0;
    for (; i < 32;)
    {
        if (reg & (1 << (31-i)))
            break;
        i++;
    }

    state.r[ra] = i;
    if (canDisassemble)
        printf("cntlzw r%d,r%d (%d)\n", ra, rs, i);
}

void CellPPU::Sld(uint32_t opcode)
{
    int ra = ra_d;
    int rs = rs_d;
    int rb = rb_d;
    bool rc = opcode & 1;

    uint32_t n = state.r[rb] & 0x7f;
	state.r[ra] = n & 0x40 ? 0 : state.r[rs] << n;

    if (rc)
        UpdateCR0<int64_t>(state.r[ra]);
    if (canDisassemble)
        printf("sld r%d,r%d,r%d\n", ra, rs, rb);
}

void CellPPU::And(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    state.r[ra] = state.r[rs] & state.r[rb];

    if (opcode & 1)
        UpdateCR0<int64_t>(state.r[ra]);

    if (canDisassemble)
        printf("and%s r%d,r%d,r%d\n", (opcode&1) ? "." : "", ra, rs, rb);
}

void CellPPU::Subf(uint32_t opcode)
{
    if ((opcode >> 10) & 1)
    {
        printf("SUBFO!\n");
        exit(1);
    }

    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;
    bool rc = opcode & 1;

    state.r[rt] = state.r[rb] - state.r[ra];

    if (rc)
    {
        UpdateCR0<int64_t>(state.r[rt]);
        if (canDisassemble)
            printf("subf. r%d,r%d,r%d\n", rt, rb, ra);
    }
    else if (canDisassemble)
        printf("subf r%d,r%d,r%d\n", rt, rb, ra);
}

void CellPPU::Cntlzd(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;

    uint64_t reg = state.r[rs];

    int i = 0;
    for (; i < 64;)
    {
        if (reg & (1 << (63-i)))
            break;
        i++;
    }

    state.r[ra] = i;
    if (canDisassemble)
        printf("cntlzd r%d,r%d (%d)\n", ra, rs, i);
}

void CellPPU::Andc(uint32_t opcode)
{
    int ra = ra_d;
    int rs = rs_d;
    int rb = rb_d;

    state.r[ra] = state.r[rs] & ~state.r[rb];

    if (opcode & 1)
        UpdateCR0<int64_t>(state.r[ra]);
    if (canDisassemble)
        printf("andc r%d,r%d,r%d\n", ra, rs, rb);
}

void CellPPU::Mulhw(uint32_t opcode)
{
	int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

	state.r[rt] = ((int64_t)(int32_t)state.r[ra] * (int64_t)(int32_t)state.r[rb]) >> 32;

	if (opcode & 1)
		UpdateCR0<int32_t>(state.r[rt]);

	if (canDisassemble)
		printf("mulhw r%d,r%d,r%d\n", rt, ra, rb);
}

void CellPPU::Ldarx(uint32_t opcode)
{
	int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

    state.r[rt] = manager.Read64(ra ? state.r[ra] + state.r[rb] : state.r[rb]);

    if (canDisassemble)
        printf("ldarx r%d, r%d(r%d)\n", rt, rb, ra);
}

void CellPPU::Lbzx(uint32_t opcode)
{
	int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

    state.r[rt] = manager.Read8(ra ? state.r[ra] + state.r[rb] : state.r[rb]);

	if (canDisassemble)
        printf("lbzx r%d, r%d(r%d)\n", rt, rb, ra);
}

void CellPPU::Lvx(uint32_t opcode)
{
	uint8_t vd = rt_d;
	uint8_t ra = ra_d;
	uint8_t rb = rb_d;

	uint32_t ea = 0;
	if (ra)
		ea = state.r[ra] + state.r[rb];
	else
		ea = state.r[rb];

	state.vpr[vd].u64[0] = manager.Read64(ea);
	state.vpr[vd].u64[1] = manager.Read64(ea+8);

	if (canDisassemble)
		printf("lvx v%d, r%d(r%d)\n", vd, ra, rb);
}

void CellPPU::Cmpl(uint32_t opcode)
{
    uint8_t bf = (opcode >> 23) & 0x7;
    bool l = (opcode >> 21) & 1;
    uint8_t ra = ra_d;
    uint8_t rb = rb_d;

    UpdateCRnU(l, bf, state.r[ra], state.r[rb]);

    if (canDisassemble)
        printf("cmpl%s state.cr%d,r%d,r%d\n", l ? "d" : "w", bf, ra, rb);
}

static uint128_t LVSR_SHIFT[16] = 
{
	MAKE_128(0x1011121314151617, 0x18191A1B1C1D1E1F),
	MAKE_128(0x0F10111213141516, 0x1718191A1B1C1D1E),
	MAKE_128(0x0E0F101112131415, 0x161718191A1B1C1D),
	MAKE_128(0x0D0E0F1011121314, 0x15161718191A1B1C),
	MAKE_128(0x0C0D0E0F10111213, 0x1415161718191A1B),
	MAKE_128(0x0B0C0D0E0F101112, 0x131415161718191A),
	MAKE_128(0x0A0B0C0D0E0F1011, 0x1213141516171819),
	MAKE_128(0x090A0B0C0D0E0F10, 0x1112131415161718),
	MAKE_128(0x08090A0B0C0D0E0F, 0x1011121314151617),
	MAKE_128(0x0708090A0B0C0D0E, 0x0F10111213141516),
	MAKE_128(0x060708090A0B0C0D, 0x0E0F101112131415),
	MAKE_128(0x05060708090A0B0C, 0x0D0E0F1011121314),
	MAKE_128(0x0405060708090A0B, 0x0C0D0E0F10111213),
	MAKE_128(0x030405060708090A, 0x0B0C0D0E0F101112),
	MAKE_128(0x0203040506070809, 0x0A0B0C0D0E0F1011),
	MAKE_128(0x0102030405060708, 0x090A0B0C0D0E0F10),
};

void CellPPU::Lvsr(uint32_t opcode)
{
	int vd = vD;
	int ra = ra_d;
	int rb = rb_d;

	uint32_t sh;
	if (ra)
		sh = state.r[ra] + state.r[rb];
	else
		sh = state.r[rb];
	
	state.vpr[vd] = LVSR_SHIFT[sh & 0xF];

	if (canDisassemble)
		printf("lvsr v%d,r%d,r%d\n", vd, ra, rb);
}

void CellPPU::Neg(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    bool oe = (opcode >> 10) & 1;
    bool rc = opcode & 1;

    state.r[rt] = 0 - state.r[ra];
    
    if (oe)
    {
        printf("TODO: NEGO/NEGO.\n");
        exit(1);
    }

    if (rc)
        UpdateCR0<int64_t>(state.r[rt]);
    
    if (canDisassemble)
        printf("neg r%d,r%d\n", rt, ra);
}

void CellPPU::Nor(uint32_t opcode)
{
    int rb = rb_d;
    int ra = ra_d;
    int rs = rs_d;

    state.r[ra] = ~(state.r[rs] | state.r[rb]);

    if (opcode & 1)
        UpdateCR0<int64_t>(state.r[ra]);
    
    if (canDisassemble)
        printf("nor r%d,r%d,r%d\n", ra, rs, rb);
}

void CellPPU::Mtocrf(uint32_t opcode)
{
    int rt = rt_d;
    bool l = (opcode >> 20) & 1;
    uint32_t crm = (opcode >> 12) & 0xFF;

    if (l)
    {
        uint32_t n = 0, count = 0;
        for (int i = 0; i < 8; i++)
        {
            if (crm & (1 << i))
            {
                n = i;
                count++;
            }
        }

        if (count == 1)
            SetCR(n, (state.r[rt] >> (4*n)) & 0xf);
        else
            state.cr.cr = 0;
        if (canDisassemble)
            printf("mtocrf r%d\n", rt);
    }
    else
    {
        for(int i = 0; i < 8; i++)
		{
			if(crm & (1 << i))
			{
				SetCR(i, state.r[rt] & (0xf << i));
			}
		}
        if (canDisassemble)
            printf("mtcr r%d\n", rt);
    }
}

void CellPPU::Stdx(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    manager.Write64((ra ? state.r[ra] + state.r[rb] : state.r[rb]), state.r[rs]);

    if (canDisassemble)
        printf("stdx r%d, r%d(r%d)\n", rs, ra, rb);
}

void CellPPU::Stwcx(uint32_t opcode)
{
	int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    manager.Write32((ra ? state.r[ra] + state.r[rb] : state.r[rb]), state.r[rs]);
	SetCRBit(0, CR_EQ, true);

    if (canDisassemble)
        printf("stwcx. r%d, r%d(r%d)\n", rs, ra, rb);
}

void CellPPU::Stwx(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    manager.Write32((ra ? state.r[ra] + state.r[rb] : state.r[rb]), state.r[rs]);

    if (canDisassemble)
        printf("stwx r%d, r%d(r%d)\n", rs, ra, rb);
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

    uint64_t ra_ = state.r[ra];

    const auto res = add64_flags(ra_, 0, state.xer.ca);
	state.r[rt] = res.result;
	state.xer.ca = res.carry;

    if (opcode & 1)
    {
        UpdateCR0<int64_t>(state.r[rt]);
        if (canDisassemble)
            printf("addze. r%d,r%d\n", rt, ra);
    }
    else if (canDisassemble)
        printf("addze r%d,r%d\n", rt, ra);
}

void CellPPU::Stdcx(uint32_t opcode)
{
	int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    manager.Write64((ra ? state.r[ra] + state.r[rb] : state.r[rb]), state.r[rs]);
	SetCRBit(0, CR_EQ, true);

    if (canDisassemble)
        printf("stdcx. r%d, r%d(r%d)\n", rs, ra, rb);
}

void CellPPU::Stvx(uint32_t opcode)
{
    int vs = vS;
    int ra = ra_d;
    int rb = rb_d;

    manager.Write64((ra ? state.r[ra] + state.r[rb] : state.r[rb]), state.vpr[vs].u64[0]);
    manager.Write64((ra ? state.r[ra] + state.r[rb] : state.r[rb]) + 8, state.vpr[vs].u64[1]);

    if (canDisassemble)
        printf("stvx v%d,r%d,r%d\n", vs, ra, rb);
}

void CellPPU::Mulld(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;
    bool oe = (opcode >> 10) & 1;
    bool rc = opcode & 1;

    state.r[rt] = (int64_t)state.r[rb] * (int64_t)state.r[ra];

    if (oe)
    {
        printf("TODO: Mulldo/Mulldo.\n");
        exit(1);
    }

    if (rc)
        UpdateCR0<int64_t>(state.r[rt]);

    if (canDisassemble)
        printf("mulld%s r%d,r%d,r%d\n", rc ? "." : "", rt, ra, rb);
}

void CellPPU::Mullw(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;
    bool oe = (opcode >> 10) & 1;
    bool rc = opcode & 1;

    state.r[rt] = (int64_t)((int32_t)state.r[ra] * (int32_t)state.r[rb]);

    if (oe)
    {
        printf("TODO: Mullwo/Mullwo.\n");
        exit(1);
    }

    if (rc)
        UpdateCR0<int64_t>(state.r[rt]);

    if (canDisassemble)
        printf("mullw%s r%d,r%d,r%d\n", rc ? "." : "", rt, ra, rb);
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
    
    const uint64_t a = state.r[ra];
    const uint64_t b = state.r[rb];
    state.r[rt] = a + b;

    if (rc)
    {
        UpdateCR0<int64_t>(state.r[rt]);
        if (canDisassemble)
            printf("add. r%d,r%d,r%d", rt, ra, rb);
    }
    else if (canDisassemble)
        printf("add r%d,r%d,r%d\n", rt, ra, rb);
}

void CellPPU::Xor(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    state.r[ra] = state.r[rs] ^ state.r[rb];

    if (opcode & 1)
        UpdateCR0<int64_t>(state.r[ra]);

    if (canDisassemble)
        printf("xor%s r%d,r%d,r%d\n", (opcode&1) ? "." : "", ra, rs, rb);
}

void CellPPU::Mfspr(uint32_t opcode)
{
    int rt = rt_d;
    uint16_t spr = (opcode >> 11) & 0x3FF;
    
    std::string name;
    state.r[rt] = GetRegBySPR(spr, name);

    if (canDisassemble)
        printf("mfspr r%d, %s\n", rt, name.c_str());
}

uint64_t getTimeBase()
{
	while (true)
	{
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

		uint64_t result = ((uint64_t)ts.tv_sec * 80000000ull + (uint64_t)ts.tv_nsec * 80000000ull / 1000000000ull) * 1 / 100u;
		if (result) return result;
	}
}

void CellPPU::Mftb(uint32_t opcode)
{
	int rt = rt_d;
    uint16_t spr = (opcode >> 11) & 0x3FF;
	spr = spr >> 5 | ((spr & 0x1F) << 5);

	switch (spr)
	{
	case 0x10C: state.r[rt] = getTimeBase(); break;
	case 0x10D: state.r[rt] = getTimeBase() >> 32; break;
	}

	if (canDisassemble)
		printf("mftb r%d\n", rt);
}

void CellPPU::Sradi(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int sh = ((opcode >> 11) & 0x1F) | (((opcode >> 1) & 1) << 5);
    bool rc = opcode & 1;

    int64_t rs_ = state.r[rs];
    state.r[ra] = rs_ >> sh;

    state.xer.ca = (rs_ < 0) && ((state.r[ra] << sh) != (uint64_t)rs_);
    
    if (rc)
        UpdateCR0<int64_t>(state.r[ra]);

    if (canDisassemble)
        printf("sradi r%d,r%d,%d\n", ra, rs, sh);
}

void CellPPU::Or(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    state.r[ra] = state.r[rs] | state.r[rb];

    if (opcode & 1)
        UpdateCR0<int64_t>(state.r[ra]);
    
    if (canDisassemble)
    {
        if (rs != rb)
            printf("or%s r%d, r%d, r%d\n", (opcode & 1) ? "." : "", ra, rs, rb);
        else
            printf("mr%s r%d, r%d\n", (opcode & 1) ? "." : "", ra, rs);
    }
}

void CellPPU::Divdu(uint32_t opcode)
{
    bool oe = (opcode >> 10) & 1;
    bool rc = opcode & 1;
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

    const uint64_t RA = state.r[ra];
    const uint64_t RB = state.r[rb];

    if (!RB)
    {
        if (oe) {printf("divduo\n"); exit(1);}
        state.r[rt] = 0;
    }
    else
    {
        state.r[rt] = RA / RB;
    }

    if (rc)
        UpdateCR0<int64_t>(state.r[rt]);
    
    if (canDisassemble)
        printf("divdu r%d,r%d,r%d\n", rt, ra, rb);
}

void CellPPU::Divwu(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

    bool oe = (opcode >> 10) & 1;
    bool rc = opcode & 1;

    if (oe)
    {
        printf("TODO: DIVWUO/DIVWUO.\n");
        exit(1);
    }

    if ((uint32_t)state.r[rb] == 0)
    {
        printf("ERROR: DIVIDE BY ZERO AT 0x%08lX\n", state.pc);
        exit(1);
    }

    state.r[rt] = (uint32_t)state.r[ra] / (uint32_t)state.r[rb];
    if (rc)
        UpdateCR0<int64_t>(state.r[rt]);

    if (canDisassemble)
        printf("divuw%s r%d,r%d,r%d\n", rc ? "." : "", rt, ra, rb);
}

void CellPPU::Mtspr(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    uint16_t spr = (opcode >> 11) & 0x3FF;

    std::string name;
    GetRegBySPR(spr, name) = state.r[rs];

    if (canDisassemble)
        printf("mtspr r%d,%s\n", rs, name.c_str());
}

void CellPPU::Divd(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;
    bool rc = opcode & 1;
    bool oe = (opcode >> 10) & 1;

    if (oe)
    {
        printf("TODO: DIVDO/DIVDO.\n");
        exit(1);
    }

    const int64_t RA = state.r[ra];
    const int64_t RB = state.r[rb];

    if (RB == 0 || (RA == INT64_MIN && RB == -1))
    {
        state.r[rt] = 0;
    }
    else
    {
        state.r[rt] = RA / RB;
    }

    if (rc)
        UpdateCR0<int64_t>(state.r[rt]);
    
    if (canDisassemble)
        printf("divd r%d,r%d,r%d\n", rt, ra, rb);
}

void CellPPU::Divw(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int rb = rb_d;
    bool rc = opcode & 1;
    bool oe = (opcode >> 10) & 1;

    if (oe)
    {
        printf("TODO: DIVWO/DIVWO.\n");
        exit(1);
    }

    const int32_t RA = state.r[ra];
    const int32_t RB = state.r[rb];

    if (RB == 0 || (RA == INT32_MIN && RB == -1))
    {
        state.r[rt] = 0;
    }
    else
    {
        state.r[rt] = (uint32_t)(RA / RB);
    }

    if (rc)
        UpdateCR0<int64_t>(state.r[rt]);
    
    if (canDisassemble)
        printf("divw r%d,r%d,r%d\n", rt, ra, rb);
}

void CellPPU::Lfsx(uint32_t opcode)
{
    int frt = rt_d;
    int ra = ra_d;
    int rb = rb_d;

    uint32_t data = manager.Read32(ra ? state.r[ra] + state.r[rb] : state.r[rb]);
	state.fpr[frt].f = (float&)data;
    
    if (canDisassemble)
        printf("lfsx f%d, r%d(r%d)\n", frt, ra, rb);
}

void CellPPU::Srw(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;
    bool rc = opcode & 1;

    int sh = state.r[rb] & 0x3F;

    uint32_t r_ = (uint32_t)state.r[rs] >> sh;

    state.r[ra] = r_;

    if (rc)
        UpdateCR0<int64_t>(state.r[ra]);

    if (canDisassemble)
        printf("srw r%d,r%d,r%d\n", ra,rs,rb);
}

void CellPPU::Srd(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;
    bool rc = opcode & 1;

    int sh = state.r[rb] & 0x7F;

    uint64_t r_ = sh & 0x40 ? 0 : state.r[rs] >> sh;

    state.r[ra] = r_;

    if (rc)
        UpdateCR0<int64_t>(state.r[ra]);

    if (canDisassemble)
        printf("srd r%d,r%d,r%d\n", ra,rs,rb);
}

void CellPPU::Stfsx(uint32_t opcode)
{
    int frs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    manager.Write32((ra ? state.r[ra] + state.r[rb] : state.r[rb]), state.fpr[frs].ToU32());
    if (canDisassemble)
        printf("stfsx f%d, r%d(r%d)\n", frs, ra, rb);
}

void CellPPU::Srawi(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int sh = rb_d;

    bool rc = opcode & 1;

    int32_t RS = state.r[rs];
    state.r[ra] = RS >> sh;
    state.xer.ca = (RS < 0) & (((uint32_t)state.r[ra] << sh) != (uint32_t)RS);
    if (rc)
        UpdateCR0<int64_t>(state.r[ra]);

    if (canDisassemble)
        printf("srawi r%d,r%d,%d\n", ra, rs, sh);
}

void CellPPU::Extsh(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    bool rc = opcode & 1;

    state.r[ra] = (int64_t)(int16_t)state.r[rs];

    if (rc)
        UpdateCR0<int64_t>(state.r[ra]);

    if (canDisassemble)
        printf("extsh%s r%d,r%d\n", rc ? "." : "", ra, rs);
}

void CellPPU::Extsb(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    bool rc = opcode & 1;

    state.r[ra] = (int64_t)(int8_t)state.r[rs];
    if (rc) UpdateCR0<int64_t>(state.r[ra]);

    if (canDisassemble)
        printf("extsb%s r%d,r%d\n", rc ? "." : "", ra, rs);
}

void CellPPU::Stfiwx(uint32_t opcode)
{
    int frs = rs_d;
    int ra = ra_d;
    int rb = rb_d;

    manager.Write32(ra ? state.r[ra] + state.r[rb] : state.r[rb], (uint32_t&)state.fpr[frs].f);

    if (canDisassemble)
        printf("stfiwx f%d,r%d(r%d)\n", frs, ra, rb);
}

void CellPPU::Extsw(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    bool rc = opcode & 1;

    state.r[ra] = (int64_t)(int32_t)state.r[rs];
    if (rc) UpdateCR0<int32_t>(state.r[ra]);

    if (canDisassemble)
        printf("extsw%s r%d,r%d\n", rc ? "." : "", ra, rs);
}

void CellPPU::Lwz(uint32_t opcode)
{
    uint8_t rt = rt_d;
    uint8_t ra = ra_d;
    int32_t d = (int32_t)(int16_t)si_d;

    state.r[rt] = manager.Read32(ra ? state.r[ra] + d : d);

    if (canDisassemble)
        printf("lwz r%d, %d(r%d)\n", rt, d, ra);
}

void CellPPU::Lwzu(uint32_t opcode)
{
    uint8_t rt = rt_d;
    uint8_t ra = ra_d;
    int32_t d = (int32_t)(int16_t)si_d;

    state.r[rt] = manager.Read32(state.r[ra] + d);
	state.r[ra] += d;

    if (canDisassemble)
        printf("lwzu r%d, %d(r%d)\n", rt, d, ra);
}

void CellPPU::Lbz(uint32_t opcode)
{
    uint8_t rt = rt_d;
    uint8_t ra = ra_d;
    int32_t d = (int32_t)(int16_t)si_d;

    state.r[rt] = manager.Read8(ra ? state.r[ra] + d : d);

    if (canDisassemble)
        printf("lbz r%d, %d(r%d)\n", rt, d, ra);
}

void CellPPU::Lbzu(uint32_t opcode)
{
    uint8_t rt = rt_d;
    uint8_t ra = ra_d;
    int32_t d = (int32_t)(int16_t)si_d;

    uint64_t addr = state.r[ra] + d;
    state.r[rt] = manager.Read8(addr);
    state.r[ra] = addr;
    if (canDisassemble)
        printf("lbzu r%d, %d(r%d)\n", rt, d, ra);
}

void CellPPU::Rlwinm(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int sh = ((opcode >> 11) & 0x1F);
    int mb = (opcode >> 6) & 0x1F;
    int me = (opcode >> 1) & 0x1F;
    bool rc = opcode & 1;

    state.r[ra] = rotl32(state.r[rs], sh) & rotate_mask[32 + mb][32 + me];
    if (rc)
        UpdateCR0<int32_t>(state.r[ra]);
    
    if (canDisassemble)
        printf("rlwinm r%d,r%d,%d,%d (0x%08lx)\n", ra, rs, mb, me, rotate_mask[32 + mb][32 + me]);
}

void CellPPU::Rlwnm(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int rb = rb_d;
    int mb = (opcode >> 6) & 0x1F;
    int me = (opcode >> 1) & 0x1F;
    bool rc = opcode & 1;

    state.r[ra] = rotl32(state.r[rs], state.r[rb] & 0x1F) & rotate_mask[32 + mb][32 + me];
    if (rc)
        UpdateCR0<int32_t>(state.r[ra]);

    if (canDisassemble)
        printf("rlwnm r%d,r%d,r%d,%d,%d (0x%08lx)\n", rs, ra, rb, mb, me, rotate_mask[32 + mb][32 + me]);
}

void CellPPU::Stw(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int32_t d = (int32_t)(int16_t)si_d;

    manager.Write32(ra ? state.r[ra] + d : d, state.r[rs]);

    if (canDisassemble)
        printf("stw r%d, %d(r%d) (0x%08x, 0x%08x)\n", rs, d, ra, state.r[rs], ra ? state.r[ra] + d : d);
}

void CellPPU::Stwu(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int32_t d = (int32_t)(int16_t)si_d;

    uint64_t addr = state.r[ra] + d;
    manager.Write32(addr, state.r[rs]);
    state.r[ra] = addr;

    if (canDisassemble)
        printf("stwu r%d, %d(r%d)\n", rs, d, ra);
}

void CellPPU::Stb(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int16_t ds = opcode & 0xFFFF;

    manager.Write8(ra ? state.r[ra] + ds : ds, state.r[rs]);

    if (canDisassemble)
    {
        printf("stb r%d, %d(r%d)\n", rs, ds, ra);
    }
}

void CellPPU::Stbu(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int16_t ds = opcode & 0xFFFF;

    uint64_t addr = state.r[ra] + ds;
    manager.Write8(addr, state.r[rs]);
    state.r[ra] = addr;

    if (canDisassemble)
    {
        printf("stbu r%d, %d(r%d)\n", rs, ds, ra);
    }
}

void CellPPU::Lhz(uint32_t opcode)
{
    int rt = rt_d;
    int ra = ra_d;
    int64_t ds = (int64_t)(int16_t)(opcode & 0xFFFF);

    state.r[rt] = manager.Read16(ra ? state.r[ra] + ds : ds);

    if (canDisassemble)
        printf("lhz r%d, %d(r%d)\n", rt, ds, ra);
}

void CellPPU::Lhzu(uint32_t opcode)
{
	int rt = rt_d;
    int ra = ra_d;
    int64_t ds = (int64_t)(int16_t)(opcode & 0xFFFF);

	uint32_t ea = ra ? state.r[ra] + ds : ds;
    state.r[rt] = manager.Read16(ea);

	state.r[ra] = ea;

    if (canDisassemble)
        printf("lhzu r%d, %d(r%d)\n", rt, ds, ra);
}

void CellPPU::Sth(uint32_t opcode)
{
    int rs = rs_d;
    int ra = ra_d;
    int16_t ds = si_d;
    bool u = opcode & 1;

    manager.Write16(ra ? state.r[ra] + ds : ds, state.r[rs]);

    if (u)
        state.r[ra] = state.r[ra] + ds;

    if (canDisassemble)
    {
        printf("sth%s r%d, %d(r%d)\n", u ? "u" : "", rs, ds, ra);
    }
}

void CellPPU::Lfs(uint32_t opcode)
{
    int frt = rt_d;
    int ra = ra_d;
    int64_t ds = (int64_t)(int16_t)si_d;

    uint32_t v = manager.Read32(ra ? state.r[ra] + ds : ds);
    state.fpr[frt].f = (float&)v;

    if (canDisassemble)
        printf("lfs f%d, %d(r%d) (%f)\n", frt, ds, ra, state.fpr[frt].f);
}

void CellPPU::Lfd(uint32_t opcode)
{
    int frt = rt_d;
    int ra = ra_d;
    int64_t ds = (int64_t)(int16_t)si_d;

    state.fpr[frt].u = manager.Read64(ra ? state.r[ra] + ds : ds);

    if (canDisassemble)
        printf("lfd f%d, %d(r%d) (%f)\n", frt, ds, ra, state.fpr[frt].f);
}

void CellPPU::Stfs(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int16_t ds = opcode & 0xFFFF;
    
    float f = (float)state.fpr[rs].f;
    manager.Write32(ra ? state.r[ra] + ds : ds, *(uint32_t*)&f);

    if (canDisassemble)
        printf("stfs f%d, %d(r%d)\n", rs, ds, ra);
}

void CellPPU::Stfsu(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int16_t ds = opcode & 0xFFFF;
    
    float f = (float)state.fpr[rs].f;
    uint64_t addr = state.r[ra] + ds;
    manager.Write32(addr, *(uint32_t*)&f);
    state.r[ra] = addr;

    if (canDisassemble)
        printf("stfsu f%d, %d(r%d)\n", rs, ds, ra);
}

void CellPPU::Stfd(uint32_t opcode)
{
    int rs = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int16_t ds = opcode & 0xFFFF;
    
    manager.Write64(ra ? state.r[ra] + ds : ds, *(uint64_t*)&state.fpr[rs].f);

    if (canDisassemble)
        printf("stfd f%d, %d(r%d)\n", rs, ds, ra);
}

void CellPPU::G_3A(uint32_t opcode)
{
    uint8_t field = (opcode & 0x3);

    if (field == 1)
    {
        Ldu(opcode);
        return;
    }
    else if (field == 0)
    {
        // LD
        Ld(opcode);
        return;
    }

    printf("ERROR: Unknown G_3A opcode 0x%08x\n", opcode);
    exit(1);
}

void CellPPU::Ld(uint32_t opcode)
{
    int rt = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int32_t ds = (int32_t)(int16_t)(opcode & 0xFFFC);

    state.r[rt] = manager.Read64(ra ? state.r[ra] + ds : ds);

    if (canDisassemble)
        printf("ld r%d, %d(r%d) (0x%08x, 0x%08x)\n", rt, ds, ra, ra ? state.r[ra] + ds : ds, state.r[rt]);
}

void CellPPU::Ldu(uint32_t opcode)
{
    int rt = ((opcode >> 21) & 0x1F);
    int ra = ((opcode >> 16) & 0x1F);
    int32_t ds = (int32_t)(int16_t)(opcode & 0xFFFC);

    uint64_t addr = state.r[ra] + ds;
    state.r[rt] = manager.Read64(addr);
    state.r[ra] = addr;

    if (canDisassemble)
        printf("ldu r%d, %d(r%d)\n", rt, ds, ra);
}

void CellPPU::G_3B(uint32_t opcode)
{
    uint8_t field = (opcode >> 1) & 0x1F;

    switch (field)
    {
    case 0x012:
        Fdivs(opcode);
        break;
    case 0x014:
        Fsubs(opcode);
        break;
    case 0x015:
        Fadds(opcode);
        break;
    case 0x019:
        Fmuls(opcode);
        break;
    case 0x01C:
        Fmsubs(opcode);
        break;
    case 0x01D:
        Fmadds(opcode);
        break;
    default:
        printf("Unknown opcode (0x%02x) 0x%08x\n", field, opcode);
        throw std::runtime_error("Couldn't execute opcode\n");
    }
}

void CellPPU::Fdivs(uint32_t opcode)
{
    int frt = rt_d;
    int fra = ra_d;
    int frb = rb_d;

    state.fpr[frt].f = (float)(state.fpr[fra].f / state.fpr[frb].f);

    if (canDisassemble)
        printf("fdivs f%d,f%d,f%d\n", frt, fra, frb);
}

void CellPPU::Fsubs(uint32_t opcode)
{
    int frt = rt_d;
    int fra = ra_d;
    int frb = rb_d;

    state.fpr[frt].f = float(state.fpr[fra].f - state.fpr[frb].f);

    if (canDisassemble)
        printf("fsubs f%d,f%d,f%d\n", frt, fra, frb);
}

void CellPPU::Fadds(uint32_t opcode)
{
    int frt = rt_d;
    int fra = ra_d;
    int frb = rb_d;

    state.fpr[frt].f = static_cast<float>(state.fpr[fra].f + state.fpr[frb].f);

    if (canDisassemble)
        printf("fadds f%d,f%d,f%d\n", frt, fra, frb);
}

void CellPPU::Fmuls(uint32_t opcode)
{
    int frt = rt_d;
    int fra = ra_d;
    int frc = rc_d;

    state.fpr[frt].f = static_cast<float>(state.fpr[fra].f * state.fpr[frc].f);

    if (canDisassemble)
        printf("fmuls f%d,f%d,f%d\n", frt, fra, frc);
}

void CellPPU::Fmsubs(uint32_t opcode)
{
    int frt = rt_d;
    int fra = ra_d;
    int frc = rc_d;
    int frb = rb_d;

    state.fpr[frt].f = static_cast<float>((state.fpr[fra].f * state.fpr[frc].f) - state.fpr[frb].f);

    if (canDisassemble)
        printf("fmsubs f%d,f%d,f%d,f%d\n", frt, fra, frc, frb);
}

void CellPPU::Fmadds(uint32_t opcode)
{
    int frt = rt_d;
    int fra = ra_d;
    int frc = rc_d;
    int frb = rb_d;

    state.fpr[frt].f = static_cast<float>((state.fpr[fra].f * state.fpr[frc].f) + state.fpr[frb].f);

    if (canDisassemble)
        printf("fmadds f%d,f%d,f%d,f%d\n", frt, fra, frc, frb);
}

void CellPPU::G_3E(uint32_t opcode)
{
    uint8_t field = (opcode & 0x3);

    if (field == 1)
    {
        // STDU
        Stdu(opcode);
        return;
    }
    else if (field == 0)
    {
        // STD
        Std(opcode);
        return;
    }

    printf("ERROR: Unknown G_3E opcode 0x%08x\n", opcode);
    exit(1);
}

void CellPPU::Std(uint32_t opcode)
{
    int32_t ds = (int32_t)(int16_t)(opcode & 0xFFFC);
    int rs = rs_d;
    int ra = ra_d;

    manager.Write64(ra ? state.r[ra] + ds : ds, state.r[rs]);

    if (canDisassemble)
        printf("std r%d, %d(r%d) (0x%08x, 0x%08x)\n", rs, ds, ra, ra ? state.r[ra] + ds : ds, state.r[rs]);
}

void CellPPU::Stdu(uint32_t opcode)
{
    int32_t ds = (int32_t)(int16_t)(opcode & 0xFFFC);
    int rs = rs_d;
    int ra = ra_d;

    const uint64_t addr = state.r[ra] + ds;
    manager.Write64(addr, state.r[rs]);
    state.r[ra] = addr;

    if (canDisassemble)
        printf("stdu r%d, %d(r%d)\n", rs, ds, ra);
}

void CellPPU::G_3F(uint32_t opcode)
{
    uint8_t field = (opcode >> 1) & 0x3FF;

    switch (field)
    {
    case 0x000:
        Fcmpu(opcode);
        break;
    case 0x00C:
        Frsp(opcode);
        break;
    case 0x00F:
        Fctiwz(opcode);
        break;
    case 0x012:
        Fdiv(opcode);
        break;
    case 0x015:
        Fadd(opcode);
        break;
    case 0x019:
        Fmul(opcode);
        break;
    case 0x028:
        Fneg(opcode);
        break;
    case 0x02F:
        Fctidz(opcode);
        break;
    case 0x048:
        Fmr(opcode);
        break;
    case 0x04E:
        Fcfid(opcode);
        break;
	default:
	{
		// This is hideous
		switch ((opcode >> 1) & 0x1F)
		{
		case 0x01D:
			Fmadd(opcode);
			break;
		default:
			printf("Unknown opcode (0x%03x) 0x%08x\n", field, opcode);
			exit(1);
		}
	}
    }
}

void CellPPU::Fcmpu(uint32_t opcode)
{
    int fra = ra_d;
    int frb = rb_d;
    int bf = (opcode >> 23) & 0x7;

    UpdateCRn<double>(bf, state.fpr[fra].f, state.fpr[frb].f);

    if (canDisassemble)
        printf("fcmpu state.cr%d,f%d,f%d\n", bf, fra, frb);
}

void CellPPU::Frsp(uint32_t opcode)
{
    int frt = rt_d;
    int frb = rb_d;

    const double b = state.fpr[frb].f;
    const double r = static_cast<float>(b);
    state.fpr[frt].f = r;

    if (canDisassemble)
        printf("frsp r%d,r%d\n", frt, frb);
}

void CellPPU::Fctiwz(uint32_t opcode)
{
    int frt = rt_d;
    int frb = rb_d;

    const auto b = _mm_load_sd(&state.fpr[frb].f);
	const auto res = _mm_xor_si128(_mm_cvttpd_epi32(b), _mm_castpd_si128(_mm_cmpge_pd(b, _mm_set1_pd(0x80000000))));
	state.fpr[frt].f = std::bit_cast<double, int64_t>(_mm_cvtsi128_si64(res));

    if (canDisassemble)
        printf("fctiwz f%d,f%d\n", frt, frb);
}

void CellPPU::Fdiv(uint32_t opcode)
{
    int frt = rt_d;
    int fra = ra_d;
    int frb = rb_d;

    state.fpr[frt].f = (state.fpr[fra].f / state.fpr[frb].f);

    if (canDisassemble)
        printf("fdiv f%d,f%d,f%d\n", frt, fra, frb);
}

void CellPPU::Fadd(uint32_t opcode)
{
    int frt = rt_d;
    int fra = ra_d;
    int frb = rb_d;

    state.fpr[frt].f = state.fpr[fra].f + state.fpr[frb].f;

    if (canDisassemble)
        printf("fadd f%d,f%d,f%d\n", frt, fra, frb);
}

void CellPPU::Fmul(uint32_t opcode)
{
    int frt = rt_d;
    int fra = ra_d;
    int frc = rc_d;

    state.fpr[frt].f = state.fpr[fra].f * state.fpr[frc].f;

    if (canDisassemble)
        printf("fmul f%d,f%d,f%d\n", frt, fra, frc);
}

void CellPPU::Fmadd(uint32_t opcode)
{
	int frt = rt_d;
	int fra = ra_d;
	int frb = rb_d;
	int frc = rc_d;

	bool rc = opcode & 1;

	state.fpr[frt].f = (state.fpr[fra].f*state.fpr[frc].f)+state.fpr[frb].f;

	if (rc)
		UpdateCR0<double>(state.fpr[frt].f);
	
	if (canDisassemble)
		printf("fmadd f%d,f%d,f%d,f%d\n", frt, fra, frc, frb);
}

void CellPPU::Fneg(uint32_t opcode)
{
    int frt = rt_d;
    int frb = rb_d;

    state.fpr[frt].f = -state.fpr[frb].f;
    if (canDisassemble)
        printf("fneg f%d,f%d\n", frt, frb);
}

void CellPPU::Fctidz(uint32_t opcode)
{
    int frt = rt_d;
    int frb = rb_d;

    const auto b = _mm_load_sd(&state.fpr[frb].f);
	const auto res = _mm_xor_si128(_mm_set1_epi64x(_mm_cvttsd_si64(b)), _mm_castpd_si128(_mm_cmpge_pd(b, _mm_set1_pd((double)(1ull << 63)))));
	state.fpr[frt].f = std::bit_cast<double>(_mm_cvtsi128_si64(res));

    if (canDisassemble)
        printf("fctidz f%d,f%d\n", frt, frb);
}

void CellPPU::Fmr(uint32_t opcode)
{
    int frt = rt_d;
    int frb = rb_d;

    state.fpr[frt].f = state.fpr[frb].f;

    if (canDisassemble)
        printf("fmr f%d,f%d\n", frt, frb);
}

void CellPPU::Fcfid(uint32_t opcode)
{
    int frt = rt_d;
    int frb = rb_d;

	_mm_store_sd(&state.fpr[frt].f, _mm_cvtsi64_sd(_mm_setzero_pd(), std::bit_cast<int64_t>(state.fpr[frt].f)));

    if (canDisassemble)
        printf("fcfid r%d,r%d (%f)\n", frt, frb, state.fpr[frt].f);
}
