#pragma once

#include "kernel/Memory.h"

#include <unordered_map>
#include <functional>

class CellPPU
{
private:
    uint64_t r[32];
    float fpr[32];

    uint64_t pc, lr;

    struct CRField
    {
        uint8_t val : 4;
    };

    struct ControlRegister
    {
        CRField cr[8];
    } cr;

    uint64_t ctr;

    MemoryManager* manager;

    std::unordered_map<uint8_t, std::function<void(uint32_t)>> opcodes;

    bool CheckCondition(uint32_t bo, uint32_t bi);

    uint8_t GetCRBit(const uint32_t bit) const { return 1 << (3 - (bit % 4)); }

    uint8_t IsCR(const uint32_t bit) const {return (cr.cr[bit >> 2].val & GetCRBit(bit)) ? 1 : 0;}

    void Cmpi(uint32_t opcode); // 0x0B
    void Addi(uint32_t opcode); // 0x0E
    void BranchCond(uint32_t opcode); // 0x10
    void Branch(uint32_t opcode); // 0x12
    void Ori(uint32_t opcode); // 0x18
    void Oris(uint32_t opcode); // 0x19
    void Rldicr(uint32_t opcode); //0x1E
    void Code1F(uint32_t opcode); // 0x1F
    void Mfspr(uint32_t opcode); // 0x1F 0x153
    void Ld(uint32_t opcode); // 0x3A
    void Std(uint32_t opcode); // 0x3E

    void InitInstructionTable();

    static constexpr bool canDisassemble = true;
public:
    CellPPU() {}
    CellPPU(uint64_t entry, uint64_t ret_addr, MemoryManager* manager);

    void Run();
    void Dump();
};