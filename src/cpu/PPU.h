#pragma once

#include "kernel/Memory.h"
#include "cpu/SPU.h"

#include <unordered_map>
#include <functional>
#include <string>

union uint128_t
{
    uint8_t u8[16];
    uint16_t u16[8];
    uint32_t u32[4];
    uint64_t u64[2];
    float f[4];
    double d[2];
    __uint128_t u128;
};

union FPR
{
    double f;
    uint64_t u;

    uint32_t ToU32()
    {
        float f32 = (float)f;
        return (uint32_t&)f32;
    }
};

union CRhdr
{
    uint32_t cr;

    struct
    {
        uint8_t cr7	: 4;
        uint8_t cr6	: 4;
        uint8_t cr5	: 4;
        uint8_t cr4	: 4;
        uint8_t cr3	: 4;
        uint8_t cr2	: 4;
        uint8_t cr1	: 4;
        uint8_t cr0	: 4;
    };
};

struct XER
{
    uint64_t ca = false;
};

struct State
{
public:
    uint64_t r[32];
    FPR fpr[32];

    uint128_t vpr[32];

    XER xer;
    
    CRhdr cr;

    uint64_t ctr;

    uint64_t pc, lr;

    uint64_t sp, usprg0;
};

class CellPPU
{
private:
    enum
    {
        CR_LT = 0x8,
        CR_GT = 0x4,
        CR_EQ = 0x2,
        CR_SO = 0x1,
    };

    struct CRField
    {
        uint8_t val : 4;
    };

	State state;

    MemoryManager& manager;

    std::unordered_map<uint8_t, std::function<void(uint32_t)>> opcodes;

    template<typename T>
    void UpdateCRn(const uint8_t n, const T a, const T b)
    {
        if (a < b) SetCR(n, CR_LT);
        else if (a > b) SetCR(n, CR_GT);
        else if (a == b) SetCR(n, CR_EQ);
        else if ((typeid(T) == typeid(float)) || (typeid(T) == typeid(double)))
        {
            printf("Float/doubles are not <, >, or ==!\n");
            exit(1);
        }
    }
    
    template<typename T>
    void UpdateCR0(const T val)
    {
        UpdateCRn<T>(val, 0, 0);
    }

    void UpdateCRnU(const uint8_t l, const uint8_t n, const uint64_t a, const uint64_t b)
    {
        if (l)
            UpdateCRn<uint64_t>(n, a, b);
        else
            UpdateCRn<uint32_t>(n, a, b);
    }

    void UpdateCRnS(const uint8_t l, const uint8_t n, const int64_t a, const int64_t b)
    {
        if (l)
            UpdateCRn<int64_t>(n, a, b);
        else
            UpdateCRn<int32_t>(n, a, b);
    }

    uint64_t& GetRegBySPR(uint32_t spr, std::string& name)
    {
        const uint32_t n = (spr >> 5) | ((spr & 0x1f) << 5);

        switch (n)
        {
		case 0x001: name = "xer"; return state.xer.ca;
        case 0x008: name = "lr"; return state.lr;
        case 0x009: name = "ctr"; return state.ctr;
        case 0x100: name = "usprg0"; return state.usprg0;
        }

        printf("ERROR: Read from unknown SPR 0x%03x\n", n);
        exit(1);
    }

    inline void SetCRBit(const uint8_t n, const uint32_t bit, const bool value)
	{
		switch(n)
		{
		case 0: value ? state.cr.cr0 |= bit : state.cr.cr0 &= ~bit; break;
		case 1: value ? state.cr.cr1 |= bit : state.cr.cr1 &= ~bit; break;
		case 2: value ? state.cr.cr2 |= bit : state.cr.cr2 &= ~bit; break;
		case 3: value ? state.cr.cr3 |= bit : state.cr.cr3 &= ~bit; break;
		case 4: value ? state.cr.cr4 |= bit : state.cr.cr4 &= ~bit; break;
		case 5: value ? state.cr.cr5 |= bit : state.cr.cr5 &= ~bit; break;
		case 6: value ? state.cr.cr6 |= bit : state.cr.cr6 &= ~bit; break;
		case 7: value ? state.cr.cr7 |= bit : state.cr.cr7 &= ~bit; break;
		}
	}

    bool CheckCondition(uint32_t bo, uint32_t bi);

    inline uint8_t GetCR(uint8_t n) const
    {
        switch (n)
        {
        case 0: return state.cr.cr0;
		case 1: return state.cr.cr1;
		case 2: return state.cr.cr2;
		case 3: return state.cr.cr3;
		case 4: return state.cr.cr4;
		case 5: return state.cr.cr5;
		case 6: return state.cr.cr6;
		case 7: return state.cr.cr7;
        }

        return 0;
    }

    inline void SetCR(uint8_t n, uint32_t value)
    {
        switch (n)
        {
        case 0: state.cr.cr0 = value; break;
        case 1: state.cr.cr1 = value; break;
        case 2: state.cr.cr2 = value; break;
        case 3: state.cr.cr3 = value; break;
        case 4: state.cr.cr4 = value; break;
        case 5: state.cr.cr5 = value; break;
        case 6: state.cr.cr6 = value; break;
        case 7: state.cr.cr7 = value; break;
        }
    }

    uint8_t GetCRBit(const uint32_t bit) const { return 1 << (3 - (bit % 4)); }

    void SetCRBit2(const uint32_t bit, bool set) {SetCRBit(bit >> 2, 0x8 >> (bit & 3), set);}

    uint8_t IsCR(const uint32_t bit) const {return (GetCR(bit >> 2) & GetCRBit(bit)) ? 1 : 0;}

    void G_04(uint32_t opcode); // 0x04
	void Vsel(uint32_t opcode); // 0x04 0x02A
	void Vperm(uint32_t opcode); // 0x04 0x02B
	void Vsldoi(uint32_t opcode); // 0x04 0x02C
	void Vnmsubfp(uint32_t opcode); // 0x04 0x02F
	void Vmaddfp(uint32_t opcode); // 0x04 0x02E
	void Vsubfp(uint32_t opcode); // 0x04 0x04A
	void Vmrghw(uint32_t opcode); // 0x04 0x08C
	void Vrsqrtefp(uint32_t opcode); // 0x04 0x14A
	void Vslw(uint32_t opcode); // 0x04 0x184
	void Vmrglw(uint32_t opcode); // 0x04 0x18C
	void Vspltw(uint32_t opcode); // 0x04 0x28C
	void Vspltisw(uint32_t opcode); // 0x04 0x38C
	void Vor(uint32_t opcode); // 0x04 0x484
    void Vxor(uint32_t opcode); // 0x04 0x4C4
	void Mulli(uint32_t opcode); // 0x07
    void Subfic(uint32_t opcode); // 0x08
    void Cmpli(uint32_t opcode); // 0x0A
    void Cmpi(uint32_t opcode); // 0x0B
	void Addic(uint32_t opcode); // 0x0C
    void Addi(uint32_t opcode); // 0x0E
    void Addis(uint32_t opcode); // 0x0F
    void BranchCond(uint32_t opcode); // 0x10
    void Branch(uint32_t opcode); // 0x12
    void G_13(uint32_t opcode); // 0x13
    void Bclr(uint32_t opcode); // 0x13 0x10
    void Cror(uint32_t opcode); // 0x13 0x1C1
    void Bcctr(uint32_t opcode); // 0x13 0x210
	void Rlwimi(uint32_t opcode); // 0x14
    void Rlwinm(uint32_t opcode); // 0x15
    void Rlwnm(uint32_t opcode); // 0x17
    void Ori(uint32_t opcode); // 0x18
    void Oris(uint32_t opcode); // 0x19
    void Xori(uint32_t opcode); // 0x1A
    void Xoris(uint32_t opcode); // 0x1B
    void G_1E(uint32_t opcode); //0x1E
    void Rldicl(uint32_t opcode); // 0x1E 0x00
    void Rldicr(uint32_t opcode); // 0x1E 0x01
    void Rldic(uint32_t opcode); // 0x1E 0x02
    void Rldimi(uint32_t opcode); // 0x1E 0x03
    void G_1F(uint32_t opcode); // 0x1F
    void Cmp(uint32_t opcode); // 0x1F 0x00
	void Lvsl(uint32_t opcode); // 0x1F 0x06
	void Subfc(uint32_t opcode); // 0x1F 0x08
    void Mulhdu(uint32_t opcode); // 0x1F 0x09
	void Addc(uint32_t opcode); // 0x1F 0x0A
    void Mulhwu(uint32_t opcode); // 0x1F 0x0B
    void Mfcr(uint32_t opcode); // 0x1F 0x13
	void Lwarx(uint32_t opcode); // 0x1F 0x14
    void Ldx(uint32_t opcode); // 0x1F 0x15
    void Lwzx(uint32_t opcode); // 0x1F 0x17
    void Slw(uint32_t opcode); // 0x1F 0x18
    void Cntlzw(uint32_t opcode); // 0x1F 0x1A
    void Sld(uint32_t opcode); // 0x1F 0x1B
    void And(uint32_t opcode); // 0x1F 0x1C
    void Cmpl(uint32_t opcode); // 0x1F 0x20
	void Lvsr(uint32_t opcode); // 0x1F 0x26
    void Subf(uint32_t opcode); // 0x1F 0x28
    void Cntlzd(uint32_t opcode); // 0x1F 0x3A
    void Andc(uint32_t opcode); // 0x1F 0x3C
	void Mulhw(uint32_t opcode); // 0x1F 0x4B
	void Ldarx(uint32_t opcode); // 0x1F 0x54
	void Lbzx(uint32_t opcode); // 0x1F 0x57
	void Lvx(uint32_t opcode); // 0x1F 0x67
    void Neg(uint32_t opcode); // 0x1F 0x68
    void Nor(uint32_t opcode); // 0x1F 0x7C
    void Mtocrf(uint32_t opcode); // 0x1F 0x90
    void Stdx(uint32_t opcode); // 0x1F 0x95
	void Stwcx(uint32_t opcode); // 0x1F 0x96
    void Stwx(uint32_t opcode); // 0x1F 0x97
    void Addze(uint32_t opcode); // 0x1F 0xCA
	void Stdcx(uint32_t opcode); // 0x1F 0xD6
    void Stvx(uint32_t opcode); // 0x1F 0xE7
    void Mulld(uint32_t opcode); // 0x1F 0xE9
    void Mullw(uint32_t opcode); // 0x1F 0xEB
    void Add(uint32_t opcode); // 0x1F 0x10A
    void Xor(uint32_t opcode); // 0x1F 0x13C
    void Mfspr(uint32_t opcode); // 0x1F 0x153
	void Mftb(uint32_t opcode); // 0x1F 0x173
    void Or(uint32_t opcode); // 0x1F 0x1BC
    void Divdu(uint32_t opcode); // 0x1F 0x1C9
    void Divwu(uint32_t opcode); // 0x1F 0x1CB
    void Mtspr(uint32_t opcode); // 0x1F 0x1D3
    void Divd(uint32_t opcode); // 0x1F 0x1E9
    void Divw(uint32_t opcode); // 0x1F 0x1EB
    void Lfsx(uint32_t opcode); // 0x1F 0x217
    void Srw(uint32_t opcode); // 0x1F 0x218
	void Srd(uint32_t opcode); // 0x1F 0x21B
    void Stfsx(uint32_t opcode); // 0x1F 0x297
    void Srawi(uint32_t opcode); // 0x1F 0x338
    void Sradi(uint32_t opcode); // 0x1F 0x19D & 0x1F 0x33D
    void Extsh(uint32_t opcode); // 0x1F 0x39A
    void Extsb(uint32_t opcode); // 0x1F 0x3BA
    void Stfiwx(uint32_t opcode); // 0x1F 0x3D7
    void Extsw(uint32_t opcode); // 0x1F 0x3DA
    void Lwz(uint32_t opcode); // 0x20
	void Lwzu(uint32_t opcode); // 0x21
    void Lbz(uint32_t opcode); // 0x22
    void Lbzu(uint32_t opcode); // 0x23
    void Stw(uint32_t opcode); // 0x24
    void Stwu(uint32_t opcode); // 0x25
    void Stb(uint32_t opcode); // 0x26
    void Stbu(uint32_t opcode); // 0x27
    void Lhz(uint32_t opcode); // 0x28
	void Lhzu(uint32_t opcode); // 0x29
    void Sth(uint32_t opcode); // 0x2C
    void Lfs(uint32_t opcode); // 0x30
    void Lfd(uint32_t opcode); // 0x32
    void Stfs(uint32_t opcode); // 0x34
    void Stfsu(uint32_t opcode); // 0x35
    void Stfd(uint32_t opcode); // 0x36
    void G_3A(uint32_t opcode); // 0x3A
    void Ld(uint32_t opcode); // 0x3A 0x00
    void Ldu(uint32_t opcode); // 0x3A 0x01
    void G_3B(uint32_t opcode); // 0x3B
    void Fdivs(uint32_t opcode); // 0x3B 0x12
    void Fsubs(uint32_t opcode); // 0x3B 0x14
    void Fadds(uint32_t opcode); // 0x3B 0x15
    void Fmuls(uint32_t opcode); // 0x3B 0x19
    void Fmsubs(uint32_t opcode); // 0x3B 0x1C
    void Fmadds(uint32_t opcode); // 0x3B 0x1D
    void G_3E(uint32_t opcode); // 0x3E
    void Std(uint32_t opcode); // 0x3E 0x00
    void Stdu(uint32_t opcode); // 0x3E 0x01
    void G_3F(uint32_t opcode); // 0x3F
    void Fcmpu(uint32_t opcode); // 0x3F 0x000
    void Frsp(uint32_t opcode); // 0x3F 0x00C
    void Fctiwz(uint32_t opcode); // 0x3F 0x00F
    void Fdiv(uint32_t opcode); // 0x3F 0x012
    void Fadd(uint32_t opcode); // 0x3F 0x015
    void Fmul(uint32_t opcode); // 0x3F 0x019
	void Fmadd(uint32_t opcode); // 0x3F 0x01D
    void Fneg(uint32_t opcode); // 0x3F 0x028
    void Fctidz(uint32_t opcode); // 0x3F 0x02F
    void Fmr(uint32_t opcode); // 0x3F 0x048
    void Fcfid(uint32_t opcode); // 0x3F 0x04E

    void InitInstructionTable();

    bool canDisassemble = false;
public:
    uint64_t GetStackAddr() {return state.sp;}

    uint64_t GetReg(int index) {return state.r[index];}
    void SetReg(int index, uint64_t value) {state.r[index] = value;}
    MemoryManager* GetManager() {return &manager;}

	State& GetState() {return state;}
	void SetState(State& state) {this->state = state;}

    CellPPU(MemoryManager& manager);
	void RunSubroutine(uint32_t addr); // Needed for callbacks and cellThreadOnce

    void Run();
    void Dump();

	SPU* spus[6];
};
