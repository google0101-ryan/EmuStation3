#pragma once

#include "kernel/Memory.h"

#include <unordered_map>
#include <functional>
#include <string>

class CellPPU
{
private:
    uint64_t r[32];
    union
    {
        double f;
        uint64_t u;
    } fpr[32];

    union
    {
        uint8_t u8[16];
        uint16_t u16[8];
        uint32_t u32[4];
        uint64_t u64[2];
        float f[4];
        double d[2];
        __uint128_t u128;
    } vpr[32];

    enum
    {
        CR_LT = 0x8,
        CR_GT = 0x4,
        CR_EQ = 0x2,
        CR_SO = 0x1,
    };

    uint64_t pc, lr;

    struct CRField
    {
        uint8_t val : 4;
    };

    struct XER
    {
        bool ca = false;
    } xer;
    
    union CRhdr
    {
        uint32_t CR;

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
    } cr;

    uint64_t ctr;

    MemoryManager& manager;

    std::unordered_map<uint8_t, std::function<void(uint32_t)>> opcodes;

    template<typename T>
    void UpdateCRn(const uint8_t n, const T a, const T b)
    {
        if (a < b) SetCR(n, CR_LT);
        if (a > b) SetCR(n, CR_GT);
        if (a == b) SetCR(n, CR_EQ);
    }
    
    template<typename T>
    void UpdateCR0(const T val)
    {
        UpdateCRn<T>(0, val, 0);
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
        case 0x008: name = "lr"; return lr;
        case 0x009: name = "ctr"; return ctr;
        }

        printf("ERROR: Read from unknown SPR 0x%03x\n", n);
        exit(1);
    }

    bool CheckCondition(uint32_t bo, uint32_t bi);

    inline uint8_t GetCR(uint8_t n) const
    {
        switch (n)
        {
        case 0: return cr.cr0;
		case 1: return cr.cr1;
		case 2: return cr.cr2;
		case 3: return cr.cr3;
		case 4: return cr.cr4;
		case 5: return cr.cr5;
		case 6: return cr.cr6;
		case 7: return cr.cr7;
        }

        return 0;
    }

    inline void SetCR(uint8_t n, uint32_t value)
    {
        switch (n)
        {
        case 0: cr.cr0 = value; break;
        case 1: cr.cr1 = value; break;
        case 2: cr.cr2 = value; break;
        case 3: cr.cr3 = value; break;
        case 4: cr.cr4 = value; break;
        case 5: cr.cr5 = value; break;
        case 6: cr.cr6 = value; break;
        case 7: cr.cr7 = value; break;
        }
    }

    uint8_t GetCRBit(const uint32_t bit) const { return 1 << (3 - (bit % 4)); }

    uint8_t IsCR(const uint32_t bit) const {return (GetCR(bit >> 2) & GetCRBit(bit)) ? 1 : 0;}

    void G_04(uint32_t opcode); // 0x04
    void Vxor(uint32_t opcode); // 0x04 0x4C4
    void Subfic(uint32_t opcode); // 0x08
    void Cmpli(uint32_t opcode); // 0x0A
    void Cmpi(uint32_t opcode); // 0x0B
    void Addi(uint32_t opcode); // 0x0E
    void Addis(uint32_t opcode); // 0x0F
    void BranchCond(uint32_t opcode); // 0x10
    void Branch(uint32_t opcode); // 0x12
    void G_13(uint32_t opcode); // 0x13
    void Bclr(uint32_t opcode); // 0x13 0x10
    void Bcctr(uint32_t opcode); // 0x13 0x210
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
    void Mulhwu(uint32_t opcode); // 0x1F 0x0B
    void Mfcr(uint32_t opcode); // 0x1F 0x13
    void Lwzx(uint32_t opcode); // 0x1F 0x17
    void Slw(uint32_t opcode); // 0x1F 0x18
    void Cntlzw(uint32_t opcode); // 0x1F 0x1A
    void And(uint32_t opcode); // 0x1F 0x1C
    void Cmpl(uint32_t opcode); // 0x1F 0x20
    void Subf(uint32_t opcode); // 0x1F 0x28
    void Cntlzd(uint32_t opcode); // 0x1F 0x3A
    void Andc(uint32_t opcode); // 0x1F 0x3C
    void Neg(uint32_t opcode); // 0x1F 0x68
    void Nor(uint32_t opcode); // 0x1F 0x7C
    void Mtocrf(uint32_t opcode); // 0x1F 0x90
    void Stdx(uint32_t opcode); // 0x1F 0x95
    void Stwx(uint32_t opcode); // 0x1F 0x97
    void Addze(uint32_t opcode); // 0x1F 0xCA
    void Stvx(uint32_t opcode); // 0x1F 0xE7
    void Mulld(uint32_t opcode); // 0x1F 0xE9
    void Mullw(uint32_t opcode); // 0x1F 0xEB
    void Add(uint32_t opcode); // 0x1F 0x10A
    void Xor(uint32_t opcode); // 0x1F 0x13C
    void Mfspr(uint32_t opcode); // 0x1F 0x153
    void Or(uint32_t opcode); // 0x1F 0x1BC
    void Divdu(uint32_t opcode); // 0x1F 0x1C9
    void Divwu(uint32_t opcode); // 0x1F 0x1CB
    void Mtspr(uint32_t opcode); // 0x1F 0x1D3
    void Divd(uint32_t opcode); // 0x1F 0x1E9
    void Srw(uint32_t opcode); // 0x1F 0x218
    void Srawi(uint32_t opcode); // 0x1F 0x338
    void Sradi(uint32_t opcode); // 0x1F 0x19D & 0x1F 0x33D
    void Extsh(uint32_t opcode); // 0x1F 0x39A
    void Extsb(uint32_t opcode); // 0x1F 0x3BA
    void Extsw(uint32_t opcode); // 0x1F 0x3DA
    void Lwz(uint32_t opcode); // 0x20
    void Lbz(uint32_t opcode); // 0x22
    void Lbzu(uint32_t opcode); // 0x23
    void Stw(uint32_t opcode); // 0x24
    void Stwu(uint32_t opcode); // 0x25
    void Stb(uint32_t opcode); // 0x26
    void Stbu(uint32_t opcode); // 0x27
    void Lhz(uint32_t opcode); // 0x28
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
    void Fadds(uint32_t opcode); // 0x3B 0x15
    void Fmuls(uint32_t opcode); // 0x3B 0x19
    void Fmadds(uint32_t opcode); // 0x3B 0x
    void G_3E(uint32_t opcode); // 0x3E
    void Std(uint32_t opcode); // 0x3E 0x00
    void Stdu(uint32_t opcode); // 0x3E 0x01
    void G_3F(uint32_t opcode); // 0x3F
    void Frsp(uint32_t opcode); // 0x3F 0x00C
    void Fmr(uint32_t opcode); // 0x3F 0x048
    void Fcfid(uint32_t opcode); // 0x3F 0x04E

    void InitInstructionTable();

    static constexpr bool canDisassemble = false;
public:
    uint64_t GetReg(int index) {return r[index];}
    void SetReg(int index, uint64_t value) {r[index] = value;}
    MemoryManager* GetManager() {return &manager;}

    CellPPU(uint64_t entry, uint64_t ret_addr, MemoryManager& manager);

    void Run();
    void Dump();
};